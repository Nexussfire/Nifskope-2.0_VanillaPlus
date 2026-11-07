#include "materialgraphdialog.h"
#include "glview.h"
#include "data/nifitem.h"
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <algorithm>
#include <cmath>

namespace {
bool document(const NifModel *nif,QByteArray &bytes) { bytes.clear(); QBuffer out(&bytes); out.open(QIODevice::WriteOnly); return nif->save(out); }
bool read(NifModel *nif,const QByteArray &bytes,const QString &file={}) { QBuffer in; in.setData(bytes); in.open(QIODevice::ReadOnly); auto path=file.toUtf8(); return nif->load(in,path.isEmpty()?nullptr:path.constData()); }
bool block(const NifModel *nif,int id,QByteArray &bytes) { bytes.clear(); QBuffer out(&bytes); out.open(QIODevice::WriteOnly); return nif->saveIndex(out,nif->getBlockIndex(id)); }

// Copy only a verified static render branch. Controller targets are remapped by
// loadAndMapLinks; external sequence/manager arrangements are never guessed.
QString layerBranch(const NifModel *nif,int shape,QList<int> &branch,int &parent) {
	auto root=nif->getBlockIndex(shape); auto type=nif->itemName(root);
	if(type!="BSTriShape" && type!="NiTriShape" && type!="NiTriStrips") return QObject::tr("Select a static BSTriShape, NiTriShape or NiTriStrips to create a layer.");
	if(nif->getLink(root,"Skin")>=0 || nif->getLink(root,"Skin Instance")>=0 || nif->getLink(root,"Collision Object")>=0 || nif->getLink(root,"Controller")>=0)
		return QObject::tr("Layer creation currently needs an unskinned shape without collision or shape animation.");
	parent=-1;
	for(int i=0;i<nif->getBlockCount();++i) if(nif->inherits(nif->itemName(nif->getBlockIndex(i)),"NiNode") && nif->getLinkArray(nif->getBlockIndex(i),"Children").contains(shape)) {
			if(parent>=0) return QObject::tr("The shape has multiple scene parents; its layer placement would be ambiguous.");
			parent=i;
	}
	if(parent<0) return QObject::tr("The shape needs one existing scene-node parent for layer placement.");
	const QStringList allowed={"BSTriShape","NiTriShape","NiTriStrips","NiTriShapeData","NiTriStripsData","BSLightingShaderProperty","BSEffectShaderProperty","BSShaderTextureSet","NiAlphaProperty",
		"BSLightingShaderPropertyFloatController","BSLightingShaderPropertyColorController","BSEffectShaderPropertyFloatController","BSEffectShaderPropertyColorController","NiFloatInterpolator","NiPoint3Interpolator","NiFloatData","NiPosData"};
	branch={shape};
	for(int n=0;n<branch.size();++n) {
		int id=branch[n]; auto index=nif->getBlockIndex(id); auto name=nif->itemName(index);
		if(!allowed.contains(name)) return QObject::tr("Layer copying cannot safely isolate %1 yet; the source is unchanged.").arg(name);
		if(nif->inherits(name,"NiTimeController")) for(int other=0;other<nif->getBlockCount();++other)
			if(nif->isNiBlock(nif->getBlockIndex(other),"NiControllerSequence")) {
				for(int link:nif->getChildLinks(other)) if(link==id) return QObject::tr("Sequence-managed controllers cannot be copied as independent layer animation.");
			}
		for(int child:nif->getChildLinks(id)) if(!branch.contains(child)) branch.append(child);
		if(branch.size()>512) return QObject::tr("The selected material branch exceeds the layer safety limit.");
	}
	for(int id:branch) if(nif->inherits(nif->itemName(nif->getBlockIndex(id)),"NiTimeController")) {
		int target=nif->getLink(nif->getBlockIndex(id),"Target"); if(target>=0 && !branch.contains(target)) return QObject::tr("An animation target lies outside this layer.");
	}
	return {};
}

class LayerWorkspace final : public QObject {
public:
	LayerWorkspace(NifModel *source,QMainWindow *host):QObject(host),source(source),host(host) { setObjectName("materialLayerWorkspace"); }
	~LayerWorkspace() override { restoreView(); }
	bool start(const QModelIndex &selection) {
		if(!document(source,original)) return false;
		working=new NifModel(this); if(!read(working,original,source->getFileInfo().absoluteFilePath())) return false;
		working->undoStack=new QUndoStack(working);
		working->refreshFileInfo(source->getFileInfo().absoluteFilePath());
		shape=source->getBlockNumber(selection); openGraph(); return bool(graph);
	}
private:
	void restoreView() {
		if(view) { view->setNif(nullptr); view=nullptr; }
		if(tabs && host && host->centralWidget()==tabs && originalView) {
			host->takeCentralWidget(); tabs->removeTab(tabs->indexOf(originalView)); originalView->setParent(host); host->setCentralWidget(originalView); originalView->show(); delete tabs.data();
		}
		tabs=nullptr; originalView=nullptr;
	}
	QWidget *placementControls() {
		auto group=new QGroupBox(tr("Animated layer placement · temporary local transform")); auto form=new QFormLayout(group);
		auto note=new QLabel(tr("Offsets use the parent node's axes and mesh units. Compare Original / Animated layer in the main render area. Zero offset can overlap the source; transparency is inherited from the source material.")); note->setWordWrap(true); form->addRow(note);
		auto row=[&](const QString &label,const char *prefix,std::array<double,3> &values,double lo,double hi) {
			auto layout=new QHBoxLayout;
			for(int axis=0;axis<3;++axis) { auto number=new QDoubleSpinBox; number->setObjectName(QString(prefix)+QString::number(axis)); number->setRange(lo,hi); number->setDecimals(4); number->setKeyboardTracking(false); number->setSingleStep(.1); number->setPrefix(QString("%1 ").arg("XYZ"[axis])); layout->addWidget(number);
				connect(number,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,&values,axis](double v) { values[size_t(axis)]=v; place(preview); }); }
			form->addRow(label,layout);
		};
		row(tr("Position offset"),"materialLayerOffset",offset,-1000000,1000000); row(tr("Rotation offset (degrees)"),"materialLayerRotation",rotation,-360,360);
		auto size=new QDoubleSpinBox; size->setObjectName("materialLayerScale"); size->setRange(.0001,10000); size->setDecimals(4); size->setValue(1); size->setKeyboardTracking(false); size->setSingleStep(.01); form->addRow(tr("Relative scale"),size);
		connect(size,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double v) { scale=v; place(preview); });
		auto frame=new QPushButton(tr("Frame new layer")); frame->setObjectName("materialLayerFrame"); form->addRow(frame);
		connect(frame,&QPushButton::clicked,this,[this]() { if(view && preview) { view->setCurrentIndex(preview->getBlockIndex(shape)); view->setCenter(); } });
		return group;
	}
	bool place(NifModel *target) {
		if(!target || !layer) return true;
		auto index=target->getBlockIndex(shape); auto p=basePosition;
		for(int j=0;j<3;++j) p[j]+=float(offset[size_t(j)]);
		auto r=rotation==std::array<double,3>{}?baseRotation:Matrix::euler(deg2rad(float(rotation[0])),deg2rad(float(rotation[1])),deg2rad(float(rotation[2])))*baseRotation;
		for(int i=0;i<3;++i) { if(!std::isfinite(p[i])) return false; for(int j=0;j<3;++j) if(!std::isfinite(r(i,j))) return false; }
		if(!std::isfinite(float(baseScale*scale))) return false;
		return target->set<Vector3>(index,"Translation",p) && target->set<Matrix>(index,"Rotation",r) && target->set<float>(index,"Scale",float(baseScale*scale));
	}
	void makeView() {
		if(!host || tabs) return;
		tabs=new QTabWidget(host); tabs->setObjectName("materialLayerRenderTabs"); originalView=host->takeCentralWidget(); tabs->addTab(originalView,tr("Original"));
		view=new GLView(nullptr); view->setObjectName("materialLayerRenderView"); view->setNif(preview); view->getScene()->animate=true;
		auto container=view->createWindowContainer(tabs); tabs->addTab(container,tr("Animated layer · preview")); host->setCentralWidget(tabs); tabs->setCurrentIndex(1);
		connect(view,&GLView::clicked,this,[this](const QModelIndex &) { if(view && preview) view->setCurrentIndex(preview->getBlockIndex(shape)); });
		QTimer::singleShot(0,this,[this]() { if(view && preview) { view->setCurrentIndex(preview->getBlockIndex(shape)); view->center(); view->setCenter(); } });
		// The normal viewport handles navigation only. Block transform/edit keys
		// so the private preview cannot diverge from the placement controls.
		view->installEventFilter(this); container->setAcceptDrops(false);
	}
	void updatePreview(const MaterialGraphPreviewState &state) {
		if(!preview || !view) return;
		auto shader=preview->getBlockIndex(shaderBlock); bool effect=preview->isNiBlock(shader,"BSEffectShaderProperty");
		auto set=[&]<class T>(const char *name,const T &value) { auto i=preview->getIndex(shader,name); if(i.isValid() && preview->get<T>(i)!=value) preview->set<T>(i,value); };
		set("Shader Flags 1",state.flags1); set("Shader Flags 2",state.flags2); if(!effect) set("Shader Type",state.type);
		for(const auto &[name,value]:state.parameters) if(name!="Alpha" && name!="Base Color Scale" && name!="Emissive Multiple") {
			auto i=preview->getIndex(shader,name); if(i.isValid()) { if(name=="Lighting Influence") { if(preview->get<quint8>(i)!=quint8(value)) preview->set<quint8>(i,quint8(value)); } else if(preview->get<float>(i)!=value) preview->set<float>(i,value); }
		}
		set("UV Scale",Vector2(state.scale[0],state.scale[1])); set("UV Offset",Vector2(state.offset[0],state.offset[1]));
		if(effect) { set("Base Color",Color4(state.color[0],state.color[1],state.color[2],state.alpha)); set("Base Color Scale",state.brightness); }
		else { set("Shader Type",state.type); set("Emissive Color",Color3(state.color[0],state.color[1],state.color[2])); set("Specular Color",Color3(state.specular[0],state.specular[1],state.specular[2])); set("Emissive Multiple",state.brightness); set("Alpha",state.alpha); }
		if(effect) { if(!state.paths.isEmpty()) set("Source Texture",state.paths[0]); if(state.paths.size()>1) set("Greyscale Texture",state.paths[1]); }
		else { auto paths=preview->getIndex(preview->getBlockIndex(preview->getLink(shader,"Texture Set")),"Textures"); for(int j=0;j<std::min(preview->rowCount(paths),int(state.paths.size()));++j) { auto i=preview->getIndex(paths,j); if(preview->get<QString>(i)!=state.paths[j]) preview->set<QString>(i,state.paths[j]); } }
		view->setSceneTime(float(state.time)); view->update();
	}
	QString duplicateLayer() {
		QList<int> branch; int parent; auto error=layerBranch(working,shape,branch,parent); if(!error.isEmpty()) return error;
		auto fresh=new NifModel(this); if(!read(fresh,original,source->getFileInfo().absoluteFilePath())) { delete fresh; return tr("Could not create the temporary layer document."); }
		fresh->refreshFileInfo(source->getFileInfo().absoluteFilePath());
		QMap<qint32,qint32> mapping; int start=fresh->getBlockCount(); for(int j=0;j<branch.size();++j) mapping.insert(branch[j],start+j);
		for(int id:branch) if(!fresh->insertNiBlock(working->itemName(working->getBlockIndex(id))).isValid()) { delete fresh; return tr("A layer block could not be created."); }
		for(int id:branch) { QByteArray data; if(!block(working,id,data)) { delete fresh; return tr("A source layer block could not be read."); } QBuffer in(&data); in.open(QIODevice::ReadOnly);
			if(!fresh->loadAndMapLinks(in,fresh->getBlockIndex(mapping[id]),mapping) || in.pos()!=data.size()) { delete fresh; return tr("Layer links could not be isolated."); } }
		auto node=fresh->getBlockIndex(parent); auto children=fresh->getLinkArray(node,"Children"); children.append(mapping[shape]); auto array=fresh->getIndex(node,"Children");
		if(!fresh->set<quint32>(node,"Num Children",quint32(children.size())) || !fresh->updateArraySize(array) || !fresh->setLinkArray(array,children)) { delete fresh; return tr("Could not attach the temporary layer to its scene parent."); }
		// Validate all old blocks against the original snapshot, allowing only
		// the explicit parent's child-list edit. Never retain edits to originals.
		NifModel baseline; read(&baseline,original);
		for(int id=0;id<start;++id) if(id!=parent) { QByteArray a,b; if(!block(&baseline,id,a) || !block(fresh,id,b) || a!=b) { delete fresh; return tr("Layer copying changed an original block."); } }
		if(graph) connect(graph,&QObject::destroyed,working,&QObject::deleteLater);
		working=fresh; working->undoStack=new QUndoStack(working); shape=mapping[shape]; parentBlock=parent; layer=true;
		auto index=working->getBlockIndex(shape); basePosition=working->get<Vector3>(index,"Translation"); baseRotation=working->get<Matrix>(index,"Rotation"); baseScale=working->get<float>(index,"Scale");
		shaderBlock=working->getLink(index,"Shader Property"); if(shaderBlock<0) for(int id:working->getChildLinks(shape)) if(working->isNiBlock(working->getBlockIndex(id),"BSLightingShaderProperty") || working->isNiBlock(working->getBlockIndex(id),"BSEffectShaderProperty")) shaderBlock=id;
		QByteArray data; if(!document(working,data)) return tr("Could not prepare the layer preview."); preview=new NifModel(this); if(!read(preview,data,source->getFileInfo().absoluteFilePath())) return tr("Could not reopen the layer preview."); preview->refreshFileInfo(source->getFileInfo().absoluteFilePath());
		preview->setLink(preview->getBlockIndex(shaderBlock),"Controller",-1);
		makeView(); transitioning=true; QTimer::singleShot(0,this,[this]() { transitioning=false; openGraph(); }); return {};
	}
	QString finish(bool createLayer) {
		QByteArray current; if(!source || !document(source,current) || current!=original) return tr("The open document changed. Cancel and reopen this graph; no layer was applied.");
		if(createLayer) return duplicateLayer();
		if(!place(working)) return tr("Layer placement could not be represented safely.");
		QByteArray candidate; if(!document(working,candidate)) return tr("Could not serialize the candidate.");
		return commitMaterialGraphDocument(source,original,candidate,layer?tr("Create animated material layer"):tr("Edit material graph"));
	}
	void openGraph() {
		if(!source || !host) { deleteLater(); return; }
		MaterialGraphHooks hooks; hooks.allowOverlay=!layer; if(layer) hooks.placement=placementControls();
		if(!layer) { QList<int> branch; int parent; hooks.overlayReason=layerBranch(working,shape,branch,parent); }
		hooks.validate=[this]() { QByteArray now; return source && document(source,now) && now==original?QString():tr("The open document changed. Cancel and reopen the graph; nothing was applied."); };
		hooks.finish=[this](bool create) { return finish(create); }; hooks.preview=[this](const MaterialGraphPreviewState &state) { updatePreview(state); };
		graph=createMaterialGraph(working,working->getBlockIndex(shape),host,std::move(hooks)); if(!graph) { deleteLater(); return; }
		graph->setObjectName("materialGraphWorkspaceDialog"); graph->setAttribute(Qt::WA_DeleteOnClose); graph->setWindowModality(Qt::NonModal);
		connect(graph,&QDialog::finished,this,[this](int) { if(!transitioning) { restoreView(); deleteLater(); } });
		graph->show();
	}
	bool eventFilter(QObject *object,QEvent *event) override {
		if(object==view && event->type()==QEvent::KeyPress) return true;
		return QObject::eventFilter(object,event);
	}
	QPointer<NifModel> source,working,preview; QPointer<QMainWindow> host; QPointer<QDialog> graph; QPointer<GLView> view; QPointer<QTabWidget> tabs; QPointer<QWidget> originalView;
	QByteArray original; int shape=-1,shaderBlock=-1,parentBlock=-1; bool layer=false,transitioning=false;
	Vector3 basePosition; Matrix baseRotation; float baseScale=1; std::array<double,3> offset{},rotation{}; double scale=1;
};
}

void showMaterialGraphWorkspace(NifModel *nif,const QModelIndex &selection,QMainWindow *parent) {
	if(!nif || !parent || !selection.isValid()) { showMaterialGraph(nif,selection,parent); return; }
	if(auto existing=parent->findChild<QDialog *>("materialGraphWorkspaceDialog")) { existing->show(); existing->raise(); existing->activateWindow(); return; }
	auto workspace=new LayerWorkspace(nif,parent); if(!workspace->start(selection)) workspace->deleteLater();
}
