#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include "geometry/vanillaplus.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QUndoStack>
#include <QRegularExpression>
#include <QTimer>
#include <iostream>
#include <algorithm>

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	if (argc != 4 || !NifModel::loadXML()) return 1;
	NifModel nif;
	if (!nif.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 1;
	QUndoStack undo; nif.undoStack=&undo;
	nif.getGameResources().close_archives();
	nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
	auto shape = nif.getBlockIndex(22);
	if (nif.get<QString>(shape, "Name") != "Farmhouse02:6") return 1;
	std::vector<std::uint32_t> rejectedFaces;
	{
		VanillaPlus::Mesh mesh;
		auto desc=nif.get<BSVertexDesc>(shape,"Vertex Desc");
		auto rows=nif.getIndex(shape,"Vertex Data");
		for (int i=0;i<nif.rowCount(rows);++i) {
			auto row=nif.getIndex(rows,i);
			auto p=nif.get<Vector3>(row,"Vertex"), n=nif.get<Vector3>(row,"Normal");
			auto uv=nif.get<Vector2>(row,"UV");
			VanillaPlus::Vertex v; v.position={p[0],p[1],p[2]}; v.normal={n[0],n[1],n[2]}; v.uv={uv[0],uv[1]};
			if (desc.HasFlag(VF_COLORS)) { auto c=nif.get<Color4>(row,"Vertex Colors"); v.color={c[0],c[1],c[2],c[3]}; }
			mesh.vertices.push_back(v);
		}
		for (auto t:nif.getArray<Triangle>(shape,"Triangles")) mesh.triangles.push_back({t[0],t[1],t[2]});
		auto patches=VanillaPlus::identifySourcePatches(mesh);
		size_t merged=0,largest=0;
		for (const auto &p:patches) { if (p.triangles.size()>1) ++merged; largest=std::max(largest,p.triangles.size()); }
		std::cout << "Source patches: " << patches.size() << "; multi-triangle patches: " << merged << "; largest: " << largest << '\n';
		size_t loops=0, rejected=0, remeshed=0, candidates=0;
		for (size_t patchIndex=0;patchIndex<patches.size();++patchIndex) {
			const auto &p=patches[patchIndex];
			try {
				auto boundary=VanillaPlus::recoverPatchBoundary(mesh,p); loops+=boundary.loops.size();
				VanillaPlus::validatePatchDomain(mesh,p);
				if (boundary.loops.size()==1 && boundary.loops[0].size()==4) ++candidates;
				VanillaPlus::PatchRemesh candidate;
				auto report=VanillaPlus::tryRetopologizePatch(mesh,p,2,candidate);
				if (report.accepted) ++remeshed;
				std::cout << "Patch " << patchIndex << ": faces=" << report.sourceFaces << " loops=" << report.loops
					<< " boundary=" << report.boundaryEdges << " protected=" << report.protectedEdges
					<< " vertices=" << report.generatedVertices << " triangles=" << report.generatedTriangles
					<< " deviation=" << report.maximumSourceDeviation << " fallback=" << report.fallback
					<< " reason=" << report.reason << '\n';
			}
			catch (const std::exception &) { ++rejected; }
		}
		std::cout << "Recovered boundary loops: " << loops << "; rejected patches: " << rejected << '\n';
		std::cout << "Verified four-edge candidates: " << remeshed << " of " << candidates << '\n';
		if (rejected) return 1;
		auto dense=VanillaPlus::subdivide(mesh,3);
		VanillaPlus::ReductionStats stats;
		auto reduced=VanillaPlus::reduceReconstruction(dense,1.0f,&stats);
		std::cout << "Flat-source uniform depth 3: " << dense.triangles.size() << " triangles; reduced: " << reduced.triangles.size() << '\n';
		try {
			std::vector<VanillaPlus::PatchRetopoReport> reports;
			auto seed=VanillaPlus::retopologizeCoplanarPatches(mesh,reports);
			for (size_t i=0;i<reports.size();++i) if (reports[i].fallback)
				for (auto f:seed.patchAuthority->patches[i].triangles) rejectedFaces.push_back(f);
			if (rejectedFaces.empty()) return 1;
			std::cout << "Integrated patch seeds: " << std::count_if(reports.begin(),reports.end(),[](const auto &r) { return r.accepted; }) << '\n';
			(void)VanillaPlus::resolvePatchSources(seed);
			std::cout << "Seed correspondence passed\n";
			auto subdivided=VanillaPlus::subdivide(seed,1);
			(void)VanillaPlus::resolvePatchSources(subdivided);
			std::cout << "Subdivision correspondence passed\n";
			auto reducedPatch=VanillaPlus::reduceReconstruction(subdivided,0.1f);
			(void)VanillaPlus::resolvePatchSources(reducedPatch);
			std::cout << "Integrated patch geometry check passed\n";
			std::vector<std::array<std::uint32_t,2>> constraints;
			for (const auto &t:mesh.triangles) for (int j=0;j<3;++j) constraints.push_back({t[j],t[(j+1)%3]});
			auto fallback=VanillaPlus::retopologizeCoplanarPatches(mesh,reports,constraints);
			if (fallback.triangles!=mesh.triangles || fallback.vertices.size()!=mesh.vertices.size()) return 1;
			for (size_t i=0;i<mesh.vertices.size();++i)
				if (fallback.vertices[i].position!=mesh.vertices[i].position || fallback.vertices[i].uv!=mesh.vertices[i].uv
					|| fallback.vertices[i].normal!=mesh.vertices[i].normal || fallback.vertices[i].color!=mesh.vertices[i].color) return 1;
			auto unchanged=VanillaPlus::subdivide(fallback,0);
			unchanged=VanillaPlus::reduceReconstruction(unchanged,0);
			(void)VanillaPlus::resolvePatchSources(unchanged);
			if (unchanged.triangles!=mesh.triangles) return 1;
			std::cout << "Farmhouse rejected-patch fallback retained exact source geometry through downstream processing\n";
		} catch (const std::exception &e) { std::cerr << "Integrated patch geometry failed: " << e.what() << '\n'; return 1; }
	}
	QByteArray before, after; QBuffer b(&before); b.open(QIODevice::WriteOnly);
	if (!nif.save(b)) return 1; b.close();
	bool passed = false;
	QTimer::singleShot(0, [&] {
		auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
		auto mode=dialog->findChild<QComboBox *>("vanillaPreviewMode");
		auto preview=dialog->findChild<QLabel *>("vanillaMeshPreview");
		auto height=dialog->findChild<QLabel *>("vanillaHeightPreview");
		auto stats=dialog->findChild<QLabel *>("vanillaPreviewStats");
		auto reduce=dialog->findChild<QCheckBox *>("vanillaReduce");
		auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
		auto relief=dialog->findChild<QComboBox *>("vanillaReliefBehavior");
		auto originalUV=dialog->findChild<QCheckBox *>("vanillaOriginalUVHandling");
		auto high=dialog->findChild<QCheckBox *>("vanillaHighPrecision");
		auto resolution=dialog->findChild<QLabel *>("vanillaHeightResolution");
		auto shallowStage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto shallowStatus=dialog->findChild<QLabel *>("vanillaPreviewStatus");
		bool diffuse=false, normal=false;
		for (auto label:dialog->findChildren<QLabel *>()) {
			diffuse |= label->text().contains("Diffuse: Loaded:");
			normal |= label->text().contains("Normal: Loaded:");
		}
		if (!generate || !mode || !preview || !height || !stats || !reduce || !strength || !relief || !originalUV || !high || !resolution || !shallowStage || !shallowStatus || !diffuse || !normal) {
			std::cerr << "Farmhouse resources or controls unavailable\n"; dialog->reject(); return;
		}
		generate->click();
		double fullStrength=strength->value(); QImage fullHeight=height->pixmap().toImage();
		originalUV->setChecked(true);
		if (!shallowStatus->text().contains("Using original UV handling") || strength->value()!=fullStrength || height->pixmap().toImage()!=fullHeight) {
			std::cerr << "Manual original-UV fallback changed material settings or was not reported\n"; dialog->reject(); return;
		}
		shallowStage->click();
		if (!shallowStatus->text().contains("Staged copy passed")) { std::cerr << "Original-UV fallback staging failed\n"; dialog->reject(); return; }
		originalUV->setChecked(false);
		if (high->isChecked() || !resolution->text().contains("128")) { std::cerr << "Default field resolution differs\n"; dialog->reject(); return; }
		high->setChecked(true);
		if (!resolution->text().contains("256") || strength->value()!=fullStrength || height->pixmap().toImage()==fullHeight) {
			std::cerr << "High precision field did not preserve settings or show additional detail\n"; dialog->reject(); return;
		}
		generate->click();
		if (!high->isChecked() || !resolution->text().contains("256")) { std::cerr << "High precision was lost during height generation\n"; dialog->reject(); return; }
		shallowStage->click();
		if (!shallowStatus->text().contains("Staged copy passed")) { std::cerr << "High precision staging failed: " << shallowStatus->text().toStdString() << '\n'; dialog->reject(); return; }
		high->setChecked(false);
		generate->click();
		if (!resolution->text().contains("128") || height->pixmap().toImage()!=fullHeight) {
			std::cerr << "Standard height field did not restore after toggling\n"; dialog->reject(); return;
		}
		relief->setCurrentIndex(1);
		if (strength->value()!=fullStrength || height->pixmap().toImage()==fullHeight) {
			std::cerr << "Shallow mode failed to preserve strength or change conditioned height\n"; dialog->reject(); return;
		}
		generate->click();
		if (!(strength->value()>0 && strength->value()<=fullStrength*0.11)) {
			std::cerr << "Shallow preset did not use a smaller starting strength\n"; dialog->reject(); return;
		}
		shallowStage->click();
		if (!shallowStatus->text().contains("Staged copy passed")) {
			std::cerr << "Shallow farmhouse staging failed: " << shallowStatus->text().toStdString() << '\n'; dialog->reject(); return;
		}
		high->setChecked(true); shallowStage->click();
		if (!resolution->text().contains("256") || !shallowStatus->text().contains("Staged copy passed")) {
			std::cerr << "High precision shallow staging failed: " << shallowStatus->text().toStdString() << '\n'; dialog->reject(); return;
		}
		high->setChecked(false);
		std::cout << "Farmhouse starting strength: full=" << fullStrength << " shallow=" << strength->value() << '\n';
		relief->setCurrentIndex(0); generate->click();
		mode->setCurrentIndex(2);
		preview->pixmap().save(QString::fromLocal8Bit(argv[3])+"/farmhouse-dense.png");
		height->pixmap().save(QString::fromLocal8Bit(argv[3])+"/farmhouse-height.png");
		std::cout << "Dense: " << stats->text().toStdString() << " strength=" << strength->value() << '\n';
		reduce->setChecked(true); mode->setCurrentIndex(6);
		preview->pixmap().save(QString::fromLocal8Bit(argv[3])+"/farmhouse-compare.png");
		std::cout << "Reduced: " << stats->text().toStdString() << '\n';
		passed = !preview->pixmap().isNull(); dialog->reject();
	});
	showVanillaPlusGeometry(&nif, shape, nullptr);
	QBuffer a(&after); a.open(QIODevice::WriteOnly);
	if (!nif.save(a) || before != after || !passed) return 1;
	auto originalTriangles=nif.getArray<Triangle>(shape,"Triangles");
	auto originalRows=nif.getIndex(shape,"Vertex Data");
	std::vector<Vector3> originalPositions; std::vector<Vector2> originalUVs; std::vector<ByteVector3> originalNormals; std::vector<ByteColor4> originalColors;
	bool hasColors=nif.get<BSVertexDesc>(shape,"Vertex Desc").HasFlag(VF_COLORS);
	for (int i=0;i<nif.rowCount(originalRows);++i) {
		auto row=nif.getIndex(originalRows,i); originalPositions.push_back(nif.get<Vector3>(row,"Vertex"));
		originalUVs.push_back(nif.get<Vector2>(row,"UV")); originalNormals.push_back(nif.get<ByteVector3>(row,"Normal"));
		if (hasColors) originalColors.push_back(nif.get<ByteColor4>(row,"Vertex Colors"));
	}
	bool fallbackApplied=false;
	QTimer::singleShot(0,[&] {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
		auto patch=dialog->findChild<QCheckBox *>("vanillaPatchRetopology");
		auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
		auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
		auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto apply=dialog->findChild<QPushButton *>("vanillaApply");
		auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
		if (!generate || !patch || !depth || !strength || !stage || !apply || !status) { dialog->reject(); return; }
		bool patchLabel=false;
		for (auto label:dialog->findChildren<QLabel *>()) patchLabel |= label->text()=="Retopologize compatible patches";
		if (!patchLabel) { std::cerr << "Patch retopology label is missing from the built dialog\n"; dialog->reject(); return; }
		generate->click(); depth->setValue(0); strength->setValue(0); patch->setChecked(true);
		stage->click(); if (!status->text().contains("Staged copy passed")) { std::cerr << "Fallback stage: " << status->text().toStdString() << '\n'; dialog->reject(); return; }
		apply->click(); fallbackApplied=dialog->result()==QDialog::Accepted;
		if (!fallbackApplied) dialog->reject();
	});
	showVanillaPlusGeometry(&nif,shape,nullptr);
	if (!fallbackApplied || undo.count()!=1) return 1;
	auto fallbackTriangles=nif.getArray<Triangle>(shape,"Triangles");
	for (auto f:rejectedFaces) {
		if (f>=size_t(fallbackTriangles.size())) return 1;
		for (int j=0;j<3;++j) {
			if (fallbackTriangles[qsizetype(f)][j]!=originalTriangles[qsizetype(f)][j]) return 1;
			auto i=originalTriangles[qsizetype(f)][j]; auto row=nif.getIndex(nif.getIndex(shape,"Vertex Data"),i);
			auto p=nif.get<Vector3>(row,"Vertex"); auto uv=nif.get<Vector2>(row,"UV"); auto n=nif.get<ByteVector3>(row,"Normal");
			for (int k=0;k<3;++k) if (p[k]!=originalPositions[i][k] || n[k]!=originalNormals[i][k]) return 1;
			for (int k=0;k<2;++k) if (uv[k]!=originalUVs[i][k]) return 1;
			if (hasColors) { auto c=nif.get<ByteColor4>(row,"Vertex Colors"); for (int k=0;k<4;++k) if (c[k]!=originalColors[i][k]) return 1; }
		}
	}
	undo.undo(); QByteArray fallbackUndone; QBuffer fallbackUndoOut(&fallbackUndone); fallbackUndoOut.open(QIODevice::WriteOnly);
	if (!nif.save(fallbackUndoOut) || fallbackUndone!=before) return 1;
	undo.clear();
	std::cout << "Rejected farmhouse patch remained unchanged through staged write-back; undo restored exact source\n";
	bool staged=false,applied=false,patchUsed=false,reductionUsed=false;
	QTimer::singleShot(0,[&] {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
		auto patch=dialog->findChild<QCheckBox *>("vanillaPatchRetopology");
		auto originalUV=dialog->findChild<QCheckBox *>("vanillaOriginalUVHandling");
		auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
		auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
		auto reduce=dialog->findChild<QCheckBox *>("vanillaReduce");
		auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto apply=dialog->findChild<QPushButton *>("vanillaApply");
		auto stats=dialog->findChild<QLabel *>("vanillaPreviewStats");
		auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
		if (!generate || !patch || !originalUV || !depth || !strength || !reduce || !stage || !apply || !stats || !status) { dialog->reject(); return; }
		generate->click(); depth->setValue(1); strength->setValue(0.25); patch->setChecked(true);
		originalUV->setChecked(true);
		if (!status->text().contains("Using original UV handling")) { dialog->reject(); return; }
		originalUV->setChecked(false);
		auto match=QRegularExpression("Patches accepted: [0-9]+; changed: ([0-9]+)").match(stats->text());
		patchUsed=match.hasMatch() && match.captured(1).toInt()>0;
		reduce->setChecked(true);
		reductionUsed=stats->text().contains("Reduced:");
		std::cout << "Farmhouse patch output: " << stats->text().toStdString() << '\n';
		if (!patchUsed || !reductionUsed) {
			std::cerr << "Farmhouse patch preview failed: " << stats->text().toStdString() << " / " << status->text().toStdString() << '\n';
			dialog->reject(); return;
		}
		stage->click(); staged=status->text().contains("Staged copy passed");
		if (!patchUsed || !reductionUsed || !staged || !apply->isEnabled()) {
			std::cerr << "Farmhouse patch pipeline failed: " << stats->text().toStdString() << " / " << status->text().toStdString() << '\n';
			dialog->reject(); return;
		}
		apply->click(); applied=dialog->result()==QDialog::Accepted;
		if (!applied) { std::cerr << "Farmhouse patch apply failed: " << status->text().toStdString() << '\n'; dialog->reject(); }
	});
	showVanillaPlusGeometry(&nif,shape,nullptr);
	if (!staged || !applied || !patchUsed || !reductionUsed || undo.count()!=1 || nif.getArray<Triangle>(shape,"Triangles")==originalTriangles) return 1;
	QByteArray appliedBytes; QBuffer appliedOut(&appliedBytes); appliedOut.open(QIODevice::WriteOnly);
	if (!nif.save(appliedOut)) return 1;
	NifModel reopened; QBuffer reopenedInput(&appliedBytes); reopenedInput.open(QIODevice::ReadOnly);
	if (!reopened.load(reopenedInput)) return 1;
	undo.undo(); QByteArray undone; QBuffer undoOut(&undone); undoOut.open(QIODevice::WriteOnly);
	if (!nif.save(undoOut) || undone!=before) { std::cerr << "Farmhouse patch undo was not exact\n"; return 1; }
	undo.redo(); QByteArray redone; QBuffer redoOut(&redone); redoOut.open(QIODevice::WriteOnly);
	if (!nif.save(redoOut) || redone!=appliedBytes) { std::cerr << "Farmhouse patch redo was not exact\n"; return 1; }
	std::cout << "Farmhouse patch preview, reduction, staged write-back, reopen, exact undo/redo passed\n";
	return 0;
}
