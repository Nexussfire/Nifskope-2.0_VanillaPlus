#include "model/nifmodel.h"
#include "model/undocommands.h"
#include "ui/materialgraphdialog.h"
#include "ui/materialuvpreview.h"
#include "gl/controllers.h"
#include "gl/glproperty.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray bytes(const NifModel &nif,QModelIndex block={}) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(block.isValid()?nif.saveIndex(out,block):nif.save(out),"serialization failed"); return data; }
void clone(NifModel &nif,QByteArray source) { QBuffer in(&source); in.open(QIODevice::ReadOnly); require(nif.load(in),"clone failed"); }
template<class T> T *control(QDialog *d,const char *name) {
	if(auto child=d->findChild<T *>(name)) return child;
	if(auto view=d->findChild<QGraphicsView *>("materialGraphView")) for(auto item:view->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(auto child=proxy->widget()->findChild<T *>(name)) return child;
	return nullptr;
}
void drag(QWidget *pad,Qt::MouseButton button,QPointF end) {
	QPointF start(40,40); QMouseEvent press(QEvent::MouseButtonPress,start,start,button,button,Qt::NoModifier);
	QMouseEvent move(QEvent::MouseMove,end,end,Qt::NoButton,button,Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease,end,end,button,Qt::NoButton,Qt::NoModifier);
	QApplication::sendEvent(pad,&press); QApplication::sendEvent(pad,&move); QApplication::sendEvent(pad,&release);
}
void runDialog(NifModel &nif,QModelIndex shader,const std::function<void(QDialog *)> &test) {
	QString error; bool done=false;
	QTimer::singleShot(0,[&]() { auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d) return;
		try { test(d); done=true; } catch(const std::exception &e) { error=e.what(); d->reject(); }
	}); showMaterialGraph(&nif,shader,nullptr); require(done,qPrintable(error));
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings;
	QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("MaterialPanProbe"); app.setApplicationName("MaterialPanProbe");
	try {
		require(argc==2 && NifModel::loadXML(),"expected project root"); QString root=QString::fromLocal8Bit(argv[1]);
		for(const char *asset:{"landscape/rocks/rockm01.nif","weapons/sword/Jeweledlongsword.nif","Weapons/Winterdawn.nif"}) {
			NifModel source; require(source.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+asset),"asset missing"); bool effect=QString(asset).contains("Winterdawn"); int shader=-1;
			for(int b=0;b<source.getBlockCount();++b) if(source.isNiBlock(source.getBlockIndex(b),effect?"BSEffectShaderProperty":"BSLightingShaderProperty") && source.getLink(source.getBlockIndex(b),"Controller")==-1) { shader=b; break; }
			require(shader>=0,"no unanimated material"); auto sourceBytes=bytes(source);
			for(int mode=0;mode<4;++mode) { // Cancel, Apply, invalid, combined short texture set
				if(effect && mode==3) continue;
				NifModel nif; clone(nif,sourceBytes); QUndoStack undo; nif.undoStack=&undo;
				if(mode==3) { auto textures=nif.getBlockIndex(nif.getLink(nif.getBlockIndex(shader),"Texture Set")); nif.set<unsigned>(textures,"Num Textures",0); nif.updateArraySize(nif.getIndex(textures,"Textures")); }
				auto flags=nif.getIndex(nif.getBlockIndex(0),"Flags"); auto original=bytes(nif); auto flagItem=nif.getItem(flags,false); NifValue old=flagItem->value(),next=old; next.set<unsigned>(nif.get<unsigned>(flags)^1,&nif,flagItem);
				ChangeValueCommand::createTransaction(); undo.push(new ChangeValueCommand(flags.siblingAtColumn(NifModel::ValueCol),old,next,"test",&nif)); ChangeValueCommand::createTransaction();
				auto before=bytes(nif); int blocks=nif.getBlockCount();
				runDialog(nif,nif.getBlockIndex(shader),[&](QDialog *d) {
					auto enabled=control<QCheckBox>(d,"materialPanEnabled"); require(enabled && enabled->isEnabled(),"Pan unavailable on unanimated material"); enabled->setChecked(true);
					auto pad=control<QWidget>(d,"materialPanPad"); auto u=control<QDoubleSpinBox>(d,"materialPanSpeedU"),v=control<QDoubleSpinBox>(d,"materialPanSpeedV"); require(pad && u && v,"Pan controls missing");
					drag(pad,Qt::LeftButton,{60,89}); require(std::abs(u->value()-.2)<1e-7 && v->value()==0,"left drag did not isolate U");
					drag(pad,Qt::RightButton,{99,10}); require(std::abs(u->value()-.2)<1e-7 && std::abs(v->value()+.3)<1e-7,"right drag did not isolate V");
					if(mode==0 && effect) { auto view=d->findChild<QGraphicsView *>("materialGraphView"); QApplication::processEvents(); view->centerOn(630,780); QApplication::processEvents(); d->grab().save("release/materialgraph-pan-instruction.png"); }
					require(bytes(nif)==before && undo.count()==1,"staged Pan edited live document");
					if(mode==0) { d->reject(); return; }
					if(mode==2) control<QLineEdit>(d,"materialEmissiveColor")->setText("nan");
					if(mode==3) control<QLineEdit>(d,"materialTexture6")->setText("textures\\effects\\test.dds");
					control<QPushButton>(d,"materialGraphApply")->click();
					if(mode==2) { require(d->result()!=QDialog::Accepted && bytes(nif)==before && undo.count()==1,"invalid Pan Apply was not atomic"); d->reject(); }
					else require(d->result()==QDialog::Accepted,qPrintable(control<QLabel>(d,"materialGraphStatus")->text()));
				});
				if(mode==0 || mode==2) { require(bytes(nif)==before,"Cancel changed model"); continue; }
				require(nif.getBlockCount()==blocks+6 && undo.count()==2,"Pan did not add six blocks in one Undo action"); auto after=bytes(nif); NifModel reopened; clone(reopened,after);
				auto base=nif.get<Vector2>(nif.getBlockIndex(shader),"UV Offset");
				BSLightingShaderProperty lighting(nullptr,{}); BSEffectShaderProperty fx(nullptr,{}); lighting.uvOffset.set(base[0],base[1]); fx.uvOffset=lighting.uvOffset;
				for(int link=nif.getLink(nif.getBlockIndex(shader),"Controller");link>=0;) {
					auto c=nif.getBlockIndex(link); require(nif.getLink(c,"Target")==shader && nif.get<int>(c,"Flags")==72,"wrong Pan controller target/flags");
					std::unique_ptr<Controller> controller=effect?std::unique_ptr<Controller>(new EffectFloatController(&fx,c)):std::unique_ptr<Controller>(new LightingFloatController(&lighting,c)); controller->update(&nif,c); controller->updateTime(1); link=nif.getLink(c,"Next Controller");
				}
				auto actual=effect?fx.uvOffset:lighting.uvOffset; require(std::abs(actual.x-(base[0]+.2))<1e-5 && std::abs(actual.y-(base[1]-.3))<1e-5,"authored Pan differs from renderer");
				int texture=nif.getLink(nif.getBlockIndex(shader),"Texture Set");
				for(int b=0;b<blocks;++b) if(b!=shader && !(mode==3 && b==texture)) require(bytes(nif,nif.getBlockIndex(b))==bytes(source,source.getBlockIndex(b)) || b==0,"unrelated block changed");
				undo.undo(); require(bytes(nif)==before,"Pan Undo not exact"); undo.undo(); require(bytes(nif)==original,"earlier Undo indices broken by Pan"); undo.redo(); require(bytes(nif)==before,"earlier Redo failed"); undo.redo(); require(bytes(nif)==after,"Pan Redo not exact");
				runDialog(nif,nif.getBlockIndex(shader),[&](QDialog *d) { auto enable=control<QCheckBox>(d,"materialPanEnabled"); require(enable->isEnabled(),"authored Pan cannot be edited"); enable->setChecked(true); control<QDoubleSpinBox>(d,"materialPanSpeedU")->setValue(-.4); control<QPushButton>(d,"materialGraphApply")->click(); require(d->result()==QDialog::Accepted,qPrintable(control<QLabel>(d,"materialGraphStatus")->text())); });
				require(nif.getBlockCount()==blocks+6,"editing Pan duplicated controllers"); auto refined=bytes(nif);
				undo.undo(); require(bytes(nif)==after,"existing Pan edit Undo failed"); undo.undo(); require(bytes(nif)==before,"creation Undo after edit failed");
				undo.redo(); require(bytes(nif)==after,"creation Redo after edit failed"); undo.redo(); require(bytes(nif)==refined,"later Pan edit lost its recreated data blocks");
				// A normal block-editor value command must also recover its specific
				// new-block identity after creation is undone and redone.
				int head=nif.getLink(nif.getBlockIndex(shader),"Controller"); auto controller=nif.getBlockIndex(head); auto valueIndex=nif.getIndex(controller,"Phase"); auto item=nif.getItem(valueIndex,false); NifValue oldPhase=item->value(),newPhase=oldPhase;
				newPhase.set<float>(.125f,&nif,item); ChangeValueCommand::createTransaction(); undo.push(new ChangeValueCommand(valueIndex.siblingAtColumn(NifModel::ValueCol),oldPhase,newPhase,"phase",&nif)); ChangeValueCommand::createTransaction(); auto manual=bytes(nif);
				for(int n=0;n<3;++n) undo.undo(); require(bytes(nif)==before,"manual edit/creation Undo failed");
				for(int n=0;n<3;++n) undo.redo(); require(bytes(nif)==manual,"block-editor Redo lost recreated controller identity");
				auto flagIndex=nif.getIndex(nif.getBlockIndex(head),"Flags"); undo.push(new ToggleCheckBoxListCommand(flagIndex.siblingAtColumn(NifModel::ValueCol),quint16(76),"flags",&nif)); auto manualFlags=bytes(nif);
				for(int n=0;n<4;++n) undo.undo(); require(bytes(nif)==before,"flag/creation Undo failed");
				for(int n=0;n<4;++n) undo.redo(); require(bytes(nif)==manualFlags,"block-editor flags Redo lost recreated identity");
				undo.undo(); undo.undo(); undo.undo(); require(bytes(nif)==after,"could not restore first Pan for stale-data check");
				auto interp=nif.getBlockIndex(nif.getLink(nif.getBlockIndex(head),"Interpolator")); auto data=nif.getBlockIndex(nif.getLink(interp,"Data")); auto keys=nif.getIndex(nif.getIndex(data,"Data"),"Keys"); auto last=nif.getIndex(keys,1); float end=nif.get<float>(last,"Value");
				runDialog(nif,nif.getBlockIndex(shader),[&](QDialog *d) {
					control<QCheckBox>(d,"materialPanEnabled")->setChecked(true); control<QDoubleSpinBox>(d,"materialPanSpeedU")->setValue(.7);
					nif.set<float>(last,"Value",end+.25f); auto stale=bytes(nif); int index=undo.index(); control<QPushButton>(d,"materialGraphApply")->click();
					require(d->result()!=QDialog::Accepted && bytes(nif)==stale && undo.index()==index,"stale Pan data was overwritten"); d->reject();
				}); nif.set<float>(last,"Value",end); require(bytes(nif)==after,"stale check restoration failed");
				std::cout<<asset<<" mode "<<mode<<": Pan drag, real renderer evaluation, stage/reopen, earlier Undo preservation and exact Undo/Redo passed\n";
			}
		}
		for(const char *asset:{"Townportal01.nif","maginvturnundead.nif"}) {
			NifModel source; require(source.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+asset),"existing-pan asset missing"); auto original=bytes(source); int covered=0;
			for(int shader=0;shader<source.getBlockCount();++shader) {
				auto s=source.getBlockIndex(shader); bool effect=source.isNiBlock(s,"BSEffectShaderProperty");
				if(!effect && !source.isNiBlock(s,"BSLightingShaderProperty")) continue;
				int selected=-1,axis=-1,dataBlock=-1;
				for(int link=source.getLink(s,"Controller");link>=0;) { auto c=source.getBlockIndex(link); unsigned variable=source.get<unsigned>(c,"Controlled Variable");
					if(variable==(effect?6u:20u) || variable==(effect?8u:22u)) { selected=link; axis=variable==(effect?6u:20u)?0:1; auto interp=source.getBlockIndex(source.getLink(c,"Interpolator")); dataBlock=source.getLink(interp,"Data"); break; }
					link=source.getLink(c,"Next Controller");
				} if(selected<0) continue;
				NifModel nif; clone(nif,original); QUndoStack undo; nif.undoStack=&undo;
				runDialog(nif,nif.getBlockIndex(shader),[&](QDialog *d) { auto enable=control<QCheckBox>(d,"materialPanEnabled"); require(enable->isEnabled(),"supplied existing pan remains disabled"); enable->setChecked(true); control<QPushButton>(d,"materialGraphApply")->click(); require(d->result()==QDialog::Accepted && bytes(nif)==original && undo.count()==0,"activation alone changed existing animation"); });
				double requested=0;
				runDialog(nif,nif.getBlockIndex(shader),[&](QDialog *d) {
					bool diagnostic=control<QLabel>(d,"materialUvPreviewStatus")->text().contains("Diagnostic checker");
					control<QCheckBox>(d,"materialPanEnabled")->setChecked(true); auto speed=control<QDoubleSpinBox>(d,axis?"materialPanSpeedV":"materialPanSpeedU"); requested=-speed->value()*1.75-.003; speed->setValue(requested); requested=speed->value();
					control<QPushButton>(d,"materialAnimationPause")->setChecked(true); control<QDoubleSpinBox>(d,"materialAnimationTime")->setValue(.37);
					if(diagnostic) {
						auto scale=source.get<Vector2>(s,"UV Scale"),base=source.get<Vector2>(s,"UV Offset"); std::array<double,2> offset{base[0],base[1]};
						for(int link=source.getLink(s,"Controller");link>=0;) {
							auto c=source.getBlockIndex(link); unsigned variable=source.get<unsigned>(c,"Controlled Variable"); int channel=variable==(effect?6u:20u)?0:variable==(effect?8u:22u)?1:-1;
							if(channel>=0) {
								struct Clock:Controller { using Controller::Controller; void updateTime(float) override {} } clock(c); clock.update(&source,c);
								auto interp=source.getBlockIndex(source.getLink(c,"Interpolator")),group=source.getIndex(source.getBlockIndex(source.getLink(interp,"Data")),"Data"),keys=source.getIndex(group,"Keys"); int previous=0; float value=0;
								require(Controller::interpolate(value,group,clock.ctrlTime(.37f),previous),"preview reference sample failed"); offset[size_t(channel)]=value;
								if(channel==axis) { double first=source.get<float>(source.getIndex(keys,0),"Value"),last=source.get<float>(source.getIndex(keys,1),"Value"); double oldRate=(last-first)/(clock.stop-clock.start)*clock.frequency; offset[size_t(channel)]=first+(value-first)*requested/oldRate; }
							}
							link=source.getLink(c,"Next Controller");
						}
						QImage checker(16,16,QImage::Format_RGB32); for(int y=0;y<16;++y) for(int x=0;x<16;++x) checker.setPixel(x,y,((x/4+y/4)%2)?qRgb(180,200,190):qRgb(75+5*x,85+4*y,100)); unsigned clamp=source.get<unsigned>(s,"Texture Clamp Mode");
						require(control<QLabel>(d,"materialUvPreview")->pixmap().toImage()==materialUvPreview(checker,{scale[0],scale[1]},offset,clamp&2,clamp&1,160),"curve-speed preview differs from authored easing/phase");
					}
					require(bytes(nif)==original,"curve-speed preview edited the model"); control<QPushButton>(d,"materialGraphApply")->click(); require(d->result()==QDialog::Accepted,qPrintable(control<QLabel>(d,"materialGraphStatus")->text()));
				});
				require(nif.getBlockCount()==source.getBlockCount() && undo.count()==1,"curve edit duplicated blocks or commands");
				for(int block=0;block<nif.getBlockCount();++block) if(block!=dataBlock) require(bytes(nif,nif.getBlockIndex(block))==bytes(source,source.getBlockIndex(block)),"curve-speed edit changed timing, material or unrelated blocks");
				auto c=source.getBlockIndex(selected),group=source.getIndex(source.getBlockIndex(dataBlock),"Data"),keys=source.getIndex(group,"Keys");
				auto first=source.getIndex(keys,0),last=source.getIndex(keys,1); double origin=source.get<float>(first,"Value"),delta=source.get<float>(last,"Value")-origin;
				double rate=delta/(source.get<float>(c,"Stop Time")-source.get<float>(c,"Start Time"))*source.get<float>(c,"Frequency");
				auto editedGroup=nif.getIndex(nif.getBlockIndex(dataBlock),"Data"); require(nif.get<int>(editedGroup,"Interpolation")==source.get<int>(group,"Interpolation"),"quadratic interpolation was flattened");
				struct Clock:Controller { using Controller::Controller; void updateTime(float) override {} } clock(c); clock.update(&source,c);
				for(float t:{0.f,.1f,.9f,2.3f,7.1f,23.4f}) { int a=0,b=0; float oldValue=0,newValue=0; require(Controller::interpolate(oldValue,group,clock.ctrlTime(t),a) && Controller::interpolate(newValue,editedGroup,clock.ctrlTime(t),b),"curve sample failed"); require(std::abs(newValue-(origin+(oldValue-origin)*requested/rate))<.0001,"curve easing/phase changed rather than scaling travel"); }
				auto after=bytes(nif); NifModel reopened; clone(reopened,after); require(bytes(reopened)==after,"curve edit reopen changed data"); undo.undo(); require(bytes(nif)==original,"curve edit Undo not exact"); undo.redo(); require(bytes(nif)==after,"curve edit Redo not exact"); ++covered;
			}
			require(covered>0,"existing pan fixture not exercised"); std::cout<<asset<<": "<<covered<<" pans activated/edited with exact clock, origin, interpolation and other-block preservation\n";
		}
		for(const char *asset:{"aurawhisperbodyfx.nif","healfxhand01.nif","lightspellactorfx.nif","fxfrostballwispyprojectile.nif"}) {
			NifModel nif; require(nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+asset),"sequence asset missing"); QUndoStack undo; nif.undoStack=&undo; auto before=bytes(nif); bool tested=false;
			for(int shader=0;shader<nif.getBlockCount() && !tested;++shader) {
				auto s=nif.getBlockIndex(shader); if(!nif.isNiBlock(s,"BSEffectShaderProperty") || nif.getLink(s,"Controller")<0) continue;
				runDialog(nif,s,[&](QDialog *d) {
					auto choices=control<QComboBox>(d,"materialAnimationSequence"); if(!choices || choices->count()<2) { d->reject(); return; }
					if(control<QCheckBox>(d,"materialPanEnabled")->isEnabled()) { d->reject(); return; }
					require(!control<QCheckBox>(d,"materialPanEnabled")->isEnabled(),"sequence UV arrangement exposed to destructive Pan replacement");
					QElapsedTimer timer; timer.start();
					for(int sequence=1;sequence<choices->count();++sequence) {
						choices->setCurrentIndex(sequence); control<QDoubleSpinBox>(d,"materialAnimationTime")->setValue(.2);
						// Resolve explicit sequence bindings as the viewport's manager does,
						// and compare the graph's evaluated color/opacity numerically.
						auto state=nif.getBlockIndex(choices->currentData().toInt()); QMap<int,int> bindings; auto rows=nif.getIndex(state,"Controlled Blocks");
						for(int row=0;row<nif.rowCount(rows);++row) { auto entry=nif.getIndex(rows,row); bindings[nif.getLink(entry,"Controller")]=nif.getLink(entry,"Interpolator"); }
						BSEffectShaderProperty expected(nullptr,{}); expected.bsVersion=nif.getBSVersion(); expected.emissiveColor=nif.get<Color4>(s,"Base Color"); expected.emissiveMult=nif.get<float>(s,"Base Color Scale");
						for(int link=nif.getLink(s,"Controller");link>=0;) {
							auto c=nif.getBlockIndex(link); QString kind=nif.itemName(c); std::unique_ptr<Controller> controller;
							if(kind=="BSEffectShaderPropertyFloatController") controller=std::make_unique<EffectFloatController>(&expected,c);
							else if(kind=="BSEffectShaderPropertyColorController") controller=std::make_unique<EffectColorController>(&expected,c);
							if(controller) { controller->update(&nif,c); if(bindings.contains(link)) controller->setInterpolator(nif.getBlockIndex(bindings.value(link))); controller->updateTime(.2f); }
							link=nif.getLink(c,"Next Controller");
						}
						auto text=control<QLabel>(d,"materialAnimationResolved")->text();
						auto alpha=QRegularExpression("opacity ([^\\s]+)").match(text);
						require(alpha.hasMatch() && std::abs(alpha.captured(1).toDouble()-expected.emissiveColor[3])<.001,"sequence opacity differs from renderer binding");
						auto rgb=QRegularExpression("RGB ([^,]+), ([^,]+), ([^\\n]+)").match(text); require(rgb.hasMatch(),"resolved RGB missing");
						for(int channel=0;channel<3;++channel) require(std::abs(rgb.captured(channel+1).toDouble()-expected.emissiveColor[channel])<.001,"sequence color differs from renderer binding");
						bool inspected=false; QTimer::singleShot(0,[&]() { auto inspect=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!inspect) return; auto tree=inspect->findChild<QTreeWidget *>("materialAnimationTree"); inspected=tree && tree->topLevelItemCount()>0; inspect->reject(); });
						control<QPushButton>(d,"materialInspectAnimation")->click(); require(inspected,"selected sequence inspector failed");
					}
					require(bytes(nif)==before && undo.count()==0,"state selection modified NIF"); tested=true; std::cout<<asset<<": "<<choices->count()-1<<" states inspected/previewed in "<<timer.elapsed()<<" ms\n"; d->reject();
				});
			}
			require(tested && bytes(nif)==before,"sequence fixture not covered");
		}
		std::cout<<"Pan and named-state regression passed\n"; return 0;
	} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
