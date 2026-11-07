#include "model/nifmodel.h"
#include "geometry/vanillaplus.h"
#include "ui/vanillaplusdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUndoStack>
#include <iostream>

int main(int argc,char **argv)
{
	QApplication app(argc,argv);
	if (argc!=3 || !NifModel::loadXML()) return 1;
	NifModel nif; if (!nif.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 1;
	QString textureRoot=QString::fromLocal8Bit(argv[2]);
	QString nestedRoot=textureRoot+"/textures/dungeons/dwemerruins";
	nif.getGameResources().close_archives(); nif.getGameResources().dataPaths.prepend(textureRoot);
	if (QDir(nestedRoot+"/textures").exists()) nif.getGameResources().dataPaths.prepend(nestedRoot+"/textures");
	if (QDir(nestedRoot+"/textures/textures").exists()) nif.getGameResources().dataPaths.prepend(nestedRoot+"/textures/textures");
	int texturedShapesTested=0;
	QByteArray originalBytes; QBuffer originalOut(&originalBytes); originalOut.open(QIODevice::WriteOnly);
	if (!nif.save(originalOut)) return 1;
	for (int shapeIndex:{5,8}) {
		auto shape=nif.getBlockIndex(shapeIndex); VanillaPlus::Mesh source;
		auto rows=nif.getIndex(shape,"Vertex Data");
		for (int i=0;i<nif.rowCount(rows);++i) {
			auto row=nif.getIndex(rows,i); auto p=nif.get<Vector3>(row,"Vertex"); auto n=nif.get<Vector3>(row,"Normal");
			auto uv=nif.get<Vector2>(row,"UV");
			VanillaPlus::Vertex v; v.position={p[0],p[1],p[2]}; v.normal={n[0],n[1],n[2]}; v.uv={uv[0],uv[1]}; source.vertices.push_back(v);
		}
		for (auto t:nif.getArray<Triangle>(shape,"Triangles")) source.triangles.push_back({t[0],t[1],t[2]});
		auto mesh=VanillaPlus::subdivide(source,2);
		VanillaPlus::HeightField uniform{1,1,{0.8f}};
		VanillaPlus::Displacement settings; settings.strength=10; settings.positiveLimit=settings.negativeLimit=10;
		settings.pinBoundaries=true; settings.preventPinching=true;
		auto inflated=VanillaPlus::displace(mesh,uniform,settings);
		double before=VanillaPlus::maximumSectionDisplacementBias(inflated);
		settings.removeSectionBias=true; VanillaPlus::DisplacementStats stats;
		auto corrected=VanillaPlus::displace(mesh,uniform,settings,{},&stats);
		double after=VanillaPlus::maximumSectionDisplacementBias(corrected);
		for (size_t i=0;i<mesh.vertices.size();++i) if (corrected.vertices[i].position!=mesh.vertices[i].position) {
			std::cerr << "Uniform evidence moved the corrected sconce surface\n"; return 1;
		}
		if (!(before>0) || after>1e-6) return 1;
		std::cout << "Sconce shape " << shapeIndex << ": uniform-cue bias " << before << " -> " << after
			<< "; centered sections " << stats.centeredSections << "; original positions preserved\n";
		auto shader=nif.getBlockIndex(nif.getLink(shape,"Shader Property"));
		auto textures=nif.getBlockIndex(nif.getLink(shader,"Texture Set"));
		auto paths=nif.getArray<QString>(textures,"Textures"); QByteArray diffuseBytes,normalBytes;
		if (paths.size()<2 || !nif.getResourceFile(diffuseBytes,paths[0],"textures",".dds")
			|| !nif.getResourceFile(normalBytes,paths[1],"textures",".dds")) {
			std::cout << "Sconce shape " << shapeIndex << ": real texture comparison pending missing source textures\n"; continue;
		}
		double texturedBias[2]={0,0};
		for (int centeredMode=0;centeredMode<2;++centeredMode) {
			NifModel edited; QBuffer in(&originalBytes); in.open(QIODevice::ReadOnly);
			if (!edited.load(in)) return 1;
			edited.getGameResources().close_archives(); edited.getGameResources().dataPaths.prepend(textureRoot);
			if (QDir(nestedRoot+"/textures").exists()) edited.getGameResources().dataPaths.prepend(nestedRoot+"/textures");
			if (QDir(nestedRoot+"/textures/textures").exists()) edited.getGameResources().dataPaths.prepend(nestedRoot+"/textures/textures");
			QUndoStack stack; edited.undoStack=&stack; auto editedShape=edited.getBlockIndex(shapeIndex);
			bool applied=false;
			QTimer::singleShot(0,[&]() {
				auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
				auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
				auto tess=dialog->findChild<QComboBox *>("vanillaTessellationMode");
				auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
				auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
				auto bias=dialog->findChild<QCheckBox *>("vanillaRemoveDisplacementBias");
				auto oldUV=dialog->findChild<QCheckBox *>("vanillaOriginalUVHandling");
				auto cleanup=dialog->findChild<QCheckBox *>("vanillaQualityCleanup");
				auto apply=dialog->findChild<QPushButton *>("vanillaApply");
				auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
				if (!generate || !tess || !depth || !strength || !bias || !oldUV || !cleanup || !apply || !status) { dialog->reject(); return; }
				bool diffuse=false,normal=false;
				for (auto label:dialog->findChildren<QLabel *>()) {
					diffuse |= label->text().contains("Diffuse: Loaded:"); normal |= label->text().contains("Normal: Loaded:");
				}
				if (!diffuse || !normal) { std::cerr << "Sconce source textures are missing\n"; dialog->reject(); return; }
				generate->click(); oldUV->setChecked(true); tess->setCurrentIndex(1); depth->setValue(2);
				strength->setValue(3); cleanup->setChecked(false); bias->setChecked(centeredMode!=0);
				if (auto mode=dialog->findChild<QComboBox *>("vanillaPreviewMode")) mode->setCurrentIndex(3);
				if (auto preview=dialog->findChild<QLabel *>("vanillaMeshPreview"))
					preview->pixmap().save(QCoreApplication::applicationDirPath()+QString("/sconce-%1-bias-%2.png").arg(shapeIndex).arg(centeredMode));
				apply->click(); applied=dialog->result()==QDialog::Accepted;
				if (!applied) { std::cerr << "Sconce apply failed: " << status->text().toStdString() << '\n'; dialog->reject(); }
			});
			showVanillaPlusGeometry(&edited,editedShape,nullptr);
			if (!applied || stack.count()!=1) return 1;
			auto appliedRows=edited.getIndex(editedShape,"Vertex Data");
			if (size_t(edited.rowCount(appliedRows))!=mesh.vertices.size()) return 1;
			auto actual=mesh;
			for (int i=0;i<edited.rowCount(appliedRows);++i) {
				auto p=edited.get<Vector3>(edited.getIndex(appliedRows,i),"Vertex"); actual.vertices[size_t(i)].position={p[0],p[1],p[2]};
			}
			texturedBias[centeredMode]=VanillaPlus::maximumSectionDisplacementBias(actual);
			QByteArray appliedBytes; QBuffer out(&appliedBytes); out.open(QIODevice::WriteOnly); if (!edited.save(out)) return 1;
			NifModel reopened; QBuffer reopen(&appliedBytes); reopen.open(QIODevice::ReadOnly); if (!reopened.load(reopen)) return 1;
			stack.undo(); QByteArray undone; QBuffer undoOut(&undone); undoOut.open(QIODevice::WriteOnly);
			if (!edited.save(undoOut) || undone!=originalBytes) return 1;
		}
		std::cout << "Sconce shape " << shapeIndex << ": real-texture output bias " << texturedBias[0] << " -> " << texturedBias[1]
			<< "; staged apply/reopen/exact undo passed\n";
		if (texturedBias[1]>1e-3 || texturedBias[1]>texturedBias[0]+1e-6) return 1;
		++texturedShapesTested;
	}
	return texturedShapesTested>0 ? 0 : 1;
}
