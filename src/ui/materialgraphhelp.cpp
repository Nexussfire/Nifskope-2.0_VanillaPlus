#include "materialgraphhelp.h"
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHelpEvent>
#include <QLabel>
#include <QHash>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace {
// Embedded popups inherit their node's stacking context. Raise the complete
// node while any of its menus/submenus is open, then restore its previous Z.
class GraphMenus final : public QObject {
public:
	GraphMenus(QDialog *dialog,QGraphicsView *view):QObject(dialog),view(view) { qApp->installEventFilter(this); }
	~GraphMenus() override { qApp->removeEventFilter(this); }
protected:
	bool eventFilter(QObject *object,QEvent *event) override {
		if(event->type()!=QEvent::Show && event->type()!=QEvent::Hide) return false;
		auto menu=qobject_cast<QWidget *>(object);
		if(!menu || menu->windowType()!=Qt::Popup || !view) return false;
		if(event->type()==QEvent::Hide) { release(menu); return false; }
		QGraphicsProxyWidget *proxy=nullptr;
		for(auto w=menu;w && !proxy;w=w->parentWidget()) proxy=w->graphicsProxyWidget();
		if(!proxy || proxy->scene()!=view->scene()) return false;
		while(auto parent=proxy->parentItem()) { auto p=qgraphicsitem_cast<QGraphicsProxyWidget *>(parent); if(!p) break; proxy=p; }
		if(active.contains(menu)) return false;
		if(!levels.contains(proxy)) levels.insert(proxy,proxy->zValue());
		active.insert(menu,proxy); proxy->setZValue(10000);
		connect(menu,&QObject::destroyed,this,[this,menu]() { release(menu); });
		return false;
	}
private:
	void release(QWidget *menu) {
		auto proxy=active.take(menu); if(!proxy) return;
		for(const auto &other:active) if(other==proxy) return;
		if(levels.contains(proxy)) { proxy->setZValue(levels.take(proxy)); }
	}
	QPointer<QGraphicsView> view;
	QHash<QWidget *,QPointer<QGraphicsProxyWidget>> active;
	QHash<QGraphicsProxyWidget *,qreal> levels;
};
class HelpBubble final : public QLabel {
public:
	explicit HelpBubble(QWidget *parent):QLabel(parent) {
		setObjectName("materialGraphHelpBubble"); setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_ShowWithoutActivating); setFocusPolicy(Qt::NoFocus);
		document.setDocumentMargin(0); auto option=document.defaultTextOption(); option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); document.setDefaultTextOption(option); hide();
	}
	void prepare(const QString &source,int width,int height) {
		QString text=source;
		if(source.startsWith("<qt>")) { QTextDocument rich; rich.setHtml(source); text=rich.toPlainText(); }
		if(this->text()!=text) { setText(text); document.setPlainText(text); }
		document.setDefaultFont(font()); document.setTextWidth(std::max(1,width-18));
		resize(width,std::min(height,int(std::ceil(document.size().height()))+18));
	}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QColor(114,129,138)); painter.setBrush(QColor(32,38,45)); painter.drawRoundedRect(rect().adjusted(0,0,-1,-1),4,4);
		painter.setClipRect(rect().adjusted(8,8,-8,-8)); painter.translate(9,9);
		QAbstractTextDocumentLayout::PaintContext context; context.palette.setColor(QPalette::Text,QColor(238,242,244)); document.documentLayout()->draw(&painter,context);
	}
private:
	QTextDocument document;
};

class GraphHelp final : public QObject {
public:
	GraphHelp(QDialog *dialog,QGraphicsView *view):QObject(dialog),dialog(dialog),view(view),bubble(new HelpBubble(dialog)) {
		hover.setSingleShot(true); probe.setSingleShot(true); probe.setInterval(16);
		connect(&probe,&QTimer::timeout,this,[this]() { locate(false); });
		connect(&hover,&QTimer::timeout,this,[this]() { show(); });
		qApp->installEventFilter(this);
		connect(dialog,&QObject::destroyed,this,[this]() { qApp->removeEventFilter(this); });
	}
	~GraphHelp() override { qApp->removeEventFilter(this); }
protected:
	bool eventFilter(QObject *object,QEvent *event) override {
		if(!dialog || !view) return false;
		switch(event->type()) {
		case QEvent::ToolTip: case QEvent::GraphicsSceneHelp: case QEvent::MouseMove: case QEvent::HoverMove: case QEvent::Enter:
		case QEvent::MouseButtonPress: case QEvent::Wheel: case QEvent::KeyPress: case QEvent::WindowDeactivate: case QEvent::Close:
		case QEvent::Hide: case QEvent::Leave: case QEvent::Resize: break;
		default: return false;
		}
		auto widget=qobject_cast<QWidget *>(object);
		bool scene=object==view->scene();
		if(!scene && (!widget || !belongs(widget))) return false;
		if(event->type()==QEvent::ToolTip || event->type()==QEvent::GraphicsSceneHelp) {
			hint=widget?widget:view->viewport();
			if(event->type()==QEvent::ToolTip) position=static_cast<QHelpEvent *>(event)->globalPos();
			else position=QCursor::pos();
			locate(true); return true; // suppress the unconstrained native popup
		}
		if(event->type()==QEvent::MouseMove || event->type()==QEvent::HoverMove || event->type()==QEvent::Enter) {
			hint=widget;
			position=event->type()==QEvent::MouseMove?static_cast<QMouseEvent *>(event)->globalPosition().toPoint():QCursor::pos();
			if(!probe.isActive()) probe.start();
		}
		if(event->type()==QEvent::MouseButtonPress || event->type()==QEvent::Wheel || event->type()==QEvent::KeyPress
			|| event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Close || (event->type()==QEvent::Hide && widget!=bubble)) dismiss();
		if((event->type()==QEvent::Leave || event->type()==QEvent::Resize) && widget==dialog) dismiss();
		if(event->type()==QEvent::Resize && (widget==view || widget==view->viewport())) dismiss();
		return false;
	}
private:
	bool belongs(QWidget *widget) const {
		for(auto w=widget;w;w=w->parentWidget()) {
			if(w==dialog) return true;
			if(auto proxy=w->graphicsProxyWidget();proxy && proxy->scene()==view->scene()) return true;
		}
		return false;
	}
	void dismiss() { hover.stop(); probe.stop(); hint.clear(); if(bubble) { bubble->hide(); if(dialog && bubble->parentWidget()!=dialog) bubble->setParent(dialog); } text.clear(); target=0; }
	void locate(bool immediate) {
		if(!dialog->isVisible()) { dismiss(); return; }
		auto widget=QApplication::widgetAt(position);
		// The event receiver is authoritative for synthetic help/accessibility
		// events too (and avoids relying on a platform-global popup hit test).
		if((!widget || !belongs(widget)) && hint && hint->rect().contains(hint->mapFromGlobal(position))) widget=hint;
		if(!widget || !belongs(widget)) { dismiss(); return; }
		QString help; quintptr identity=0;
		bool inView=(widget==view->viewport() || view->isAncestorOf(widget)) && view->viewport()->rect().contains(view->viewport()->mapFromGlobal(position));
		if(inView) {
			auto scenePos=view->mapToScene(view->viewport()->mapFromGlobal(position));
			for(auto item:view->scene()->items(scenePos)) {
				if(auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) {
					auto root=proxy->widget(); auto local=proxy->mapFromScene(scenePos).toPoint(); widget=root->childAt(local); if(!widget) widget=root;
					break;
				}
				if(!item->toolTip().isEmpty()) { help=item->toolTip(); identity=reinterpret_cast<quintptr>(item); break; }
			}
		}
		if(help.isEmpty()) {
			for(auto w=widget;w && belongs(w);w=w->parentWidget()) {
				if(auto list=qobject_cast<QAbstractItemView *>(w)) { auto index=list->indexAt(list->viewport()->mapFromGlobal(position)); help=index.data(Qt::ToolTipRole).toString(); }
				if(help.isEmpty()) help=w->toolTip();
				if(!help.isEmpty()) { identity=reinterpret_cast<quintptr>(w); break; }
			}
		}
		if(help.isEmpty()) { dismiss(); return; }
		bool changed=target!=identity || text!=help; bool wasVisible=bubble && bubble->isVisible();
		target=identity; text=help; insideView=inView;
		if(changed) { if(bubble) bubble->hide(); hover.start(wasVisible?100:200); }
		if(immediate) { hover.stop(); show(); }
	}
	void show() {
		if(text.isEmpty() || !dialog->isVisible()) return;
		if(!bubble) bubble=new HelpBubble(dialog);
		auto window=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!window || !belongs(window)) window=dialog;
		if(bubble->parentWidget()!=window) bubble->setParent(window);
		QRect bounds=window->rect().adjusted(6,6,-6,-6);
		if(window==dialog && insideView) bounds=bounds.intersected(QRect(view->viewport()->mapTo(dialog,QPoint()),view->viewport()->size()).adjusted(3,3,-3,-3));
		if(bounds.width()<24 || bounds.height()<24) return;
		bubble->prepare(text,std::min(360,bounds.width()),bounds.height());
		QPoint at=window->mapFromGlobal(position)+QPoint(12,18);
		if(at.y()+bubble->height()>bounds.bottom()+1) at.setY(window->mapFromGlobal(position).y()-bubble->height()-12);
		at.setX(std::clamp(at.x(),bounds.left(),bounds.right()-bubble->width()+1));
		at.setY(std::clamp(at.y(),bounds.top(),bounds.bottom()-bubble->height()+1));
		bubble->move(at); bubble->show(); bubble->raise();
	}
	QPointer<QDialog> dialog; QPointer<QGraphicsView> view; QPointer<HelpBubble> bubble; QPointer<QWidget> hint;
	QTimer hover,probe; QPoint position; QString text; quintptr target=0; bool insideView=false;
};
}
void installMaterialGraphHelp(QDialog *dialog,QGraphicsView *view) { new GraphHelp(dialog,view); new GraphMenus(dialog,view); }
