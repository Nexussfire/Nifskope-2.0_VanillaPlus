#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include "ui/materialgraphdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <iostream>

int main(int argc,char **argv)
{
	QApplication app(argc,argv); if (argc!=3 || !NifModel::loadXML()) return 1;
	QString root=QString::fromLocal8Bit(argv[1]),tag=QString::fromLocal8Bit(argv[2]);
	NifModel nif; if (!nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/landscape/rocks/rockm01.nif")) return 1;
	nif.getGameResources().close_archives(); nif.getGameResources().dataPaths.prepend(root+"/Textures Vanilla Test");
	QModelIndex shape; for (int i=0;i<nif.getBlockCount();++i) if (nif.isNiBlock(nif.getBlockIndex(i),"BSTriShape")) { shape=nif.getBlockIndex(i); break; }
	QByteArray before; QBuffer save(&before); save.open(QIODevice::WriteOnly); if (!nif.save(save)) return 1;
	QUndoStack stack; nif.undoStack=&stack;
	QFile report(QCoreApplication::applicationDirPath()+"/performance-"+tag+".csv"); if (!report.open(QIODevice::WriteOnly)) return 1;
	QTextStream out(&report); out << "operation,milliseconds,image_sha256,statistics_sha256\n";
	bool passed=false; QElapsedTimer opening; opening.start();
	QTimer::singleShot(0,[&]() {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
		auto preview=dialog->findChild<QLabel *>("vanillaMeshPreview"),stats=dialog->findChild<QLabel *>("vanillaPreviewStats");
		auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight"),stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto view=dialog->findChild<QComboBox *>("vanillaPreviewMode"),profile=dialog->findChild<QComboBox *>("vanillaSurfaceProfile");
		auto wire=dialog->findChild<QCheckBox *>("vanillaWireframe"),smooth=dialog->findChild<QCheckBox *>("vanillaSmoothShading");
		auto dark=dialog->findChild<QCheckBox *>("vanillaDarkFeatureBias"),darkPreview=dialog->findChild<QCheckBox *>("vanillaDarkAttractionPreview");
		auto profilePreview=dialog->findChild<QCheckBox *>("vanillaRockSuppressionPreview");
		if (!preview || !stats || !status || !generate || !stage || !view || !profile || !wire || !smooth || !dark || !darkPreview || !profilePreview) { dialog->reject(); return; }
		auto record=[&](const char *name,double milliseconds) {
			auto image=preview->pixmap().toImage(); QByteArray pixels(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes());
			auto picture=QCryptographicHash::hash(pixels,QCryptographicHash::Sha256).toHex();
			auto text=QCryptographicHash::hash(stats->text().toUtf8(),QCryptographicHash::Sha256).toHex();
			out << name << ',' << milliseconds << ',' << picture << ',' << text << '\n'; out.flush();
			std::cout << name << ": " << milliseconds << " ms\n";
		};
		auto measure=[&](const char *name,auto operation) { QElapsedTimer timer; timer.start(); operation(); record(name,double(timer.nsecsElapsed())/1e6); };
		record("open",double(opening.nsecsElapsed())/1e6);
		measure("generate",[&]() { generate->click(); });
		measure("view_displaced",[&]() { view->setCurrentIndex(2); });
		measure("wire_on",[&]() { wire->setChecked(true); });
		measure("wire_off",[&]() { wire->setChecked(false); });
		measure("flat_shading",[&]() { smooth->setChecked(false); });
		measure("smooth_shading",[&]() { smooth->setChecked(true); });
		measure("dark_preview_on",[&]() { darkPreview->setChecked(true); });
		measure("dark_preview_off",[&]() { darkPreview->setChecked(false); });
		measure("dark_relocation_on",[&]() { dark->setChecked(true); });
		measure("dark_relocation_off",[&]() { dark->setChecked(false); });
		measure("profile_metal",[&]() { profile->setCurrentIndex(4); });
		measure("profile_mask_on",[&]() { profilePreview->setChecked(true); });
		measure("profile_mask_off",[&]() { profilePreview->setChecked(false); });
		measure("profile_neutral",[&]() { profile->setCurrentIndex(0); });
		measure("stage",[&]() { stage->click(); });
		if (!status->text().contains("Staged copy passed")) { std::cerr << status->text().toStdString() << '\n'; dialog->reject(); return; }
		auto flags=nif.getIndex(nif.getBlockIndex(0),"Flags"); auto originalFlags=nif.get<quint32>(flags);
		measure("unrelated_edits_200",[&]() { for (int i=0;i<200;++i) nif.set<quint32>(flags,originalFlags^quint32(i&1)); nif.set<quint32>(flags,originalFlags); });
		passed=true; dialog->reject();
	});
	showVanillaPlusGeometry(&nif,shape,nullptr);
	bool applied=false;
	if (passed) {
		QTimer::singleShot(0,[&]() { auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight"),apply=dialog->findChild<QPushButton *>("vanillaApply");
			if (!generate || !apply) { dialog->reject(); return; } generate->click(); apply->click(); applied=dialog->result()==QDialog::Accepted; if (!applied) dialog->reject(); });
		showVanillaPlusGeometry(&nif,shape,nullptr);
	}
	if (!applied) return 1;
	QByteArray candidate; QBuffer candidateOut(&candidate); candidateOut.open(QIODevice::WriteOnly); if (!nif.save(candidateOut)) return 1;
	auto candidateHash=QCryptographicHash::hash(candidate,QCryptographicHash::Sha256).toHex();
	out << "applied_candidate,0," << candidateHash << ',' << candidateHash << '\n';
	auto flags=nif.getIndex(nif.getBlockIndex(0),"Flags"); auto originalFlags=nif.get<quint32>(flags);
	QElapsedTimer editing; editing.start();
	for (int i=0;i<200;++i) nif.set<quint32>(flags,originalFlags^quint32(i&1)); nif.set<quint32>(flags,originalFlags);
	double editTime=double(editing.nsecsElapsed())/1e6;
	out << "unrelated_large_edits_200," << editTime << ",-,-\n"; out.flush(); std::cout << "unrelated_large_edits_200: " << editTime << " ms\n";
	stack.undo();
	bool graphPassed=false; QElapsedTimer graphOpening; graphOpening.start();
	QTimer::singleShot(0,[&]() {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
		auto graph=dialog->findChild<QGraphicsView *>("materialGraphView"); if (!graph) { dialog->reject(); return; }
		QLineEdit *offset=nullptr;
		for (auto item:graph->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
			if (auto edit=proxy->widget()->findChild<QLineEdit *>("materialUVOffsetU")) offset=edit;
		if (!offset) { dialog->reject(); return; }
		auto graphRecord=[&](const char *name,double milliseconds) {
			QByteArray pixels;
			for (auto item:graph->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
				for (auto label:proxy->widget()->findChildren<QLabel *>()) if (!label->pixmap().isNull()) {
					auto image=label->pixmap().toImage().convertToFormat(QImage::Format_RGB32);
					pixels.append(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes());
				}
			auto hash=QCryptographicHash::hash(pixels,QCryptographicHash::Sha256).toHex();
			out << name << ',' << milliseconds << ',' << hash << ",-\n"; out.flush(); std::cout << name << ": " << milliseconds << " ms\n";
		};
		graphRecord("graph_open",double(graphOpening.nsecsElapsed())/1e6);
		QString value=offset->text(); QElapsedTimer moving; moving.start();
		for (int i=0;i<50;++i) offset->setText(i%2?"0.125":"0.25"); offset->setText(value);
		graphRecord("graph_uv_edits_50",double(moving.nsecsElapsed())/1e6); graphPassed=true; dialog->reject();
	});
	showMaterialGraph(&nif,shape,nullptr);
	QByteArray after; QBuffer verify(&after); verify.open(QIODevice::WriteOnly);
	if (!passed || !graphPassed || !nif.save(verify) || after!=before) return 1;
	std::cout << "Performance probe staging and unchanged Cancel passed\n";
}
