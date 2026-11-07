#include "materialgraphdialog.h"
#include "materialgraphhelp.h"
#include "materialuvpreview.h"
#include "model/nifmodel.h"
#include "model/undocommands.h"
#include "data/nifitem.h"
#include "gl/glproperty.h"
#include "gl/controllers.h"
#include "ui/widgets/filebrowser.h"
#include "ui/widgets/colorwheel.h"
#include "ddstxt16.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGraphicsProxyWidget>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QPointer>
#include <QScrollBar>
#include <QScrollArea>
#include <QUndoStack>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct LightingFeatures {
	bool height=false,glow=false,environment=false,inner=false,slot2=false,slot3=false,known=false;
};
QString lightingFlagHelp(int word,int bit)
{
	// Keep undocumented engine bits explicit rather than promising invented behavior.
	static const char *tips[2][32]={
		{
			"Enables specular highlights from lighting. Appearance depends on Glossiness, Specular strength and the normal/specular texture data.",
			"Marks a skinned material. Requires an already skinned mesh; this does not create skin weights or a skin instance.",
			"Selects a temporary or specialized refraction mode. Exact Skyrim SE triggering conditions and shader requirements are undocumented.",
			"Uses the alpha component of mesh vertex colors. Visible transparency also depends on the alpha property and blending setup.",
			"Uses grayscale texture values to select colors from a palette, primarily in effect shaders. This lighting-material graph does not create the effect shader's palette setup.",
			"Uses grayscale texture values to derive alpha from a palette, primarily in effect shaders. Requires the corresponding palette and effect-shader setup.",
			"Enables effect-shader falloff using configured falloff parameters. It is not a universal Fresnel switch for lighting materials and does not add effect-shader data here.",
			"Enables cubemap reflections with the Environment shader, slot 4 and Environment strength. A missing cubemap gives no reflection.",
			"Allows the object to receive shadows in the game. Visible results depend on scene lighting and shadow settings.",
			"Allows the object to cast shadows in the game. Visible results depend on scene lighting and shadow settings.",
			"FaceGen material setup, also documented as Facegen Detail Map. The bundled schema specifies detail in slot 3 and face tint in slot 6 with the FaceGen shader. Slot numbers here are zero-based: slot 3 is the fourth texture.",
			"This NifSkope renderer uses this flag with the Parallax shader and slot-3 height; the bundled schema also calls for Vertex Colors. Published NifTools documentation marks the bit uncertain. Runtime support depends on the game's shader/rendering setup. This does not displace mesh geometry.",
			"Interprets the normal map in model space, with an external specular map. Does not convert textures. The schema excludes Back Lighting when Specular is enabled.",
			"Selects specialized non-projective shadow handling. Its exact runtime effect is not well documented; it is not a general shadow-quality control.",
			"Identifies a landscape-oriented shader configuration. Intended for the terrain material pipeline, not a general-purpose material enhancement. Does not create terrain or its texture setup.",
			"Uses normal-map information for refraction. Requires an appropriate material and rendering setup; it does not create transparent geometry.",
			"Selects specialized fire or heat-distortion refraction behavior. Exact triggering conditions and required shader configuration are uncertain.",
			"Eye reflections: the schema requires the Eye shader and an already skinned model. Does not create skinning data.",
			"Hair-specific lighting behavior that limits excessive brightness under lights, with the matching hair shader. Also documented as Hair Soft Lighting; this build's schema names the bit Hair Tint.",
			"Selects screen-door-style fading: a pixel coverage pattern rather than ordinary alpha blending. This describes the intended mode; exact Skyrim thresholds and runtime conditions are not documented.",
			"The schema says this hides the object and objects below it from the game's local-map view.",
			"Enables tint-mask coloring for compatible face/skin materials. Also documented as FaceGen RGB Tint; this build's schema calls the bit Skin Tint. Requires the matching shader and tint-texture setup.",
			"Uses the material's own emissive color and strength. Black emission color emits nothing. This does not create a light source.",
			"Selects projected UV or decal-related mapping; exact behavior depends on shader configuration. The ordinary base-UV preview cannot represent this mapping and is disabled when it is set.",
			"Indicates a shader mode involving multiple texture inputs. It does not automatically combine arbitrary texture slots. The exact arrangement is undocumented, so the base-UV preview is disabled.",
			"Marks textures for a specialized remapping mechanism. Exact Skyrim SE usage is not established; this does not repack UVs or replace texture files.",
			"Identifies decal-oriented rendering. Requires suitable depth, blending and shader settings; it does not create decal geometry. Exact runtime rules remain shader-dependent.",
			"Selects or identifies a specialized dynamic decal mode. Exact runtime behavior is uncertain; it does not create moving decals, controllers or geometry.",
			"Selects parallax-occlusion-related rendering where the shader supports it. Requires the appropriate shader and height setup; setting the bit alone does not implement the effect. The base-UV preview cannot represent it.",
			"Indicates emission may be controlled by an external source rather than only local material values. Exact Skyrim requirements are undocumented; it does not create an external source or a light.",
			"Selects a specialized soft-effect rendering mode. Exact application and requirements are not well documented. It does not smooth mesh geometry or act as a universal blur control.",
			"Enables depth testing against the depth buffer. Disabling it can make surfaces draw over geometry that would normally hide them."
		},
		{
			"Writes surface depth to the depth buffer so later geometry can be occluded. Disabling can help some transparency setups, but can also produce incorrect overlap or sorting.",
			"Landscape LOD flag. It does not generate landscape LOD meshes; the schema does not document the full engine setup.",
			"Object LOD flag. It does not generate object LOD meshes; the schema does not document the full engine setup.",
			"Requests bypassing certain engine fading behavior. Which fade systems are affected is not fully documented; this is not a guarantee against every kind of fading.",
			"Renders both sides of triangles instead of culling their back faces. Useful for thin surfaces; it does not add thickness or duplicate geometry.",
			"Uses authored vertex colors when present. Does not add vertex-color data to the mesh.",
			"Uses slot 2 as a glow map with the Glow shader and Own emission. The map modulates emission color and strength.",
			"Assume-shadowmask flag. Its precise Skyrim behavior is not documented in the bundled schema.",
			"This build's schema calls this Character Lighting and associates it with actors, head parts and FaceGen BSDynamicTriShape meshes. Published NifTools documentation calls the same bit Packed Tangent. The interpretation differs between sources; toggling it does not repack tangent data or change the mesh vertex format.",
			"Multi-index snow flag. Its complete data requirements are not documented here; setting it does not author snow indices or textures.",
			"Selects a vertex-lighting-related shader mode. Its precise interaction with Skyrim's lighting pipeline is uncertain; it does not calculate or bake vertex lighting here.",
			"Indicates a specialized uniform-scaling mode. Its renderer-specific effect is not established; it does not change the mesh transform.",
			"Indicates slope-fitting behavior for supported geometry or effects. Exact application is undocumented; this does not fit or move vertices here.",
			"Billboard-oriented rendering for compatible objects, typically intended to face the camera. Exact engine behavior is not fully documented, and this does not create billboard nodes.",
			"Requests disabling a landscape LOD blending behavior. Relevant to terrain transitions rather than ordinary material enhancement; exact runtime scope is undocumented. Does not generate LOD geometry.",
			"Lighting-dependent fading of environment mapping; the exact response is not fully documented. The bundled schema lists it for Environment, Eye EnvMap and MultiLayer Parallax shaders.",
			"Wireframe material flag. The schema notes it appears to work only on particles; it is separate from NifSkope's viewport wireframe control.",
			"Used for weapon blood decals. Requires the appropriate engine/material setup; it does not add blood textures or geometry.",
			"Local-map hiding flag. The schema suggests it hides only the object itself, but marks that interpretation as uncertain.",
			"Indicates premultiplied-alpha color data, where RGB has already been multiplied by alpha. Blend settings must match; incorrect use can produce dark or bright fringes. This does not modify texture files.",
			"Identifies a cloud-specific level-of-detail rendering mode. Exact Skyrim behavior and asset requirements are not established; it does not generate cloud LOD geometry.",
			"Directional lighting behavior associated with compatible hair shaders. The bundled schema marks hair usage as tentative, so exact Skyrim behavior should not be assumed for ordinary materials.",
			"Requests disabling a transparency-related multisampling behavior. It may affect edge quality, but exact renderer conditions are uncertain. This does not change the viewport's global antialiasing setting.",
			"Unused01: the schema marks this bit as possibly unused. No verified visual effect is documented.",
			"Enables multilayer/inner-layer parallax mapping. Requires the appropriate material data and textures; the base-UV preview cannot represent it.",
			"Enables specialized soft lighting using the associated material map, commonly for skin-like materials. Requires compatible shader settings. The bundled schema says it is incompatible with Rim Lighting.",
			"Enables rim lighting that emphasizes surface edges or the silhouette, using the associated material map and compatible shader settings. The bundled schema says it is incompatible with Soft Lighting.",
			"Enables specialized backlighting using the associated material map, often for thin or translucent-looking surfaces. The bundled schema excludes combining it with Model Space Normals and Specular.",
			"Unused02: the schema marks this bit as possibly unused. No verified visual effect is documented.",
			"Enables tree vertex/flutter animation. The schema requires a BSTreeNode or BSLeafAnimNode root. This does not create animation nodes or data.",
			"Selects specialized effect-lighting behavior. Exact Skyrim SE usage and shader requirements are not fully documented; it is not a general-purpose lighting-quality switch.",
			"High-detail object LOD flag. It does not generate LOD geometry; its precise Skyrim behavior is not documented in the bundled schema."
		}
	};
	return QString::fromUtf8(tips[word][bit]);
}
bool blockBytes(const NifModel *nif,const QModelIndex &block,QByteArray &bytes)
{
	bytes.clear(); QBuffer buffer(&bytes);
	return buffer.open(QIODevice::WriteOnly) && nif->saveIndex(buffer,block);
}

struct MaterialBlockEdit {
	QPersistentModelIndex index;
	QByteArray before,after;
};
bool replaceMaterialBlocks(NifModel *model,const std::array<MaterialBlockEdit,2> &edits,bool forward)
{
	if (!model) return false;
	for (const auto &edit:edits) {
		QByteArray bytes;
		if (!edit.index.isValid() || !blockBytes(model,edit.index,bytes) || bytes!=(forward?edit.before:edit.after)) return false;
	}
	auto load=[model](const QModelIndex &index,const QByteArray &bytes) {
		QBuffer input; input.setData(bytes); input.open(QIODevice::ReadOnly); QByteArray result;
		return model->loadIndex(input,index) && input.pos()==bytes.size() && blockBytes(model,index,result) && result==bytes;
	};
	bool ok=true;
	for (const auto &edit:edits) if (edit.before!=edit.after && !load(edit.index,forward?edit.after:edit.before)) { ok=false; break; }
	if (!ok) for (const auto &edit:edits)
		if (!load(edit.index,forward?edit.before:edit.after)) qCritical("Material graph could not restore a block after a failed replacement");
	for (const auto &edit:edits) if (edit.before!=edit.after) emit model->dataChanged(edit.index,edit.index);
	return ok;
}
class ExpandedMaterialCommand final : public QUndoCommand {
public:
	ExpandedMaterialCommand(NifModel *nif,std::array<MaterialBlockEdit,2> edits,std::shared_ptr<bool> initial)
		: model(nif),blocks(std::move(edits)),success(std::move(initial)) { setText(QObject::tr("Edit material graph")); }
	void redo() override { change(true); }
	void undo() override { change(false); }
private:
	void change(bool forward) {
		bool ok=replaceMaterialBlocks(model,blocks,forward);
		if (success) { *success=ok; success.reset(); }
		if (!ok) { setObsolete(true); QMessageBox::warning(nullptr,QObject::tr("Material Graph"),QObject::tr("The material changed; replacement was refused and current data was retained.")); }
	}
	QPointer<NifModel> model;
	std::array<MaterialBlockEdit,2> blocks;
	std::shared_ptr<bool> success;
};

struct TexturePreviews { QPixmap thumbnail; QImage surface; };
TexturePreviews texturePreviews(const QByteArray &bytes,bool surface)
{
	if (bytes.isEmpty()) return {};
	try {
		DDSTexture16 texture(reinterpret_cast<const unsigned char *>(bytes.constData()),size_t(bytes.size()),0,true);
		if (texture.getIsCubeMap()) return {};
		auto image=[&](int width,int height) {
			QImage out(width,height,QImage::Format_RGB32);
			for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
				int sx=int((x+0.5)*texture.getWidth()/width),sy=int((y+0.5)*texture.getHeight()/height);
				FloatVector4 c=FloatVector4::convertFloat16(texture.getPixelC(sx,sy,0));
				auto channel=[](float f) { return int(255*std::clamp(std::isfinite(f)?f:0.0f,0.0f,1.0f)); };
				out.setPixel(x,y,qRgb(channel(c[0]),channel(c[1]),channel(c[2])));
			}
			return out;
		};
		TexturePreviews out; out.thumbnail=QPixmap::fromImage(image(48,48));
		if (surface) out.surface=image(std::min(256,int(texture.getWidth())),std::min(256,int(texture.getHeight())));
		return out;
	} catch (...) { return {}; }
}

class UvOffsetPad final : public QWidget {
public:
	std::function<void(double,double)> onChange;
	explicit UvOffsetPad(QWidget *parent=nullptr) : QWidget(parent) {
		setObjectName("materialUvOffsetPad"); setFixedSize(96,96);
		setToolTip(tr("Drag to change sampled U/V; Shift locks the dominant axis and Ctrl adjusts finely. Positive sampled U moves the texture left."));
	}
	void setOffset(double u,double v) { currentU=u; currentV=v; }
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(),QColor(32,38,45));
		p.setPen(QPen(QColor(94,109,119),1)); p.drawRect(rect().adjusted(0,0,-1,-1));
		p.drawLine(8,48,88,48); p.drawLine(48,8,48,88);
		p.setPen(QColor(190,211,213)); p.drawText(QRect(67,48,25,20),Qt::AlignCenter,"U →");
		p.drawText(QRect(35,72,30,20),Qt::AlignCenter,"V ↓");
		p.setBrush(QColor(116,208,185)); p.setPen(Qt::NoPen);
		p.drawEllipse(QPointF(48+marker.x(),48+marker.y()),5,5);
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button()!=Qt::LeftButton) return;
		start=e->position(); startU=currentU; startV=currentV; dragging=true; e->accept();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (!dragging) return;
		QPointF delta=e->position()-start;
		if (e->modifiers() & Qt::ShiftModifier) {
			if (std::abs(delta.x())>=std::abs(delta.y())) delta.setY(0); else delta.setX(0);
		}
		double multiplier=(e->modifiers() & Qt::ControlModifier)?0.1:1.0;
		marker=QPointF(std::clamp(delta.x(),-38.0,38.0),std::clamp(delta.y(),-38.0,38.0));
		if (onChange) onChange(startU+marker.x()/76.0*multiplier,startV+marker.y()/76.0*multiplier);
		update(); e->accept();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button()==Qt::LeftButton && dragging) { dragging=false; marker={}; update(); e->accept(); }
	}
private:
	QPointF start,marker; double currentU=0,currentV=0,startU=0,startV=0; bool dragging=false;
};

// Private renderer property targets: animation can change these, never the NIF.
struct MaterialPlayback {
	BSLightingShaderProperty lighting{nullptr,{}};
	BSEffectShaderProperty effect{nullptr,{}};
	std::vector<std::unique_ptr<Controller>> controllers;
	QStringList limitations;
	static QMap<int,int> bindings(const NifModel *nif,const QModelIndex &sequence) {
		QMap<int,int> result;
		if (!sequence.isValid()) return result;
		auto blocks=nif->getIndex(sequence,"Controlled Blocks");
		for(int row=0;row<nif->rowCount(blocks) && row<4096;++row) {
			auto entry=nif->getIndex(blocks,row); int controller=nif->getLink(entry,"Controller");
			if(controller<0) continue;
			// Duplicate bindings are ambiguous, even when one appears usable.
			if(result.contains(controller)) result[controller]=-1;
			else result[controller]=nif->getLink(entry,"Interpolator");
		}
		return result;
	}
	MaterialPlayback(const NifModel *nif,const QModelIndex &shader,bool isEffect) { load(nif,shader,isEffect,{}); }
	void load(const NifModel *nif,const QModelIndex &shader,bool isEffect,const QModelIndex &sequence) {
		controllers.clear(); limitations.clear(); auto bound=bindings(nif,sequence);
		lighting.bsVersion=effect.bsVersion=nif->getBSVersion();
		QSet<int> visited;
		for (int link=nif->getLink(shader,"Controller");link>=0;) {
			if (visited.contains(link) || visited.size()>=256) { limitations<<QObject::tr("Repeated or excessive controller chain"); break; }
			visited.insert(link); auto index=nif->getBlockIndex(link,"NiTimeController");
			if (!index.isValid()) { limitations<<QObject::tr("Invalid controller link"); break; }
			QString name=nif->itemName(index); std::unique_ptr<Controller> controller;
			if (isEffect && name=="BSEffectShaderPropertyFloatController") controller=std::make_unique<EffectFloatController>(&effect,index);
			if (isEffect && name=="BSEffectShaderPropertyColorController") controller=std::make_unique<EffectColorController>(&effect,index);
			if (!isEffect && name=="BSLightingShaderPropertyFloatController") controller=std::make_unique<LightingFloatController>(&lighting,index);
			if (!isEffect && name=="BSLightingShaderPropertyColorController") controller=std::make_unique<LightingColorController>(&lighting,index);
			if (nif->getLink(index,"Target")!=nif->getBlockNumber(shader)) {
				limitations<<QObject::tr("Shared or mismatched controller target; playback omitted"); controller.reset();
			}
			if (controller) {
				controller->update(nif,index);
				auto interp=nif->getBlockIndex(nif->getLink(index,"Interpolator"));
				if(bound.contains(link)) {
					interp=nif->getBlockIndex(bound.value(link));
					if(!interp.isValid()) { limitations<<QObject::tr("Ambiguous or invalid sequence binding; channel omitted"); link=nif->getLink(index,"Next Controller"); continue; }
					controller->setInterpolator(interp);
				}
				auto data=nif->getBlockIndex(nif->getLink(interp,"Data"));
				if (!data.isValid()) data=nif->getBlockIndex(nif->getLink(index,"Data"));
				bool color=name.contains("ColorController");
				if (!nif->isNiBlock(data,color?"NiPosData":"NiFloatData")) limitations<<QObject::tr("Unsupported or absent key data; base value retained");
				else if (std::isfinite(controller->start) && std::isfinite(controller->stop) && std::isfinite(controller->frequency) && std::isfinite(controller->phase))
					controllers.push_back(std::move(controller));
				else limitations<<QObject::tr("Non-finite controller timing; playback omitted");
			} else if (nif->getLink(index,"Target")==nif->getBlockNumber(shader)) limitations<<QObject::tr("Unsupported controller: %1").arg(name);
			link=nif->getLink(index,"Next Controller");
		}
	}
	void evaluate(float time) { for (const auto &controller:controllers) controller->updateTime(time); }
};

class MaterialCurve final : public QWidget {
public:
	MaterialCurve() { setMinimumHeight(110); setObjectName("materialAnimationCurve"); }
	void setGroup(const NifModel *nif,const QModelIndex &group,bool rgb) {
		curves={}; auto keys=nif->getIndex(group,"Keys"); int count=nif->rowCount(keys);
		if (!group.isValid() || count<1) { update(); return; }
		float start=nif->get<float>(nif->getIndex(keys,0),"Time"),stop=nif->get<float>(nif->getIndex(keys,count-1),"Time");
		if (!std::isfinite(start) || !std::isfinite(stop) || stop<start) { update(); return; }
		for(int i=0;i<193;++i) {
			float t=start+(stop-start)*i/192; int last=0; Vector3 v; float scalar=0;
			bool ok=rgb?Controller::interpolate(v,group,t,last):Controller::interpolate(scalar,group,t,last);
			if (!ok) { curves={}; break; }
			for(int c=0;c<(rgb?3:1);++c) { float value=rgb?v[c]:scalar; if(std::isfinite(value)) curves[size_t(c)].push_back(value); }
		}
		update();
	}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this); p.fillRect(rect(),QColor(28,32,38)); p.setRenderHint(QPainter::Antialiasing);
		if(curves[0].empty()) { p.setPen(Qt::gray); p.drawText(rect(),Qt::AlignCenter,tr("Select a keyed channel to view its renderer curve")); return; }
		float lo=curves[0][0],hi=lo;
		for(const auto &curve:curves) for(float v:curve) { lo=std::min(lo,v); hi=std::max(hi,v); }
		float span=std::max(hi-lo,1e-6f); const QColor colors[]={QColor(220,115,105),QColor(115,205,150),QColor(110,165,235)};
		for(size_t c=0;c<curves.size();++c) { const auto &values=curves[c]; QPainterPath path;
			for(size_t i=0;i<values.size();++i) { QPointF point(8+(width()-16)*double(i)/192,height()-18-(height()-36)*(values[i]-lo)/span); if(i==0) path.moveTo(point); else path.lineTo(point); }
			p.setPen(QPen(colors[c],1.6)); p.drawPath(path);
		}
		p.setPen(Qt::lightGray); p.drawText(8,13,tr("%1 … %2 · local key time").arg(lo,0,'g',4).arg(hi,0,'g',4));
	}
private:
	std::array<std::vector<float>,3> curves;
};

void inspectAnimation(const NifModel *nif,const QModelIndex &shape,const QModelIndex &shader,QWidget *parent,const QModelIndex &sequence={})
{
	QDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Material Animation")); dialog.resize(850,680);
	auto layout=new QVBoxLayout(&dialog);
	auto info=new QLabel(QObject::tr("Existing material behavior and keys. Playback is available in the graph. This inspector does not edit controller arrangements."));
	info->setWordWrap(true); layout->addWidget(info);
	if(sequence.isValid()) layout->addWidget(new QLabel(QObject::tr("Selected state: %1 · sequence-bound keys; no automatic transitions").arg(nif->get<QString>(sequence,"Name"))));
	auto bound=MaterialPlayback::bindings(nif,sequence);
	auto tree=new QTreeWidget; tree->setObjectName("materialAnimationTree");
	tree->setHeaderLabels({QObject::tr("Controller / channel"),QObject::tr("Timing, interpolation and values")});
	tree->setColumnWidth(0,300); layout->addWidget(tree);
	auto curve=new MaterialCurve; layout->addWidget(curve);
	QObject::connect(tree,&QTreeWidget::currentItemChanged,&dialog,[=](QTreeWidgetItem *item,QTreeWidgetItem *) {
		curve->setGroup(nif,item?item->data(0,Qt::UserRole).value<QModelIndex>():QModelIndex(),item && item->data(0,Qt::UserRole+1).toBool());
	});
	QSet<int> visited;
	auto keyGroup=[&](QTreeWidgetItem *parentItem,const QModelIndex &group,const QString &label,bool alphaChannel,bool rgb=false) {
		if (!group.isValid()) { new QTreeWidgetItem(parentItem,{label,QObject::tr("No key data")}); return; }
		int count=nif->get<int>(group,"Num Keys"), interpolation=nif->get<int>(group,"Interpolation");
		QString type=interpolation==1 ? QObject::tr("Linear") : interpolation==2 ? QObject::tr("Quadratic (tangents)")
			: interpolation==3 ? QObject::tr("Tension / bias / continuity (renderer uses linear fallback)") : interpolation==5?QObject::tr("Constant"):QObject::tr("Type %1").arg(interpolation);
		auto channel=new QTreeWidgetItem(parentItem,{label,QObject::tr("%1 keys · %2").arg(count).arg(count?type:QObject::tr("none"))});
		channel->setData(0,Qt::UserRole,QVariant::fromValue(group)); channel->setData(0,Qt::UserRole+1,rgb);
		auto keys=nif->getIndex(group,"Keys");
		auto valueText=[&](const QModelIndex &key,const char *field) {
			if (!rgb) return QString::number(nif->get<float>(key,field),'g',8);
			auto v=nif->get<Vector3>(key,field); return QString("RGB (%1, %2, %3)").arg(v[0],0,'g',8).arg(v[1],0,'g',8).arg(v[2],0,'g',8);
		};
		// Equal endpoints alone do not prove a quadratic curve is constant.
		bool constant=count>0 && count<=512 && nif->rowCount(keys)==count;
		for (int k=0;k<count && constant;++k) {
			auto key=nif->getIndex(keys,k);
			if (rgb) { auto first=nif->get<Vector3>(nif->getIndex(keys,0),"Value"),v=nif->get<Vector3>(key,"Value"); for(int c=0;c<3;++c) constant=constant && v[c]==first[c]; }
			else constant=nif->get<float>(key,"Value")==nif->get<float>(nif->getIndex(keys,0),"Value");
			if (interpolation==2 && count>1) {
				if (rgb) for(const char *f:{"Forward","Backward"}) { auto v=nif->get<Vector3>(key,f); for(int c=0;c<3;++c) constant=constant && v[c]==0; }
				else constant=constant && nif->get<float>(key,"Forward")==0 && nif->get<float>(key,"Backward")==0;
			}
		}
		channel->setText(1,channel->text(1)+QObject::tr(" · %1").arg(constant?QObject::tr("Constant value"):QObject::tr("Changing / curve-dependent")));
		int visible=std::min(nif->rowCount(keys),512);
		for (int k=0;k<visible;++k) {
			auto key=nif->getIndex(keys,k);
			QString detail=QObject::tr("Value %1").arg(valueText(key,"Value"));
			if (alphaChannel && k+1<nif->rowCount(keys)) {
				auto next=nif->getIndex(keys,k+1);
				float startValue=nif->get<float>(key,"Value"), endValue=nif->get<float>(next,"Value");
				if (nif->get<float>(next,"Time")>nif->get<float>(key,"Time")) {
					QString transition=endValue>startValue ? QObject::tr("toward fade in") : endValue<startValue ? QObject::tr("toward fade out") : QObject::tr("equal endpoints");
					detail+=QObject::tr(" · %1").arg(transition);
					if (interpolation!=1) detail+=QObject::tr(" (endpoints; curve may overshoot)");
				}
			}
			if (interpolation==2) detail+=QObject::tr(" · forward %1 · backward %2").arg(valueText(key,"Forward"),valueText(key,"Backward"));
			if (interpolation==3) {
				auto tbc=nif->get<Vector3>(key,"TBC");
				detail+=QObject::tr(" · tension %1 · bias %2 · continuity %3")
					.arg(tbc[0],0,'g',8).arg(tbc[1],0,'g',8).arg(tbc[2],0,'g',8);
			}
			new QTreeWidgetItem(channel,{QObject::tr("Time %1").arg(nif->get<float>(key,"Time"),0,'g',8),detail});
		}
		if (nif->rowCount(keys)>visible) new QTreeWidgetItem(channel,{QObject::tr("Additional keys"),QObject::tr("Showing the first %1 keys.").arg(visible)});
		channel->setExpanded(true);
	};
	auto chain=[&](const QModelIndex &owner,const QString &ownerName) {
		if (!owner.isValid()) return;
		int link=nif->getLink(owner,"Controller");
		while (link>=0) {
			if (visited.size()>=256) { new QTreeWidgetItem(tree,{ownerName,QObject::tr("Controller inspection limit reached")}); break; }
			if (visited.contains(link)) { new QTreeWidgetItem(tree,{ownerName,QObject::tr("Controller chain repeats or contains a cycle")}); break; }
			visited.insert(link);
			auto controller=nif->getBlockIndex(link,"NiTimeController");
			if (!controller.isValid()) { new QTreeWidgetItem(tree,{ownerName,QObject::tr("Invalid controller link %1").arg(link)}); break; }
			QString name=nif->itemName(controller), target;
			if (name=="BSLightingShaderPropertyFloatController") {
				unsigned value=nif->get<unsigned>(controller,"Controlled Variable");
				target=NifValue::enumOptionName("LightingShaderControlledFloat",value);
				if (target.isEmpty()) target=QObject::tr("Unknown variable %1").arg(value);
			} else if (name=="BSEffectShaderPropertyFloatController") {
				target=NifValue::enumOptionName("EffectShaderControlledVariable",nif->get<unsigned>(controller,"Controlled Variable"));
			} else if (name=="BSLightingShaderPropertyColorController" || name=="BSEffectShaderPropertyColorController") {
				target=NifValue::enumOptionName(name.startsWith("BSEffect")?"EffectShaderControlledColor":"LightingShaderControlledColor",nif->get<unsigned>(controller,"Controlled Color"));
			} else if (name=="NiTextureTransformController") {
				unsigned op=nif->get<unsigned>(controller,"Operation");
				const QStringList ops={QObject::tr("U translation"),QObject::tr("V translation"),QObject::tr("Rotation"),QObject::tr("U scale"),QObject::tr("V scale")};
				target=op<unsigned(ops.size())?ops[int(op)]:QObject::tr("Unknown operation %1").arg(op);
			} else if (name=="NiAlphaController") target=QObject::tr("Alpha");
			else if (name=="NiUVController") target=QObject::tr("UV offset and scale");
			else target=QObject::tr("See controller block for details");
			int flags=nif->get<int>(controller,"Flags"), cycle=(flags&6)>>1;
			QString loop=cycle==0?QObject::tr("loop"):cycle==1?QObject::tr("reverse"):QObject::tr("clamp");
			auto item=new QTreeWidgetItem(tree,{ownerName+QStringLiteral(" · ")+name+QStringLiteral(" #")+QString::number(link),target});
			new QTreeWidgetItem(item,{QObject::tr("Controller timing"),
				QObject::tr("%1; %2; start %3, stop %4, frequency %5, phase %6")
					.arg((flags&8)?QObject::tr("active"):QObject::tr("inactive"),loop)
					.arg(nif->get<float>(controller,"Start Time"),0,'g',8).arg(nif->get<float>(controller,"Stop Time"),0,'g',8)
					.arg(nif->get<float>(controller,"Frequency"),0,'g',8).arg(nif->get<float>(controller,"Phase"),0,'g',8)});
			double frequency=nif->get<float>(controller,"Frequency"),duration=nif->get<float>(controller,"Stop Time")-nif->get<float>(controller,"Start Time");
			if (std::isfinite(frequency) && frequency!=0 && std::isfinite(duration) && duration>0)
				new QTreeWidgetItem(item,{QObject::tr("Scene timing"),QObject::tr("%1 seconds per %2; local time = scene time × frequency + phase").arg(duration/std::abs(frequency)*(cycle==1?2:1),0,'g',8).arg(cycle==0?QObject::tr("loop"):cycle==1?QObject::tr("round trip"):QObject::tr("traversal"))});
			else new QTreeWidgetItem(item,{QObject::tr("Scene timing"),QObject::tr("Frozen or invalid duration/frequency")});
			if (nif->getLink(controller,"Target")!=nif->getBlockNumber(owner)) new QTreeWidgetItem(item,{QObject::tr("Target warning"),QObject::tr("Target differs from this owner; shared links are preserved.")});
			if (name=="NiUVController") {
				auto data=nif->getBlockIndex(nif->getLink(controller,"Data"),"NiUVData");
				auto groups=nif->getIndex(data,"UV Groups");
				const QStringList labels={QObject::tr("U offset"),QObject::tr("V offset"),QObject::tr("U scale"),QObject::tr("V scale")};
				for (int i=0;i<4;++i) keyGroup(item,nif->getIndex(groups,i),labels[i],false);
			} else {
				auto interpolator=nif->getBlockIndex(nif->getLink(controller,"Interpolator"));
				if(bound.contains(link)) interpolator=nif->getBlockIndex(bound.value(link));
				bool rgb=name=="BSLightingShaderPropertyColorController" || name=="BSEffectShaderPropertyColorController";
				auto data=nif->getBlockIndex(nif->getLink(interpolator,"Data"),rgb?"NiPosData":"NiFloatData");
				if (!data.isValid()) data=nif->getBlockIndex(nif->getLink(controller,"Data"),rgb?"NiPosData":"NiFloatData");
				bool alphaChannel=(name=="BSLightingShaderPropertyFloatController" && nif->get<unsigned>(controller,"Controlled Variable")==12)
					|| name=="NiAlphaController" || (name=="BSEffectShaderPropertyFloatController" && nif->get<unsigned>(controller,"Controlled Variable")==5);
				if (data.isValid()) keyGroup(item,nif->getIndex(data,"Data"),rgb?QObject::tr("RGB keys"):QObject::tr("Float keys"),alphaChannel,rgb);
				else if (nif->isNiBlock(interpolator,"NiFloatInterpolator"))
					new QTreeWidgetItem(item,{QObject::tr("Interpolator"),QObject::tr("Constant value: %1").arg(nif->get<float>(interpolator,"Value"),0,'g',8)});
				else new QTreeWidgetItem(item,{QObject::tr("Interpolator"),QObject::tr("Unsupported, external, or absent; inspect the NIF blocks")});
			}
			item->setExpanded(true); link=nif->getLink(controller,"Next Controller");
		}
	};
	chain(shader,QObject::tr("Material"));
	chain(shape,QObject::tr("Shape"));
	if (shape.isValid()) {
		chain(nif->getBlockIndex(nif->getLink(shape,"Alpha Property"),"NiAlphaProperty"),QObject::tr("Alpha property"));
		for (int child:nif->getChildLinks(nif->getBlockNumber(shape))) {
			auto property=nif->getBlockIndex(child,"NiTexturingProperty");
			if (property.isValid()) chain(property,QObject::tr("Texturing property"));
		}
	}
	if (tree->topLevelItemCount()==0) new QTreeWidgetItem(tree,{QObject::tr("No direct controllers"),QObject::tr("Animations may also come from controller sequences or external files.")});
	auto limitation=new QLabel(QObject::tr("U/V offset and scale, color and opacity use their material channels. No shader UV rotation is established. Selected sequence bindings are inspected; external behavior, transitions, geometry and particles are not evaluated here."));
	limitation->setWordWrap(true); layout->addWidget(limitation);
	auto buttons=new QDialogButtonBox(QDialogButtonBox::Close); QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); layout->addWidget(buttons);
	dialog.exec();
}

struct PanRecipe {
	bool compatible=true;
	QString reason;
	std::array<int,2> data{-1,-1},controller{-1,-1};
	std::array<double,2> speed{0,0};
	std::array<double,2> origin{0,0};
	double duration=10;
	QMap<int,QByteArray> watched;
};
PanRecipe inspectPan(const NifModel *nif,const QModelIndex &shader,bool effect) {
	PanRecipe result; QSet<int> chain;
	auto reject=[&](const QString &why) { result.compatible=false; result.reason=why; return result; };
	auto users=[&](int block) { int count=0; for(int b=0;b<nif->getBlockCount();++b) if(nif->getChildLinks(b).contains(block)) ++count; return count; };
	for(int link=nif->getLink(shader,"Controller");link>=0;) {
		if(chain.contains(link) || chain.size()>=256) return reject(QObject::tr("Controller chain is repeated or too large."));
		chain.insert(link); auto c=nif->getBlockIndex(link,"NiTimeController");
		if(!c.isValid() || nif->getLink(c,"Target")!=nif->getBlockNumber(shader)) return reject(QObject::tr("A controller target is invalid or shared with another owner."));
		if(users(link)!=1) return reject(QObject::tr("Existing controllers are shared or sequence-bound. Their animation is preserved; select a state to inspect/play it."));
		QString type=nif->itemName(c); unsigned variable=nif->get<unsigned>(c,"Controlled Variable");
		if(type!=(effect?"BSEffectShaderPropertyFloatController":"BSLightingShaderPropertyFloatController")) {
			if(!type.endsWith("ShaderPropertyColorController")) return reject(QObject::tr("This controller arrangement is not supported for Pan authoring."));
		} else if(variable==(effect?6u:20u) || variable==(effect?8u:22u)) {
			int axis=variable==(effect?6u:20u)?0:1;
			if(result.controller[size_t(axis)]>=0) return reject(QObject::tr("More than one controller drives this pan axis."));
			auto interp=nif->getBlockIndex(nif->getLink(c,"Interpolator"),"NiFloatInterpolator");
			auto data=nif->getBlockIndex(nif->getLink(interp,"Data"),"NiFloatData");
			auto group=nif->getIndex(data,"Data"),keys=nif->getIndex(group,"Keys");
			double start=nif->get<float>(c,"Start Time"),stop=nif->get<float>(c,"Stop Time"),frequency=nif->get<float>(c,"Frequency"),phase=nif->get<float>(c,"Phase");
			int kind=nif->get<int>(group,"Interpolation"); double duration=stop-start;
			if(!interp.isValid() || !data.isValid() || nif->get<int>(c,"Flags")!=72
				|| !std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(frequency) || frequency==0 || !std::isfinite(phase) || duration<=0
				|| (kind!=1 && kind!=2) || nif->rowCount(keys)!=2 || nif->get<int>(group,"Num Keys")!=2)
				return reject(QObject::tr("This UV arrangement is not a supported two-key pan. Its keys and timing are preserved; inspect/play it instead."));
			auto first=nif->getIndex(keys,0),last=nif->getIndex(keys,1);
			double origin=nif->get<float>(first,"Value"),end=nif->get<float>(last,"Value");
			if(nif->get<float>(first,"Time")!=start || nif->get<float>(last,"Time")!=stop || !std::isfinite(origin)
				|| !std::isfinite(end)
				|| users(link)!=1 || users(nif->getBlockNumber(interp))!=1 || users(nif->getBlockNumber(data))!=1)
				return reject(QObject::tr("Existing pan data has unsupported key ranges or shared links; it is preserved."));
			if(kind==2) for(auto key:{first,last}) for(const char *field:{"Forward","Backward"})
				if(!std::isfinite(nif->get<float>(key,field))) return reject(QObject::tr("Pan tangents are non-finite; the animation is preserved."));
			if(kind==2 && origin==end && (nif->get<float>(first,"Backward")!=0 || nif->get<float>(last,"Forward")!=0))
				return reject(QObject::tr("This curve oscillates without net travel; it cannot be represented as a pan speed."));
			result.origin[size_t(axis)]=origin; result.speed[size_t(axis)]=(end-origin)/duration*frequency;
			result.controller[size_t(axis)]=link; result.data[size_t(axis)]=nif->getBlockNumber(data);
			if(std::abs(result.speed[size_t(axis)])>1000) return reject(QObject::tr("Existing pan speed exceeds the authoring range; it is preserved."));
			for(auto index:{c,interp,data}) { QByteArray bytes; blockBytes(nif,index,bytes); result.watched[nif->getBlockNumber(index)]=bytes; }
		}
		link=nif->getLink(c,"Next Controller");
	}
	return result;
}

class PanPad final : public QWidget {
public:
	PanPad() { setFixedSize(120,90); setObjectName("materialPanPad"); setToolTip(tr("Left-drag horizontally: U speed. Right-drag vertically: V speed. Arrows show sample-coordinate travel; the displayed texture moves in the opposite direction.")); }
	std::function<void(int,double)> onChange;
	void setSpeed(double u,double v) { speed={u,v}; update(); }
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(),QColor(32,38,45)); QPointF center(60,45);
		for(int axis=0;axis<2;++axis) {
			double v=speed[size_t(axis)],length=std::min(34.0,8+std::abs(v)*35)*(v<0?-1:1);
			QPointF direction=axis?QPointF(0,1):QPointF(1,0),side=axis?QPointF(1,0):QPointF(0,1),end=center+direction*length;
			p.setPen(QPen(axis?QColor(220,166,86):QColor(225,105,105),isEnabled()?3:1));
			if(v!=0) { p.drawLine(center,end); double sign=v<0?-1:1; p.drawLine(end,end-direction*sign*7+side*5); p.drawLine(end,end-direction*sign*7-side*5); }
		}
		p.setPen(Qt::lightGray); p.drawText(5,14,tr("U · left mouse")); p.drawText(5,84,tr("V · right mouse"));
	}
	void mousePressEvent(QMouseEvent *event) override {
		if(event->button()!=Qt::LeftButton && event->button()!=Qt::RightButton) return;
		axis=event->button()==Qt::LeftButton?0:1; start=event->position(); initial=speed[size_t(axis)]; event->accept();
	}
	void mouseMoveEvent(QMouseEvent *event) override {
		if(axis<0) return;
		if(event->buttons()!=(axis?Qt::RightButton:Qt::LeftButton)) return;
		auto delta=event->position()-start; double amount=(axis?delta.y():delta.x())*.01;
		if(event->modifiers() & Qt::ControlModifier) amount*=.1;
		if(onChange) onChange(axis,initial+amount);
		event->accept();
	}
	void mouseReleaseEvent(QMouseEvent *event) override { axis=-1; event->accept(); }
private:
	std::array<double,2> speed{0,0}; QPointF start; double initial=0; int axis=-1;
};

// Only existing block contents and tail additions change. Persistent indices in
// earlier Undo commands remain valid; a full-document load would invalidate them.
struct PanBlock { QString type; QByteArray bytes; };
struct PanEdit { int block; QString type; QByteArray before,after; };
bool materialDocument(const NifModel *nif,QByteArray &bytes) { bytes.clear(); QBuffer out(&bytes); return out.open(QIODevice::WriteOnly) && nif->save(out); }
bool installPanBlocks(NifModel *nif,const std::vector<PanEdit> &edits,const std::vector<PanBlock> &added,int count,bool forward) {
	auto load=[&](QModelIndex index,const QByteArray &bytes) { if(!index.isValid()) return false; QBuffer in; in.setData(bytes); in.open(QIODevice::ReadOnly); return nif->loadIndex(in,index) && in.pos()==bytes.size(); };
	if(forward) {
		for(const auto &block:added) if(!nif->insertNiBlock(block.type).isValid()) return false;
		for(size_t i=0;i<added.size();++i) if(!load(nif->getBlockIndex(count+int(i)),added[i].bytes)) return false;
	}
	for(const auto &edit:edits) if(!load(nif->getBlockIndex(edit.block,edit.type),forward?edit.after:edit.before)) return false;
	if(!forward) while(nif->getBlockCount()>count) nif->removeNiBlock(nif->getBlockCount()-1);
	nif->updateHeader();
	for(const auto &edit:edits) { auto index=nif->getBlockIndex(edit.block); emit nif->dataChanged(index,index); }
	return true;
}
class PanMaterialCommand final : public QUndoCommand {
public:
	PanMaterialCommand(NifModel *nif,std::vector<PanEdit> edits,std::vector<PanBlock> added,QByteArray before,QByteArray after,int count,std::shared_ptr<bool> success)
		: model(nif),edits(std::move(edits)),added(std::move(added)),before(std::move(before)),after(std::move(after)),count(count),success(std::move(success)) { setText(QObject::tr("Edit material and Pan instruction")); }
	void redo() override { change(true); } void undo() override { change(false); }
private:
	void change(bool forward) {
		QByteArray current; bool ok=model && materialDocument(model,current) && current==(forward?before:after);
		if(ok) {
			ok=installPanBlocks(model,edits,added,count,forward) && materialDocument(model,current) && current==(forward?after:before);
			if(!ok) { installPanBlocks(model,edits,{},count,false); if(!forward) installPanBlocks(model,edits,added,count,true); }
			if(ok && forward) {
				handles.resize(added.size());
				for(size_t i=0;i<added.size();++i) handles[i]=bindUndoBlock(model,model->getBlockIndex(count+int(i)),handles[i]);
			}
		}
		if(success) { *success=ok; success.reset(); }
		if(!ok) { setObsolete(true); QMessageBox::warning(nullptr,QObject::tr("Material Graph"),QObject::tr("Pan replacement was refused or rolled back; current document data was retained.")); }
	}
	QPointer<NifModel> model; std::vector<PanEdit> edits; std::vector<PanBlock> added; QByteArray before,after; int count; std::shared_ptr<bool> success;
	std::vector<std::shared_ptr<UndoBlockHandle>> handles;
};

class MaterialGraph final : public QDialog {
public:
	MaterialGraph(NifModel *nif,QModelIndex shape,QModelIndex shader,QModelIndex textures,QWidget *parent,MaterialGraphHooks hooks={})
		: QDialog(parent),model(nif),hooks(std::move(hooks)),shapeIndex(shape),shaderIndex(shader),textureIndex(textures),hasShape(shape.isValid()),isEffect(nif->isNiBlock(shader,"BSEffectShaderProperty"))
	{
		setWindowTitle(tr("Material Graph")); resize(1080,720);
		blockBytes(nif,shader,shaderBefore); if (!isEffect) blockBytes(nif,textures,texturesBefore);
		if (shape.isValid()) blockBytes(nif,shape,shapeBefore);
		auto main=new QVBoxLayout(this);
		auto note=new QLabel(isEffect?tr("Skyrim effect material • Edit values, then Apply. Shared shader settings are staged locally; saving is manual."):tr("Skyrim lighting material • Edit values, then Apply. Shader settings are staged locally; saving is manual."));
		note->setWordWrap(true); main->addWidget(note);
		if(this->hooks.placement) main->addWidget(this->hooks.placement);
		if (nif->getLink(shader,"Controller")>=0 || (shape.isValid() && nif->getLink(shape,"Controller")>=0)) {
			auto animated=new QLabel(tr("Animation can override these base values. Select an available state to preview and inspect its material keys."));
			animated->setWordWrap(true); main->addWidget(animated);
		}
		int shaderUsers=0,textureUsers=0;
		for (int i=0;i<nif->getBlockCount();++i) {
			auto links=nif->getChildLinks(i);
			if (links.contains(nif->getBlockNumber(shader))) ++shaderUsers;
			if (links.contains(nif->getBlockNumber(textures))) ++textureUsers;
		}
		if (shaderUsers>1 || textureUsers>1) {
			auto shared=new QLabel(tr("This shader or texture set is shared. Edits affect all shapes that use it."));
			shared->setWordWrap(true); main->addWidget(shared);
		}
		auto scene=new QGraphicsScene(this); scene->setSceneRect(0,0,1070,1180);
		auto view=new QGraphicsView(scene); view->setObjectName("materialGraphView");
		view->setRenderHint(QPainter::Antialiasing); view->setBackgroundBrush(QColor(29,32,39)); main->addWidget(view);
		auto node=[&](const QString &title,int x,int y,int width,int height) {
			auto box=new QGroupBox(title); box->setFixedSize(width,height);
			box->setStyleSheet("QGroupBox { background: #303741; color: #eef2f4; border: 1px solid #72818a; border-radius: 5px; margin-top: 1em; padding: 8px; } QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; } QLabel, QCheckBox { color: #eef2f4; } QLineEdit, QDoubleSpinBox { color: #eef2f4; background: #20262d; }");
			auto proxy=scene->addWidget(box); proxy->setPos(x,y); proxy->setZValue(1); return box;
		};
		auto wire=[&](QPointF from,QPointF to,bool active) {
			QPainterPath path(from); double dx=(to.x()-from.x())*0.5;
			path.cubicTo(from+QPointF(dx,0),to-QPointF(dx,0),to);
			return scene->addPath(path,QPen(active?QColor(116,208,185):QColor(95,101,107),active?2.2:1.2,active?Qt::SolidLine:Qt::DashLine));
		};
		unsigned type=nif->get<unsigned>(shader,"Shader Type");
		unsigned flags1=nif->get<unsigned>(shader,"Shader Flags 1"),flags2=nif->get<unsigned>(shader,"Shader Flags 2");
		initialType=stagedType=type; initialFlags1=stagedFlags1=flags1; initialFlags2=stagedFlags2=flags2;
		auto shaderRow=new QHBoxLayout; main->insertLayout(1,shaderRow);
		shaderRow->addWidget(new QLabel(tr("Shader"))); shaderChoice=new QComboBox; shaderChoice->setObjectName("materialShaderType");
		shaderChoice->addItem(tr("Default"),unsigned(ShaderFlags::ST_Default));
		shaderChoice->addItem(tr("Glow"),unsigned(ShaderFlags::ST_GlowShader));
		shaderChoice->addItem(tr("Environment"),unsigned(ShaderFlags::ST_EnvironmentMap));
		shaderChoice->addItem(tr("Parallax / height"),unsigned(ShaderFlags::ST_Heightmap));
		if (shaderChoice->findData(type)<0) shaderChoice->addItem(tr("Existing shader type %1 (preserved)").arg(type),type);
		shaderChoice->setCurrentIndex(shaderChoice->findData(type));
		if (isEffect) { shaderChoice->clear(); shaderChoice->addItem(tr("Effect material (BSEffectShaderProperty)"),type); shaderChoice->setEnabled(false); }
		shaderChoice->setToolTip(tr("Choose a Skyrim lighting shader and its matching feature flags. Unexposed flags are retained. Glow needs a slot-2 glow texture and non-black emissive color; Environment uses a slot-4 cubemap; Parallax uses slot 3."));
		if (isEffect) shaderChoice->setToolTip(tr("An effect material is a separate Bethesda property family. Editing it does not convert it to a lighting shader."));
		shaderRow->addWidget(shaderChoice); auto flagButton=new QPushButton(tr("Shader flags…")); flagButton->setObjectName("materialShaderFlags");
		flagButton->setToolTip(tr("Edit common lighting flags. Other flag bits are preserved exactly.")); shaderRow->addWidget(flagButton); shaderRow->addStretch();
		bool hasHeight=type==ShaderFlags::ST_Heightmap && (flags1 & ShaderFlags::SLSF1_Parallax);
		bool hasGlow=type==ShaderFlags::ST_GlowShader && (flags2 & ShaderFlags::SLSF2_Glow_Map)
			&& (flags1 & ShaderFlags::SLSF1_Own_Emit);
		bool hasEnvironment=(type==ShaderFlags::ST_EnvironmentMap && (flags1 & ShaderFlags::SLSF1_Environment_Mapping))
			|| (type==ShaderFlags::ST_EyeEnvmap && (flags1 & ShaderFlags::SLSF1_Eye_Environment_Mapping))
			|| (nif->getBSVersion()==100 && (flags2 & ShaderFlags::SLSF2_Multi_Layer_Parallax));
		bool hasSlot2=hasGlow || (flags2 & (ShaderFlags::SLSF2_Rim_Lighting | ShaderFlags::SLSF2_Soft_Lighting));
		bool hasSlot3=hasHeight || type==ShaderFlags::ST_Facegen;
		auto paths=nif->getIndex(textures,"Textures");
		std::array<QLineEdit *,7> pathEdits{};
		auto diffusePreview=std::make_shared<QImage>();
		auto availability=std::make_shared<std::array<bool,7>>();
		auto textureNode=[&](int slot,const QString &name,int y,bool showThumb) {
			auto box=node(name,20,y,270,showThumb?145:110); auto layout=new QVBoxLayout(box);
			auto index=isEffect?nif->getIndex(shader,slot==0?"Source Texture":"Greyscale Texture"):nif->getIndex(paths,slot);
			auto edit=new QLineEdit(index.isValid()?nif->get<QString>(index):QString()); edit->setObjectName(QString("materialTexture%1").arg(slot));
			pathEdits[size_t(slot)]=edit;
			textureEdits[size_t(slot)]=edit; initialTexturePaths[size_t(slot)]=edit->text();
			edit->setPlaceholderText(tr("No texture path")); edit->setToolTip(tr("Texture-set slot %1. A path does not enable its shader feature.").arg(slot));
			const QString tips[7]={tr("Slot 0: RGB base color; alpha supplies opacity or shader-specific blending."),tr("Slot 1: RGB surface normals; alpha supplies specular intensity for ordinary tangent-space Skyrim lighting materials, not the Glossiness value."),
				tr("Slot 2: glow map for the Glow shader; can instead be a lighting/material mask for other flags. A path alone does not enable glow."),
				tr("Slot 3: parallax height for the Parallax shader, or a shader-specific detail mask. Parallax is view-dependent shading, not mesh displacement."),
				tr("Slot 4: RGB environment cubemap for reflections. Requires a compatible shader type and environment-mapping flag."),
				tr("Slot 5: single-channel environment/reflection intensity mask. The renderer samples its red channel. BC4 is a possible compression format, not a required slot type. Without a separate mask, the renderer can use normal-map alpha for reflection intensity."),
			tr("Slot 6: RGB inner-layer color and alpha depth/thickness for multilayer parallax. Requires the matching material setup; assigning a path alone does not enable it. The base-UV preview does not show this layer or its depth.")};
			box->setToolTip(tips[slot]); edit->setToolTip(tips[slot]);
			auto pathRow=new QHBoxLayout; pathRow->setContentsMargins(0,0,0,0); pathRow->addWidget(edit,1);
			auto choose=new QToolButton; choose->setText(QStringLiteral("+")); choose->setObjectName(QString("materialTextureChoose%1").arg(slot));
			choose->setToolTip(tr("Choose a texture from NifSkope resources")); choose->setFixedWidth(25); pathRow->addWidget(choose);
			auto clear=new QToolButton; clear->setText(QStringLiteral("−")); clear->setObjectName(QString("materialTextureClear%1").arg(slot));
			clear->setToolTip(tr("Clear this texture path")); clear->setFixedWidth(25); pathRow->addWidget(clear); layout->addLayout(pathRow);
			connect(clear,&QToolButton::clicked,this,[edit]() { edit->clear(); emit edit->editingFinished(); });
			connect(choose,&QToolButton::clicked,this,[this,nif,edit]() {
				auto filter=[](void *,const std::string_view &path) { return path.starts_with("textures/") && path.ends_with(".dds"); };
				std::set<std::string_view> files;
				nif->listResourceFiles(files,filter);
				QString resolved=nif->findResourceFile(edit->text(),"textures",".dds");
				std::string selected=resolved.toStdString();
				FileBrowserWidget browser(640,600,"Choose Texture",files,selected,&nif->getGameResources());
				if (browser.exec()!=QDialog::Accepted) return;
				const auto *path=browser.getItemSelected(); if (!path || path->empty()) return;
				edit->setText(QString::fromUtf8(path->data(),qsizetype(path->size())).replace(QChar('/'),QChar('\\')));
				emit edit->editingFinished();
			});
			auto status=new QLabel; status->setObjectName(QString("materialTextureStatus%1").arg(slot)); status->setWordWrap(true); layout->addWidget(status);
			QLabel *img=nullptr;
			if (showThumb) { img=new QLabel; img->setFixedSize(48,48); layout->addWidget(img); }
			if (isEffect) {
				index=nif->getIndex(shader,slot==0?"Source Texture":"Greyscale Texture");
				edit->setText(nif->get<QString>(index)); initialTexturePaths[size_t(slot)]=edit->text();
				box->setToolTip(slot==0?tr("Effect source texture; this is not a lighting texture-set slot."):tr("Effect palette texture, used by grayscale-to-color/alpha flags. It is not a normal map."));
				edit->setToolTip(box->toolTip());
			}
			auto refreshTimer=new QTimer(this); refreshTimer->setSingleShot(true); refreshTimer->setInterval(250);
			auto resourceStatus=std::make_shared<QString>();
			auto refreshStatus=[=,this]() {
				auto f=features(); bool active=slot<2 || (slot==2?f.slot2:slot==3?f.slot3:slot==6?f.inner:f.environment);
				if(isEffect && slot==1) active=(stagedFlags1 & (ShaderFlags::SLSF1_Greyscale_To_PaletteColor|ShaderFlags::SLSF1_Greyscale_To_PaletteAlpha))!=0;
				QString feature=slot<2 && !(isEffect && slot==1)?tr("Texture input"):((slot==2 || slot==3) && !f.known)?tr("Shader-specific usage"):active?tr("Feature enabled"):tr("Feature inactive");
				status->setText(tr("%1; %2").arg(feature,*resourceStatus));
				if (slot==3) box->setTitle(stagedType==ShaderFlags::ST_Facegen?tr("Detail mask · slot 3"):tr("Height / parallax · slot 3"));
			};
			textureStatuses[size_t(slot)]=refreshStatus;
			auto refresh=[=]() {
				QString path=edit->text(); QByteArray bytes;
				bool available=!path.isEmpty() && nif->getResourceFile(bytes,path,"textures",".dds");
				(*availability)[size_t(slot)]=available;
				QString resource=path.isEmpty()?tr("No texture path"):available?tr("Resource available"):tr("Resource not found");
				if (img) {
					auto decoded=available?texturePreviews(bytes,slot==0):TexturePreviews(); img->setPixmap(decoded.thumbnail);
					if (slot==0) *diffusePreview=decoded.surface;
					if (available && decoded.thumbnail.isNull()) resource=tr("Resource available; thumbnail unavailable");
				}
				*resourceStatus=resource; refreshStatus();
			};
			connect(edit,&QLineEdit::textChanged,this,[=]() { (*availability)[size_t(slot)]=false; status->setText(tr("Checking edited texture path…")); refreshTimer->start(); });
			connect(refreshTimer,&QTimer::timeout,this,[edit]() { emit edit->editingFinished(); });
			connect(edit,&QLineEdit::editingFinished,this,[=]() { refreshTimer->stop(); refresh(); }); refresh();
			if (index.isValid()) fields.push_back({index,edit,edit->text(),Kind::Text});
		};
		textureNode(0,isEffect?tr("Source texture"):tr("Diffuse · slot 0"),20,true);
		textureNode(1,isEffect?tr("Grayscale palette"):tr("Normal / specular · slot 1"),180,true);
		if (!isEffect) {
		textureNode(2,tr("Slot 2 · glow / material mask"),340,false);
		textureNode(3,type==ShaderFlags::ST_Facegen?tr("Detail mask · slot 3"):tr("Height / parallax · slot 3"),470,true);
		textureNode(4,tr("Environment · slot 4"),630,false);
		textureNode(5,tr("Environment mask · slot 5"),760,true);
		textureNode(6,tr("Inner layer · slot 6"),920,true);
		}
		auto surface=node(isEffect?tr("Effect Surface"):tr("BS Lighting Surface"),390,40,280,240); auto form=new QFormLayout(surface);
		auto scalar=[&](const char *name,const QString &label,double low,double high,const QString &tip) -> QLineEdit * {
			auto index=nif->getIndex(shader,name);
			// Environment strength is a real schema field that is hidden while
			// another type is active. Stage its existing/default value without
			// changing the live Shader Type merely to obtain the index.
			if (!index.isValid() && QString::fromLatin1(name)=="Environment Map Scale") {
				for (int row=0;row<nif->rowCount(shader);++row) {
					auto candidate=nif->getIndex(shader,row);
					auto item=nif->getItem(candidate,false);
					// An inherited older-game field has the same name. Its
					// version must not be mistaken for the Skyrim field.
					if (item && item->hasName(name) && nif->evalVersion(item)) { index=candidate; break; }
				}
			}
			if (!index.isValid()) return nullptr;
			auto edit=new QLineEdit(QString::number(nif->get<float>(index),'g',9));
			edit->setObjectName(QString("material%1").arg(QString::fromLatin1(name).remove(' ')));
			edit->setToolTip(tip);
			form->addRow(label,edit); fields.push_back({index,edit,edit->text(),Kind::Scalar,low,high});
			return edit;
		};
		if (!isEffect) {
		scalar("Alpha",tr("Alpha"),0,1,tr("Base opacity: 1 is opaque, 0 is transparent. Visible transparency also depends on the shape's alpha property and vertex-alpha settings."));
		scalar("Specular Strength",tr("Specular strength"),0,1000,tr("Brightness of reflected light highlights. Requires the Specular flag."));
		scalar("Glossiness",tr("Glossiness"),0,10000,tr("Higher values make specular highlights tighter; lower values spread them out."));
		scalar("Emissive Multiple",tr("Emission strength"),0,1000,tr("Multiplies the emissive color when Own emission is enabled. This is surface emission, not a light source illuminating nearby geometry."));
		}
		else {
			scalar("Base Color Scale",tr("Brightness"),0,360,tr("Multiplies effect base RGB; existing animation can override it. Does not create a scene light."));
			auto index=nif->getIndex(shader,"Lighting Influence");
			if (index.isValid()) {
				auto edit=new QLineEdit(QString::number(nif->get<quint8>(index))); edit->setObjectName("materialLightingInfluence");
				edit->setToolTip(tr("Stored byte, 0–255. With Effect Lighting enabled, NifSkope's renderer divides this by 255 to obtain lighting influence."));
				form->addRow(tr("Lighting influence"),edit); fields.push_back({index,edit,edit->text(),Kind::Byte,0,255});
			}
		}
		auto environmentStrength=isEffect?nullptr:scalar("Environment Map Scale",tr("Environment strength"),0,10,tr("Reflection intensity for the Environment shader, using the slot-4 cubemap and any material mask."));
		auto colorIndex=nif->getIndex(shader,isEffect?"Base Color":"Emissive Color");
		auto colorPicker=[this](QLineEdit *edit,bool alpha,const char *id) {
			auto row=new QHBoxLayout; row->setContentsMargins(0,0,0,0); row->setSpacing(4); edit->setMinimumWidth(0); row->addWidget(edit,1);
			auto button=new QToolButton; button->setText(QStringLiteral("…")); button->setObjectName(id); button->setToolTip(tr("Choose color using NifSkope's color wheel. Numeric RGB entry remains available, including values above 1.")); row->addWidget(button);
			connect(button,&QToolButton::clicked,this,[this,edit,alpha]() { chooseColor(edit,alpha); }); return row;
		};
		if (colorIndex.isValid()) {
			auto color=isEffect?Color3(nif->get<Color4>(colorIndex)):nif->get<Color3>(colorIndex); auto edit=new QLineEdit(QString("%1 %2 %3").arg(color[0],0,'g',9).arg(color[1],0,'g',9).arg(color[2],0,'g',9));
			if (isEffect) edit->setText(edit->text()+QString(" %1").arg(nif->get<Color4>(colorIndex)[3],0,'g',9));
			edit->setObjectName("materialEmissiveColor"); edit->setToolTip(tr("Emission tint as three linear RGB numbers separated by spaces. 0 0 0 emits nothing; 1 1 1 is white. Requires Own emission. A glow map modulates this color."));
			if (isEffect) edit->setToolTip(tr("Effect base color as R G B A numbers. RGB multiplies the effect; alpha controls opacity with the existing blend state. Animation can override these base values."));
			form->addRow(isEffect?tr("Base R G B A"):tr("Emission R G B"),colorPicker(edit,isEffect,"materialEmissivePicker")); fields.push_back({colorIndex,edit,edit->text(),isEffect?Kind::ColorAlpha:Kind::Color,0,1000});
		}
		if(!isEffect) {
			auto colors=node(tr("Specular / reflection color"),760,760,280,180); auto colorForm=new QFormLayout(colors);
			auto index=nif->getIndex(shader,"Specular Color");
			if(index.isValid()) {
				auto c=nif->get<Color3>(index); auto edit=new QLineEdit(QString("%1 %2 %3").arg(c[0],0,'g',9).arg(c[1],0,'g',9).arg(c[2],0,'g',9)); edit->setObjectName("materialSpecularColor");
				edit->setToolTip(tr("Linear RGB tint of specular highlights. Requires the Specular flag; strength and glossiness remain in the surface node. This does not tint the environment cubemap."));
				colorForm->addRow(tr("Specular RGB"),colorPicker(edit,false,"materialSpecularPicker")); fields.push_back({index,edit,edit->text(),Kind::Color,0,1000});
			}
			auto note=new QLabel(tr("Environment reflection color comes from the slot-4 cubemap. These Skyrim shaders have no separate environment RGB tint; environment strength and the slot-5 mask control its contribution.")); note->setObjectName("materialEnvironmentColorInfo"); note->setWordWrap(true); colorForm->addRow(note);
		}
		auto specularStatus=new QLabel; form->addRow(specularStatus);
		if (isEffect) {
			auto falloff=node(tr("View / depth falloff"),20,350,270,280); auto falloffForm=new QFormLayout(falloff);
			auto surfaceForm=form; form=falloffForm;
			for (const auto &[name,label]:std::vector<std::pair<const char *,QString>>{{"Falloff Start Angle",tr("Start cosine")},{"Falloff Stop Angle",tr("Stop cosine")},{"Falloff Start Opacity",tr("Start opacity")},{"Falloff Stop Opacity",tr("Stop opacity")},{"Soft Falloff Depth",tr("Soft depth")}}) {
				auto edit=scalar(name,label,QString(name).contains("Angle")?-1:0,QString(name).contains("Depth")?9999:1,tr("Stored effect falloff parameter. Angle fields store cosines (−1 to 1), not degrees. Opacity is 0–1. View-angle and scene-depth effects require the normal viewport; the 2D sampling preview does not evaluate them."));
				Q_UNUSED(edit);
			}
			form=surfaceForm;
		}
		auto uv=node(tr("UV Transform"),390,290,280,250); auto uvForm=new QFormLayout(uv);
		QLineEdit *scaleU=nullptr,*scaleV=nullptr,*offsetU=nullptr,*offsetV=nullptr;
		auto vector=[&](const char *name,const QString &label) {
			auto index=nif->getIndex(shader,name); if (!index.isValid()) return;
			auto value=nif->get<Vector2>(index); auto a=new QLineEdit(QString::number(value[0],'g',9)), b=new QLineEdit(QString::number(value[1],'g',9));
			a->setObjectName(QString("material%1U").arg(QString::fromLatin1(name).remove(' ')));
			b->setObjectName(QString("material%1V").arg(QString::fromLatin1(name).remove(' ')));
			a->setToolTip(tr("Exact U component. Base sampling uses UV × Scale + Offset; this does not animate the texture."));
			b->setToolTip(tr("Exact V component. Negative and zero scale are retained. Wrap/clamp is taken from the material."));
			auto row=new QHBoxLayout; row->addWidget(a); row->addWidget(b); uvForm->addRow(label,row);
			vectors.push_back({index,a,b,a->text(),b->text()});
			if (QString::fromLatin1(name)=="UV Scale") { scaleU=a; scaleV=b; } else { offsetU=a; offsetV=b; }
		};
		vector("UV Scale",tr("Scale U / V")); vector("UV Offset",tr("Offset U / V"));
		auto clamp=nif->get<unsigned>(shader,"Texture Clamp Mode");
		uvForm->addRow(tr("U / V addressing"),new QLabel(QString("%1 / %2").arg((clamp&2)?tr("wrap"):tr("clamp"),(clamp&1)?tr("wrap"):tr("clamp"))));
		auto pad=new UvOffsetPad; uvForm->addRow(tr("Drag sampled U/V"),pad);
		auto animation=node(tr("Animation"),390,585,280,115); auto animLayout=new QVBoxLayout(animation);
		QStringList behaviors; QSet<int> seen;
		for(int link=nif->getLink(shader,"Controller");link>=0 && !seen.contains(link) && seen.size()<256;) {
			seen.insert(link); auto c=nif->getBlockIndex(link,"NiTimeController"); if(!c.isValid()) break;
			QString name=nif->itemName(c); unsigned variable=nif->get<unsigned>(c,"Controlled Variable");
			QString behavior=name.contains("ColorController")?tr("Color"):
				variable==(isEffect?6u:20u)?tr("Pan U"):variable==(isEffect?8u:22u)?tr("Pan V"):
				variable==(isEffect?7u:21u)?tr("Scale U"):variable==(isEffect?9u:23u)?tr("Scale V"):
				variable==(isEffect?5u:12u)?tr("Opacity"):variable==(isEffect?0u:11u)?tr("Brightness"):tr("Other behavior");
			if (!behaviors.contains(behavior)) behaviors<<behavior;
			link=nif->getLink(c,"Next Controller");
		}
		auto animInfo=new QLabel(behaviors.isEmpty()?tr("No direct material animation"):behaviors.join(tr(" · "))); animInfo->setObjectName("materialBehaviors"); animInfo->setWordWrap(true); animLayout->addWidget(animInfo);
		auto animButton=new QPushButton(tr("Inspect Animation…")); animButton->setObjectName("materialInspectAnimation");
		animLayout->addWidget(animButton);
		connect(animButton,&QPushButton::clicked,this,[this]() { inspectAnimation(model,shapeIndex,shaderIndex,this,model->getBlockIndex(sequenceChoice->currentData().toInt())); });
		// This is an artist instruction, independent of the static UV pad.
		panRecipe=inspectPan(nif,shader,isEffect);
		auto pan=node(tr("PAN · animated texture travel"),390,730,280,370); auto panLayout=new QVBoxLayout(pan);
		pan->setToolTip(tr("Animates the material's shared U/V offsets, affecting texture inputs that use that transform. It does not create independent per-slot motion or change stored mesh UVs."));
		panEnabled=new QCheckBox(tr("Edit / add Pan")); panEnabled->setObjectName("materialPanEnabled"); panEnabled->setEnabled(panRecipe.compatible); panLayout->addWidget(panEnabled);
		auto panPad=new PanPad; panLayout->addWidget(panPad,0,Qt::AlignCenter);
		auto panForm=new QFormLayout; panLayout->addLayout(panForm);
		for(int axis=0;axis<2;++axis) {
			auto speed=new QDoubleSpinBox; speed->setRange(-1000,1000); speed->setDecimals(6); speed->setSingleStep(.01); speed->setValue(panRecipe.speed[size_t(axis)]);
			speed->setObjectName(axis?"materialPanSpeedV":"materialPanSpeedU"); speed->setSuffix(tr(" UV/s")); panSpeed[size_t(axis)]=speed; initialPanSpeed[size_t(axis)]=speed->value();
			speed->setToolTip(tr("Average signed UV travel per scene second. Existing easing, phase and per-axis timing are retained. Negative reverses travel; displayed image movement is opposite to sampled coordinates."));
			panForm->addRow(axis?tr("V · right mouse"):tr("U · left mouse"),speed);
		}
		panDuration=new QDoubleSpinBox; panDuration->setObjectName("materialPanDuration"); panDuration->setRange(.1,10000); panDuration->setDecimals(3); panDuration->setValue(panRecipe.duration); panDuration->setSuffix(tr(" s")); initialPanDuration=panDuration->value(); panForm->addRow(tr("New-axis loop"),panDuration);
		panDuration->setToolTip(tr("Used only for newly created pan axes. Existing axes retain their individual start/stop times, frequency and phase."));
		auto panInfo=new QLabel(panRecipe.compatible?(panRecipe.controller[0]>=0 || panRecipe.controller[1]>=0?tr("Average speeds; existing curve shape, origin and timing are kept. New axes use the loop duration."):tr("Loop resets to its start. Seamless travel needs wrap and whole UV repeats per loop.")):panRecipe.reason); panInfo->setObjectName("materialPanStatus"); panInfo->setWordWrap(true); panLayout->addWidget(panInfo);
		auto enablePan=[=,this]() { bool enabled=panEnabled->isChecked() && panRecipe.compatible; panPad->setEnabled(enabled); for(auto speed:panSpeed) speed->setEnabled(enabled); panDuration->setEnabled(enabled && (panRecipe.controller[0]<0 || panRecipe.controller[1]<0)); };
		connect(panEnabled,&QCheckBox::toggled,this,enablePan); enablePan();
		panPad->onChange=[this](int axis,double value) { panSpeed[size_t(axis)]->setValue(value); };
		auto refreshPanPad=[=,this]() { panPad->setSpeed(panSpeed[0]->value(),panSpeed[1]->value()); };
		for(auto speed:panSpeed) connect(speed,qOverload<double>(&QDoubleSpinBox::valueChanged),this,refreshPanPad);
		refreshPanPad();
		auto output=node(tr("Material Output"),760,100,260,150); auto out=new QVBoxLayout(output);
		out->addWidget(new QLabel(isEffect?tr("BSEffectShaderProperty"):tr("BSLightingShaderProperty")));
		auto typeLabel=new QLabel; typeLabel->setObjectName("materialShaderSummary"); out->addWidget(typeLabel);
		auto flagNote=new QLabel; flagNote->setWordWrap(true); flagNote->setToolTip(tr("Complete flag words. Only the exposed bits change; other engine-specific flags are retained.")); out->addWidget(flagNote);
		auto preview=node(tr("Material sampling preview"),760,300,280,440); auto previewLayout=new QVBoxLayout(preview);
		auto previewImage=new QLabel; previewImage->setObjectName("materialUvPreview"); previewImage->setFixedSize(160,160); previewLayout->addWidget(previewImage,0,Qt::AlignCenter);
		auto previewInfo=new QLabel; previewInfo->setObjectName("materialUvPreviewStatus"); previewInfo->setWordWrap(true); previewLayout->addWidget(previewInfo);
		auto play=new QCheckBox(tr("Use existing animation")); play->setObjectName("materialAnimationPlay"); previewLayout->addWidget(play);
		auto time=new QDoubleSpinBox; time->setObjectName("materialAnimationTime"); time->setRange(-100000,100000); time->setDecimals(3); time->setSuffix(tr(" s"));
		auto pause=new QPushButton(tr("Pause")); pause->setObjectName("materialAnimationPause"); pause->setCheckable(true); pause->setEnabled(false);
		auto transport=new QHBoxLayout; transport->addWidget(time,1); transport->addWidget(pause); previewLayout->addLayout(transport);
		auto resolved=new QLabel; resolved->setObjectName("materialAnimationResolved"); resolved->setWordWrap(true); previewLayout->addWidget(resolved);
		auto colorSwatch=new QLabel; colorSwatch->setObjectName("materialAnimationColor"); colorSwatch->setFixedHeight(18); previewLayout->addWidget(colorSwatch);
		colorSwatch->setAutoFillBackground(true);
		struct SamplingCache { qint64 source=0; std::array<double,2> scale{},offset{}; QPixmap image; };
		auto samplingCache=std::make_shared<SamplingCache>();
		auto playback=std::make_shared<MaterialPlayback>(nif,shader,isEffect);
		sequenceChoice=new QComboBox; sequenceChoice->setObjectName("materialAnimationSequence"); sequenceChoice->addItem(tr("Direct material animation"),-1);
		const auto directAnimationHelp=tr("Lists animation states already stored in this NIF for the selected material, not animation types you can add. Direct material animation uses the material's directly linked controllers. If no compatible named states exist, it is the only option. Use the Pan node to add or adjust supported panning.");
		sequenceChoice->setToolTip(directAnimationHelp);
		sequenceChoice->setItemData(0,directAnimationHelp,Qt::ToolTipRole);
		for(int b=0;b<nif->getBlockCount();++b) {
			auto sequence=nif->getBlockIndex(b,"NiControllerSequence"); if(!sequence.isValid()) continue;
			auto manager=nif->getBlockIndex(nif->getLink(sequence,"Manager"),"NiControllerManager");
			if(!manager.isValid() || !nif->getLinkArray(manager,"Controller Sequences").contains(b)) continue;
			auto bindings=MaterialPlayback::bindings(nif,sequence); bool relevant=false;
			for(auto entry=bindings.cbegin();entry!=bindings.cend();++entry) if(seen.contains(entry.key())) relevant=true;
			if(relevant) sequenceChoice->addItem(nif->get<QString>(sequence,"Name"),b);
		}
		animLayout->addWidget(sequenceChoice); animation->setFixedHeight(135);
		play->setToolTip(tr("Evaluates existing direct material controllers with NifSkope's renderer timing and interpolation. Animation overrides staged base values. Does not create or edit animation."));
		auto clock=new QTimer(this); clock->setInterval(33);
		auto elapsed=std::make_shared<QElapsedTimer>(); auto origin=std::make_shared<double>(0);
		connect(play,&QCheckBox::toggled,this,[=](bool enabled) { pause->setEnabled(enabled); if (enabled && !pause->isChecked()) { *origin=time->value(); elapsed->start(); clock->start(); } else clock->stop(); });
		connect(pause,&QPushButton::toggled,this,[=](bool paused) { pause->setText(paused?tr("Resume"):tr("Pause")); if(paused) clock->stop(); else if(play->isChecked()) { *origin=time->value(); elapsed->start(); clock->start(); } });
		connect(clock,&QTimer::timeout,this,[=,this]() { if (isVisible()) { QSignalBlocker blocked(time); time->setValue(*origin+elapsed->elapsed()/1000.0); } });
		QImage diagnostic(16,16,QImage::Format_RGB32);
		for (int y=0;y<16;++y) for (int x=0;x<16;++x)
			diagnostic.setPixel(x,y,((x/4+y/4)%2)?qRgb(180,200,190):qRgb(75+5*x,85+4*y,100));
		auto textureImage=diffusePreview;
		auto updatePreview=[=,this]() {
			bool unsupportedUV=clamp>3 || (stagedType!=ShaderFlags::ST_Default && stagedType!=ShaderFlags::ST_EnvironmentMap
				&& stagedType!=ShaderFlags::ST_GlowShader && stagedType!=ShaderFlags::ST_Heightmap)
				|| (stagedFlags1 & (ShaderFlags::SLSF1_Projected_UV | ShaderFlags::SLSF1_Multiple_Textures | ShaderFlags::SLSF1_Landscape | ShaderFlags::SLSF1_Parallax_Occlusion))!=0
				|| (stagedFlags2 & ShaderFlags::SLSF2_Multi_Layer_Parallax)!=0;
			if (!scaleU || !scaleV || !offsetU || !offsetV) return;
			bool ok[4]; double s0=scaleU->text().toDouble(&ok[0]),s1=scaleV->text().toDouble(&ok[1]);
			double u=offsetU->text().toDouble(&ok[2]),v=offsetV->text().toDouble(&ok[3]);
			if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || !std::isfinite(s0) || !std::isfinite(s1) || !std::isfinite(u) || !std::isfinite(v)
				|| !std::isfinite(float(s0)) || !std::isfinite(float(s1)) || !std::isfinite(float(u)) || !std::isfinite(float(v))) {
				previewImage->clear(); previewInfo->setText(tr("Enter finite UV values to preview.")); return;
			}
			pad->setOffset(u,v);
			std::array<double,2> sampleScale{s0,s1},sampleOffset{u,v};
			auto &l=playback->lighting; auto &e=playback->effect;
			l.uvScale.set(float(s0),float(s1)); e.uvScale=l.uvScale;
			l.uvOffset.set(float(u),float(v)); e.uvOffset=l.uvOffset;
			l.emissiveColor=nif->get<Color3>(shader,"Emissive Color"); l.emissiveMult=nif->get<float>(shader,"Emissive Multiple"); l.alpha=nif->get<float>(shader,"Alpha");
			if(!isEffect) l.specularColor=nif->get<Color3>(shader,"Specular Color");
			e.emissiveColor=nif->get<Color4>(shader,"Base Color"); e.emissiveMult=nif->get<float>(shader,"Base Color Scale");
			for (const auto &field:fields) {
				QString name=nif->itemName(field.index);
				if (name=="Base Color" || name=="Emissive Color") {
					auto parts=field.edit->text().simplified().split(' '); Color4 c(0,0,0,1); bool valid=parts.size()==(isEffect?4:3);
					for(int j=0;j<parts.size() && j<4;++j) { bool ok=false; c[j]=parts[j].toFloat(&ok); valid=valid && ok && std::isfinite(c[j]); }
					if (valid) { e.emissiveColor=c; l.emissiveColor=Color3(c[0],c[1],c[2]); }
				} else if(name=="Specular Color") {
					auto parts=field.edit->text().simplified().split(' ',Qt::SkipEmptyParts); Color3 c; bool valid=parts.size()==3;
					for(int j=0;j<parts.size() && j<3;++j) { bool ok=false; c[j]=parts[j].toFloat(&ok); valid=valid && ok && std::isfinite(c[j]) && c[j]>=0 && c[j]<=1000; } if(valid) l.specularColor=c;
				} else { bool ok=false; float value=field.edit->text().toFloat(&ok); if (!ok || !std::isfinite(value)) continue;
					if (name=="Base Color Scale") e.emissiveMult=value;
					if (name=="Emissive Multiple") l.emissiveMult=value;
					if (name=="Alpha") l.alpha=value;
				}
			}
			if (play->isChecked()) playback->evaluate(float(time->value()));
			if (play->isChecked()) { auto &p=isEffect?static_cast<BSShaderLightingProperty &>(e):static_cast<BSShaderLightingProperty &>(l); sampleScale={p.uvScale.x,p.uvScale.y}; sampleOffset={p.uvOffset.x,p.uvOffset.y}; }
			if(panEnabled->isChecked()) {
				struct Clock:Controller { Clock(QModelIndex index={}):Controller(index){} void updateTime(float) override {} } clock;
				clock.start=0; clock.stop=float(panDuration->value()); clock.frequency=1; clock.phase=0;
				float fraction=clock.ctrlTime(float(time->value()))/clock.stop;
				for(int axis=0;axis<2;++axis) {
					if(panRecipe.controller[size_t(axis)]<0) { float base=float(axis?v:u),end=float(base+panSpeed[size_t(axis)]->value()*clock.stop); sampleOffset[size_t(axis)]=base+(end-base)*fraction; continue; }
					auto controller=nif->getBlockIndex(panRecipe.controller[size_t(axis)]); Clock authored(controller); authored.update(nif,controller);
					float original=0; int last=0; auto group=nif->getIndex(nif->getBlockIndex(panRecipe.data[size_t(axis)]),"Data");
					if(!Controller::interpolate(original,group,authored.ctrlTime(float(time->value())),last)) { previewImage->clear(); previewInfo->setText(tr("Pan curve could not be sampled.")); return; }
					double rate=panSpeed[size_t(axis)]->value(),oldRate=panRecipe.speed[size_t(axis)],origin=panRecipe.origin[size_t(axis)];
					if(rate==initialPanSpeed[size_t(axis)]) sampleOffset[size_t(axis)]=original;
					else sampleOffset[size_t(axis)]=oldRate!=0?origin+(original-origin)*(rate/oldRate):origin+rate/authored.frequency*(authored.ctrlTime(float(time->value()))-authored.start);
				}
			}
			Color3 tint=isEffect?Color3(e.emissiveColor[0],e.emissiveColor[1],e.emissiveColor[2]):l.emissiveColor;
			float brightness=isEffect?e.emissiveMult:l.emissiveMult,alpha=isEffect?e.emissiveColor[3]:l.alpha;
			resolved->setText(tr("%1 · RGB %2, %3, %4\nBrightness %5 · opacity %6").arg(play->isChecked()?tr("Animated values"):tr("Base values")).arg(tint[0],0,'g',4).arg(tint[1],0,'g',4).arg(tint[2],0,'g',4).arg(brightness,0,'g',4).arg(alpha,0,'g',4));
			bool finite=std::isfinite(brightness) && std::isfinite(alpha);
			for(int j=0;j<3;++j) finite=finite && std::isfinite(tint[j]);
			if(!isEffect) for(int j=0;j<3;++j) finite=finite && std::isfinite(l.specularColor[j]);
			for(double v:sampleScale) finite=finite && std::isfinite(v);
			for(double v:sampleOffset) finite=finite && std::isfinite(v);
			if (!finite) { previewImage->clear(); resolved->setText(tr("Non-finite animated result; preview refused.")); return; }
			if(this->hooks.preview) {
				MaterialGraphPreviewState state; state.time=time->value(); state.scale={float(sampleScale[0]),float(sampleScale[1])}; state.offset={float(sampleOffset[0]),float(sampleOffset[1])};
				state.color={tint[0],tint[1],tint[2],alpha}; state.brightness=brightness; state.alpha=alpha; state.type=stagedType; state.flags1=stagedFlags1; state.flags2=stagedFlags2;
				if(!isEffect) state.specular={l.specularColor[0],l.specularColor[1],l.specularColor[2]};
				for(auto edit:textureEdits) state.paths.append(edit?edit->text():QString());
				for(const auto &field:fields) if(field.kind==Kind::Scalar || field.kind==Kind::Byte) { bool ok=false; double value=field.edit->text().toDouble(&ok); if(ok && std::isfinite(value) && value>=field.low && value<=field.high) state.parameters.emplace_back(nif->itemName(field.index),float(value)); }
				this->hooks.preview(state);
			}
			QColor swatch(int(std::clamp(double(tint[0])*brightness,0.0,1.0)*255),int(std::clamp(double(tint[1])*brightness,0.0,1.0)*255),int(std::clamp(double(tint[2])*brightness,0.0,1.0)*255));
			if(colorSwatch->palette().color(QPalette::Window)!=swatch) { auto palette=colorSwatch->palette(); palette.setColor(QPalette::Window,swatch); colorSwatch->setPalette(palette); }
			if (unsupportedUV) { previewImage->clear(); previewInfo->setText(tr("Base UV preview unavailable for this shader type or mapping.")); return; }
			const auto &source=textureImage->isNull()?diagnostic:*textureImage;
			if(samplingCache->image.isNull() || samplingCache->source!=source.cacheKey() || samplingCache->scale!=sampleScale || samplingCache->offset!=sampleOffset) {
				auto rendered=materialUvPreview(source,sampleScale,sampleOffset,clamp&2,clamp&1,160);
				samplingCache->image=QPixmap::fromImage(rendered); samplingCache->source=source.cacheKey(); samplingCache->scale=sampleScale; samplingCache->offset=sampleOffset;
			}
			if(previewImage->pixmap().cacheKey()!=samplingCache->image.cacheKey()) previewImage->setPixmap(samplingCache->image);
			previewInfo->setText(play->isChecked()?tr("Animated UV sampling + emission swatch. Full effects require the viewport.%1").arg(playback->limitations.isEmpty()?QString():tr(" Playback limitations: hover for details.")):tr("%1. Staged base UV only; no parallax or animation.").arg(textureImage->isNull()?tr("Diagnostic checker"):tr("Source texture, downsampled")));
			previewInfo->setToolTip(tr("Sampling and emission diagnostics only. No full lighting, palette lookup, view/depth falloff, refraction, parallax, vertex colors/alpha, billboards or external sequences. %1").arg(playback->limitations.join("; ")));
		};
		// The earlier slot refresh already decoded both small images. Reuse its
		// result; every explicit refresh still reads current resource bytes.
		auto loadPreview=[=]() { updatePreview(); };
		if (pathEdits[0]) {
			connect(pathEdits[0],&QLineEdit::editingFinished,this,loadPreview);
			connect(pathEdits[0],&QLineEdit::textChanged,this,[=]() { previewImage->clear(); previewInfo->setText(tr("Loading edited texture path…")); });
		}
		for (auto edit:{scaleU,scaleV,offsetU,offsetV}) if (edit) connect(edit,&QLineEdit::textChanged,this,[=]() { updatePreview(); });
		for (const auto &field:fields) if (field.kind!=Kind::Text) connect(field.edit,&QLineEdit::textChanged,this,updatePreview);
		connect(play,&QCheckBox::toggled,this,[=]() { updatePreview(); });
		connect(time,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=]() { if(play->isChecked()) { *origin=time->value(); elapsed->restart(); } updatePreview(); });
		connect(clock,&QTimer::timeout,this,updatePreview);
		connect(sequenceChoice,qOverload<int>(&QComboBox::currentIndexChanged),this,[=,this]() {
			auto sequence=nif->getBlockIndex(sequenceChoice->currentData().toInt()); playback->load(nif,shader,isEffect,sequence);
			sequenceChoice->setToolTip(sequence.isValid()?tr("State %1 · local range %2–%3 s. Resolves this material's explicit controller/interpolator links. Material controller clocks are retained; geometry, particles, blend transitions and external behavior are not simulated.")
				.arg(nif->get<QString>(sequence,"Name")).arg(nif->get<float>(sequence,"Start Time"),0,'g',6).arg(nif->get<float>(sequence,"Stop Time"),0,'g',6):directAnimationHelp);
			pause->setChecked(true); time->setValue(sequence.isValid()?nif->get<float>(sequence,"Start Time"):0); play->setChecked(true); updatePreview();
		});
		connect(panEnabled,&QCheckBox::toggled,this,[=](bool enabled) { if(enabled) { play->setChecked(true); pause->setChecked(false); } updatePreview(); });
		for(auto speed:panSpeed) connect(speed,qOverload<double>(&QDoubleSpinBox::valueChanged),this,updatePreview);
		connect(panDuration,qOverload<double>(&QDoubleSpinBox::valueChanged),this,updatePreview);
		pad->onChange=[=](double u,double v) {
			if (!offsetU || !offsetV || !std::isfinite(u) || !std::isfinite(v)
				|| !std::isfinite(float(u)) || !std::isfinite(float(v))) return;
			offsetU->setText(QString::number(float(u),'g',9)); offsetV->setText(QString::number(float(v),'g',9));
		};
		loadPreview();
		auto diffuseWire=wire({290,90},{390,90},true); auto normalWire=wire({290,250},{390,145},true);
		auto heightWire=wire({290,540},{390,195},hasSlot3); auto glowWire=wire({290,395},{390,220},hasSlot2);
		auto environmentWire=wire({290,685},{760,235},hasEnvironment); wire({670,125},{760,150},true);
		auto environmentMaskWire=wire({290,830},{760,220},hasEnvironment);
		auto innerWire=wire({290,990},{760,240},bool(flags2&ShaderFlags::SLSF2_Multi_Layer_Parallax));
		if (isEffect) wire({290,490},{760,200},true)->setToolTip(tr("Falloff contributes in the normal scene renderer; the sampling preview does not evaluate it."));
		wire({670,345},{760,205},true);
		wire({670,420},{760,355},true);
		auto panWire=wire({670,840},{760,600},false); panWire->setToolTip(tr("Pan contributes to the preview. This connection does not define an engine operation order."));
		connect(panEnabled,&QCheckBox::toggled,this,[panWire](bool enabled) { panWire->setPen(QPen(enabled?QColor(116,208,185):QColor(95,101,107),enabled?2.2:1.2,enabled?Qt::SolidLine:Qt::DashLine)); });
		auto updateWires=[=,this]() {
			auto f=features(); const bool enabled[7]={true,!isEffect || bool(stagedFlags1 & (ShaderFlags::SLSF1_Greyscale_To_PaletteColor|ShaderFlags::SLSF1_Greyscale_To_PaletteAlpha)),f.slot2,f.slot3,f.environment,f.environment,f.inner};
			QGraphicsPathItem *wires[7]={diffuseWire,normalWire,glowWire,heightWire,environmentWire,environmentMaskWire,innerWire};
			for (int i=0;i<7;++i) {
				wires[i]->setVisible(pathEdits[size_t(i)]!=nullptr);
				bool active=enabled[i] && pathEdits[size_t(i)] && (*availability)[size_t(i)];
				wires[i]->setPen(QPen(active?QColor(116,208,185):QColor(95,101,107),active?2.2:1.2,active?Qt::SolidLine:Qt::DashLine));
			}
		};
		for (auto edit:pathEdits) if (edit) {
			connect(edit,&QLineEdit::textChanged,this,[=]() { updateWires(); });
			connect(edit,&QLineEdit::editingFinished,this,[=]() { updateWires(); });
		}
		updateWires();
		auto updateMaterial=[=,this]() {
			if (environmentStrength) environmentStrength->setEnabled(stagedType==ShaderFlags::ST_EnvironmentMap);
			for (const auto &refresh:textureStatuses) if (refresh) refresh();
			typeLabel->setText(isEffect?tr("Effect material"):tr("%1 (type %2)").arg(shaderChoice->currentText()).arg(stagedType));
			flagNote->setText(tr("Flags 1: 0x%1\nFlags 2: 0x%2").arg(stagedFlags1,8,16,QChar('0')).arg(stagedFlags2,8,16,QChar('0')));
			specularStatus->setText(isEffect?tr("Palette / falloff use shader flags"):((stagedFlags1 & ShaderFlags::SLSF1_Specular)?tr("Specular enabled"):tr("Specular flag disabled")));
			updateWires(); updatePreview();
		};
		connect(shaderChoice,qOverload<int>(&QComboBox::currentIndexChanged),this,[=,this]() {
			stagedType=shaderChoice->currentData().toUInt();
			constexpr unsigned managed1=ShaderFlags::SLSF1_Environment_Mapping|ShaderFlags::SLSF1_Parallax|ShaderFlags::SLSF1_Own_Emit;
			stagedFlags1=(stagedFlags1&~managed1)|(initialFlags1&ShaderFlags::SLSF1_Own_Emit); stagedFlags2&=~unsigned(ShaderFlags::SLSF2_Glow_Map);
			if (stagedType==initialType) { stagedFlags1=(stagedFlags1&~managed1)|(initialFlags1&managed1); stagedFlags2|=initialFlags2&ShaderFlags::SLSF2_Glow_Map; }
			else if (stagedType==ShaderFlags::ST_GlowShader) { stagedFlags1|=ShaderFlags::SLSF1_Own_Emit; stagedFlags2|=ShaderFlags::SLSF2_Glow_Map; }
			else if (stagedType==ShaderFlags::ST_EnvironmentMap) stagedFlags1|=ShaderFlags::SLSF1_Environment_Mapping;
			else if (stagedType==ShaderFlags::ST_Heightmap) stagedFlags1|=ShaderFlags::SLSF1_Parallax;
			updateMaterial();
		});
		connect(flagButton,&QPushButton::clicked,this,[this,updateMaterial]() { editFlags(); updateMaterial(); });
		updateMaterial();
		QTimer::singleShot(0,view,[view]() { view->horizontalScrollBar()->setValue(0); view->verticalScrollBar()->setValue(0); });
		status=new QLabel; status->setObjectName("materialGraphStatus"); status->setWordWrap(true); main->addWidget(status);
		auto buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);
		if(this->hooks.allowOverlay && hasShape) {
			auto layer=buttons->addButton(tr("Create animated layer…"),QDialogButtonBox::ActionRole); layer->setObjectName("materialCreateLayer");
			layer->setToolTip(tr("Copies this shape and its edited material into an independent temporary overlay. Position it in the main render area before Apply. The source stays unchanged."));
			if(!this->hooks.overlayReason.isEmpty()) { layer->setEnabled(false); layer->setToolTip(this->hooks.overlayReason); }
			connect(layer,&QPushButton::clicked,this,[this]() { creatingLayer=true; applyChanges(); creatingLayer=false; });
		}
		auto apply=buttons->addButton(tr("Apply"),QDialogButtonBox::ApplyRole); apply->setObjectName("materialGraphApply");
		apply->setEnabled(nif->undoStack!=nullptr); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
		connect(apply,&QPushButton::clicked,this,[this]() { applyChanges(); }); main->addWidget(buttons);
		installMaterialGraphHelp(this,view);
	}
private:
	void chooseColor(QLineEdit *edit,bool hasAlpha) {
		auto parts=edit->text().simplified().split(' ',Qt::SkipEmptyParts); int count=hasAlpha?4:3; std::array<float,4> original{0,0,0,1};
		if(parts.size()!=count) { status->setText(tr("Enter valid RGB values before opening the color wheel.")); return; }
		for(int j=0;j<count;++j) { bool ok=false; original[size_t(j)]=parts[j].toFloat(&ok); if(!ok || !std::isfinite(original[size_t(j)]) || original[size_t(j)]<0 || original[size_t(j)]>(j==3?1:1000)) { status->setText(tr("The color wheel needs RGB in 0–1000 and alpha in 0–1.")); return; } }
		QDialog dialog(this); dialog.setObjectName("materialColorPicker"); dialog.setWindowTitle(tr("Choose material color")); auto layout=new QVBoxLayout(&dialog);
		float initialGain=std::max({1.f,original[0],original[1],original[2]});
		auto wheel=new ColorWheel; wheel->setObjectName("materialColorWheel"); wheel->setSizeHint(QSize(220,220)); wheel->setAlpha(false); wheel->setColor(QColor::fromRgbF(original[0]/initialGain,original[1]/initialGain,original[2]/initialGain)); layout->addWidget(wheel);
		auto hex=new QLineEdit(wheel->getColor().name()); hex->setObjectName("materialColorHex"); hex->setMaxLength(7); hex->setToolTip(tr("Color wheel RGB as #RRGGBB. The multiplier is applied separately.")); layout->addWidget(hex);
		connect(wheel,&ColorWheel::sigColor,&dialog,[hex](const QColor &c) { hex->setText(c.name()); });
		auto form=new QFormLayout; layout->addLayout(form); auto gain=new QDoubleSpinBox; gain->setObjectName("materialColorGain"); gain->setRange(0,1000); gain->setDecimals(6); gain->setValue(initialGain); form->addRow(tr("RGB multiplier"),gain);
		auto alpha=new QDoubleSpinBox; alpha->setObjectName("materialColorAlpha"); alpha->setRange(0,1); alpha->setDecimals(6); alpha->setValue(original[3]); if(hasAlpha) form->addRow(tr("Alpha"),alpha); else { alpha->setParent(&dialog); alpha->hide(); }
		bool rgbEdited=false,alphaEdited=false;
		connect(wheel,&ColorWheel::sigColorEdited,&dialog,[&]() { rgbEdited=true; });
		connect(hex,&QLineEdit::textEdited,&dialog,[&](const QString &text) { QColor c(text); if(text.size()==7 && text.startsWith('#') && c.isValid()) { wheel->setColor(c); rgbEdited=true; } });
		connect(gain,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&]() { rgbEdited=true; }); connect(alpha,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,[&]() { alphaEdited=true; });
		auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
		if(dialog.exec()!=QDialog::Accepted || (!rgbEdited && !alphaEdited)) return;
		auto c=wheel->getColor(); if(rgbEdited) { original[0]=float(c.redF()*gain->value()); original[1]=float(c.greenF()*gain->value()); original[2]=float(c.blueF()*gain->value()); } if(alphaEdited) original[3]=float(alpha->value());
		QStringList result=parts; if(rgbEdited) for(int j=0;j<3;++j) result[j]=QString::number(original[size_t(j)],'g',9); if(alphaEdited && hasAlpha) result[3]=QString::number(original[3],'g',9); edit->setText(result.join(' '));
	}
	void finishApply() {
		if(hooks.finish) { auto error=hooks.finish(creatingLayer); if(!error.isEmpty()) { status->setText(error); return; } }
		accept();
	}
	LightingFeatures features() const {
		LightingFeatures f;
		f.height=stagedType==ShaderFlags::ST_Heightmap && (stagedFlags1&ShaderFlags::SLSF1_Parallax);
		f.glow=stagedType==ShaderFlags::ST_GlowShader && (stagedFlags2&ShaderFlags::SLSF2_Glow_Map) && (stagedFlags1&ShaderFlags::SLSF1_Own_Emit);
		f.environment=(stagedType==ShaderFlags::ST_EnvironmentMap && (stagedFlags1&ShaderFlags::SLSF1_Environment_Mapping))
			|| (stagedType==ShaderFlags::ST_EyeEnvmap && (stagedFlags1&ShaderFlags::SLSF1_Eye_Environment_Mapping))
			|| (model->getBSVersion()==100 && (stagedFlags2&ShaderFlags::SLSF2_Multi_Layer_Parallax));
		f.slot2=f.glow || (stagedFlags2&(ShaderFlags::SLSF2_Rim_Lighting|ShaderFlags::SLSF2_Soft_Lighting));
		f.slot3=f.height || stagedType==ShaderFlags::ST_Facegen;
		f.inner=(stagedFlags2&ShaderFlags::SLSF2_Multi_Layer_Parallax)!=0;
		f.known=stagedType==ShaderFlags::ST_Default || stagedType==ShaderFlags::ST_EnvironmentMap || stagedType==ShaderFlags::ST_GlowShader
			|| stagedType==ShaderFlags::ST_Heightmap || stagedType==ShaderFlags::ST_Facegen || stagedType==ShaderFlags::ST_EyeEnvmap;
		return f;
	}
	void editFlags() {
		QDialog dialog(this); dialog.setObjectName("materialShaderFlagsDialog"); dialog.setWindowTitle(tr("Shader flags"));
		dialog.resize(850,720);
		auto layout=new QVBoxLayout(&dialog);
		auto note=new QLabel(tr("Both complete Skyrim flag lists. Hover over a flag for its meaning and requirements. Undocumented or uncertain behavior is identified explicitly. Changes reach the NIF only after Apply in the graph.")); note->setWordWrap(true); layout->addWidget(note);
		auto columns=new QHBoxLayout; layout->addLayout(columns,1);
		std::array<std::array<QCheckBox *,32>,2> boxes{};
		for (int word=0;word<2;++word) {
			auto group=new QGroupBox(tr("Shader Flags %1").arg(word+1)); auto groupLayout=new QVBoxLayout(group);
			auto scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setObjectName(QString("materialFlagList%1").arg(word+1));
			auto contents=new QWidget; auto rows=new QVBoxLayout(contents);
			QString schema=QString("SkyrimShaderPropertyFlags%1").arg(word+1);
			const auto &options=NifValue::enumOptionData(schema).o;
			for (int bit=0;bit<32;++bit) {
				bool known=options.contains(quint32(bit)); QString name=known?options.value(quint32(bit)).first:tr("Unknown bit %1").arg(bit);
				auto label=name; label.replace('_',' ');
				auto box=new QCheckBox(QString("%1 · %2").arg(bit,2,10,QChar('0')).arg(label)); boxes[size_t(word)][size_t(bit)]=box;
				box->setObjectName(QString("materialFlag%1Bit%2").arg(word+1).arg(bit));
				// Preserve existing object names used by established integrations.
				if (word==0) {
					if (bit==0) box->setObjectName("materialFlagSpecular");
					if (bit==3) box->setObjectName("materialFlagVertexAlpha");
					if (bit==7) box->setObjectName("materialFlagEnvironment");
					if (bit==11) box->setObjectName("materialFlagParallax");
					if (bit==22) box->setObjectName("materialFlagEmission");
				} else {
					if (bit==4) box->setObjectName("materialFlagDoubleSided");
					if (bit==5) box->setObjectName("materialFlagVertexColors");
					if (bit==6) box->setObjectName("materialFlagGlow");
				}
				box->setProperty("flagWord",word+1); box->setProperty("flagBit",bit);
				quint32 mask=quint32(1)<<bit; box->setChecked((word?stagedFlags2:stagedFlags1)&mask); box->setEnabled(known);
				QString help=known?lightingFlagHelp(word,bit):tr("This bit is not defined by the loaded Skyrim schema. Its current value is preserved.");
				if (isEffect && word==0) {
					if (bit==4) help=tr("Use the effect's Greyscale Texture palette to resolve source grayscale into RGB. Requires a valid palette. The 2D sampling preview does not evaluate palette lookup.");
					if (bit==5) help=tr("Use the effect's Greyscale Texture palette for opacity. Requires a valid palette and appropriate alpha blending. The 2D preview does not evaluate palette alpha.");
					if (bit==6) help=tr("Enable the effect's view-angle falloff, using its start/stop angle and opacity parameters. Requires scene/view information; see the normal viewport.");
					if (bit==30) help=tr("Enable soft effect rendering using scene depth and Soft Falloff Depth. The 2D sampling preview has no scene-depth buffer.");
				}
				box->setToolTip(QString("<qt><b>%1</b><br>Bit %2 · 0x%3<p>%4</p></qt>").arg(name.toHtmlEscaped()).arg(bit).arg(mask,8,16,QChar('0')).arg(help.toHtmlEscaped()));
				rows->addWidget(box);
			}
			rows->addStretch(); scroll->setWidget(contents); groupLayout->addWidget(scroll); columns->addWidget(group,1);
		}
		auto words=new QLabel; words->setObjectName("materialFlagWords"); words->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(words);
		auto editedWord=[boxes](int word) { quint32 value=0; for (int bit=0;bit<32;++bit) if (boxes[size_t(word)][size_t(bit)]->isChecked()) value|=quint32(1)<<bit; return value; };
		auto updateWords=[words,editedWord]() { words->setText(tr("Flags 1: 0x%1     Flags 2: 0x%2").arg(editedWord(0),8,16,QChar('0')).arg(editedWord(1),8,16,QChar('0'))); };
		for (const auto &list:boxes) for (auto box:list) connect(box,&QCheckBox::toggled,&dialog,[updateWords]() { updateWords(); });
		updateWords();
		auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons);
		connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
		if (dialog.exec()!=QDialog::Accepted) return;
		stagedFlags1=editedWord(0); stagedFlags2=editedWord(1);
	}
	enum class Kind { Text,Scalar,Color,ColorAlpha,Byte };
	struct Field { QPersistentModelIndex index; QLineEdit *edit; QString initial; Kind kind; double low=0,high=0; };
	struct VectorField { QPersistentModelIndex index; QLineEdit *u,*v; QString initialU,initialV; };
	struct Change { QModelIndex index; NifValue before,after; };
	bool applyExpandedTextureSet(const std::vector<Change> &changes,int count,bool withPan=false) {
		QByteArray document; QBuffer output(&document); output.open(QIODevice::WriteOnly);
		if (!model->save(output)) { status->setText(tr("Could not stage the material.")); return false; }
		NifModel copy; QBuffer input(&document); input.open(QIODevice::ReadOnly);
		if (!copy.load(input)) { status->setText(tr("Could not reopen the staged document.")); return false; }
		for (const auto &change:changes) {
			std::vector<int> rows; for (auto index=change.index;index.isValid();index=index.parent()) rows.push_back(index.row());
			QModelIndex mapped; for (auto row=rows.rbegin();row!=rows.rend();++row) mapped=copy.index(*row,0,mapped);
			mapped=mapped.sibling(mapped.row(),NifModel::ValueCol);
			if (!mapped.isValid() || !copy.setData(mapped,change.after.toVariant(),Qt::EditRole)) { status->setText(tr("A staged material value could not be written.")); return false; }
		}
		int shaderBlock=model->getBlockNumber(shaderIndex),textureBlock=model->getBlockNumber(textureIndex);
		auto textures=copy.getBlockIndex(textureBlock),paths=copy.getIndex(textures,"Textures");
		if (!isEffect && (!copy.set<quint32>(textures,"Num Textures",quint32(count)) || !copy.updateArraySize(paths) || copy.rowCount(paths)!=count)) {
			status->setText(tr("Could not expand the staged texture set.")); return false;
		}
		for (int slot=0;!isEffect && slot<7 && slot<count;++slot) if (!copy.set<QString>(copy.getIndex(paths,slot),textureEdits[size_t(slot)]->text())) {
			status->setText(tr("Could not write a staged texture path.")); return false;
		}
		QSet<int> panEdits;
		if(withPan) {
			auto recipe=inspectPan(model,shaderIndex,isEffect);
			if(!recipe.compatible || recipe.watched!=panRecipe.watched) { status->setText(tr("The animation changed or cannot safely be edited as Pan. Reopen the graph.")); return false; }
			auto shader=copy.getBlockIndex(shaderBlock); auto base=copy.get<Vector2>(shader,"UV Offset");
			float duration=float(panDuration->value());
			for(int axis=0;axis<2;++axis) {
				QModelIndex controller=copy.getBlockIndex(recipe.controller[size_t(axis)]),data=copy.getBlockIndex(recipe.data[size_t(axis)]);
				if(!controller.isValid()) {
					if(panSpeed[size_t(axis)]->value()==0) continue;
					controller=copy.insertNiBlock(isEffect?"BSEffectShaderPropertyFloatController":"BSLightingShaderPropertyFloatController");
					auto interp=copy.insertNiBlock("NiFloatInterpolator"); data=copy.insertNiBlock("NiFloatData");
					if(!controller.isValid() || !interp.isValid() || !data.isValid()
						|| !copy.setLink(controller,"Next Controller",copy.getLink(shader,"Controller")) || !copy.setLink(shader,"Controller",copy.getBlockNumber(controller))
						|| !copy.setLink(controller,"Target",shaderBlock) || !copy.setLink(controller,"Interpolator",copy.getBlockNumber(interp))
						|| !copy.setLink(interp,"Data",copy.getBlockNumber(data)) || !copy.set<quint16>(controller,"Flags",72)
						|| !copy.set<float>(controller,"Start Time",0) || !copy.set<float>(controller,"Frequency",1) || !copy.set<float>(controller,"Phase",0)
						|| !copy.set<unsigned>(controller,"Controlled Variable",isEffect?(axis?8u:6u):(axis?22u:20u))) {
						status->setText(tr("Could not create the supported Pan controller arrangement.")); return false;
					}
				} else {
					// Existing channels keep their own clock, key times, origin and
					// interpolation. Scale travel and active tangents about the origin.
					if(panSpeed[size_t(axis)]->value()==initialPanSpeed[size_t(axis)]) continue;
					panEdits.insert(copy.getBlockNumber(data));
					auto group=copy.getIndex(data,"Data"),keys=copy.getIndex(group,"Keys"); auto first=copy.getIndex(keys,0),last=copy.getIndex(keys,1);
					double rate=panSpeed[size_t(axis)]->value(),oldRate=recipe.speed[size_t(axis)],origin=recipe.origin[size_t(axis)];
					double delta=oldRate!=0?(copy.get<float>(last,"Value")-origin)*(rate/oldRate)
						:rate/copy.get<float>(controller,"Frequency")*(copy.get<float>(controller,"Stop Time")-copy.get<float>(controller,"Start Time"));
					float end=float(origin+delta); bool ok=std::isfinite(end) && (rate==0 || end!=origin) && copy.set<float>(last,"Value",end);
					if(copy.get<int>(group,"Interpolation")==2) for(auto pair:{std::pair{first,"Backward"},std::pair{last,"Forward"}}) {
						float tangent=float(oldRate!=0?copy.get<float>(pair.first,pair.second)*(rate/oldRate):delta);
						ok=ok && std::isfinite(tangent) && copy.set<float>(pair.first,pair.second,tangent);
					}
					if(!ok) { status->setText(tr("Pan speed cannot be represented safely by these keys.")); return false; }
					continue;
				}
				auto group=copy.getIndex(data,"Data"),keys=copy.getIndex(group,"Keys");
				bool ok=copy.set<float>(controller,"Stop Time",duration) && copy.set<unsigned>(group,"Num Keys",2)
					&& copy.set<unsigned>(group,"Interpolation",1) && copy.updateArraySize(keys) && copy.rowCount(keys)==2;
				float end=float(double(base[axis])+panSpeed[size_t(axis)]->value()*duration);
				ok=ok && std::isfinite(end) && (panSpeed[size_t(axis)]->value()==0 || end!=base[axis]);
				for(int key=0;key<2 && ok;++key) { auto row=copy.getIndex(keys,key); ok=copy.set<float>(row,"Time",key?duration:0) && copy.set<float>(row,"Value",key?end:base[axis]); }
				if(!ok) { status->setText(tr("Pan keys could not be represented safely.")); return false; }
			}
		}
		QByteArray candidate; QBuffer saved(&candidate); saved.open(QIODevice::WriteOnly);
		if (!copy.save(saved)) { status->setText(tr("Could not serialize the staged material.")); return false; }
		NifModel verify; QBuffer reopened(&candidate); reopened.open(QIODevice::ReadOnly);
		if (!verify.load(reopened) || verify.getBlockCount()!=copy.getBlockCount()) { status->setText(tr("The staged material failed its reopen check.")); return false; }
		auto checkedPaths=verify.getIndex(verify.getBlockIndex(textureBlock),"Textures");
		if (!isEffect && verify.rowCount(checkedPaths)!=count) { status->setText(tr("The staged texture count changed on reopen.")); return false; }
		for (int slot=0;!isEffect && slot<7 && slot<count;++slot) if (verify.get<QString>(verify.getIndex(checkedPaths,slot))!=textureEdits[size_t(slot)]->text()) {
			status->setText(tr("A staged texture path changed on reopen.")); return false;
		}
		for (int block=0;block<model->getBlockCount();++block) if (block!=shaderBlock && block!=textureBlock && !panEdits.contains(block)) {
			QByteArray before,after;
			if (!blockBytes(model,model->getBlockIndex(block),before) || !blockBytes(&verify,verify.getBlockIndex(block),after) || before!=after) {
				status->setText(tr("An unrelated block changed in the staged document.")); return false;
			}
		}
		if(withPan) {
			if(!inspectPan(&verify,verify.getBlockIndex(shaderBlock),isEffect).compatible) { status->setText(tr("The saved Pan arrangement failed verification.")); return false; }
			std::vector<PanEdit> edits; std::vector<PanBlock> added;
			for(int block=0;block<copy.getBlockCount();++block) {
				QByteArray after; if(!blockBytes(&verify,verify.getBlockIndex(block),after)) return false;
				if(block>=model->getBlockCount()) { added.push_back({verify.itemName(verify.getBlockIndex(block)),after}); continue; }
				QByteArray before; if(!blockBytes(model,model->getBlockIndex(block),before)) return false;
				if(before!=after) edits.push_back({block,copy.itemName(copy.getBlockIndex(block)),before,after});
			}
			QByteArray check;
			if(!installPanBlocks(&copy,edits,added,model->getBlockCount(),false) || !materialDocument(&copy,check) || check!=document
				|| !installPanBlocks(&copy,edits,added,model->getBlockCount(),true) || !materialDocument(&copy,check) || check!=candidate) {
				status->setText(tr("Pan rollback/redo preflight failed; the open NIF is unchanged.")); return false;
			}
			auto success=std::make_shared<bool>(false);
			model->undoStack->push(new PanMaterialCommand(model,std::move(edits),std::move(added),document,candidate,model->getBlockCount(),success));
			return *success;
		}
		QByteArray shaderAfter,texturesAfter;
		if (!blockBytes(&verify,verify.getBlockIndex(shaderBlock),shaderAfter) || !blockBytes(&verify,verify.getBlockIndex(textureBlock),texturesAfter)) return false;
		std::array<MaterialBlockEdit,2> preflight{{{copy.getBlockIndex(shaderBlock),shaderBefore,shaderAfter},{copy.getBlockIndex(textureBlock),texturesBefore,texturesAfter}}};
		if (!replaceMaterialBlocks(&copy,preflight,false) || !replaceMaterialBlocks(&copy,preflight,true)) {
			status->setText(tr("The material replacement/rollback preflight failed.")); return false;
		}
		auto success=std::make_shared<bool>(false);
		std::array<MaterialBlockEdit,2> edits{{{shaderIndex,shaderBefore,shaderAfter},{textureIndex,texturesBefore,texturesAfter}}};
		model->undoStack->push(new ExpandedMaterialCommand(model,std::move(edits),success));
		if (!*success) status->setText(tr("The material replacement failed; reopen the graph."));
		return *success;
	}
	void applyChanges() {
		if(hooks.validate) { auto error=hooks.validate(); if(!error.isEmpty()) { status->setText(error); return; } }
		QByteArray a,b,c;
		if (!shaderIndex.isValid() || (!isEffect && !textureIndex.isValid()) || (hasShape && !shapeIndex.isValid())
			|| !blockBytes(model,shaderIndex,a) || a!=shaderBefore || (!isEffect && (!blockBytes(model,textureIndex,b) || b!=texturesBefore))
			|| (shapeIndex.isValid() && (!blockBytes(model,shapeIndex,c) || c!=shapeBefore))) {
			status->setText(tr("The selected material changed while this graph was open. Reopen it before applying.")); return;
		}
		std::vector<Change> changes;
		const std::pair<const char *,unsigned> shaderValues[]={{"Shader Type",stagedType},{"Shader Flags 1",stagedFlags1},{"Shader Flags 2",stagedFlags2}};
		for (const auto &[name,value]:shaderValues) {
			if (isEffect && QString(name)=="Shader Type") continue;
			auto index=model->getIndex(shaderIndex,name); auto item=model->getItem(index,false);
			if (!item) { status->setText(tr("A shader setting no longer exists.")); return; }
			if (model->get<unsigned>(index)==value) continue;
			NifValue before=item->value(),after=before;
			if (!after.set<unsigned>(value,model,item)) { status->setText(tr("A shader setting could not be represented.")); return; }
			changes.push_back({index.sibling(index.row(),NifModel::ValueCol),before,after});
		}
		for (const auto &f:fields) {
			if (f.edit->text()==f.initial) continue;
			auto item=model->getItem(f.index,false); if (!item) { status->setText(tr("A material field no longer exists.")); return; }
			NifValue original=item->value(), next=original; bool ok=false;
			if (f.kind==Kind::Text) {
				QString path=f.edit->text();
				if (path.contains(QChar::Null) || path.size()>4096) { status->setText(tr("Texture path is invalid or too long.")); return; }
				ok=next.set<QString>(path,model,item);
			} else if (f.kind==Kind::Color || f.kind==Kind::ColorAlpha) {
				auto components=f.edit->text().simplified().split(' ',Qt::SkipEmptyParts); Color4 color;
				int count=f.kind==Kind::ColorAlpha?4:3;
				if (components.size()!=count) { status->setText(tr("Color needs %1 numbers.").arg(count)); return; }
				for (int j=0;j<count;++j) {
					bool parsed=false; double v=components[j].toDouble(&parsed);
					if (!parsed || !std::isfinite(v) || v<f.low || v>(j==3?1:f.high)) { status->setText(tr("Color needs finite RGB in 0–1000 and alpha in 0–1.")); return; }
					color[j]=float(v);
				}
				ok=count==4?next.set<Color4>(color,model,item):next.set<Color3>(Color3(color[0],color[1],color[2]),model,item);
			} else {
				bool parsed=false; double v=f.edit->text().toDouble(&parsed);
				if (!parsed || !std::isfinite(v) || v<f.low || v>f.high) { status->setText(tr("A material value is outside its allowed range.")); return; }
				if (f.kind==Kind::Byte && std::floor(v)!=v) { status->setText(tr("Lighting influence must be a whole number in 0–255.")); return; }
				ok=f.kind==Kind::Byte?next.set<quint8>(quint8(v),model,item):next.set<float>(float(v),model,item);
			}
			if (!ok) { status->setText(tr("A material value could not be represented.")); return; }
			changes.push_back({f.index.sibling(f.index.row(),NifModel::ValueCol),original,next});
		}
		for (const auto &f:vectors) {
			if (f.u->text()==f.initialU && f.v->text()==f.initialV) continue;
			auto item=model->getItem(f.index,false); if (!item) { status->setText(tr("A UV field no longer exists.")); return; }
			bool aok=false,bok=false; double a=f.u->text().toDouble(&aok), b=f.v->text().toDouble(&bok);
			if (!aok || !bok || !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(float(a)) || !std::isfinite(float(b))) {
				status->setText(tr("UV values must be finite and representable as 32-bit floats.")); return;
			}
			NifValue original=item->value(), next=original;
			auto value=model->get<Vector2>(f.index);
			if (f.u->text()!=f.initialU) value[0]=float(a);
			if (f.v->text()!=f.initialV) value[1]=float(b);
			if (!next.set<Vector2>(value,model,item)) { status->setText(tr("The UV transform could not be represented.")); return; }
			changes.push_back({f.index.sibling(f.index.row(),NifModel::ValueCol),original,next});
		}
		int count=isEffect?0:model->rowCount(model->getIndex(textureIndex,"Textures")),required=count;
		for (int slot=0;!isEffect && slot<7;++slot) {
			const auto &path=textureEdits[size_t(slot)]->text();
			if (path!=initialTexturePaths[size_t(slot)] && (path.contains(QChar::Null) || path.size()>4096)) { status->setText(tr("Texture path is invalid or too long.")); return; }
			if (slot>=count && !path.isEmpty()) required=std::max(required,slot+1);
		}
		bool writePan=false;
		if(panEnabled->isChecked()) for(int axis=0;axis<2;++axis)
			writePan=writePan || (panRecipe.controller[size_t(axis)]<0?panSpeed[size_t(axis)]->value()!=0:panSpeed[size_t(axis)]->value()!=initialPanSpeed[size_t(axis)]);
		if (required>count || writePan) { if (applyExpandedTextureSet(changes,required,writePan)) finishApply(); return; }
		if (changes.empty()) { finishApply(); return; }
		ChangeValueCommand::createTransaction();
		model->undoStack->beginMacro(tr("Edit material graph"));
		bool shaderSettingsChanged=stagedType!=initialType || stagedFlags1!=initialFlags1 || stagedFlags2!=initialFlags2;
		for (const auto &c:changes) {
			// Merged multi-index commands suppress per-item condition updates.
			// Keep shader-setting commands separate inside the same one-step
			// Undo macro so conditional fields are invalidated on Undo/Redo too.
			if (shaderSettingsChanged) ChangeValueCommand::createTransaction();
			model->undoStack->push(new ChangeValueCommand(c.index,c.before,c.after,tr("material"),model));
		}
		model->undoStack->endMacro();
		ChangeValueCommand::createTransaction();
		finishApply();
	}
	NifModel *model;
	MaterialGraphHooks hooks;
	bool creatingLayer=false;
	QPersistentModelIndex shapeIndex,shaderIndex,textureIndex;
	bool hasShape,isEffect;
	QByteArray shapeBefore,shaderBefore,texturesBefore;
	std::vector<Field> fields;
	std::vector<VectorField> vectors;
	QLabel *status=nullptr;
	QComboBox *shaderChoice=nullptr;
	QComboBox *sequenceChoice=nullptr;
	PanRecipe panRecipe;
	QCheckBox *panEnabled=nullptr;
	std::array<QDoubleSpinBox *,2> panSpeed{};
	std::array<double,2> initialPanSpeed{};
	QDoubleSpinBox *panDuration=nullptr;
	double initialPanDuration=10;
	unsigned initialType=0,initialFlags1=0,initialFlags2=0,stagedType=0,stagedFlags1=0,stagedFlags2=0;
	std::array<std::function<void()>,7> textureStatuses;
	std::array<QLineEdit *,7> textureEdits{};
	std::array<QString,7> initialTexturePaths{};
};
}

QDialog *createMaterialGraph(NifModel *nif,const QModelIndex &selection,QWidget *parent,MaterialGraphHooks hooks)
{
	if (!nif || !selection.isValid()) { QMessageBox::information(parent,QObject::tr("Material Graph"),QObject::tr("Select a shape, lighting shader or effect shader.")); return nullptr; }
	if (nif->getVersionNumber()!=0x14020007 || nif->getUserVersion()!=12 || (nif->getBSVersion()!=83 && nif->getBSVersion()!=100)) {
		QMessageBox::information(parent,QObject::tr("Material Graph"),QObject::tr("This graph currently supports Skyrim and Skyrim SE lighting and effect materials.")); return nullptr;
	}
	auto block=nif->getBlockIndex(selection); QModelIndex shape,shader;
	if (nif->isNiBlock(block,"BSLightingShaderProperty") || nif->isNiBlock(block,"BSEffectShaderProperty")) shader=block;
	else {
		shape=block; shader=nif->getBlockIndex(nif->getLink(shape,"Shader Property"),"BSLightingShaderProperty");
		if (!shader.isValid()) shader=nif->getBlockIndex(nif->getLink(shape,"Shader Property"),"BSEffectShaderProperty");
		if (!shader.isValid()) for (int child:nif->getChildLinks(nif->getBlockNumber(shape))) {
			auto candidate=nif->getBlockIndex(child,"BSLightingShaderProperty");
			if (!candidate.isValid()) candidate=nif->getBlockIndex(child,"BSEffectShaderProperty");
			if (candidate.isValid()) { shader=candidate; break; }
		}
	}
	auto textures=nif->getBlockIndex(nif->getLink(shader,"Texture Set"),"BSShaderTextureSet");
	if (!shader.isValid() || (!nif->isNiBlock(shader,"BSEffectShaderProperty") && !textures.isValid())) {
		QMessageBox::information(parent,QObject::tr("Material Graph"),QObject::tr("The selection needs a BSLightingShaderProperty and BSShaderTextureSet.")); return nullptr;
	}
	return new MaterialGraph(nif,shape,shader,textures,parent,std::move(hooks));
}

void showMaterialGraph(NifModel *nif,const QModelIndex &selection,QWidget *parent) {
	if(auto dialog=createMaterialGraph(nif,selection,parent)) { dialog->exec(); delete dialog; }
}

QString commitMaterialGraphDocument(NifModel *nif,const QByteArray &before,const QByteArray &candidate,const QString &label) {
	QByteArray current; if(!nif || !nif->undoStack || !materialDocument(nif,current) || current!=before)
		return QObject::tr("The open document changed. Cancel and reopen the graph before applying.");
	if(candidate==before) return {};
	NifModel staged; QBuffer input; input.setData(candidate); input.open(QIODevice::ReadOnly);
	if(!staged.load(input) || staged.getBlockCount()<nif->getBlockCount()) return QObject::tr("The staged material document could not be reopened.");
	std::vector<PanEdit> edits; std::vector<PanBlock> added; const int count=nif->getBlockCount();
	for(int i=0;i<staged.getBlockCount();++i) {
		QByteArray after; if(!blockBytes(&staged,staged.getBlockIndex(i),after)) return QObject::tr("Could not read a staged block.");
		if(i>=count) { added.push_back({staged.itemName(staged.getBlockIndex(i)),after}); continue; }
		QByteArray original; if(!blockBytes(nif,nif->getBlockIndex(i),original)) return QObject::tr("Could not read an original block.");
		if(original!=after) edits.push_back({i,nif->itemName(nif->getBlockIndex(i)),original,after});
	}
	if(!installPanBlocks(&staged,edits,added,count,false) || !materialDocument(&staged,current) || current!=before
		|| !installPanBlocks(&staged,edits,added,count,true) || !materialDocument(&staged,current) || current!=candidate)
		return QObject::tr("Layer rollback/redo verification failed; the open document is unchanged.");
	auto success=std::make_shared<bool>(false);
	auto command=new PanMaterialCommand(nif,std::move(edits),std::move(added),before,candidate,count,success); command->setText(label); nif->undoStack->push(command);
	return *success?QString():QObject::tr("The layer commit was refused or rolled back.");
}
