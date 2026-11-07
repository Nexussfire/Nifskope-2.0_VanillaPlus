#include "model/nifmodel.h"
#include "ui/materialgraphdialog.h"
#include "ui/materialuvpreview.h"
#include "gl/glcontroller.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray bytes(const NifModel &nif,QModelIndex block={}) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(block.isValid()?nif.saveIndex(out,block):nif.save(out),"serialize failed"); return data; }
template<class T> T *control(QDialog *dialog,const char *name) {
	if(auto child=dialog->findChild<T *>(name)) return child;
	if(auto view=dialog->findChild<QGraphicsView *>("materialGraphView"))
		for(auto item:view->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(auto child=proxy->widget()->findChild<T *>(name)) return child;
	return nullptr;
}
QString allText(QTreeWidget *tree) {
	QString text; std::function<void(QTreeWidgetItem *)> visit=[&](QTreeWidgetItem *item) { text+=item->text(0)+" "+item->text(1)+"\n"; for(int j=0;j<item->childCount();++j) visit(item->child(j)); };
	for(int i=0;i<tree->topLevelItemCount();++i) visit(tree->topLevelItem(i)); return text;
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings;
	QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
	app.setOrganizationName("MaterialEffectsProbe"); app.setApplicationName("MaterialEffectsProbe");
	try {
		require(argc==3 && NifModel::loadXML(),"expected project and texture root"); QString root=QString::fromLocal8Bit(argv[1]);
		for(const char *asset:{"Weapons/Winterdawn.nif","Weapons/1stpersonWinterdawn.nif","Townportal01.nif"}) {
			QString path=root+"/Meshes Vanilla Test/meshes/"+asset; QFile file(path); require(file.open(QIODevice::ReadOnly),"asset missing"); auto disk=file.readAll();
			NifModel source; require(source.loadFromFile(path),"load failed"); auto before=bytes(source);
			for(int block=0;block<source.getBlockCount();++block) {
				auto original=source.getBlockIndex(block); bool effect=source.isNiBlock(original,"BSEffectShaderProperty");
				if(!effect && !source.isNiBlock(original,"BSLightingShaderProperty")) continue;
				for(int mode=0;mode<(effect?4:1);++mode) { // Cancel, Apply, invalid, stale
					NifModel nif; QBuffer in(&before); in.open(QIODevice::ReadOnly); require(nif.load(in),"clone failed"); QUndoStack undo; nif.undoStack=&undo;
					nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
					bool done=false; QString failure; auto shader=nif.getBlockIndex(block);
					QElapsedTimer opening; opening.start();
					QTimer::singleShot(0,[&]() {
						auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d) return;
						try {
							qint64 openNs=opening.nsecsElapsed(); auto play=control<QCheckBox>(d,"materialAnimationPlay"); auto time=control<QDoubleSpinBox>(d,"materialAnimationTime"); auto image=control<QLabel>(d,"materialUvPreview");
							require(play && time && image,"missing playback controls"); require(control<QWidget>(d,"materialUVOffsetU"),"UV controls lost");
							require(!effect || (control<QLineEdit>(d,"materialBaseColorScale") && control<QLineEdit>(d,"materialFalloffStartAngle") && !control<QLineEdit>(d,"materialTexture2")),"effect adapter fields incorrect");
							play->setChecked(true); QElapsedTimer timer; timer.start();
							for(int frame=0;frame<120;++frame) time->setValue(frame*.017);
							double frameMs=timer.nsecsElapsed()/120000000.0;
							require(frameMs<20,"preview updates exceed 20 ms budget");
							require(bytes(nif)==before && undo.count()==0,"playback mutated model");
							if(QString(asset)=="Townportal01.nif" && block==9) {
								auto pause=control<QPushButton>(d,"materialAnimationPause"); require(pause,"pause missing"); pause->click();
								double frozen=time->value(); QEventLoop loop; QTimer::singleShot(90,&loop,&QEventLoop::quit); loop.exec(); require(time->value()==frozen,"paused clock advanced");
								pause->click(); QTimer::singleShot(90,&loop,&QEventLoop::quit); loop.exec(); require(time->value()>frozen,"playback clock did not advance"); pause->click();
							}
							// Known Townportal pan: direct controller timing/interpolation must
							// produce the same image as the graph (diagnostic texture, no resources).
							if((QString(asset)=="Townportal01.nif" && (block==9 || block==17)) || (QString(asset)=="Weapons/Winterdawn.nif" && block==61)) {
								time->setValue(1); auto c=nif.getBlockIndex(nif.getLink(shader,"Controller"));
								struct Clock:Controller { using Controller::Controller; void updateTime(float) override {} } clock(c); clock.update(&nif,c);
								auto interp=nif.getBlockIndex(nif.getLink(c,"Interpolator")); auto data=nif.getBlockIndex(nif.getLink(interp,"Data")); float v=0; int last=0;
								require(Controller::interpolate(v,nif.getIndex(data,"Data"),clock.ctrlTime(1),last),"expected pan keys missing");
								QImage diagnostic(16,16,QImage::Format_RGB32); for(int y=0;y<16;++y) for(int x=0;x<16;++x) diagnostic.setPixel(x,y,((x/4+y/4)%2)?qRgb(180,200,190):qRgb(75+5*x,85+4*y,100));
								auto scale=nif.get<Vector2>(shader,"UV Scale"),offset=nif.get<Vector2>(shader,"UV Offset"); unsigned clamp=nif.get<unsigned>(shader,"Texture Clamp Mode");
								std::array<double,2> animatedOffset{offset[0],offset[1]}; animatedOffset[block==17?0:1]=v;
								require(image->pixmap().toImage()==materialUvPreview(diagnostic,{scale[0],scale[1]},animatedOffset,clamp&2,clamp&1,160),"animated pan differs from renderer interpolation");
							}
							if(QString(asset)=="Weapons/Winterdawn.nif" && block==26) {
								time->setValue(2); auto text=control<QLabel>(d,"materialAnimationResolved")->text();
								require(text.contains("0.8824") && text.contains("0.5961") && text.contains("0.4392"),"RGB controller playback missing");
							}
							bool inspected=false; QTimer::singleShot(0,[&]() {
								auto inspect=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!inspect) return;
								auto tree=inspect->findChild<QTreeWidget *>("materialAnimationTree");
								if(tree) { auto text=allText(tree); inspected=true;
								if(block==26 && QString(asset)=="Weapons/Winterdawn.nif") {
									inspected=text.contains("RGB keys") && text.contains("RGB (") && text.contains("seconds per loop");
									if(tree->topLevelItemCount()) { auto top=tree->topLevelItem(0); for(int j=0;j<top->childCount();++j) if(top->child(j)->text(0)=="RGB keys") tree->setCurrentItem(top->child(j)); }
									inspect->grab().save("release/materialgraph-rgb-inspector.png");
								}
									if(effect && block==39 && QString(asset)=="Weapons/Winterdawn.nif") inspected=text.contains("Constant value");
									if(tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(0));
								}
								inspect->reject();
							}); control<QPushButton>(d,"materialInspectAnimation")->click(); require(inspected,"animation inspection incomplete");
							std::cout<<asset<<" #"<<block<<" mode "<<mode<<" open "<<openNs/1000000.0<<" ms; frame "<<frameMs<<" ms\n";
							if(effect) {
								control<QLineEdit>(d,"materialBaseColorScale")->setText(mode==2?"nan":"2.5"); control<QLineEdit>(d,"materialEmissiveColor")->setText("0.25 0.5 0.75 0.6");
								control<QLineEdit>(d,"materialTexture0")->setText("textures\\effects\\test.dds");
								control<QLineEdit>(d,"materialLightingInfluence")->setText("128");
								control<QLineEdit>(d,"materialFalloffStartAngle")->setText("0.75");
								if(block==39 && mode==0 && QString(asset)=="Weapons/Winterdawn.nif") d->grab().save("release/materialgraph-effects.png");
							}
							if(mode==3) nif.set<float>(shader,"Soft Falloff Depth",987);
							if(mode==0) d->reject(); else { control<QPushButton>(d,"materialGraphApply")->click();
								if(mode>=2) { require(d->result()!=QDialog::Accepted && undo.count()==0,"unsafe Apply accepted"); d->reject(); }
								else require(d->result()==QDialog::Accepted,qPrintable(control<QLabel>(d,"materialGraphStatus")->text()));
							}
							done=true;
							} catch(const std::exception &e) { failure=e.what(); d->reject(); }
					});
					showMaterialGraph(&nif,shader,nullptr); require(done,qPrintable(failure));
					if(mode==0 || mode==2) require(bytes(nif)==before,"Cancel/invalid input changed document");
					if(mode==1) {
						require(undo.count()==1,"Apply not one Undo"); auto after=bytes(nif); NifModel reopened; QBuffer data(&after); data.open(QIODevice::ReadOnly); require(reopened.load(data),"effect save/reopen failed");
						require(reopened.get<float>(reopened.getBlockIndex(block),"Base Color Scale")==2.5f,"effect value lost");
						require(reopened.get<quint8>(reopened.getBlockIndex(block),"Lighting Influence")==128 && reopened.get<float>(reopened.getBlockIndex(block),"Falloff Start Angle")==.75f,"effect lighting/falloff values lost");
						for(int j=0;j<nif.getBlockCount();++j) if(j!=block) require(bytes(nif,nif.getBlockIndex(j))==bytes(source,source.getBlockIndex(j)),"unrelated/controller block changed");
						undo.undo(); require(bytes(nif)==before,"effect Undo not exact"); undo.redo(); require(bytes(nif)==after,"effect Redo not exact");
					}
				}
			}
			file.seek(0); require(file.readAll()==disk,"source asset overwritten");
		}
		std::cout<<"Effect materials, RGB/float inspection, animated sampling, performance, Cancel, stale, Apply/reopen and exact Undo/Redo passed.\n"; return 0;
	} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
