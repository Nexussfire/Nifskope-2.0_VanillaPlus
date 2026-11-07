#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include "animated_shader_fixture.h"
#include "gl/glproperty.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray bytes(NifModel &nif,QModelIndex index={}) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(index.isValid()?nif.saveIndex(out,index):nif.save(out),"serialization failed"); return data; }
void dialogRun(const std::function<void()> &open,const std::function<void(QDialog *)> &action) {
	bool done=false; QString failure;
	QTimer::singleShot(0,[&]() { auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d) return; try { action(d); done=true; } catch(const std::exception &e) { failure=e.what(); d->reject(); } });
	open(); require(done,qPrintable(failure.isEmpty()?QString("dialog did not open"):failure));
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("AnimatedGeometryProbe"); app.setApplicationName("AnimatedGeometryProbe");
	try {
		require(argc==2 && NifModel::loadXML(),"expected project root"); QString root=QString::fromLocal8Bit(argv[1]);
		for(const char *asset:{"landscape/rocks/rockm01.nif","weapons/sword/Jeweledlongsword.nif"}) {
			NifModel nif; require(nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+asset),"asset load failed"); auto diskBefore=bytes(nif); nif.getGameResources().dataPaths.prepend(root+"/Textures Vanilla Test"); QUndoStack undo; nif.undoStack=&undo;
			int shape=-1,shader=-1; for(int id=0;id<nif.getBlockCount();++id) if(nif.isNiBlock(nif.getBlockIndex(id),"BSTriShape") || nif.isNiBlock(nif.getBlockIndex(id),"NiTriShape")) {
				// Use the body selected by the established classic write-back suite.
				if(QString::fromLatin1(asset).contains("Jeweledlongsword") && id!=13) continue;
				auto index=nif.getBlockIndex(id); shader=nif.getLink(index,"Shader Property"); if(shader<0) for(int child:nif.getChildLinks(id)) if(nif.isNiBlock(nif.getBlockIndex(child),"BSLightingShaderProperty")) shader=child;
				if(shader>=0) { shape=id; break; }
			}
			require(shape>=0 && shader>=0,"lighting shape missing");
			if(nif.isNiBlock(nif.getBlockIndex(shape),"NiTriShape")) {
				// Original sword textures are not supplied. Match the established
				// classic test's in-memory mountain-texture fixture; no files change.
				auto paths=nif.getIndex(nif.getBlockIndex(nif.getLink(nif.getBlockIndex(shader),"Texture Set")),"Textures");
				nif.set<QString>(nif.getIndex(paths,0),QStringLiteral("textures\\landscape\\mountains\\MountainSlab02.dds")); nif.set<QString>(nif.getIndex(paths,1),QStringLiteral("textures\\landscape\\mountains\\MountainSlab02_N.dds"));
			}
			addAnimatedShaderFixture(nif,shader); auto before=bytes(nif); int write=nif.isNiBlock(nif.getBlockIndex(shape),"NiTriShape")?nif.getLink(nif.getBlockIndex(shape),"Data"):shape;
			dialogRun([&]() { showSculptGeometry(&nif,nif.getBlockIndex(shape),nullptr); },[&](QDialog *d) {
				require(d->objectName()=="sculptWorkspace" && d->findChild<QLabel *>("geometryAnimationNotice"),"animated Sculpt was blocked instead of showing a notice"); d->findChild<QPushButton *>("sculptCheck")->click(); require(d->findChild<QLabel *>("sculptStatus")->text().contains("passed"),qPrintable(d->findChild<QLabel *>("sculptStatus")->text())); d->reject();
			}); require(bytes(nif)==before && undo.count()==0,"Sculpt check/Cancel changed animated source");
			for(bool apply:{false,true}) {
				dialogRun([&]() { showVanillaPlusGeometry(&nif,nif.getBlockIndex(shape),nullptr); },[&](QDialog *d) {
					auto notice=d->findChild<QLabel *>("geometryAnimationNotice"); require(notice && notice->text().contains("not an animation frame"),"animated tessellation was blocked or notice missing");
					d->findChild<QPushButton *>("vanillaGenerateHeight")->click(); d->findChild<QComboBox *>("vanillaTessellationMode")->setCurrentIndex(1); d->findChild<QSpinBox *>("vanillaSubdivisionDepth")->setValue(1); d->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength")->setValue(0);
					for(const char *name:{"vanillaReduce","vanillaQualityCleanup","vanillaPatchRetopology","vanillaDarkFeatureBias"}) d->findChild<QCheckBox *>(name)->setChecked(false);
					if(!d->findChild<QPushButton *>("vanillaApply")->isEnabled()) std::cerr<<"Preview diagnostic: "<<d->findChild<QLabel *>("vanillaPreviewStatus")->text().toStdString()<<'\n';
					d->findChild<QPushButton *>("vanillaStageCopy")->click(); require(d->findChild<QLabel *>("vanillaPreviewStatus")->text().contains("passed"),qPrintable(d->findChild<QLabel *>("vanillaPreviewStatus")->text()));
					if(apply) { auto button=d->findChild<QPushButton *>("vanillaApply"); require(button->isEnabled(),"animated Apply disabled"); button->click(); require(d->result()==QDialog::Accepted,qPrintable(d->findChild<QLabel *>("vanillaPreviewStatus")->text())); } else d->reject();
				});
				if(!apply) require(bytes(nif)==before && undo.count()==0,"animated tessellation Cancel changed source");
			}
			auto after=bytes(nif); NifModel original; QBuffer old(&before); old.open(QIODevice::ReadOnly); require(original.load(old),"original reopen failed");
			for(int id=0;id<nif.getBlockCount();++id) if(id!=write) require(bytes(nif,nif.getBlockIndex(id))==bytes(original,original.getBlockIndex(id)),"shader/controller/texture/unrelated block changed");
			NifModel reopen; QBuffer saved(&after); saved.open(QIODevice::ReadOnly); require(reopen.load(saved) && bytes(reopen)==after,"animated candidate reopen failed"); undo.undo(); require(bytes(nif)==before,"animated Undo not exact"); undo.redo(); require(bytes(nif)==after,"animated Redo not exact");
			// Existing material-interpretation restrictions still apply.
			auto index=nif.getBlockIndex(shader); nif.set<unsigned>(index,"Shader Flags 1",nif.get<unsigned>(index,"Shader Flags 1")|ShaderFlags::SLSF1_Projected_UV); auto unsupported=bytes(nif);
			dialogRun([&]() { showSculptGeometry(&nif,nif.getBlockIndex(shape),nullptr); },[](QDialog *d) { auto message=qobject_cast<QMessageBox *>(d); require(message && message->text().contains("unsupported normal or UV"),"unrelated unsafe-mapping restriction was removed"); d->reject(); }); require(bytes(nif)==unsupported,"unsafe-mapping rejection changed source");
			NifModel disk; require(disk.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+asset) && bytes(disk)==diskBefore,"source file changed"); nif.undoStack=nullptr;
			std::cout<<asset<<": animated UV/alpha notice, Sculpt check/Cancel, tessellation stage/Apply/reopen, exact animation-block preservation, Undo/Redo and retained UV safety passed\n";
		}
	} catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
