#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUndoStack>
#include <cmath>
#include <iostream>

int main(int argc,char **argv)
{
	QApplication app(argc,argv);
	if (argc!=3 || !NifModel::loadXML()) return 1;
	NifModel initial; if (!initial.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 1;
	int block=-1;
	for (int i=0;i<initial.getBlockCount();++i) if (initial.isNiBlock(initial.getBlockIndex(i),"BSTriShape")) { block=i; break; }
	if (block<0) return 1;
	QByteArray sourceBytes; QBuffer save(&sourceBytes); save.open(QIODevice::WriteOnly); if (!initial.save(save)) return 1;
	QVector<Vector3> basePositions,baseNormals;
	auto originalRows=initial.getIndex(initial.getBlockIndex(block),"Vertex Data");
	for (int i=0;i<initial.rowCount(originalRows);++i) {
		auto row=initial.getIndex(originalRows,i); basePositions.push_back(initial.get<Vector3>(row,"Vertex")); baseNormals.push_back(initial.get<Vector3>(row,"Normal"));
	}
	const QStringList profileNames{"Neutral","Rock","Wood","Masonry","Metal","Soil"};
	for (int variant:{0,1,2,3,4,5,6}) {
		bool relief=variant!=0;
		int selectedProfile=variant>0?variant-1:0;
		QByteArray current=sourceBytes; int firstTriangles=0; double firstMovement=0,lastMovement=0;
		NifModel nif; QBuffer input(&current); input.open(QIODevice::ReadOnly); if (!nif.load(input)) return 1;
		nif.getGameResources().close_archives(); nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
		QUndoStack undo; nif.undoStack=&undo;
		QByteArray firstOutput;
		for (int pass=1;pass<=3;++pass) {
			auto shape=nif.getBlockIndex(block); bool applied=false;
			QTimer::singleShot(0,[&]() {
				auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
				auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
				auto tess=dialog->findChild<QComboBox *>("vanillaTessellationMode"); auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
				auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength"); auto apply=dialog->findChild<QPushButton *>("vanillaApply");
				auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
				if (!generate || !generate->isEnabled() || !tess || !depth || !strength || !apply || !status) { dialog->reject(); return; }
				if (pass>1) {
					auto reference=dialog->findChild<QLabel *>("vanillaReferenceStatus");
					if (!reference || !reference->text().contains("Refining the retained reference") || strength->value()!=(relief?3:0) || depth->value()!=1) { dialog->reject(); return; }
				}
				if (pass==1) generate->click();
				tess->setCurrentIndex(1); depth->setValue(1); strength->setValue(relief?3:0);
				if (auto profile=dialog->findChild<QComboBox *>("vanillaSurfaceProfile")) profile->setCurrentIndex(selectedProfile);
				for (const char *name:{"vanillaDarkFeatureBias","vanillaQualityCleanup","vanillaReduce","vanillaPatchRetopology"})
					if (auto box=dialog->findChild<QCheckBox *>(name)) box->setChecked(false);
				apply->click(); applied=dialog->result()==QDialog::Accepted;
				if (!applied) { std::cerr << status->text().toStdString() << '\n'; dialog->reject(); }
			});
			showVanillaPlusGeometry(&nif,shape,nullptr); if (!applied || undo.count()!=pass) return 1;
			auto rows=nif.getIndex(shape,"Vertex Data"); double movement=0,lateral=0;
			for (int i=0;i<basePositions.size();++i) {
				auto p=nif.get<Vector3>(nif.getIndex(rows,i),"Vertex"); double d[3],n[3],parallel=0,nn=0;
				for (int j=0;j<3;++j) { d[j]=double(p[j])-basePositions[i][j]; n[j]=baseNormals[i][j]; nn+=n[j]*n[j]; movement+=d[j]*d[j]; }
				for (int j=0;j<3;++j) { n[j]/=std::sqrt(nn); parallel+=d[j]*n[j]; }
				for (int j=0;j<3;++j) { double side=d[j]-parallel*n[j]; lateral+=side*side; }
			}
			movement=std::sqrt(movement/basePositions.size()); lateral=std::sqrt(lateral/basePositions.size());
			int triangles=nif.get<int>(shape,"Num Triangles");
			std::cout << "Real rock " << (relief?profileNames[selectedProfile].toStdString():"zero displacement") << " pass " << pass << ": triangles=" << triangles
				<< ", original-vertex movement RMS=" << movement << ", lateral RMS=" << lateral << '\n';
			if (pass==1) { firstTriangles=triangles; firstMovement=movement; }
			else if (triangles!=firstTriangles) return 1;
			lastMovement=movement;
			current.clear(); QBuffer output(&current); output.open(QIODevice::WriteOnly); if (!nif.save(output)) return 1;
			if (pass==1) firstOutput=current; else if (current!=firstOutput) { std::cerr << "Repeat Apply changed the target surface\n"; return 1; }
			NifModel reopened; QBuffer verify(&current); verify.open(QIODevice::ReadOnly); if (!reopened.load(verify)) return 1;
		}
		if ((!relief && lastMovement!=0) || std::abs(lastMovement-firstMovement)>1e-8) return 1;
		undo.setIndex(0);
		QByteArray undone; QBuffer undoOutput(&undone); undoOutput.open(QIODevice::WriteOnly); if (!nif.save(undoOutput) || undone!=sourceBytes) return 1;
		auto checkReference=[&](bool retained,double expectedStrength=-1) {
			bool correct=false;
			QTimer::singleShot(0,[&]() { auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
				auto notice=dialog->findChild<QLabel *>("vanillaReferenceStatus"); correct=notice && notice->text().contains(retained?"Refining the retained reference":"New Vanilla+ operation");
				if (expectedStrength>=0) { auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength"); correct &= strength && strength->value()==expectedStrength; }
				dialog->reject(); });
			showVanillaPlusGeometry(&nif,nif.getBlockIndex(block),nullptr); return correct;
		};
		if (!checkReference(true,0)) return 1;
		undo.setIndex(undo.count());
		QByteArray redone; QBuffer redoOutput(&redone); redoOutput.open(QIODevice::WriteOnly); if (!nif.save(redoOutput) || redone!=firstOutput) return 1;
		if (!checkReference(true,relief?3:0)) return 1;
		if (relief) {
			bool applied=false;
			QTimer::singleShot(0,[&]() { auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
				auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength"); auto apply=dialog->findChild<QPushButton *>("vanillaApply");
				if (!strength || !apply) { dialog->reject(); return; } strength->setValue(1.5); apply->click(); applied=dialog->result()==QDialog::Accepted; if (!applied) dialog->reject(); });
			showVanillaPlusGeometry(&nif,nif.getBlockIndex(block),nullptr); if (!applied) return 1;
			undo.undo(); if (!checkReference(true,3)) return 1;
			undo.redo(); if (!checkReference(true,1.5)) return 1;
			redone.clear(); QBuffer halfOutput(&redone); halfOutput.open(QIODevice::WriteOnly); if (!nif.save(halfOutput) || redone==firstOutput) return 1;
		}
		// An external vertex edit explicitly ends refinement; closing/loading a
		// file in the same model also ends it, even if block numbers are reused.
		auto row=nif.getIndex(nif.getIndex(nif.getBlockIndex(block),"Vertex Data"),0);
		auto position=nif.get<Vector3>(row,"Vertex"); position[0]+=0.25f; nif.set<Vector3>(row,"Vertex",position);
		if (!checkReference(false)) return 1;
		QBuffer reload(&redone); reload.open(QIODevice::ReadOnly); if (!nif.load(reload) || !checkReference(false)) return 1;
	}
	std::cout << "All six profiles: document-lifetime Apply/reopen is byte-identical; settings/geometry Undo/Redo, invalidation and independent NIF reopen passed; source file untouched.\n";
}
