#include "model/nifmodel.h"
#include "ui/materialgraphdialog.h"
#include "geometry/sculpt.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QPushButton>
#include <QUndoStack>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray bytes(const NifModel &nif) { QByteArray out; QBuffer buffer(&out); buffer.open(QIODevice::WriteOnly); require(nif.save(buffer),"save failed"); return out; }
template<class T> T *control(QDialog *d,const char *name) {
	if(auto child=d->findChild<T *>(name)) return child;
	if(auto view=d->findChild<QGraphicsView *>("materialGraphView")) for(auto item:view->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(auto child=proxy->widget()->findChild<T *>(name)) return child;
	return nullptr;
}
void settle(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
quint64 fingerprint(const VanillaPlus::Mesh &mesh) {
	quint64 hash=1469598103934665603ULL;
	for(const auto &v:mesh.vertices) for(float c:v.position) { auto bits=std::bit_cast<quint32>(c); for(int byte=0;byte<4;++byte) { hash^=(bits>>(8*byte))&255; hash*=1099511628211ULL; } }
	return hash;
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings;
	QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("UiAuditProbe"); app.setApplicationName("UiAuditProbe");
	try {
		require(argc==3 && NifModel::loadXML(),"expected project root and tag"); QString root=QString::fromLocal8Bit(argv[1]),tag=QString::fromLocal8Bit(argv[2]); bool before=tag.contains("before");
		QFile file(QCoreApplication::applicationDirPath()+"/newfeatures-performance-"+tag+".csv"); require(file.open(QIODevice::WriteOnly),"report open failed"); QTextStream out(&file); out<<"operation,milliseconds,fingerprint\n";
		auto record=[&](const QString &name,double ms,quint64 hash=0) { out<<name<<','<<ms<<','<<hash<<'\n'; out.flush(); std::cout<<name.toStdString()<<": "<<ms<<" ms; fingerprint "<<hash<<'\n'; };
		VanillaPlus::Mesh grid; constexpr unsigned n=96;
		for(unsigned y=0;y<=n;++y) for(unsigned x=0;x<=n;++x) { VanillaPlus::Vertex v; v.position={float(x),float(y),float(.2*std::sin(x*.2)+.15*std::cos(y*.17))}; v.normal={0,0,1}; v.uv={float(x)/n,float(y)/n}; grid.vertices.push_back(v); }
		for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) { unsigned a=y*(n+1)+x,b=a+n+1; grid.triangles.push_back({a,a+1,b+1}); grid.triangles.push_back({a,b+1,b}); }
		grid=VanillaPlus::captureSource(grid);
		for(auto tool:{VanillaPlus::SculptTool::Push,VanillaPlus::SculptTool::Smooth,VanillaPlus::SculptTool::Flatten,VanillaPlus::SculptTool::Restore}) {
			QElapsedTimer timer; timer.start(); VanillaPlus::SculptSession session(grid); record(QString("sculpt_open_%1").arg(int(tool)),timer.nsecsElapsed()/1e6);
			VanillaPlus::SculptHit hit; timer.restart(); for(int i=0;i<100;++i) require(session.pick({48.2f,48.3f,20},{48.2f,48.3f,-20},hit),"grid pick failed"); record(QString("sculpt_pick_100_%1").arg(int(tool)),timer.nsecsElapsed()/1e6);
			session.beginStroke(hit); timer.restart(); for(int i=0;i<20;++i) session.dab(hit,{tool,10,.35,2,true}); session.endStroke(); record(QString("sculpt_dabs_20_%1").arg(int(tool)),timer.nsecsElapsed()/1e6,fingerprint(session.mesh()));
		}
		for(int scenario=0;scenario<3;++scenario) {
			NifModel nif; require(nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/Weapons/Winterdawn.nif"),"Winterdawn missing"); QUndoStack undo; nif.undoStack=&undo; auto original=bytes(nif); int shader=scenario==0?12:scenario==1?26:61; bool done=false; QString failure;
			QTimer::singleShot(150,[&]() { auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d) return;
				try {
					auto play=control<QCheckBox>(d,"materialAnimationPlay"); auto time=control<QDoubleSpinBox>(d,"materialAnimationTime"); play->setChecked(true); auto pause=control<QPushButton>(d,"materialAnimationPause"); pause->setChecked(true);
					QElapsedTimer timer; timer.start(); for(int frame=0;frame<240;++frame) time->setValue(frame*.017);
					record(QString("graph_frames_240_%1").arg(scenario),timer.nsecsElapsed()/1e6);
					if(scenario==0) {
						d->raise(); d->activateWindow(); settle(30);
						auto target=control<QComboBox>(d,"materialShaderType"); require(target,"target missing");
						record("native_tooltip_style_delay",target->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay));
						QToolTip::hideText(); QCursor::setPos(d->mapToGlobal(QPoint(-20,-20))); settle(50);
						QPoint local=target->rect().center(),global=target->mapToGlobal(local); QElapsedTimer delay; delay.start(); QCursor::setPos(global);
						QMouseEvent move(QEvent::MouseMove,local,global,Qt::NoButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(target,&move);
						QWidget *bubble=nullptr; bool shown=false;
						while(delay.elapsed()<1800) { settle(10); bubble=d->findChild<QWidget *>("materialGraphHelpBubble"); if((bubble && bubble->isVisible()) || QToolTip::isVisible()) { shown=true; break; } }
						if(!before) require(shown,"tooltip never appeared"); record("tooltip_hover_latency",shown?delay.elapsed():-1);
						target->setToolTip(QString("Wrapped help ").repeated(90)+QString(240,'X')); QHelpEvent help(QEvent::ToolTip,local,global); timer.restart(); QApplication::sendEvent(target,&help); settle(20); record("tooltip_long_show",timer.nsecsElapsed()/1e6);
						bubble=d->findChild<QWidget *>("materialGraphHelpBubble");
						if(!before) { require(bubble && bubble->isVisible(),"bounded help missing"); auto bounds=QRect(d->mapToGlobal(QPoint()),d->size()),tip=QRect(bubble->mapToGlobal(QPoint()),bubble->size()); require(bounds.contains(tip) && tip.width()<=360,"help escaped the graph bounds"); require(tip.height()>50,"long help did not wrap"); }
						record("tooltip_width",bubble?bubble->width():0); record("tooltip_height",bubble?bubble->height():0);
						if(!before) {
							target->setToolTip("<qt><b>Shader flag</b><p>Rich help wraps too.</p></qt>"); QApplication::sendEvent(target,&help); settle(10);
							auto label=qobject_cast<QLabel *>(bubble); require(label && label->text().contains("Shader flag") && !label->text().contains("<b>"),"rich help text not preserved");
							QMouseEvent click(QEvent::MouseButtonPress,local,global,Qt::RightButton,Qt::RightButton,Qt::NoModifier); QApplication::sendEvent(target,&click); require(!bubble->isVisible(),"help did not hide for interaction");
							auto view=d->findChild<QGraphicsView *>("materialGraphView"); auto offset=control<QLineEdit>(d,"materialUVOffsetU"); require(view && offset,"UV help target missing");
							for(auto item:view->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item);proxy && proxy->widget()->isAncestorOf(offset)) {
								auto scene=proxy->mapToScene(offset->mapTo(proxy->widget(),offset->rect().center())); auto point=view->mapFromScene(scene); auto absolute=view->viewport()->mapToGlobal(point);
								QHelpEvent embedded(QEvent::ToolTip,point,absolute); QApplication::sendEvent(view->viewport(),&embedded); settle(10);
								auto label=d->findChild<QLabel *>("materialGraphHelpBubble"); require(label && label->isVisible() && label->text().contains("Exact U component"),"embedded node help resolved incorrectly");
								require(QRect(view->viewport()->mapToGlobal(QPoint()),view->viewport()->size()).contains(QRect(label->mapToGlobal(QPoint()),label->size())),"embedded help escaped viewport");
							}
							bool flagHelp=false; QTimer::singleShot(0,[&]() {
								auto flags=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!flags) return;
								settle(80); // finish native modal activation/layout before hovering
								auto box=flags->findChild<QCheckBox *>("materialFlagSpecular"); if(box) { auto point=box->rect().center(); QHelpEvent event(QEvent::ToolTip,point,box->mapToGlobal(point)); QApplication::sendEvent(box,&event); settle(10);
									auto label=flags->findChild<QLabel *>("materialGraphHelpBubble"); flagHelp=label && label->isVisible() && label->text().contains("Specular") && QRect(flags->mapToGlobal(QPoint()),flags->size()).contains(QRect(label->mapToGlobal(QPoint()),label->size()));
									if(!flagHelp) std::cerr<<"Flag help: label="<<bool(label)<<" visible="<<bool(label && label->isVisible())<<" text="<<(label?label->text().toStdString():"absent")<<'\n';
								} flags->reject();
							}); control<QPushButton>(d,"materialShaderFlags")->click(); require(flagHelp,"owned flags dialog help failed");
							QApplication::sendEvent(target,&help); settle(10); require(d->findChild<QWidget *>("materialGraphHelpBubble")->isVisible(),"help lost ownership after flags dialog closed");
							d->grab().save(QCoreApplication::applicationDirPath()+"/materialgraph-bounded-help.png");
						}
					}
					require(bytes(nif)==original && undo.count()==0,"audit changed model"); done=true; d->reject();
				} catch(const std::exception &e) { failure=e.what(); d->reject(); }
			}); showMaterialGraph(&nif,nif.getBlockIndex(shader),nullptr); require(done,qPrintable(failure));
		}
		std::cout<<"New-feature audit passed\n"; return 0;
	} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
