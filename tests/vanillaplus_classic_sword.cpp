#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include "ui/materialgraphdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUndoStack>
#include <iostream>
#include <bit>

int main(int argc,char **argv)
{
	QApplication app(argc,argv);
	if (argc!=5 || !NifModel::loadXML()) return 1;
	QFile original(QString::fromLocal8Bit(argv[1]));
	if (!original.open(QIODevice::ReadOnly)) return 2;
	QByteArray fileBytes=original.readAll(); original.close();
	NifModel n; if (!n.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 3;
	n.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
	int shapeNumber=QString::fromLocal8Bit(argv[3]).toInt();
	auto shape=n.getBlockIndex(shapeNumber,"NiTriShape"), data=n.getBlockIndex(n.getLink(shape,"Data"),"NiTriShapeData");
	int dataNumber=n.getBlockNumber(data);
	bool displace=QString::fromLocal8Bit(argv[4])==QStringLiteral("displaced");
	auto originalNormals=n.getArray<Vector3>(data,"Normals");
	auto shader=n.getBlockIndex(n.getLink(shape,"Shader Property"),"BSLightingShaderProperty");
	auto textures=n.getBlockIndex(n.getLink(shader,"Texture Set"),"BSShaderTextureSet");
	if (!shape.isValid() || !data.isValid() || !shader.isValid() || !textures.isValid()) {
		std::cerr << "Sword shape/material not found\n"; return 4;
	}
	auto paths=n.getIndex(textures,"Textures");
	n.set<QString>(n.getIndex(paths,0),QStringLiteral("textures\\landscape\\mountains\\MountainSlab02.dds"));
	n.set<QString>(n.getIndex(paths,1),QStringLiteral("textures\\landscape\\mountains\\MountainSlab02_N.dds"));
	QUndoStack undo; n.undoStack=&undo;
	auto serialize=[&]() { QByteArray b; QBuffer out(&b); out.open(QIODevice::WriteOnly); if (!n.save(out)) b.clear(); return b; };
	QByteArray before=serialize(); bool applied=false, staged=false, staleRefused=false;
	int oldVerts=n.get<int>(data,"Num Vertices"), oldTriangles=n.get<int>(data,"Num Triangles");
	QTimer::singleShot(0,[&]() {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) { if (auto message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { std::cerr << message->text().toStdString() << '\n'; message->accept(); } return; }
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
		auto apply=dialog->findChild<QPushButton *>("vanillaApply");
		auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto mode=dialog->findChild<QComboBox *>("vanillaTessellationMode");
		auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
		auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
		auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
		if (!generate || !apply || !stage || !mode || !depth || !strength || !status || !generate->isEnabled()) { dialog->reject(); return; }
		generate->click(); mode->setCurrentIndex(1); depth->setValue(1); strength->setValue(displace?0.25:0.0);
		auto sourceUV=n.getIndex(n.getIndex(n.getIndex(data,"UV Sets"),0),0);
		auto uvValue=n.get<Vector2>(sourceUV);
		n.set<Vector2>(sourceUV,Vector2(uvValue[0]+0.25f,uvValue[1]));
		stage->click(); staleRefused=status->text().contains("geometry data changed",Qt::CaseInsensitive);
		n.set<Vector2>(sourceUV,uvValue);
		stage->click(); staged=status->text().contains("Staged copy passed");
		if (!staged) { std::cerr << "Sword stage: " << status->text().toStdString() << '\n'; dialog->reject(); return; }
		apply->click(); applied=dialog->result()==QDialog::Accepted;
		if (!applied) { std::cerr << "Sword apply: " << status->text().toStdString() << '\n'; dialog->reject(); }
	});
	showVanillaPlusGeometry(&n,shape,nullptr);
	if (!staged || !applied || !staleRefused || undo.count()!=1 || n.get<int>(data,"Num Vertices")<=oldVerts
		|| n.get<int>(data,"Num Triangles")!=oldTriangles*4) {
		std::cerr << "Sword stage or apply failed\n"; return 5;
	}
	QByteArray after=serialize(); QBuffer afterBuffer(&after); afterBuffer.open(QIODevice::ReadOnly);
	NifModel reopen; if (!reopen.load(afterBuffer)) { std::cerr << "Sword save/reopen failed\n"; return 6; }
	if (!displace) {
		auto resultNormals=reopen.getArray<Vector3>(reopen.getBlockIndex(dataNumber),"Normals");
		if (resultNormals.size()<originalNormals.size()) return 13;
		for (qsizetype i=0;i<originalNormals.size();++i) for (int j=0;j<3;++j)
			if (std::bit_cast<quint32>(originalNormals[i][j])!=std::bit_cast<quint32>(resultNormals[i][j])) {
				std::cerr << "Sword authored normal changed at vertex " << i << '\n'; return 14;
			}
	}
	for (int i=0;i<n.getBlockCount();++i) if (i!=dataNumber) {
		QByteArray a,b; QBuffer b1(&a),b2(&b); b1.open(QIODevice::WriteOnly); b2.open(QIODevice::WriteOnly);
		if (!n.saveIndex(b1,n.getBlockIndex(i)) || !reopen.saveIndex(b2,reopen.getBlockIndex(i)) || a!=b) return 7;
	}
	undo.undo(); if (serialize()!=before) { std::cerr << "Sword undo differs\n"; return 8; }
	undo.redo(); if (serialize()!=after) { std::cerr << "Sword redo differs\n"; return 9; }
	if (shapeNumber==13) {
		bool graphOpened=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) { if (auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept(); return; }
			graphOpened=dialog->findChild<QGraphicsView *>("materialGraphView")!=nullptr; dialog->reject();
		});
		showMaterialGraph(&n,shader,nullptr);
		if (!graphOpened || serialize()!=after) { std::cerr << "Classic sword material graph failed or changed model\n"; return 15; }
		auto duplicate=n.insertNiBlock("NiTriShape",-1);
		if (!n.setLink(duplicate,"Data",dataNumber)) return 11;
		bool sharedRefused=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
			auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			if (generate && stage && status) {
				generate->click(); stage->click();
				sharedRefused=status->text().contains("shared with another block");
			}
			dialog->reject();
		});
		showVanillaPlusGeometry(&n,shape,nullptr);
		if (!sharedRefused) { std::cerr << "Shared geometry data was not rejected\n"; return 12; }
	}
	if (!original.open(QIODevice::ReadOnly) || original.readAll()!=fileBytes) { std::cerr << "Sword file changed\n"; return 10; }
	n.undoStack=nullptr;
	std::cout << "Sword shape " << shapeNumber << (displace?" displaced":" undisplaced")
		<< " stage/apply/reopen/undo/redo passed; source file untouched\n";
}
