#include "nifskope.h"
#include "glview.h"
#include "model/nifproxymodel.h"
#include "ui/widgets/nifview.h"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoStack>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool v,const char *message) { if(!v) throw std::runtime_error(message); }
void settle(int ms=100) { QElapsedTimer t; t.start(); do { QApplication::processEvents(); QThread::msleep(2); } while(t.elapsed()<ms); }
QByteArray bytes(NifModel *nif) { QByteArray b; QBuffer out(&b); out.open(QIODevice::WriteOnly); require(nif->save(out),"serialize failed"); return b; }
QModelIndex row(NifTreeView *view,const QModelIndex &source) { if(auto proxy=qobject_cast<NifProxyModel *>(view->model())) return proxy->mapFrom(source,QModelIndex()); return source; }
void clickEye(NifTreeView *view,const QModelIndex &source) {
	auto index=row(view,source); require(index.isValid(),"block list row missing"); view->scrollTo(index); settle(); auto gutter=view->findChild<QWidget *>("blockVisibilityGutter"); require(gutter && gutter->isVisible(),"eye gutter missing");
	auto rect=view->visualRect(index); require(rect.height()>0 && rect.top()>=0 && rect.bottom()<gutter->height(),"eye row outside viewport"); QPointF p(11,rect.center().y()),global=gutter->mapToGlobal(p.toPoint());
	QMouseEvent press(QEvent::MouseButtonPress,p,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,p,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
	QApplication::sendEvent(gutter,&press); QApplication::sendEvent(gutter,&release); settle();
}
}
int main(int argc,char **argv) {
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path()); app.setOrganizationName("BlockVisibilityProbe"); app.setApplicationName("BlockVisibilityProbe");
	try {
		require(argc==2 && NifModel::loadXML(),"expected project root");
		for(const char *asset:{"landscape/rocks/rockm01.nif","Townportal01.nif"}) {
			auto window=NifSkope::createWindow(QString::fromLocal8Bit(argv[1])+"/Meshes Vanilla Test/meshes/"+asset); settle(600);
			auto renderer=window->getGLView(); auto scene=renderer->getScene(); QElapsedTimer loading; loading.start(); while(!scene->nifModel && loading.elapsed()<20000) settle(100);
			auto nif=const_cast<NifModel *>(scene->nifModel); if(!nif) { window->grab().save("release/block-visibility-load-failure.png"); std::cerr<<window->windowTitle().toStdString()<<"\n"; } require(nif,"renderer document missing"); auto list=window->findChild<NifTreeView *>("list"); require(list,"normal block list missing");
			settle(300); auto initial=bytes(nif); auto undoCount=nif->undoStack->count(); window->setWindowModified(false);
			Node *shape=nullptr; for(auto n:scene->getNodes()) if(n->parentNode() && (nif->blockInherits(n->index(),"NiGeometry") || nif->blockInherits(n->index(),"BSTriShape"))) { shape=n; break; } require(shape,"renderable shape missing");
			QPersistentModelIndex shapeIndex=shape->index(),parentIndex=shape->parentNode()->index();
			for(bool hierarchy:{true,false}) {
				auto mode=window->findChild<QAction *>(hierarchy?"aHierarchy":"aList"); require(mode,"list-mode action missing"); mode->trigger(); list->expandAll(); settle();
				auto selected=list->currentIndex(); clickEye(list,shapeIndex); shape=scene->nodes.get(shapeIndex); require(shape && shape->isHidden() && shape->isViewportHidden(),"eye did not hide shape"); require(list->currentIndex()==selected,"eye changed block selection");
				clickEye(list,shapeIndex); require(!shape->isHidden(),"eye did not show shape");
				scene->setViewportVisibility(shapeIndex,false); clickEye(list,parentIndex); require(scene->nodes.get(parentIndex)->isHidden() && shape->isHidden(),"parent hide did not cascade"); clickEye(list,parentIndex); require(!scene->nodes.get(parentIndex)->isHidden() && shape->isHidden(),"parent show lost child's hidden choice");
				scene->options|=Scene::ShowHidden; require(shape->isHidden(),"Show Hidden bypassed manual eye state"); scene->clearViewportVisibility(); require(!shape->isHidden(),"reset did not restore authored visibility"); scene->options&=~Scene::ShowHidden;
				if(!hierarchy) { clickEye(list,nif->getIndex(shapeIndex,"Translation")); require(scene->viewportVisibility(shapeIndex)==-1 && !shape->isHidden(),"metadata row received a visibility toggle"); }
				auto gutter=list->findChild<QWidget *>("blockVisibilityGutter"); require(gutter->width()==22 && list->viewport()->x()>=22,"gutter did not reserve its own left space"); window->resize(850,620); settle(); window->resize(1000,700); settle(); require(gutter->height()==list->viewport()->height(),"eye alignment failed after resize");
			}
			require(!window->isWindowModified(),"eye marked the document modified"); require(bytes(nif)==initial && nif->undoStack->count()==undoCount,"eye edited document or Undo state");
			// Authored hidden flags remain intact while a viewport override can
			// reveal the object. Reset goes back to the authored state.
			auto flags=nif->get<unsigned>(shapeIndex,"Flags"); nif->set<unsigned>(shapeIndex,"Flags",flags|1u); settle(); shape=scene->nodes.get(shapeIndex); require(shape->isHidden(),"authored-hidden fixture failed"); auto authored=bytes(nif); clickEye(list,shapeIndex); require(!shape->isHidden() && bytes(nif)==authored,"reveal edited authored flags"); scene->clearViewportVisibility(); require(shape->isHidden(),"reset did not restore authored hidden flag"); nif->set<unsigned>(shapeIndex,"Flags",flags); settle();
			// A harmless block insertion moves numbers but not object identity.
			scene->setViewportVisibility(shapeIndex,false); auto extra=nif->insertNiBlock("NiNode",0); settle(); require(scene->viewportVisibility(shapeIndex)==0 && scene->nodes.get(shapeIndex)->isHidden(),"block renumbering lost hidden object identity"); nif->removeNiBlock(nif->getBlockNumber(extra)); settle(); require(bytes(nif)==initial,"test insertion rollback failed");
			scene->clearViewportVisibility(); renderer->setNif(nif); settle(); require(scene->viewportVisibility(shapeIndex)==-1,"renderer reload retained overrides"); window->setWindowModified(false);
			scene->setViewportVisibility(shapeIndex,false); QByteArray saved=initial; QBuffer reload(&saved); reload.open(QIODevice::ReadOnly); require(nif->load(reload),"document reload failed"); settle(300); require(scene->viewportVisibilityOverrides.isEmpty() && bytes(nif)==initial,"document reload retained eye overrides or changed data"); window->setWindowModified(false);
			window->findChild<QAction *>("aHierarchy")->trigger(); list->expandAll(); list->scrollToTop(); settle(); window->grab().save(QString("release/block-visibility-%1.png").arg(QString(asset).contains("Town")?"portal":"rock"));
			window->close(); settle(); std::cout<<asset<<": native hierarchical/flat eye clicks, parent/child, Show Hidden, authored flags, selection, resize, identity, reset and unchanged NIF/Undo passed\n";
		}
	} catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
