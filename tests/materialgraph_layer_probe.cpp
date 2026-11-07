#include "ui/materialgraphdialog.h"
#include "model/nifmodel.h"
#include "glview.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGraphicsProxyWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool v,const char *s) { if(!v) throw std::runtime_error(s); }
QByteArray bytes(NifModel &nif,int id=-1) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(id<0?nif.save(out):nif.saveIndex(out,nif.getBlockIndex(id)),"serialize failed"); return data; }
QByteArray arrayBytes(NifModel &nif,const QModelIndex &index) { QByteArray data; QBuffer out(&data); out.open(QIODevice::WriteOnly); require(nif.saveIndex(out,index),"array serialize failed"); return data; }
void settle(int milliseconds=70) { QElapsedTimer timer; timer.start(); do { QApplication::processEvents(); QThread::msleep(2); } while(timer.elapsed()<milliseconds); }
template<class T> T *control(QDialog *d,const char *name) {
	if(auto child=d->findChild<T *>(name)) return child;
	if(auto view=d->findChild<QGraphicsView *>("materialGraphView")) for(auto item:view->scene()->items()) if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) if(auto child=proxy->widget()->findChild<T *>(name)) return child;
	return nullptr;
}
QDialog *graph(QMainWindow &host) { for(auto d:host.findChildren<QDialog *>("materialGraphWorkspaceDialog")) if(d->isVisible()) return d; return nullptr; }
int shaderOf(NifModel &nif,int id) { auto shape=nif.getBlockIndex(id); int shader=nif.getLink(shape,"Shader Property"); if(shader>=0) return shader;
	for(int child:nif.getChildLinks(id)) if(nif.isNiBlock(nif.getBlockIndex(child),"BSLightingShaderProperty") || nif.isNiBlock(nif.getBlockIndex(child),"BSEffectShaderProperty")) return child; return -1; }
}

int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings;
	QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("MaterialLayerProbe"); app.setApplicationName("MaterialLayerProbe");
	try {
		require(argc>=2,"source path missing"); NifModel::loadXML();
		for(const QString &relative:{QString("Meshes Vanilla Test/meshes/landscape/rocks/rockm01.nif"),QString("Meshes Vanilla Test/meshes/Townportal01.nif"),QString("Meshes Vanilla Test/meshes/weapons/sword/Jeweledlongsword.nif")}) {
			QString path=QString::fromLocal8Bit(argv[1])+"/"+relative; NifModel nif; require(nif.loadFromFile(path),qPrintable("load failed "+path)); QUndoStack undo; nif.undoStack=&undo;
			int selected=-1,parent=-1;
			for(int id=0;id<nif.getBlockCount() && selected<0;++id) if(shaderOf(nif,id)>=0 && (nif.itemName(nif.getBlockIndex(id))=="BSTriShape" || nif.itemName(nif.getBlockIndex(id))=="NiTriShape")) {
				for(int p=0;p<nif.getBlockCount();++p) if(nif.inherits(nif.itemName(nif.getBlockIndex(p)),"NiNode") && nif.getLinkArray(nif.getBlockIndex(p),"Children").contains(id)) { selected=id; parent=p; break; }
			}
			if(selected<0) for(int id=0;id<nif.getBlockCount();++id) std::cerr<<id<<" "<<nif.itemName(nif.getBlockIndex(id)).toStdString()<<" shader "<<shaderOf(nif,id)<<"\n";
			require(selected>=0,"no test shape"); auto initial=bytes(nif); int count=nif.getBlockCount();
			QMainWindow host; auto originalRenderer=new GLView(nullptr); originalRenderer->setNif(&nif); auto originalView=originalRenderer->createWindowContainer(&host); host.setCentralWidget(originalView); host.resize(1050,700); host.show();
			showMaterialGraphWorkspace(&nif,nif.getBlockIndex(selected),&host); settle(); auto first=graph(host); require(first && !first->isModal(),"modeless graph missing");
			if(auto alpha=control<QLineEdit>(first,"materialAlpha")) alpha->setText("0.6");
			auto initialPan=control<QCheckBox>(first,"materialPanEnabled"); require(initialPan && initialPan->isEnabled(),"initial Pan missing"); initialPan->setChecked(true); control<QDoubleSpinBox>(first,"materialPanSpeedU")->setValue(-.2);
			auto create=control<QPushButton>(first,"materialCreateLayer"); require(create && create->isEnabled(),"create layer missing"); create->click(); settle(180);
			auto d=graph(host); require(d && d!=first,"layer graph transition failed"); auto tabs=host.findChild<QTabWidget *>("materialLayerRenderTabs");
			require(tabs && tabs->count()==2 && tabs->widget(0)==originalView,"main render preview tabs missing"); require(bytes(nif)==initial,"creation modified source before Apply");
			control<QDoubleSpinBox>(d,"materialLayerOffset0")->setValue(2.5); control<QDoubleSpinBox>(d,"materialLayerOffset1")->setValue(-1.25); control<QDoubleSpinBox>(d,"materialLayerOffset2")->setValue(.75);
			control<QDoubleSpinBox>(d,"materialLayerRotation2")->setValue(15); control<QDoubleSpinBox>(d,"materialLayerScale")->setValue(1.02);
			require(control<QDoubleSpinBox>(d,"materialPanSpeedU")->value()==-.2,"first graph edits were not inherited by the new layer");
			QElapsedTimer frameTimer; frameTimer.start(); for(int tick=0;tick<120;++tick) { control<QDoubleSpinBox>(d,"materialAnimationTime")->setValue(tick/30.0); QApplication::processEvents(); } std::cout<<"120 layer preview updates: "<<frameTimer.elapsed()<<" ms\n";
			auto pan=control<QCheckBox>(d,"materialPanEnabled"); require(pan && pan->isEnabled(),"copied layer Pan unavailable"); pan->setChecked(true); control<QDoubleSpinBox>(d,"materialPanSpeedU")->setValue(.1);
			control<QPushButton>(d,"materialGraphApply")->click(); settle(120);
			if(graph(host)) std::cerr<<control<QLabel>(graph(host),"materialGraphStatus")->text().toStdString()<<'\n';
			require(!graph(host) && host.centralWidget()==originalView,"Apply did not restore original render space"); require(undo.count()==1 && nif.getBlockCount()>count,"layer not committed as one Undo");
			require(originalRenderer->getScene()->nifModel==&nif,"original renderer was rebound to the private model");
			auto children=nif.getLinkArray(nif.getBlockIndex(parent),"Children"); int added=children.last(); require(added>=count,"layer not appended to same parent");
			auto sourcePosition=nif.get<Vector3>(nif.getBlockIndex(selected),"Translation"),newPosition=nif.get<Vector3>(nif.getBlockIndex(added),"Translation");
			require(newPosition[0]==sourcePosition[0]+2.5f && newPosition[1]==sourcePosition[1]-1.25f && newPosition[2]==sourcePosition[2]+.75f,"wrong local XYZ offset");
			require(shaderOf(nif,added)>=count && shaderOf(nif,added)!=shaderOf(nif,selected),"shader was shared with source");
			for(const char *name:{"Vertex Data","Triangles","Vertex Desc"}) { auto originalArray=nif.getIndex(nif.getBlockIndex(selected),name),newArray=nif.getIndex(nif.getBlockIndex(added),name); if(originalArray.isValid()) require(newArray.isValid() && arrayBytes(nif,originalArray)==arrayBytes(nif,newArray),"copied vertex/index attributes changed"); }
			int oldData=nif.getLink(nif.getBlockIndex(selected),"Data"),newData=nif.getLink(nif.getBlockIndex(added),"Data"); if(oldData>=0) require(newData>=count && bytes(nif,oldData)==bytes(nif,newData),"classic geometry data was not copied exactly");
			NifModel baseline; QBuffer original(&initial); original.open(QIODevice::ReadOnly); require(baseline.load(original),"baseline reopen failed");
			for(int id=0;id<count;++id) if(id!=parent) require(bytes(nif,id)==bytes(baseline,id),"an original unrelated block changed");
			auto candidate=bytes(nif); NifModel reopen; QBuffer saved(&candidate); saved.open(QIODevice::ReadOnly); require(reopen.load(saved) && bytes(reopen)==candidate,"candidate reopen failed");
			undo.undo(); require(bytes(nif)==initial,"Undo not exact"); undo.redo(); require(bytes(nif)==candidate,"Redo not exact");
			showMaterialGraphWorkspace(&nif,nif.getBlockIndex(added),&host); settle(); d=graph(host); require(d,"second graph missing"); control<QPushButton>(d,"materialCreateLayer")->click(); settle(140); d=graph(host); require(d && host.centralWidget()!=originalView,"second layer preview missing"); d->reject(); settle(); require(bytes(nif)==candidate && host.centralWidget()==originalView && undo.count()==1,"Cancel changed document/history/view");
			showMaterialGraphWorkspace(&nif,nif.getBlockIndex(added),&host); settle(); d=graph(host); nif.set<quint16>(nif.getBlockIndex(parent),"Flags",quint16(nif.get<quint16>(nif.getBlockIndex(parent),"Flags")^1)); auto externallyChanged=bytes(nif); control<QPushButton>(d,"materialGraphApply")->click(); require(bytes(nif)==externallyChanged && d->isVisible(),"stale Apply was not refused"); d->reject(); settle();
			// Editing the existing surface still commits through the modeless
			// workspace without making another layer or disturbing earlier Undo.
			showMaterialGraphWorkspace(&nif,nif.getBlockIndex(added),&host); settle(); d=graph(host); control<QCheckBox>(d,"materialPanEnabled")->setChecked(true); control<QDoubleSpinBox>(d,"materialPanSpeedU")->setValue(.35); control<QPushButton>(d,"materialGraphApply")->click(); settle(); auto materialEdited=bytes(nif); require(materialEdited!=externallyChanged && host.centralWidget()==originalView,"selected-surface editing failed"); undo.undo(); require(bytes(nif)==externallyChanged,"selected-surface Undo failed"); undo.redo(); require(bytes(nif)==materialEdited,"selected-surface Redo failed");
			auto unsupported=nif.insertNiBlock("NiTransformController"); nif.setLink(unsupported,"Target",selected); nif.setLink(nif.getBlockIndex(selected),"Controller",nif.getBlockNumber(unsupported)); auto protectedDocument=bytes(nif);
			showMaterialGraphWorkspace(&nif,nif.getBlockIndex(selected),&host); settle(); d=graph(host); create=control<QPushButton>(d,"materialCreateLayer"); require(create && !create->isEnabled() && !create->toolTip().isEmpty(),"shape-animation rejection missing"); d->reject(); settle(); require(bytes(nif)==protectedDocument,"unsupported branch rejection changed the document");
			std::cout<<relative.toStdString()<<": modeless creation, main render placement, independent Pan, reopen, exact Undo/Redo, Cancel and stale protection passed\n";
			nif.undoStack=nullptr;
		}
	} catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
