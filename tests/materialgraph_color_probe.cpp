#include "model/nifmodel.h"
#include "ui/materialgraphdialog.h"
#include "ui/widgets/colorwheel.h"
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray bytes(NifModel &nif) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(nif.save(out),"save failed"); return data; }
void settle() { QElapsedTimer t; t.start(); do { QApplication::processEvents(); QThread::msleep(2); } while(t.elapsed()<40); }
template<class T> T *control(QDialog *d,const char *name) {
	if(auto c=d->findChild<T *>(name)) return c;
	for(auto item:d->findChild<QGraphicsView *>("materialGraphView")->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(auto c=proxy->widget()->findChild<T *>(name)) return c;
	return nullptr;
}
void picker(QDialog *graph,const char *name,const std::function<void(QDialog *)> &action) {
	bool done=false; QString failure;
	QTimer::singleShot(0,[&]() { auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!d) return;
		try { require(d->objectName()=="materialColorPicker" && !d->graphicsProxyWidget(),"picker was embedded behind graph nodes"); action(d); done=true; } catch(const std::exception &e) { failure=e.what(); d->reject(); }
	}); auto button=control<QToolButton>(graph,name); require(button,"picker button missing"); button->click(); require(done,qPrintable(failure.isEmpty()?QString("picker did not open"):failure));
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("MaterialColorProbe"); app.setApplicationName("MaterialColorProbe");
	try {
		require(argc==2 && NifModel::loadXML(),"expected project root"); QString root=QString::fromLocal8Bit(argv[1]);
		for(const QString &relative:{QString("landscape/rocks/rockm01.nif"),QString("weapons/sword/Jeweledlongsword.nif"),QString("Weapons/Winterdawn.nif")}) {
			NifModel nif; require(nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+relative),"asset load failed"); QUndoStack undo; nif.undoStack=&undo;
			int shader=-1; bool effect=relative.contains("Winterdawn"); for(int id=0;id<nif.getBlockCount();++id) if(nif.isNiBlock(nif.getBlockIndex(id),effect?"BSEffectShaderProperty":"BSLightingShaderProperty")) { shader=id; break; }
			require(shader>=0,"shader missing"); auto index=nif.getBlockIndex(shader); if(effect) nif.set<Color4>(index,"Base Color",Color4(3.25f,.7500001f,0,.8f)); else nif.set<Color3>(index,"Emissive Color",Color3(3.25f,.7500001f,0));
			auto original=bytes(nif); MaterialGraphPreviewState lastState; MaterialGraphHooks hooks; hooks.preview=[&](const MaterialGraphPreviewState &state) { lastState=state; }; std::unique_ptr<QDialog> graph(createMaterialGraph(&nif,index,nullptr,std::move(hooks))); require(bool(graph),"graph missing"); graph->show(); settle();
			auto emission=control<QLineEdit>(graph.get(),"materialEmissiveColor"); auto initial=emission->text();
			picker(graph.get(),"materialEmissivePicker",[](QDialog *d) { require(d->findChild<ColorWheel *>("materialColorWheel"),"existing NifSkope color wheel missing"); d->findChild<QDoubleSpinBox *>("materialColorGain")->setValue(9); d->reject(); }); require(emission->text()==initial && bytes(nif)==original,"picker Cancel changed data");
			picker(graph.get(),"materialEmissivePicker",[](QDialog *d) { d->accept(); }); require(emission->text()==initial,"unchanged picker quantized HDR color");
			if(effect) {
				picker(graph.get(),"materialEmissivePicker",[](QDialog *d) { d->findChild<QDoubleSpinBox *>("materialColorAlpha")->setValue(.125); d->accept(); });
				require(emission->text().section(' ',0,2)==initial.section(' ',0,2),"alpha-only edit changed RGB precision");
			} else {
				require(control<QLineEdit>(graph.get(),"materialSpecularColor") && control<QLabel>(graph.get(),"materialEnvironmentColorInfo"),"specular/environment inputs missing");
				picker(graph.get(),"materialSpecularPicker",[](QDialog *d) { d->findChild<ColorWheel *>()->setColor(QColor::fromRgbF(.05,.1,.15)); d->findChild<QDoubleSpinBox *>("materialColorGain")->setValue(2); d->accept(); });
				require(lastState.color[0]==3.25f && std::abs(lastState.specular[0]-.1)<.0001,"specular preview was mixed into emission");
				// Exercise embedded menus across adjacent node bounds, including
				// nested menus and restoration after the final popup closes.
				auto view=graph->findChild<QGraphicsView *>("materialGraphView"); QGraphicsProxyWidget *node=nullptr;
				for(auto item:view->scene()->items()) if(auto p=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(p->widget()->findChild<QLineEdit *>("materialEmissiveColor")) node=p;
				require(node,"surface node missing"); qreal z=node->zValue(); QMenu menu(emission); for(int i=0;i<12;++i) menu.addAction(QString("Action %1").arg(i)); auto child=menu.addMenu("More"); child->addAction("Nested action");
				menu.popup(emission->mapToGlobal(QPoint(0,emission->height()))); settle(); require(menu.graphicsProxyWidget() && node->zValue()>100,"popup did not raise its node above neighbors");
				child->popup(menu.mapToGlobal(QPoint(menu.width(),40))); settle(); require(node->zValue()>100,"submenu lost stacking"); child->hide(); settle(); require(node->zValue()>100 && menu.isVisible(),"submenu dismissal lowered an active parent menu"); menu.hide(); settle(); require(node->zValue()==z,"popup dismissal did not restore node stacking");
			}
			picker(graph.get(),"materialEmissivePicker",[](QDialog *d) { d->findChild<ColorWheel *>()->setColor(QColor::fromRgbF(.2,.4,.6)); d->findChild<QDoubleSpinBox *>("materialColorGain")->setValue(2); d->accept(); });
			require(bytes(nif)==original,"choosing colors changed the NIF before Apply"); control<QPushButton>(graph.get(),"materialGraphApply")->click(); require(graph->result()==QDialog::Accepted,"Apply failed");
			if(!effect) { auto c=nif.get<Color3>(index,"Specular Color"); require(std::abs(c[0]-.1)<.0001 && std::abs(c[2]-.3)<.0001,"specular color not stored"); }
			auto candidate=bytes(nif); NifModel reopened; QBuffer in(&candidate); in.open(QIODevice::ReadOnly); require(reopened.load(in) && bytes(reopened)==candidate,"color candidate reopen failed"); undo.undo(); require(bytes(nif)==original,"color Undo not exact"); undo.redo(); require(bytes(nif)==candidate,"color Redo not exact");
			std::cout<<relative.toStdString()<<": existing wheel, HDR/no-op/Cancel, alpha precision, specular persistence, menu/submenu stacking and exact Undo/Redo passed\n";
			nif.undoStack=nullptr;
		}
	} catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
