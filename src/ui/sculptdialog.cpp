#include "sculptdialog.h"
#include "geometry/sculpt.h"
#include "glview.h"
#include "spells/mesh.h"
#include "spells/tangentspace.h"
#include <QCheckBox>
#include <QApplication>
#include <QCursor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
using namespace VanillaPlus;
class SculptDialog final : public QDialog {
public:
	SculptDialog(NifModel *model,int shape,int data,bool classic,const Mesh &mesh,
		std::function<QString(const Mesh &,bool)> action,QWidget *parent,const QString &notice)
		: QDialog(parent),session(mesh),preview(model),shapeBlock(shape),dataBlock(data),isClassic(classic),commit(std::move(action))
	{
		setObjectName("sculptWorkspace"); setWindowTitle(tr("Sculpt — temporary working copy")); resize(1100,760);
		auto main=new QVBoxLayout(this);
		if(!notice.isEmpty()) { auto label=new QLabel(notice); label->setObjectName("geometryAnimationNotice"); label->setWordWrap(true); main->addWidget(label); }
		auto row=new QHBoxLayout; main->addLayout(row,1);
		auto controls=new QVBoxLayout; row->addLayout(controls); auto form=new QFormLayout; controls->addLayout(form);
		tool=new QComboBox; tool->setObjectName("sculptTool"); tool->addItems({tr("Push"),tr("Pull"),tr("Flatten"),tr("Smooth"),tr("Restore")}); form->addRow(tr("Brush"),tool);
		double diagonal=0; Vec3 low=mesh.vertices[0].position,high=low;
		for (const auto &v:mesh.vertices) for (int j=0;j<3;++j) { low[j]=std::min(low[j],v.position[j]); high[j]=std::max(high[j],v.position[j]); }
		for (int j=0;j<3;++j) diagonal+=std::pow(double(high[j])-low[j],2);
		diagonal=std::sqrt(diagonal);
		auto number=[form](const QString &name,const char *id,double minimum,double maximum,double value,int decimals) {
			auto box=new QDoubleSpinBox; box->setObjectName(id); box->setDecimals(decimals); box->setRange(minimum,maximum); box->setValue(value); form->addRow(name,box); return box;
		};
		radius=number(tr("Radius (mesh units)"),"sculptRadius",.0001,std::max(1.0,diagonal*2),std::max(.001,diagonal*.1),4);
		strength=number(tr("Strength"),"sculptStrength",0,3,.25,3); falloff=number(tr("Falloff"),"sculptFalloff",.25,8,2,2);
		strength->setToolTip(tr("Higher values deepen Push/Pull strokes and make other brushes converge faster."));
		smoothFalloff=new QCheckBox(tr("Smooth stroke falloff")); smoothFalloff->setObjectName("sculptSmoothFalloff");
		smoothFalloff->setToolTip(tr("Softens accumulated Push/Pull edits within each stroke to reduce sharp pokes and pinches. The starting surface and locked boundaries are preserved."));
		controls->addWidget(smoothFalloff);
		auto releaseCreases=new QCheckBox(tr("Release crease locks")); releaseCreases->setObjectName("sculptReleaseCreases");
		releaseCreases->setToolTip(tr("Allow brushes to edit vertices locked by sharp-angle detection, including existing spikes. UV seams, coincident vertices and open boundaries remain protected. Face-inversion checks stay active."));
		controls->addWidget(releaseCreases);
		connect(releaseCreases,&QCheckBox::toggled,this,[this](bool release) {
			finishStroke(); session.releaseCreaseLocks(release);
			status->setText(tr("%1 vertices locked. %2").arg(std::count(session.lockedVertices().begin(),session.lockedVertices().end(),true))
				.arg(release?tr("Sharp creases are editable; seams and open boundaries remain protected."):tr("Crease protection restored at the current positions.")));
		});
		painting=new QCheckBox(tr("Brush mode")); painting->setObjectName("sculptBrushMode"); painting->setChecked(true); controls->addWidget(painting);
		auto help=new QLabel(tr("Brush: left drag. Shift reverses Push/Pull.\n[ smaller radius / ] larger radius.\nRing shows the local surface radius;\nlocked vertices remain protected.\nAlt + drag navigates; wheel zooms.\nTurn Brush mode off for normal navigation.\n\nSeams and open boundaries remain locked.\nRelease crease locks to work on sharp spikes.\nBrushes use existing vertices; tessellate separately\nwhen more samples are needed.\n\nRevisions exist only in this open workspace.\nApply does not save the NIF.")); help->setWordWrap(true); controls->addWidget(help);
		auto undoRow=new QHBoxLayout; undoButton=new QPushButton(tr("Undo stroke")); redoButton=new QPushButton(tr("Redo stroke"));
		undoButton->setObjectName("sculptUndo"); redoButton->setObjectName("sculptRedo"); undoRow->addWidget(undoButton); undoRow->addWidget(redoButton); controls->addLayout(undoRow);
		connect(undoButton,&QPushButton::clicked,this,[this]() { finishStroke(); session.undo(); refresh(); });
		connect(redoButton,&QPushButton::clicked,this,[this]() { finishStroke(); session.redo(); refresh(); });
		auto undoShortcut=new QShortcut(QKeySequence::Undo,this); connect(undoShortcut,&QShortcut::activated,undoButton,&QPushButton::click);
		auto redoShortcut=new QShortcut(QKeySequence::Redo,this); connect(redoShortcut,&QShortcut::activated,redoButton,&QPushButton::click);
		checkpoints=new QComboBox; checkpoints->setObjectName("sculptCheckpoints"); controls->addWidget(checkpoints);
		auto add=new QPushButton(tr("Save checkpoint…")); add->setObjectName("sculptSaveCheckpoint"); controls->addWidget(add);
		auto restore=new QPushButton(tr("Restore checkpoint")); restore->setObjectName("sculptRestoreCheckpoint"); controls->addWidget(restore);
		connect(add,&QPushButton::clicked,this,[this]() {
			finishStroke(); if (revisions.size()>=8) { status->setText(tr("Eight checkpoints retained. Remove one before adding another.")); return; }
			bool ok=false; auto name=QInputDialog::getText(this,tr("Checkpoint"),tr("Name"),QLineEdit::Normal,tr("Revision %1").arg(revisions.size()+1),&ok);
			if (ok && !name.trimmed().isEmpty()) { revisions.push_back(session.positions()); checkpoints->addItem(name.trimmed()); checkpoints->setCurrentIndex(checkpoints->count()-1); }
		});
		connect(restore,&QPushButton::clicked,this,[this]() {
			finishStroke(); int i=checkpoints->currentIndex(); if (i>=0 && std::size_t(i)<revisions.size()) {
				bool changed=session.restoreCheckpoint(revisions[std::size_t(i)]); refresh(); status->setText(changed?tr("Checkpoint restored; Undo stroke returns to the previous revision."):tr("Checkpoint unchanged or rejected by face safety checks."));
			}
		});
		auto remove=new QPushButton(tr("Remove checkpoint")); controls->addWidget(remove);
		connect(remove,&QPushButton::clicked,this,[this]() { int i=checkpoints->currentIndex(); if (i>=0) { revisions.erase(revisions.begin()+i); checkpoints->removeItem(i); } });
		auto wire=new QCheckBox(tr("Triangle edges")); wire->setObjectName("sculptWireframe"); controls->addWidget(wire);
		controls->addStretch();
		auto viewColumn=new QVBoxLayout; row->addLayout(viewColumn,1); tabs=new QTabBar; tabs->setObjectName("sculptTabs"); tabs->addTab(tr("Sculpt")); tabs->addTab(tr("Original")); viewColumn->addWidget(tabs);
		view=new GLView(nullptr); view->setObjectName("sculptViewport");
		auto container=view->createWindowContainer(this); container->setMouseTracking(true); viewColumn->addWidget(container,1);
		view->setNif(preview); view->setCurrentIndex(preview->getBlockIndex(shapeBlock)); view->setSceneTime(0);
		view->getScene()->options &= ~(Scene::ShowCollision|Scene::ShowNodes|Scene::ShowMarkers|Scene::ShowAxes|Scene::ShowGrid);
		// A quiet, translucent overlay for reading the sculpted surface. Keep
		// this local to the working viewport rather than changing user settings.
		view->getScene()->wireframeColor=FloatVector4(.42f,.58f,.52f,.16f);
		view->installEventFilter(this);
		// Hover work is coalesced instead of doing a GPU pick and triangle scan
		// for every mouse event. Drawing the ring itself never changes geometry.
		auto hoverTimer=new QTimer(this); hoverTimer->setInterval(33);
		connect(hoverTimer,&QTimer::timeout,this,[this]() { if (hoverDirty) { hoverDirty=false; updateBrushRing(); } }); hoverTimer->start();
		connect(view,&GLView::viewpointChanged,this,[this]() { hoverDirty=true; });
		connect(radius,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]() { hoverDirty=true; updateBrushRing(); });
		for (int direction:{-1,1}) {
			auto shortcut=new QShortcut(QKeySequence(direction<0?Qt::Key_BracketLeft:Qt::Key_BracketRight),this);
			connect(shortcut,&QShortcut::activated,this,[this,direction]() { resizeBrush(direction); });
		}
		connect(wire,&QCheckBox::toggled,this,[this](bool enabled) { showWireframe(enabled); });
		connect(tabs,&QTabBar::currentChanged,this,[this]() { finishStroke(); refresh(); });
		connect(painting,&QCheckBox::toggled,this,[this]() { finishStroke(); updateCursor(); });
		connect(tool,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]() { finishStroke(); });
		auto focus=new QPushButton(tr("Frame selected mesh")); focus->setObjectName("sculptFrameSelected"); controls->addWidget(focus);
		connect(focus,&QPushButton::clicked,this,[this,wire]() { view->setCurrentIndex(preview->getBlockIndex(shapeBlock)); view->setCenter(); showWireframe(wire->isChecked()); });
		status=new QLabel; status->setObjectName("sculptStatus"); status->setWordWrap(true); main->addWidget(status);
		auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel); main->addWidget(buttons);
		apply=buttons->button(QDialogButtonBox::Apply); apply->setObjectName("sculptApply");
		auto check=new QPushButton(tr("Check staged copy")); check->setObjectName("sculptCheck"); buttons->addButton(check,QDialogButtonBox::ActionRole);
		auto commitAction=[this](bool applyChanges) { finishStroke(); QString error=commit(session.mesh(),applyChanges); status->setText(error.isEmpty()?tr("Staged copy passed save/reopen checks. The open NIF is unchanged."):error); if (error.isEmpty() && applyChanges) accept(); };
		connect(check,&QPushButton::clicked,this,[commitAction]() { commitAction(false); });
		connect(apply,&QPushButton::clicked,this,[commitAction]() { commitAction(true); });
		connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
		refresh(); QTimer::singleShot(0,this,[this]() { view->center(); showWireframe(false); });
	}
	~SculptDialog() override { view->removeEventFilter(this); view->setNif(nullptr); }
protected:
	bool eventFilter(QObject *object,QEvent *event) override
	{
		if (object!=view) return QDialog::eventFilter(object,event);
		if (event->type()==QEvent::Enter) { pointerInside=true; hoverPosition=view->mapFromGlobal(QCursor::pos()); hoverDirty=true; }
		if (event->type()==QEvent::FocusIn) hoverDirty=true;
		if (event->type()==QEvent::Leave) { pointerInside=false; view->clearBrushRing(); }
		if (event->type()==QEvent::FocusOut || event->type()==QEvent::WindowDeactivate) { finishStroke(); brushGesture=false; view->clearBrushRing(); }
		if (event->type()==QEvent::KeyPress) {
			auto key=static_cast<QKeyEvent *>(event);
			if ((key->key()==Qt::Key_BracketLeft || key->key()==Qt::Key_BracketRight) && !(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))) { resizeBrush(key->key()==Qt::Key_BracketLeft?-1:1); return true; }
			if (key->key()==Qt::Key_Alt) { hoverNavigating=true; view->clearBrushRing(); }
			if (key->key()==Qt::Key_Escape) { finishStroke(); reject(); return true; }
			// Prevent viewport shortcuts that author transforms/vertices in the
			// private model behind the brush session's authoritative buffers.
			if (key->key()!=Qt::Key_Control && key->key()!=Qt::Key_Shift && key->key()!=Qt::Key_Alt) return true;
		}
		if (event->type()==QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Alt) { hoverNavigating=false; hoverDirty=true; }
		if (event->type()==QEvent::Wheel || event->type()==QEvent::Resize) { view->clearBrushRing(); hoverDirty=true; }
		if (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove || event->type()==QEvent::MouseButtonRelease) {
			auto mouse=static_cast<QMouseEvent *>(event);
			pointerInside=true; hoverPosition=mouse->position(); hoverDirty=true;
			hoverNavigating=bool(mouse->modifiers()&Qt::AltModifier) || bool(mouse->buttons()&(Qt::RightButton|Qt::MiddleButton))
				|| bool((mouse->buttons()&Qt::LeftButton) && !brushGesture && event->type()!=QEvent::MouseButtonPress);
			if (hoverNavigating) view->clearBrushRing();
			bool brush=painting->isChecked() && tabs->currentIndex()==0 && !(mouse->modifiers()&Qt::AltModifier);
			if (event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton) brushGesture=brush;
			if (event->type()==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton && brushGesture) { finishStroke(); brushGesture=false; return true; }
			if (brushGesture && !brush && event->type()==QEvent::MouseMove) { finishStroke(); return true; }
			if (brushGesture && ((event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton)
				|| (event->type()==QEvent::MouseMove && (mouse->buttons()&Qt::LeftButton)))) {
				try {
					// Coalesce stroke motion before GPU picking, ray tests and hit
					// traversal; rejected high-rate events need none of that work.
					if(stroking && event->type()==QEvent::MouseMove && (QLineF(lastDab,mouse->position()).length()<3 || dabClock.elapsed()<33)) return true;
					Vector3 nearPoint,farPoint; SculptHit hit;
					if (!view->localRay(preview->getBlockIndex(shapeBlock),mouse->position(),nearPoint,farPoint)) return true;
					if (preview->getBlockNumber(view->indexAt(mouse->position()))!=shapeBlock) return true;
					if (!session.pick({nearPoint[0],nearPoint[1],nearPoint[2]},{farPoint[0],farPoint[1],farPoint[2]},hit)) return true;
					if (!stroking) { session.beginStroke(hit); stroking=true; lastDab=mouse->position(); }
					dabClock.start();
					lastDab=mouse->position(); SculptBrush brushSettings{SculptTool(tool->currentIndex()),radius->value(),strength->value(),falloff->value(),smoothFalloff->isChecked()};
					if (mouse->modifiers()&Qt::ShiftModifier) {
						if (brushSettings.tool==SculptTool::Push) brushSettings.tool=SculptTool::Pull;
						else if (brushSettings.tool==SculptTool::Pull) brushSettings.tool=SculptTool::Push;
					}
					auto changed=session.dab(hit,brushSettings); if(changed) refresh(false);
					status->setText(tr("%1 vertices affected. %2 vertices locked. No NIF changes until Apply.").arg(changed).arg(std::count(session.lockedVertices().begin(),session.lockedVertices().end(),true)));
				} catch (const std::exception &e) { finishStroke(); status->setText(QString::fromUtf8(e.what())); }
				return true;
			}
			if (stroking) { finishStroke(); }
			// Right/middle navigation is safe; viewport transform shortcuts are
			// blocked above, and file drops/context commands are suppressed below.
		}
		if (event->type()==QEvent::Drop || event->type()==QEvent::DragEnter || event->type()==QEvent::ContextMenu || event->type()==QEvent::MouseButtonDblClick) return true;
		return QDialog::eventFilter(object,event);
	}
private:
	void showWireframe(bool enabled) {
		if (enabled) view->setCurrentIndex(preview->getBlockIndex(shapeBlock));
		else {
			// GLView ignores invalid setCurrentIndex requests; explicitly clear
			// the private scene's selection when the overlay checkbox is off.
			view->getScene()->currentBlock=QModelIndex(); view->getScene()->currentIndex=QModelIndex();
		}
		view->update();
	}
	void resizeBrush(int direction) {
		if (!painting->isChecked() || tabs->currentIndex()!=0) return;
		double value=radius->value(),quantum=std::pow(10.0,-radius->decimals());
		radius->setValue(direction<0?std::min(value/1.15,value-quantum):std::max(value*1.15,value+quantum));
	}
	void updateBrushRing() {
		if (!pointerInside || hoverNavigating || !painting->isChecked() || tabs->currentIndex()!=0 || !isActiveWindow()) { view->clearBrushRing(); return; }
		if (hoverPosition.x()<0 || hoverPosition.y()<0 || hoverPosition.x()>=view->width() || hoverPosition.y()>=view->height()) { view->clearBrushRing(); return; }
		Vector3 nearPoint,farPoint; SculptHit hit;
		auto shape=preview->getBlockIndex(shapeBlock);
		if (!view->localRay(shape,hoverPosition,nearPoint,farPoint) || preview->getBlockNumber(view->indexAt(hoverPosition))!=shapeBlock
			|| !session.pick({nearPoint[0],nearPoint[1],nearPoint[2]},{farPoint[0],farPoint[1],farPoint[2]},hit)) { view->clearBrushRing(); return; }
		view->setBrushRing(shape,Vector3(hit.position[0],hit.position[1],hit.position[2]),Vector3(hit.normal[0],hit.normal[1],hit.normal[2]),float(radius->value()));
	}
	void updateCursor() { view->setCursor(painting->isChecked() && tabs->currentIndex()==0?Qt::CrossCursor:Qt::ArrowCursor); hoverDirty=true; if (!painting->isChecked() || tabs->currentIndex()!=0) view->clearBrushRing(); }
	void finishStroke() {
		if (stroking) {
			bool changed=session.endStroke(); stroking=false;
			if (changed) refresh();
			undoButton->setEnabled(session.canUndo()); redoButton->setEnabled(session.canRedo());
		}
	}
	void refresh(bool rebuildTangents=true)
	{
		const Mesh &shown=tabs->currentIndex()==0?session.mesh():session.original();
		bool positionsChanged=shown.vertices.size()!=renderedPositions.size();
		for(std::size_t i=0;!positionsChanged && i<shown.vertices.size();++i) positionsChanged=shown.vertices[i].position!=renderedPositions[i];
		auto normals=positionsChanged?shadingNormals(shown):renderedNormals;
		bool ok=true;
		{
			QSignalBlocker blocker(preview); auto data=preview->getBlockIndex(dataBlock);
			if (isClassic) {
				QVector<Vector3> p,n; for (std::size_t i=0;i<shown.vertices.size();++i) { const auto &v=shown.vertices[i].position; const auto &normal=normals[i]; p.push_back(Vector3(v[0],v[1],v[2])); n.push_back(Vector3(normal[0],normal[1],normal[2])); }
				preview->setArray<Vector3>(data,"Vertices",p); preview->setArray<Vector3>(data,"Normals",n);
				ok=preview->getArray<Vector3>(data,"Vertices")==p && preview->getArray<Vector3>(data,"Normals")==n;
			} else {
				auto vertices=preview->getIndex(data,"Vertex Data");
				for (std::size_t i=0;i<shown.vertices.size();++i) { auto item=preview->getIndex(vertices,int(i)); const auto &p=shown.vertices[i].position; const auto &n=normals[i];
					if (i>=renderedPositions.size() || renderedPositions[i]!=p) ok=preview->set<Vector3>(item,"Vertex",Vector3(p[0],p[1],p[2])) && ok;
					if (i>=renderedNormals.size() || renderedNormals[i]!=n) ok=preview->set<ByteVector3>(item,"Normal",ByteVector3(n[0],n[1],n[2])) && ok;
				}
			}
			// Keep the textured renderer's basis and culling bounds in step with
			// the positions. These existing spells touch only the private copy.
			if (rebuildTangents) { spTangentSpace tangent; tangent.cast(preview,preview->getBlockIndex(shapeBlock)); }
			if(positionsChanged) {
				if (isClassic) { spUpdateCenterRadius bounds; bounds.cast(preview,data); }
				else { spUpdateBounds bounds; bounds.cast(preview,data); }
			}
		}
		renderedPositions.clear(); renderedPositions.reserve(shown.vertices.size()); for (const auto &v:shown.vertices) renderedPositions.push_back(v.position);
		renderedNormals=std::move(normals);
		if (!ok) { renderedPositions.clear(); renderedNormals.clear(); }
		emit preview->dataChanged(preview->getBlockIndex(dataBlock),preview->getBlockIndex(dataBlock)); view->update();
		undoButton->setEnabled(session.canUndo()); redoButton->setEnabled(session.canRedo()); updateCursor();
		bool changed=false; for (std::size_t i=0;i<shown.vertices.size();++i) changed |= session.mesh().vertices[i].position!=session.original().vertices[i].position;
		if (apply) apply->setEnabled(ok && changed);
		if (status) status->setText(ok?tr("Temporary sculpt session: %1 vertices, %2 triangles. Source topology and UVs are preserved.").arg(shown.vertices.size()).arg(shown.triangles.size()):tr("Private preview update failed; Apply disabled."));
	}
	SculptSession session; NifModel *preview; int shapeBlock,dataBlock; bool isClassic;
	std::function<QString(const Mesh &,bool)> commit;
	GLView *view=nullptr; QComboBox *tool,*checkpoints; QDoubleSpinBox *radius,*strength,*falloff;
	QCheckBox *painting,*smoothFalloff; QTabBar *tabs; QLabel *status=nullptr; QPushButton *apply=nullptr,*undoButton,*redoButton;
	std::vector<std::vector<Vec3>> revisions; bool stroking=false,brushGesture=false; QPointF lastDab;
	std::vector<Vec3> renderedPositions,renderedNormals; QElapsedTimer dabClock;
	QPointF hoverPosition; bool pointerInside=false,hoverDirty=false,hoverNavigating=false;
};
}
void showSculptWorkspace(NifModel *preview,int shape,int data,bool classic,const VanillaPlus::Mesh &mesh,
	std::function<QString(const VanillaPlus::Mesh &,bool)> commit,QWidget *parent,const QString &notice)
{
	SculptDialog dialog(preview,shape,data,classic,mesh,std::move(commit),parent,notice); dialog.exec();
}
