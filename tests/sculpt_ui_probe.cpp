#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include "geometry/sculpt.h"
#include "glview.h"
#include "animated_shader_fixture.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QSpinBox>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <bit>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
QByteArray bytes(const NifModel &model) { QByteArray result; QBuffer buffer(&result); buffer.open(QIODevice::WriteOnly); require(model.save(buffer),"save failed"); return result; }
VanillaPlus::Mesh geometry(const NifModel &model,int block) {
	VanillaPlus::Mesh mesh; auto shape=model.getBlockIndex(block); auto rows=model.getIndex(shape,"Vertex Data");
	for (int i=0;i<model.rowCount(rows);++i) {
		auto row=model.getIndex(rows,i); auto p=model.get<Vector3>(row,"Vertex"),n=model.get<Vector3>(row,"Normal"); auto uv=model.get<HalfVector2>(row,"UV");
		VanillaPlus::Vertex v; v.position={p[0],p[1],p[2]}; v.normal={n[0],n[1],n[2]}; v.uv={uv[0],uv[1]}; mesh.vertices.push_back(v);
	}
	for (auto t:model.getArray<Triangle>(shape,"Triangles")) mesh.triangles.push_back({t[0],t[1],t[2]});
	return VanillaPlus::captureSource(mesh);
}
std::vector<VanillaPlus::Vec3> positions(const NifModel &model,int block) { auto mesh=geometry(model,block); std::vector<VanillaPlus::Vec3> result; for (auto v:mesh.vertices) result.push_back(v.position); return result; }
void click(GLView *view,const QPointF &p) {
	QMouseEvent press(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
	QApplication::sendEvent(view,&press); QApplication::sendEvent(view,&release);
}
void settle() { QEventLoop loop; QTimer::singleShot(100,&loop,&QEventLoop::quit); loop.exec(); }
void key(GLView *view,int code) {
	QKeyEvent press(QEvent::KeyPress,code,Qt::NoModifier),release(QEvent::KeyRelease,code,Qt::NoModifier);
	QApplication::sendEvent(view,&press); QApplication::sendEvent(view,&release);
}
QRect ringBounds(const QImage &image,const QImage &withoutRing) {
	QRect bounds;
	for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
		auto color=image.pixelColor(x,y),base=withoutRing.pixelColor(x,y);
		if (color.red()>base.red()+30 && color.red()>color.green()+10 && color.green()>color.blue()+20) bounds=bounds.united(QRect(x,y,1,1));
	}
	return bounds;
}
}
int main(int argc,char **argv)
{
	QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
	QTemporaryDir settings; QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
	app.setOrganizationName("SculptProbe"); app.setApplicationName("SculptProbe");
	QSettings().setValue("Settings/Render/General/Camera/Field Of View",45);
	try {
		require((argc==3 || argc==4) && NifModel::loadXML(),"expected NIF and textures"); NifModel nif; const bool animated=argc==4;
		require(nif.loadFromFile(QString::fromLocal8Bit(argv[1])),"NIF load failed");
		nif.getGameResources().close_archives(); nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
		QUndoStack undo; nif.undoStack=&undo;
		int block=-1; for (int i=0;i<nif.getBlockCount();++i) if (nif.isNiBlock(nif.getBlockIndex(i),"BSTriShape")) { block=i; break; }
		require(block>=0,"no shape"); const auto untouched=bytes(nif);
		if(animated) addAnimatedShaderFixture(nif,nif.getLink(nif.getBlockIndex(block),"Shader Property"));
		// Prove use as both a preparation pass and a final pass on tessellated geometry.
		for (int tessellated=0;tessellated<2;++tessellated) {
			if (tessellated) {
				bool applied=false;
				QTimer::singleShot(0,[&]() {
					auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
					try {
						if(animated) require(dialog->findChild<QLabel *>("geometryAnimationNotice"),"animated tessellation notice missing");
						dialog->findChild<QPushButton *>("vanillaGenerateHeight")->click();
						dialog->findChild<QComboBox *>("vanillaTessellationMode")->setCurrentIndex(1);
						dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth")->setValue(1);
						dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength")->setValue(0);
						for (const char *name:{"vanillaReduce","vanillaQualityCleanup","vanillaPatchRetopology","vanillaDarkFeatureBias"}) dialog->findChild<QCheckBox *>(name)->setChecked(false);
						dialog->findChild<QPushButton *>("vanillaApply")->click(); applied=dialog->result()==QDialog::Accepted;
						if (!applied) { std::cerr << dialog->findChild<QLabel *>("vanillaPreviewStatus")->text().toStdString() << '\n'; dialog->reject(); }
					} catch (...) { dialog->reject(); }
				});
				showVanillaPlusGeometry(&nif,nif.getBlockIndex(block),nullptr); require(applied,"preparation tessellation failed");
			}
			const auto before=bytes(nif); auto baseline=positions(nif,block); auto sourceGeometry=geometry(nif,block); int undoBefore=undo.index();
			// Each operation gets real window events and the renderer's ray. Tests
			// need a native OpenGL platform, not Qt's offscreen QWindow stub.
			for (int mode=0;mode<3;++mode) { // Cancel, Apply, reject stale source
				bool done=false; QString failure; QTimer driver; driver.setInterval(150); int attempts=0;
				QObject::connect(&driver,&QTimer::timeout,[&]() {
					auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
					if (!dialog || dialog->objectName()!="sculptWorkspace") return;
					try {
						if(animated) require(dialog->findChild<QLabel *>("geometryAnimationNotice"),"animated Sculpt notice missing");
						GLView *view=nullptr;
						for (auto window:QGuiApplication::allWindows()) if (window->objectName()=="sculptViewport" && window->isVisible()) {
							auto root=window; while (root->parent()) root=root->parent();
							if (root==dialog->windowHandle()) view=qobject_cast<GLView *>(window);
						}
						require(view,"missing viewport");
						if (!view->isValid() || !view->getScene()->renderer) { require(++attempts<100,"GL viewport never initialized"); return; }
						driver.stop();
						require(!view->getScene()->currentBlock.isValid(),"wireframe remained selected while overlay checkbox is off");
						auto privateModel=const_cast<NifModel *>(view->getScene()->nifModel); require(privateModel && privateModel!=&nif,"preview is not private");
						auto radius=dialog->findChild<QDoubleSpinBox *>("sculptRadius"); radius->setValue(radius->maximum()/4);
						auto strength=dialog->findChild<QDoubleSpinBox *>("sculptStrength"); require(strength && strength->maximum()==3,"brush strength maximum is not three");
						auto smoothing=dialog->findChild<QCheckBox *>("sculptSmoothFalloff"); require(smoothing && !smoothing->isChecked(),"missing optional falloff smoothing");
						auto releaseCreases=dialog->findChild<QCheckBox *>("sculptReleaseCreases"); require(releaseCreases && !releaseCreases->isChecked(),"crease protection is not the default");
						auto protectedCopy=bytes(*privateModel); releaseCreases->setChecked(true); releaseCreases->setChecked(false);
						require(bytes(*privateModel)==protectedCopy && bytes(nif)==before,"protection toggle changed geometry"); releaseCreases->setChecked(mode==1);
						strength->setValue(mode==1?3:.5); smoothing->setChecked(mode==1);
						auto original=positions(*privateModel,block); bool moved=false; QPointF brushPoint;
						for (int y=2;y<12 && !moved;++y) for (int x=2;x<12 && !moved;++x) {
							QPointF point(double(view->width())*x/14,double(view->height())*y/14); Vector3 a,b;
							if (!view->localRay(privateModel->getBlockIndex(block),point,a,b)) continue;
							click(view,point); moved=positions(*privateModel,block)!=original; if (moved) brushPoint=point;
						}
						require(moved,"real brush events affected no editable vertices"); auto sculpted=positions(*privateModel,block);
						require(bytes(nif)==before,"stroke changed live NIF");
						dialog->findChild<QPushButton *>("sculptUndo")->click(); require(positions(*privateModel,block)==original,"stroke Undo failed");
						dialog->findChild<QPushButton *>("sculptRedo")->click(); require(positions(*privateModel,block)==sculpted,"stroke Redo failed");
						if(mode==0) {
							QMouseEvent press(QEvent::MouseButtonPress,brushPoint,brushPoint,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
							QApplication::sendEvent(view,&press); auto afterPress=positions(*privateModel,block); QElapsedTimer timer; timer.start();
							for(int event=0;event<50;++event) { QMouseEvent move(QEvent::MouseMove,brushPoint,brushPoint,Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(view,&move); }
							std::cout<<"Sculpt stationary moves (50), "<<sourceGeometry.triangles.size()<<" triangles: "<<timer.nsecsElapsed()/1e6<<" ms\n";
							require(positions(*privateModel,block)==afterPress,"coalesced motion changed geometry");
							QMouseEvent release(QEvent::MouseButtonRelease,brushPoint,brushPoint,Qt::LeftButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(view,&release);
							if(afterPress!=sculpted) dialog->findChild<QPushButton *>("sculptUndo")->click();
							require(positions(*privateModel,block)==sculpted,"coalesced-stroke Undo failed");
						}
						auto tabs=dialog->findChild<QTabBar *>("sculptTabs"); tabs->setCurrentIndex(1); require(positions(*privateModel,block)==original,"Original tab not original");
						tabs->setCurrentIndex(0); require(positions(*privateModel,block)==sculpted,"Sculpt tab lost revision");
						if (mode==0) {
							auto paint=dialog->findChild<QCheckBox *>("sculptBrushMode");
							dialog->activateWindow(); view->requestActivate();
							QCursor::setPos(view->mapToGlobal(brushPoint.toPoint()));
							radius->setValue(radius->maximum()/25); paint->setChecked(false); settle(); auto noRing=view->grabFramebuffer();
							paint->setChecked(true);
							QMouseEvent hover(QEvent::MouseMove,brushPoint,brushPoint,Qt::NoButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(view,&hover);
							settle(); auto small=view->grabFramebuffer(); auto smallBounds=ringBounds(small,noRing);
							require(!smallBounds.isEmpty(),"brush influence ring is not visible on hover"); double oldRadius=radius->value();
							key(view,Qt::Key_BracketRight); require(radius->value()>oldRadius,"right bracket did not enlarge radius"); settle();
							auto largeBounds=ringBounds(view->grabFramebuffer(),noRing);
							require(largeBounds.width()>smallBounds.width() || largeBounds.height()>smallBounds.height(),"ring did not grow with brush radius");
							if (tessellated) dialog->screen()->grabWindow(dialog->winId()).save("release/sculpt-brush-ring.png");
							key(view,Qt::Key_BracketLeft); require(radius->value()<oldRadius*1.01,"left bracket did not shrink radius");
							radius->setValue(radius->minimum()); key(view,Qt::Key_BracketLeft); require(radius->value()==radius->minimum(),"radius fell below minimum");
							key(view,Qt::Key_BracketRight); require(radius->value()>radius->minimum(),"radius stuck at minimum precision");
							radius->setValue(radius->maximum()); key(view,Qt::Key_BracketRight); require(radius->value()==radius->maximum(),"radius exceeded maximum");
							radius->setValue(oldRadius); tabs->setCurrentIndex(1); double frozen=radius->value(); key(view,Qt::Key_BracketRight); require(radius->value()==frozen,"Original tab accepted brush resize");
							settle(); require(ringBounds(view->grabFramebuffer(),noRing).isEmpty(),"Original tab retained brush ring"); tabs->setCurrentIndex(0);
							paint->setChecked(false); settle(); require(ringBounds(view->grabFramebuffer(),noRing).isEmpty(),"navigation mode retained brush ring"); paint->setChecked(true);
							require(positions(*privateModel,block)==sculpted && bytes(nif)==before,"ring or bracket keys changed geometry");
							QTimer::singleShot(0,[]() { if (auto input=qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { input->setTextValue("Test revision"); input->accept(); } });
							dialog->findChild<QPushButton *>("sculptSaveCheckpoint")->click();
							require(dialog->findChild<QComboBox *>("sculptCheckpoints")->count()==1,"named checkpoint not saved");
							dialog->findChild<QPushButton *>("sculptUndo")->click();
							dialog->findChild<QPushButton *>("sculptRestoreCheckpoint")->click(); require(positions(*privateModel,block)==sculpted,"named checkpoint restoration failed");
							dialog->findChild<QPushButton *>("sculptUndo")->click(); require(positions(*privateModel,block)==original,"checkpoint Undo failed");
							dialog->findChild<QPushButton *>("sculptRedo")->click(); require(positions(*privateModel,block)==sculpted,"checkpoint Redo failed");
						}
						auto check=dialog->findChild<QPushButton *>("sculptCheck"); auto apply=dialog->findChild<QPushButton *>("sculptApply"); auto status=dialog->findChild<QLabel *>("sculptStatus");
						check->click(); require(status->text().contains("passed save/reopen"),qPrintable(status->text())); require(bytes(nif)==before,"Check changed live model");
						if (mode==2) {
							auto row=nif.getIndex(nif.getIndex(nif.getBlockIndex(block),"Vertex Data"),0); auto p=nif.get<Vector3>(row,"Vertex"); p[0]+=.25f; nif.set<Vector3>(row,"Vertex",p); auto edited=bytes(nif);
							apply->click(); require(dialog->result()!=QDialog::Accepted && bytes(nif)==edited,"stale Apply changed live source"); require(status->text().contains("changed"),"stale Apply missing explanation");
							dialog->reject();
						} else if (mode==1) {
							dialog->findChild<QCheckBox *>("sculptWireframe")->setChecked(true); QApplication::processEvents();
							if (tessellated) dialog->screen()->grabWindow(dialog->winId()).save("release/sculpt-workspace.png");
							apply->click(); require(dialog->result()==QDialog::Accepted,qPrintable(status->text()));
						}
						else dialog->reject();
						done=true;
					} catch(const std::exception &e) { failure=QString::fromUtf8(e.what()); driver.stop(); dialog->reject(); }
				});
				driver.start(); showSculptGeometry(&nif,nif.getBlockIndex(block),nullptr); driver.stop(); require(done,qPrintable(failure));
				if (mode==0) require(bytes(nif)==before && undo.index()==undoBefore,"Cancel changed source/history");
				if (mode==1) {
					auto after=bytes(nif); require(after!=before && undo.index()==undoBefore+1,"Apply did not commit one command");
					NifModel reopened; QBuffer input(&after); input.open(QIODevice::ReadOnly); require(reopened.load(input),"sculpt NIF cannot reopen");
					for (int i=0;i<nif.getBlockCount();++i) if (i!=block) {
						QByteArray a,b; QBuffer ba(&a),bb(&b); ba.open(QIODevice::WriteOnly); bb.open(QIODevice::WriteOnly);
						NifModel source; QBuffer oldInput(const_cast<QByteArray *>(&before)); oldInput.open(QIODevice::ReadOnly); require(source.load(oldInput),"source reload failed");
						require(source.saveIndex(ba,source.getBlockIndex(i)) && nif.saveIndex(bb,nif.getBlockIndex(i)) && a==b,"unrelated block changed");
					}
					auto source=geometry(reopened,block); require(source.triangles==sourceGeometry.triangles && source.vertices.size()==sourceGeometry.vertices.size(),"triangle connectivity changed on reopen");
					for (std::size_t i=0;i<source.vertices.size();++i) for (int j=0;j<2;++j)
						require(std::bit_cast<quint32>(source.vertices[i].uv[j])==std::bit_cast<quint32>(sourceGeometry.vertices[i].uv[j]),"UV bits changed on Apply/reopen");
					undo.undo(); require(bytes(nif)==before,"document Undo not exact"); undo.redo(); require(bytes(nif)==after,"document Redo not exact");
					// Sculpt must invalidate the previous retained tessellation reference.
					bool fresh=false; QTimer::singleShot(0,[&]() { auto d=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!d) return; auto label=d->findChild<QLabel *>("vanillaReferenceStatus"); fresh=label && label->text().contains("New Vanilla+ operation"); d->reject(); });
					showVanillaPlusGeometry(&nif,nif.getBlockIndex(block),nullptr); require(fresh,"sculpt retained a stale pre-sculpt tessellation reference");
					undo.undo(); require(bytes(nif)==before,"return to source after reference check failed");
				}
				if (mode==2) { QBuffer reset(const_cast<QByteArray *>(&before)); reset.open(QIODevice::ReadOnly); require(nif.load(reset),"reset failed"); nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2])); }
			}
			std::cout << (tessellated?"Post-tessellation":"Original mesh") << ": native viewport stroke, Original/Sculpt tabs, stage/reopen, Cancel, Apply, exact document and stroke Undo/Redo, stale-source rejection passed\n";
		}
		// Files were never saved. Reloading the fixture remains byte-identical.
		NifModel disk; require(disk.loadFromFile(QString::fromLocal8Bit(argv[1])) && bytes(disk)==untouched,"source file changed");
		return 0;
	} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
