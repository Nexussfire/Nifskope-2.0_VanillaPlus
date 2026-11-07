#include "vanillaplusdialog.h"
#include "sculptdialog.h"

#include "geometry/vanillaplus.h"
#include "gl/glproperty.h"
#include "model/nifmodel.h"
#include "lib/nvtristripwrapper.h"
#include "spells/mesh.h"
#include "spells/tangentspace.h"
#include "ddstxt16.hpp"

#include <QCheckBox>
#include <QBuffer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLinearGradient>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QUndoStack>
#include <QVariantMap>
#include <qfloat16.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>

namespace {
using namespace VanillaPlus;
constexpr int previewSize = 128;
constexpr int highPreviewSize = 256;

struct ShapeSnapshot {
	Mesh geometry;
	bool writable = false;
	bool classic = false;
	bool animatedShader = false;
	int writeBlock = -1;
	std::vector<std::array<quint16, 2>> originalHalfUV;
	int shapeBlock = -1, shaderBlock = -1, textureSetBlock = -1;
	QVector<QString> texturePaths;
	QByteArray shapeBytes, writeBytes, shaderBytes, textureSetBytes;
	unsigned shaderType = 0, flags1 = 0, flags2 = 0, clamp = 0;
	Displacement mapping;
	quint32 version=0,userVersion=0,bsVersion=0;
};

struct VanillaOperation {
	ShapeSnapshot reference;
	QByteArray currentShape,currentWrite;
	QVariantMap settings;
	bool valid=true;
};

// Document-owned reference state. Geometry history remains on the existing
// undo stack; each Apply command carries its matching before/after settings.
struct OperationStore : QObject {
	explicit OperationStore(NifModel *model) : QObject(model) { setObjectName("vanillaPlusOperations"); }
	std::vector<std::shared_ptr<VanillaOperation>> operations;
	int changing=0;
};

bool serializeBlock(const NifModel *nif, const QModelIndex &index, QByteArray &data)
{
	data.clear(); QBuffer buffer(&data);
	return buffer.open(QIODevice::WriteOnly) && nif->saveIndex(buffer,index);
}

QString inspectShape( const NifModel * nif, const QModelIndex & index, ShapeSnapshot & snapshot )
{
	Mesh & mesh = snapshot.geometry;
	if ( !nif || !index.isValid() )
		return QObject::tr( "Select a supported triangle shape in the block list or viewport." );
	snapshot.version=nif->getVersionNumber(); snapshot.userVersion=nif->getUserVersion(); snapshot.bsVersion=nif->getBSVersion();
	bool packed=nif->isNiBlock(index,"BSTriShape");
	bool strips=nif->isNiBlock(index,"NiTriStrips");
	bool classic=nif->isNiBlock(index,"NiTriShape") || strips;
	if (!packed && !classic)
		return QObject::tr( "Select a BSTriShape, NiTriShape, or NiTriStrips. Other geometry types are not supported yet." );
	snapshot.writable=packed && nif->getVersionNumber()==0x14020007 && nif->getUserVersion()==12 && nif->getBSVersion()==100;
	snapshot.classic=classic && !strips;
	snapshot.writeBlock=nif->getBlockNumber(index);
	if (classic) {
		if (nif->getLink(index,"Skin Instance")>=0) return QObject::tr("Skinned geometry is not supported.");
		auto data=nif->getBlockIndex(nif->getLink(index,"Data"),strips ? "NiTriStripsData" : "NiTriShapeData");
		if (!data.isValid()) return QObject::tr("The shape has no supported triangle data block.");
		snapshot.writeBlock=nif->getBlockNumber(data);
		snapshot.writable=!strips && nif->getVersionNumber()==0x14020007 && nif->getUserVersion()==12 && nif->getBSVersion()==83;
		auto positions=nif->getArray<Vector3>(data,"Vertices");
		auto normals=nif->getArray<Vector3>(data,"Normals");
		auto colors=nif->getArray<Color4>(data,"Vertex Colors");
		auto sets=nif->getIndex(data,"UV Sets");
		auto uv=nif->getArray<Vector2>(nif->getIndex(sets,0));
		QVector<::Triangle> faces;
		if (strips) {
			auto points=nif->getIndex(data,"Points"); QVector<QVector<quint16>> stripData;
			for (int i=0;i<nif->rowCount(points);++i) stripData.push_back(nif->getArray<quint16>(nif->getIndex(points,i)));
			faces=triangulate(stripData);
		} else faces=nif->getArray<::Triangle>(data,"Triangles");
		if (positions.isEmpty() || normals.size()!=positions.size() || uv.size()!=positions.size() || faces.isEmpty()
			|| (!colors.isEmpty() && colors.size()!=positions.size()))
			return QObject::tr("The triangle shape needs consistent positions, normals, UVs and triangle arrays.");
		mesh.vertices.reserve(size_t(positions.size()));
		for (qsizetype i=0;i<positions.size();++i) {
			Vertex v; v.position={positions[i][0],positions[i][1],positions[i][2]};
			v.normal={normals[i][0],normals[i][1],normals[i][2]}; v.uv={uv[i][0],uv[i][1]};
			if (!colors.isEmpty()) v.color={colors[i][0],colors[i][1],colors[i][2],colors[i][3]};
			mesh.vertices.push_back(v);
		}
		for (const auto &t:faces) mesh.triangles.push_back({t[0],t[1],t[2]});
	} else {
	auto desc = nif->get<BSVertexDesc>( index, "Vertex Desc" );
	if ( nif->getLink( index, "Skin" ) >= 0 || desc.HasFlag( VF_SKINNED ) )
		return QObject::tr( "Skinned geometry is not supported." );
	if ( desc.HasFlag( VF_EYEDATA ) || desc.HasFlag( VF_LANDDATA ) || desc.HasFlag( VF_UV_2 ) )
		return QObject::tr( "This shape has vertex attributes that the preview cannot preserve yet." );
	if ( !desc.HasFlag( VF_VERTEX ) || !desc.HasFlag( VF_NORMAL ) || !desc.HasFlag( VF_UV ) )
		return QObject::tr( "The shape must have positions, normals, and UV coordinates." );
	auto vertices = nif->getIndex( index, "Vertex Data" );
	auto triangles = nif->getIndex( index, "Triangles" );
	int nv = nif->get<int>( index, "Num Vertices" );
	int nt = nif->get<int>( index, "Num Triangles" );
	if ( !vertices.isValid() || !triangles.isValid() || nv <= 0 || nt <= 0
		 || nif->rowCount( vertices ) != nv || nif->rowCount( triangles ) != nt )
		return QObject::tr( "The geometry arrays are missing or inconsistent." );
	mesh.vertices.reserve( size_t( nv ) );
	for ( int i = 0; i < nv; ++i ) {
		auto v = nif->getIndex( vertices, i );
		if ( !nif->getIndex( v, "Vertex" ).isValid() || !nif->getIndex( v, "Normal" ).isValid()
			 || !nif->getIndex( v, "UV" ).isValid() )
			return QObject::tr( "A vertex is missing a required attribute." );
		Vector3 p = nif->get<Vector3>( v, "Vertex" );
		Vector3 n = nif->get<Vector3>( v, "Normal" );
		Vector2 uv = nif->get<Vector2>( v, "UV" );
		std::array<quint16,2> bits{};
		for (int j=0;j<2;++j) {
			if (!std::isfinite(uv[j])) return QObject::tr( "The shape has non-finite UV coordinates." );
			qfloat16 half(uv[j]);
			if (std::bit_cast<quint32>(float(half)) != std::bit_cast<quint32>(uv[j]))
				return QObject::tr( "An original UV cannot be preserved exactly in the NIF's half-precision format." );
			bits[size_t(j)] = std::bit_cast<quint16>(half);
		}
		snapshot.originalHalfUV.push_back(bits);
		Vertex out;
		out.position = { p[0], p[1], p[2] };
		out.normal = { n[0], n[1], n[2] };
		out.uv = { uv[0], uv[1] };
		if ( desc.HasFlag( VF_COLORS ) ) {
			if ( !nif->getIndex( v, "Vertex Colors" ).isValid() )
				return QObject::tr( "A vertex color is missing." );
			Color4 c = nif->get<Color4>( v, "Vertex Colors" );
			out.color = { c[0], c[1], c[2], c[3] };
		}
		mesh.vertices.push_back( out );
	}
	for ( const ::Triangle & t : nif->getArray<::Triangle>( triangles ) )
		mesh.triangles.push_back( { t[0], t[1], t[2] } );
	}
	try { mesh = captureSource( mesh ); }
	catch ( const std::exception & e ) { return QObject::tr( "Invalid geometry: %1" ).arg( e.what() ); }
	snapshot.shapeBlock = nif->getBlockNumber(index);
	auto shader = nif->getBlockIndex( nif->getLink( index, "Shader Property" ), "BSLightingShaderProperty" );
	if (!shader.isValid()) for (int child:nif->getChildLinks(snapshot.shapeBlock)) {
		auto candidate=nif->getBlockIndex(child,"BSLightingShaderProperty");
		if (candidate.isValid()) { shader=candidate; break; }
	}
	if (!shader.isValid()) return QObject::tr( "The shape needs a BSLightingShaderProperty for UV-aware preview." );
	auto textureSet = nif->getBlockIndex( nif->getLink( shader, "Texture Set" ), "BSShaderTextureSet" );
	if (!textureSet.isValid()) return QObject::tr( "The shader has no supported BSShaderTextureSet." );
	snapshot.shaderBlock = nif->getBlockNumber(shader); snapshot.textureSetBlock = nif->getBlockNumber(textureSet);
	snapshot.texturePaths = nif->getArray<QString>( textureSet, "Textures" );
	snapshot.shaderType = nif->get<unsigned int>(shader, "Shader Type");
	snapshot.flags1 = nif->get<unsigned int>(shader, "Shader Flags 1");
	snapshot.flags2 = nif->get<unsigned int>(shader, "Shader Flags 2");
	snapshot.clamp = nif->get<unsigned int>(shader, "Texture Clamp Mode");
	if (snapshot.shaderType != ShaderFlags::ST_Default && snapshot.shaderType != ShaderFlags::ST_EnvironmentMap
		&& snapshot.shaderType != ShaderFlags::ST_GlowShader && snapshot.shaderType != ShaderFlags::ST_Heightmap)
		return QObject::tr( "This shader type has material behavior the preview cannot interpret safely." );
	if ((snapshot.flags1 & (ShaderFlags::SLSF1_Model_Space_Normals | ShaderFlags::SLSF1_Projected_UV
		| ShaderFlags::SLSF1_Multiple_Textures
		| ShaderFlags::SLSF1_Parallax_Occlusion | ShaderFlags::SLSF1_Landscape))
		|| (snapshot.flags2 & ShaderFlags::SLSF2_Multi_Layer_Parallax))
		return QObject::tr( "This shader uses unsupported normal or UV material features." );
	snapshot.animatedShader=nif->getLink(shader,"Controller")>=0;
	if (snapshot.clamp > 3) return QObject::tr( "The shader uses an unsupported texture addressing mode." );
	if (!nif->getIndex(shader,"UV Scale").isValid() || !nif->getIndex(shader,"UV Offset").isValid()
		|| !nif->getIndex(shader,"Texture Clamp Mode").isValid())
		return QObject::tr( "The shader is missing required UV mapping parameters." );
	Vector2 scale=nif->get<Vector2>(shader, "UV Scale"), offset=nif->get<Vector2>(shader, "UV Offset");
	if (!std::isfinite(scale[0]) || !std::isfinite(scale[1]) || !std::isfinite(offset[0]) || !std::isfinite(offset[1]))
		return QObject::tr( "The shader has a non-finite UV transform." );
	snapshot.mapping.uvScale={scale[0],scale[1]}; snapshot.mapping.uvOffset={offset[0],offset[1]};
	snapshot.mapping.u=(snapshot.clamp & 2) ? Address::Wrap : Address::Clamp;
	snapshot.mapping.v=(snapshot.clamp & 1) ? Address::Wrap : Address::Clamp;
	snapshot.mapping.pinBoundaries=true;
	if (!serializeBlock(nif,index,snapshot.shapeBytes) || !serializeBlock(nif,shader,snapshot.shaderBytes)
		|| !serializeBlock(nif,textureSet,snapshot.textureSetBytes)
		|| !serializeBlock(nif,nif->getBlockIndex(snapshot.writeBlock),snapshot.writeBytes))
		return QObject::tr("Could not snapshot the shape and material blocks.");
	return {};
}

QString stageClassicShape(const NifModel *source,const ShapeSnapshot &snapshot,const Mesh &mesh,QByteArray &staged)
{
	auto sourceData=source->getBlockIndex(snapshot.writeBlock,"NiTriShapeData");
	if (!sourceData.isValid() || source->getLink(source->getBlockIndex(snapshot.shapeBlock),"Data")!=snapshot.writeBlock)
		return QObject::tr("The original triangle data link changed.");
	int owners=0;
	for (int i=0;i<source->getBlockCount();++i) if (source->getChildLinks(i).contains(snapshot.writeBlock)) {
		++owners; if (i!=snapshot.shapeBlock) return QObject::tr("The triangle data is shared with another block.");
	}
	if (owners!=1) return QObject::tr("The triangle data ownership is ambiguous.");
	auto uvSets=source->getIndex(sourceData,"UV Sets");
	if (source->rowCount(uvSets)!=1 || source->getLink(sourceData,"Additional Data")>=0
		|| source->get<int>(sourceData,"Num Match Groups")!=0)
		return QObject::tr("Multiple UV sets, additional vertex data or match groups are not supported for write-back.");
	auto oldPositions=source->getArray<Vector3>(sourceData,"Vertices");
	auto oldNormals=source->getArray<Vector3>(sourceData,"Normals");
	auto oldTangents=source->getArray<Vector3>(sourceData,"Tangents");
	auto oldBitangents=source->getArray<Vector3>(sourceData,"Bitangents");
	auto oldColors=source->getArray<Color4>(sourceData,"Vertex Colors");
	auto oldUV=source->getArray<Vector2>(source->getIndex(uvSets,0));
	if (oldPositions.size()!=qsizetype(snapshot.geometry.vertices.size()) || oldNormals.size()!=oldPositions.size()
		|| oldTangents.size()!=oldPositions.size() || oldBitangents.size()!=oldPositions.size()
		|| oldUV.size()!=oldPositions.size() || (!oldColors.isEmpty() && oldColors.size()!=oldPositions.size())
		|| mesh.vertices.size()<size_t(oldPositions.size()))
		return QObject::tr("The original vertex arrays cannot be preserved safely.");
	QVector<Vector3> positions, normals; QVector<Vector2> uvs; QVector<Color4> colors; QVector<::Triangle> faces;
	positions.reserve(qsizetype(mesh.vertices.size())); normals.resize(qsizetype(mesh.vertices.size()));
	uvs.reserve(qsizetype(mesh.vertices.size())); colors.reserve(qsizetype(mesh.vertices.size())); faces.reserve(qsizetype(mesh.triangles.size()));
	std::vector<Vec3> shading;
	try { shading=shadingNormals(mesh); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
	for (const auto &t:mesh.triangles) {
		faces.push_back(::Triangle(quint16(t[0]),quint16(t[1]),quint16(t[2])));
	}
	for (size_t i=0;i<mesh.vertices.size();++i) {
		const auto &v=mesh.vertices[i]; const auto &n=shading[i];
		if (i<size_t(oldUV.size())) for (int j=0;j<2;++j)
			if (std::bit_cast<quint32>(oldUV[qsizetype(i)][j])!=std::bit_cast<quint32>(v.uv[j]))
				return QObject::tr("An original UV changed in the working geometry.");
		positions.push_back(Vector3(v.position[0],v.position[1],v.position[2]));
		normals[qsizetype(i)]=Vector3(n[0],n[1],n[2]);
		uvs.push_back(Vector2(v.uv[0],v.uv[1]));
		if (!oldColors.isEmpty()) colors.push_back(Color4(v.color[0],v.color[1],v.color[2],v.color[3]));
	}
	QByteArray bytes; QBuffer writer(&bytes); writer.open(QIODevice::WriteOnly);
	if (!source->save(writer)) return QObject::tr("Could not copy the NIF into memory.");
	writer.close(); QBuffer reader(&bytes); reader.open(QIODevice::ReadOnly); NifModel copy;
	if (!copy.load(reader)) return QObject::tr("Could not reopen the copied NIF.");
	auto data=copy.getBlockIndex(snapshot.writeBlock), shape=copy.getBlockIndex(snapshot.shapeBlock);
	if (!copy.set<quint16>(data,"Num Vertices",quint16(mesh.vertices.size()))
		|| !copy.set<quint16>(data,"Num Triangles",quint16(mesh.triangles.size()))
		|| !copy.set<quint32>(data,"Num Triangle Points",quint32(mesh.triangles.size()*3)))
		return QObject::tr("Could not update copied geometry counts.");
	for (const char *name:{"Vertices","Normals","Tangents","Bitangents","Triangles"})
		if (!copy.updateArraySize(data,name)) return QObject::tr("Could not resize a copied geometry array.");
	if (!oldColors.isEmpty() && !copy.updateArraySize(data,"Vertex Colors")) return QObject::tr("Could not resize copied vertex colors.");
	auto copiedSets=copy.getIndex(data,"UV Sets");
	if (!copy.updateArraySize(copy.getIndex(copiedSets,0))) return QObject::tr("Could not resize copied UVs.");
	copy.setArray<Vector3>(data,"Vertices",positions); copy.setArray<Vector3>(data,"Normals",normals);
	copy.setArray<Vector2>(copy.getIndex(copiedSets,0),uvs); copy.setArray<::Triangle>(data,"Triangles",faces);
	if (!oldColors.isEmpty()) copy.setArray<Color4>(data,"Vertex Colors",colors);
	spUpdateCenterRadius bounds; bounds.cast(&copy,data);
	spTangentSpace tangent; tangent.cast(&copy,shape);
	staged.clear(); QBuffer result(&staged); result.open(QIODevice::WriteOnly);
	if (!copy.save(result)) return QObject::tr("Could not serialize the staged copy.");
	result.close(); QBuffer checkBuffer(&staged); checkBuffer.open(QIODevice::ReadOnly); NifModel check;
	if (!check.load(checkBuffer) || check.getBlockCount()!=source->getBlockCount()) return QObject::tr("The staged copy could not be reopened.");
	auto checked=check.getBlockIndex(snapshot.writeBlock,"NiTriShapeData");
	auto checkedUV=check.getArray<Vector2>(check.getIndex(check.getIndex(checked,"UV Sets"),0));
	auto checkedPositions=check.getArray<Vector3>(checked,"Vertices");
	auto checkedNormals=check.getArray<Vector3>(checked,"Normals");
	auto checkedTangents=check.getArray<Vector3>(checked,"Tangents");
	auto checkedBitangents=check.getArray<Vector3>(checked,"Bitangents");
	auto checkedColors=check.getArray<Color4>(checked,"Vertex Colors");
	auto checkedFaces=check.getArray<::Triangle>(checked,"Triangles");
	if (checkedUV.size()!=uvs.size() || checkedPositions.size()!=positions.size() || checkedNormals.size()!=normals.size()
		|| checkedTangents.size()!=normals.size() || checkedBitangents.size()!=normals.size()
		|| checkedColors.size()!=colors.size() || checkedFaces.size()!=faces.size())
		return QObject::tr("The staged vertex or triangle arrays changed on reopen.");
	for (qsizetype i=0;i<uvs.size();++i) {
		for (int j=0;j<2;++j) if (std::bit_cast<quint32>(checkedUV[i][j])!=std::bit_cast<quint32>(uvs[i][j]))
			return QObject::tr("A staged UV changed on reopen.");
		for (int j=0;j<3;++j)
			if (checkedPositions[i][j]!=positions[i][j] || !std::isfinite(checkedNormals[i][j])
				|| !std::isfinite(checkedTangents[i][j]) || !std::isfinite(checkedBitangents[i][j]))
				return QObject::tr("A staged position, normal or tangent is invalid.");
		if (checkedNormals[i].length()<0.9f || checkedTangents[i].length()<0.9f || checkedBitangents[i].length()<0.9f)
			return QObject::tr("A staged normal or tangent is invalid.");
		if (!colors.isEmpty()) for (int j=0;j<4;++j)
			if (std::bit_cast<quint32>(checkedColors[i][j])!=std::bit_cast<quint32>(colors[i][j]))
				return QObject::tr("A staged vertex color changed on reopen.");
	}
	for (qsizetype i=0;i<faces.size();++i) for (int j=0;j<3;++j)
		if (checkedFaces[i][j]!=faces[i][j]) return QObject::tr("A staged triangle changed on reopen.");
	for (int i=0;i<source->getBlockCount();++i) if (i!=snapshot.writeBlock) {
		QByteArray a,b;
		if (!serializeBlock(source,source->getBlockIndex(i),a) || !serializeBlock(&check,check.getBlockIndex(i),b) || a!=b)
			return QObject::tr("An unrelated block changed in the staged copy.");
	}
	return {};
}

// Prepare a complete, independent in-memory NIF. No changes reach the live
// model or filesystem; replacement is deliberately a separate transaction.
QString stageShape( const NifModel * source, const ShapeSnapshot & snapshot, const Mesh & mesh, QByteArray & staged )
{
	if (!snapshot.writable) return QObject::tr("Write-back is not yet supported for this NIF geometry format; preview only.");
	if (!source || !(source->isNiBlock(source->getBlockIndex(snapshot.shapeBlock),"BSTriShape")
		|| source->isNiBlock(source->getBlockIndex(snapshot.shapeBlock),"NiTriShape")))
		return QObject::tr("The original shape is no longer available.");
	QByteArray currentShape,currentShader,currentTextures;
	if (!serializeBlock(source,source->getBlockIndex(snapshot.shapeBlock),currentShape) || currentShape!=snapshot.shapeBytes
		|| !serializeBlock(source,source->getBlockIndex(snapshot.shaderBlock),currentShader) || currentShader!=snapshot.shaderBytes
		|| !serializeBlock(source,source->getBlockIndex(snapshot.textureSetBlock),currentTextures) || currentTextures!=snapshot.textureSetBytes)
		return QObject::tr("The shape or material changed since the preview opened; reopen the tool.");
	QByteArray currentData;
	if (!serializeBlock(source,source->getBlockIndex(snapshot.writeBlock),currentData) || currentData!=snapshot.writeBytes)
		return QObject::tr("The geometry data changed since the preview opened; reopen the tool.");
	try { validate(mesh); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
	if (mesh.patchAuthority) {
		if (mesh.patchAuthority->original!=snapshot.geometry.source)
			return QObject::tr("Patch correspondence does not match the original shape snapshot.");
		try { (void)resolvePatchSources(mesh); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
	}
	if (snapshot.classic) return stageClassicShape(source,snapshot,mesh,staged);
	QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
	if (!source->save(output)) return QObject::tr("Could not copy the NIF into memory.");
	output.close(); QBuffer input(&bytes); input.open(QIODevice::ReadOnly);
	NifModel copy;
	if (!copy.load(input)) return QObject::tr("Could not reopen the copied NIF in memory.");
	auto shape=copy.getBlockIndex(snapshot.shapeBlock);
	auto desc=copy.get<BSVertexDesc>(shape,"Vertex Desc");
	if (snapshot.flags2 & (ShaderFlags::SLSF2_Tree_Anim | ShaderFlags::SLSF2_Multi_Index_Snow))
		return QObject::tr("Tree animation or multi-index snow uses material behavior that staging cannot preserve safely yet.");
	if (!desc.HasFlag(VF_TANGENT) || copy.get<quint32>(shape,"Particle Data Size") != 0)
		return QObject::tr("Staging currently requires tangent data and no particle data.");
	if (mesh.vertices.size()<snapshot.originalHalfUV.size())
		return QObject::tr("The original vertices are missing from the working geometry.");
	for (size_t i=0;i<mesh.vertices.size();++i) for (int j=0;j<2;++j) {
		qfloat16 half(mesh.vertices[i].uv[j]);
		if (!std::isfinite(float(half))) return QObject::tr("A generated UV exceeds the NIF half-precision range.");
		if (i<snapshot.originalHalfUV.size() && std::bit_cast<quint16>(half)!=snapshot.originalHalfUV[i][size_t(j)])
			return QObject::tr("An original UV changed in the working geometry.");
	}
	std::vector<Vec3> normals;
	try { normals=shadingNormals(mesh); } catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
	QVector<::Triangle> triangles; triangles.reserve(qsizetype(mesh.triangles.size()));
	for (const auto &t : mesh.triangles) {
		triangles.push_back(::Triangle(quint16(t[0]),quint16(t[1]),quint16(t[2])));
	}
	if (!copy.set<quint16>(shape,"Num Vertices",quint16(mesh.vertices.size()))
		|| !copy.set<quint16>(shape,"Num Triangles",quint16(mesh.triangles.size())))
		return QObject::tr("Could not set geometry counts on the copied shape.");
	auto verts=copy.getIndex(shape,"Vertex Data"), tris=copy.getIndex(shape,"Triangles");
	if (!copy.updateArraySize(verts) || !copy.updateArraySize(tris)
		|| copy.rowCount(verts)!=int(mesh.vertices.size()) || copy.rowCount(tris)!=int(mesh.triangles.size()))
		return QObject::tr("Could not resize geometry on the copied shape.");
	for (size_t i=0;i<mesh.vertices.size();++i) {
		const auto &v=mesh.vertices[i]; const auto &n=normals[i];
		auto row=copy.getIndex(verts,int(i));
		if (!copy.set<Vector3>(row,"Vertex",Vector3(v.position[0],v.position[1],v.position[2]))
			|| !copy.set<HalfVector2>(row,"UV",HalfVector2(v.uv[0],v.uv[1]))
			|| !copy.set<ByteVector3>(row,"Normal",ByteVector3(n[0],n[1],n[2])))
			return QObject::tr("Could not write a copied vertex attribute.");
		if (desc.HasFlag(VF_COLORS)) {
			ByteColor4 color;
			for (int j=0;j<4;++j) color[j]=v.color[size_t(j)];
			if (!copy.set<ByteColor4>(row,"Vertex Colors",color)) return QObject::tr("Could not write copied vertex colors.");
		}
	}
	copy.setArray<::Triangle>(tris,triangles);
	spRemoveWasteVertices::updateBSTriShape(&copy,shape);
	spUpdateBounds bounds; bounds.cast(&copy,shape);
	spTangentSpace tangent; tangent.cast(&copy,shape);
	staged.clear(); QBuffer result(&staged); result.open(QIODevice::WriteOnly);
	if (!copy.save(result)) return QObject::tr("Could not serialize the staged copy.");
	result.close(); QBuffer verifyInput(&staged); verifyInput.open(QIODevice::ReadOnly); NifModel verify;
	if (!verify.load(verifyInput)) return QObject::tr("The staged copy could not be reopened.");
	auto checked=verify.getBlockIndex(snapshot.shapeBlock);
	if (!verify.isNiBlock(checked,"BSTriShape") || verify.get<int>(checked,"Num Vertices")!=int(mesh.vertices.size())
		|| verify.get<int>(checked,"Num Triangles")!=int(mesh.triangles.size())
		|| verify.getLink(checked,"Shader Property")!=snapshot.shaderBlock)
		return QObject::tr("The staged shape failed its reopen checks.");
	auto checkedVerts=verify.getIndex(checked,"Vertex Data");
	for (size_t i=0;i<mesh.vertices.size();++i) {
		auto row=verify.getIndex(checkedVerts,int(i));
		auto uv=verify.get<HalfVector2>(row,"UV"); auto pos=verify.get<Vector3>(row,"Vertex");
		auto normal=verify.get<ByteVector3>(row,"Normal"); auto tangentValue=verify.get<ByteVector3>(row,"Tangent");
		for (int j=0;j<2;++j) if (std::bit_cast<quint16>(qfloat16(uv[j]))!=std::bit_cast<quint16>(qfloat16(mesh.vertices[i].uv[j])))
			return QObject::tr("A UV failed the staged save/reopen check.");
		for (int j=0;j<3;++j) if (pos[j]!=mesh.vertices[i].position[size_t(j)] || !std::isfinite(normal[j]) || !std::isfinite(tangentValue[j]))
			return QObject::tr("A position, normal or tangent failed the staged save/reopen check.");
		if (normal.length()<0.9f || tangentValue.length()<0.9f)
			return QObject::tr("A normal or tangent is invalid in the staged copy.");
		if (desc.HasFlag(VF_COLORS)) {
			auto color=verify.get<ByteColor4>(row,"Vertex Colors");
			for (int j=0;j<4;++j) if (std::abs(color[j]-mesh.vertices[i].color[size_t(j)])>(i<snapshot.originalHalfUV.size()?0.000001f:0.0041f))
				return QObject::tr("A vertex color failed the staged save/reopen check.");
		}
	}
	auto reopenedTriangles=verify.getArray<::Triangle>(checked,"Triangles");
	if (reopenedTriangles.size()!=triangles.size()) return QObject::tr("Staged triangle count changed on reopen.");
	for (qsizetype i=0;i<triangles.size();++i) for (int j=0;j<3;++j)
		if (reopenedTriangles[i][j]!=triangles[i][j]) return QObject::tr("Staged triangle indices changed on reopen.");
	if (verify.getBlockCount()!=source->getBlockCount()) return QObject::tr("The staged copy changed the block count.");
	for (int i=0;i<source->getBlockCount();++i) if (i!=snapshot.shapeBlock) {
		QByteArray before,after; QBuffer b1(&before),b2(&after); b1.open(QIODevice::WriteOnly); b2.open(QIODevice::WriteOnly);
		if (!source->saveIndex(b1,source->getBlockIndex(i)) || !verify.saveIndex(b2,verify.getBlockIndex(i)) || before!=after)
			return QObject::tr("An unrelated block changed in the staged copy.");
	}
	return {};
}

bool replaceShapeBytes(NifModel *nif, const QModelIndex &shape, const QByteArray &expected, const QByteArray &replacement)
{
	QByteArray current;
	if (!nif || !shape.isValid() || !serializeBlock(nif,shape,current) || current!=expected) return false;
	auto load = [&](const QByteArray &bytes) {
		QBuffer buffer; buffer.setData(bytes); if (!buffer.open(QIODevice::ReadOnly)) return false;
		bool ok=nif->loadIndex(buffer,shape) && buffer.pos()==bytes.size();
		QByteArray actual;
		return ok && serializeBlock(nif,shape,actual) && actual==bytes;
	};
	if (!load(replacement)) {
		if (!load(current)) qCritical("Vanilla+ could not restore the original shape after a failed replacement");
		emit nif->dataChanged(shape,shape);
		return false;
	}
	emit nif->dataChanged(shape,shape);
	return true;
}

bool operationMatches(const NifModel *nif,const VanillaOperation &op)
{
	QByteArray bytes;
	return op.valid && nif->getVersionNumber()==op.reference.version && nif->getUserVersion()==op.reference.userVersion && nif->getBSVersion()==op.reference.bsVersion
		&& serializeBlock(nif,nif->getBlockIndex(op.reference.shapeBlock),bytes) && bytes==op.currentShape
		&& serializeBlock(nif,nif->getBlockIndex(op.reference.writeBlock),bytes) && bytes==op.currentWrite
		&& serializeBlock(nif,nif->getBlockIndex(op.reference.shaderBlock),bytes) && bytes==op.reference.shaderBytes
		&& serializeBlock(nif,nif->getBlockIndex(op.reference.textureSetBlock),bytes) && bytes==op.reference.textureSetBytes;
}

OperationStore *operationStore(NifModel *nif)
{
	if (auto child=nif->findChild<QObject *>("vanillaPlusOperations",Qt::FindDirectChildrenOnly)) return static_cast<OperationStore *>(child);
	auto store=new OperationStore(nif);
	auto invalidate=[store]() { if (!store->changing) for (auto &op:store->operations) op->valid=false; };
	QObject::connect(nif,&QAbstractItemModel::modelAboutToBeReset,store,[store,invalidate]() { invalidate(); if (!store->changing) store->operations.clear(); });
	auto rowsChanged=[nif,store](const QModelIndex &parent) {
		if (store->changing) return;
		int block=parent.isValid()?nif->getBlockNumber(parent):-1;
		for (auto &op:store->operations) {
			const auto &s=op->reference;
			if (block<0 || block==s.shapeBlock || block==s.writeBlock || block==s.shaderBlock || block==s.textureSetBlock) op->valid=false;
		}
	};
	QObject::connect(nif,&QAbstractItemModel::rowsInserted,store,rowsChanged);
	QObject::connect(nif,&QAbstractItemModel::rowsRemoved,store,rowsChanged);
	QObject::connect(nif,&QAbstractItemModel::dataChanged,store,[nif,store](const QModelIndex &first,const QModelIndex &last) {
		if (store->changing) return;
		int block=first.parent()==last.parent()?nif->getBlockNumber(first):-1;
		if (block>=0 && nif->getBlockNumber(last)!=block) block=-1;
		for (auto &op:store->operations) if (op->valid) {
			const auto &s=op->reference;
			// Header/ambiguous ranges remain conservative. An unrelated block's
			// value edit cannot change these exact captured geometry/material bytes.
			if (block>=0 && block!=s.shapeBlock && block!=s.writeBlock && block!=s.shaderBlock && block!=s.textureSetBlock) continue;
			if (!operationMatches(nif,*op)) op->valid=false;
		}
	});
	return store;
}

QString stageOperation(const NifModel *source,const ShapeSnapshot &live,const ShapeSnapshot &reference,const Mesh &mesh,QByteArray &staged)
{
	QByteArray bytes;
	if (source->getVersionNumber()!=live.version || source->getUserVersion()!=live.userVersion || source->getBSVersion()!=live.bsVersion)
		return QObject::tr("The NIF header changed since the preview opened; reopen the tool.");
	for (auto pair: {std::pair{live.shapeBlock,live.shapeBytes},
		std::pair{live.shaderBlock,live.shaderBytes},std::pair{live.textureSetBlock,live.textureSetBytes}})
		if (!serializeBlock(source,source->getBlockIndex(pair.first),bytes) || bytes!=pair.second)
			return QObject::tr("The shape or material changed since the preview opened; reopen the tool.");
	if (!serializeBlock(source,source->getBlockIndex(live.writeBlock),bytes) || bytes!=live.writeBytes)
		return QObject::tr("The geometry data changed since the preview opened; reopen the tool.");
	if (live.writeBytes==reference.writeBytes) return stageShape(source,reference,mesh,staged);
	// Stage from a private copy with only this operation's reference geometry
	// restored. The live NIF is never rolled back merely to produce a preview.
	QByteArray document; QBuffer output(&document); output.open(QIODevice::WriteOnly);
	if (!source->save(output)) return QObject::tr("Could not copy the current document for refinement.");
	QBuffer input(&document); input.open(QIODevice::ReadOnly); NifModel copy;
	if (!copy.load(input) || !replaceShapeBytes(&copy,copy.getBlockIndex(live.writeBlock),live.writeBytes,reference.writeBytes))
		return QObject::tr("Could not restore the reference in the private staging copy.");
	return stageShape(&copy,reference,mesh,staged);
}

class ReplaceShapeCommand final : public QUndoCommand
{
public:
	ReplaceShapeCommand(NifModel *nif, const QModelIndex &shape, QByteArray before, QByteArray after,
		std::shared_ptr<bool> initialSuccess,std::shared_ptr<VanillaOperation> op,QVariantMap beforeSettings,QVariantMap afterSettings)
		: model(nif), index(shape), oldBytes(std::move(before)), newBytes(std::move(after)), success(std::move(initialSuccess)),
		operation(std::move(op)),oldSettings(std::move(beforeSettings)),newSettings(std::move(afterSettings))
	{ setText(QObject::tr("Apply Vanilla+ Geometry")); }
	void redo() override { change(oldBytes,newBytes,newSettings); }
	void undo() override { change(newBytes,oldBytes,oldSettings); }
private:
	void change(const QByteArray &expected, const QByteArray &replacement,const QVariantMap &settings) {
		auto store=model ? operationStore(model) : nullptr;
		if (store) ++store->changing;
		bool ok=model && index.isValid() && replaceShapeBytes(model,index,expected,replacement);
		if (ok && operation->valid) {
			operation->currentWrite=replacement; operation->settings=settings;
			serializeBlock(model,model->getBlockIndex(operation->reference.shapeBlock),operation->currentShape);
		}
		if (store) --store->changing;
		if (success) { *success=ok; success.reset(); if (!ok) setObsolete(true); }
		else if (!ok) {
			setObsolete(true);
			QMessageBox::warning(nullptr,QObject::tr("Vanilla+ Geometry"),
				QObject::tr("Undo or redo could not be applied because the shape changed. The current shape was left intact."));
		}
	}
	QPointer<NifModel> model;
	QPersistentModelIndex index;
	QByteArray oldBytes,newBytes;
	std::shared_ptr<bool> success;
	std::shared_ptr<VanillaOperation> operation;
	QVariantMap oldSettings,newSettings;
};

enum class HeightSource { Diffuse, Normal, Height };
bool readHeightSource( const NifModel * nif, const QString & path, HeightSource source, HeightField & field, HeightField & highField, QString & status )
{
	if ( path.isEmpty() ) { status = QObject::tr( "Not referenced" ); return false; }
	QByteArray bytes;
	if ( !nif->getResourceFile( bytes, path, "textures", ".dds" ) ) {
		status = QObject::tr( "Not found: %1" ).arg( path );
		return false;
	}
	try {
		DDSTexture16 texture( reinterpret_cast<const unsigned char *>( bytes.constData() ), size_t( bytes.size() ), 0, true );
		if ( texture.getIsCubeMap() ) throw std::invalid_argument( "Cubemap is not a 2D height source" );
		HeightField out{ size_t(texture.getWidth()), size_t(texture.getHeight()), {} };
		out.values.reserve( out.width * out.height );
		for ( int y = 0; y < texture.getHeight(); ++y ) {
			for ( int x = 0; x < texture.getWidth(); ++x ) {
				FloatVector4 c = FloatVector4::convertFloat16( texture.getPixelC( x, y, 0 ) );
				Color rgba{ c[0], c[1], c[2], c[3] };
				if ( source == HeightSource::Normal ) {
					// Normal-derived directional cue only, not mathematical height.
					for ( int j = 0; j < 3; ++j ) rgba[j] = std::clamp( rgba[j], 0.0f, 1.0f );
					float nx = rgba[0] * 2 - 1, ny = rgba[1] * 2 - 1, nz = rgba[2] * 2 - 1;
					out.values.push_back( nx * nx + ny * ny + nz * nz < 1e-12f ? 0.5f : normalReliefCue( rgba ) );
				} else if ( source == HeightSource::Height ) {
					out.values.push_back( rgba[0] ); // Skyrim's height shader samples red.
				} else {
					out.values.push_back( diffuseScalar( rgba ) );
				}
			}
		}
		validate( out );
		field = areaResample( out, previewSize, previewSize );
		highField = areaResample( out, highPreviewSize, highPreviewSize );
		status = QObject::tr( "Loaded: %1 (%2 × %3)" ).arg( path ).arg( texture.getWidth() ).arg( texture.getHeight() );
		return true;
	} catch ( const std::exception & e ) {
		status = QObject::tr( "Could not decode %1: %2" ).arg( path, QString::fromUtf8( e.what() ) );
		return false;
	} catch ( ... ) {
		status = QObject::tr( "Could not decode %1" ).arg( path );
		return false;
	}
}

QPixmap heightPicture( const HeightField & field )
{
	QImage img( int( field.width ), int( field.height ), QImage::Format_RGB32 );
	for ( int y = 0; y < img.height(); ++y ) {
		for ( int x = 0; x < img.width(); ++x ) {
			int g = int( std::clamp( field.values[size_t( y ) * field.width + size_t( x )], 0.0f, 1.0f ) * 255.0f + 0.5f );
			img.setPixel( x, y, qRgb( g, g, g ) );
		}
	}
	return QPixmap::fromImage( img.scaled( 256, 256, Qt::KeepAspectRatio, Qt::FastTransformation ) );
}

QPixmap histogramPicture( const HeightField & field )
{
	auto bins = histogram( field );
	QPixmap image( 256, 90 ); image.fill( QColor( 27, 32, 39 ) );
	QPainter p( &image ); p.setPen( QColor( 132, 205, 190 ) );
	size_t maxValue = *std::max_element( bins.begin(), bins.end() );
	if ( maxValue ) for ( int x = 0; x < 256; ++x )
		p.drawLine( x, 89, x, 89 - int( 85.0 * bins[size_t( x )] / maxValue ) );
	return image;
}

QPixmap meshPicture( const Mesh & mesh, const Mesh & reference, bool wireframe, bool smooth )
{
	QPixmap image( 520, 370 ); image.fill( QColor( 27, 32, 39 ) );
	std::vector<QPointF> points; points.reserve( mesh.vertices.size() );
	double minX = std::numeric_limits<double>::infinity(), minY = minX, maxX = -minX, maxY = -minX;
	for ( const auto & v : reference.vertices ) {
		double x = double( v.position[0] ) - double( v.position[1] );
		double y = ( double( v.position[0] ) + double( v.position[1] ) ) * 0.45 - double( v.position[2] );
		minX = std::min( minX, x ); maxX = std::max( maxX, x );
		minY = std::min( minY, y ); maxY = std::max( maxY, y );
	}
	for ( const auto & v : mesh.vertices )
		points.emplace_back( double( v.position[0] ) - double( v.position[1] ),
			( double( v.position[0] ) + double( v.position[1] ) ) * 0.45 - double( v.position[2] ) );
	double scale = std::min( 420.0 / std::max( maxX - minX, 1e-9 ), 285.0 / std::max( maxY - minY, 1e-9 ) );
	for ( auto & p : points ) p = QPointF( 260 + ( p.x() - ( minX + maxX ) / 2 ) * scale,
		185 + ( p.y() - ( minY + maxY ) / 2 ) * scale );
	struct Face { size_t index; double depth; };
	std::vector<Face> order; order.reserve( mesh.triangles.size() );
	for ( size_t i = 0; i < mesh.triangles.size(); ++i ) {
		double d = 0;
		for ( auto index : mesh.triangles[i] ) {
			const auto & v = mesh.vertices[index].position;
			d += double( v[0] ) + double( v[1] ) + 0.9 * double( v[2] );
		}
		order.push_back( { i, d } );
	}
	std::stable_sort( order.begin(), order.end(), []( const Face & a, const Face & b ) { return a.depth < b.depth; } );
	std::vector<Vec3> normals;
	if (smooth) normals=shadingNormals(mesh);
	std::vector<double> brightness(mesh.vertices.size());
	for (size_t i=0;i<normals.size();++i) {
		const auto &n=normals[i]; double length=std::hypot(n[0],n[1],n[2]);
		double light=length>0 ? std::abs((-0.35*n[0]-0.25*n[1]+0.90*n[2])/length) : 0;
		brightness[i]=70+150*std::clamp(light,0.0,1.0);
	}
	QPainter painter( &image );
	for ( const auto & face : order ) {
		const auto & t = mesh.triangles[face.index];
		const auto & a = mesh.vertices[t[0]].position, & b = mesh.vertices[t[1]].position, & c = mesh.vertices[t[2]].position;
		double ax = double(b[0])-a[0], ay = double(b[1])-a[1], az = double(b[2])-a[2];
		double bx = double(c[0])-a[0], by = double(c[1])-a[1], bz = double(c[2])-a[2];
		double nx = ay*bz-az*by, ny = az*bx-ax*bz, nz = ax*by-ay*bx;
		double norm = std::hypot( nx, ny, nz );
		double light = norm > 0 ? std::abs( ( -0.35*nx - 0.25*ny + 0.90*nz ) / norm ) : 0;
		int shade = int( 70 + 150 * std::clamp( light, 0.0, 1.0 ) );
		QBrush brush(QColor(shade,shade,shade));
		if (smooth) {
			const QPointF p0=points[t[0]], p1=points[t[1]], p2=points[t[2]];
			double dx1=p1.x()-p0.x(), dy1=p1.y()-p0.y(), dx2=p2.x()-p0.x(), dy2=p2.y()-p0.y();
			double det=dx1*dy2-dy1*dx2;
			double s0=brightness[t[0]], s1=brightness[t[1]], s2=brightness[t[2]];
			double low=std::min({s0,s1,s2}), high=std::max({s0,s1,s2});
			if (std::abs(det)>1e-9 && high-low>0.01) {
				double gx=((s1-s0)*dy2-(s2-s0)*dy1)/det;
				double gy=(dx1*(s2-s0)-dx2*(s1-s0))/det;
				double norm2=gx*gx+gy*gy;
				if (norm2>1e-12) {
					QPointF start(p0.x()+gx*(low-s0)/norm2,p0.y()+gy*(low-s0)/norm2);
					QPointF end(start.x()+gx*(high-low)/norm2,start.y()+gy*(high-low)/norm2);
					QLinearGradient gradient(start,end);
					int l=int(std::clamp(low,0.0,255.0)),h=int(std::clamp(high,0.0,255.0));
					gradient.setColorAt(0,QColor(l,l,l)); gradient.setColorAt(1,QColor(h,h,h)); brush=QBrush(gradient);
				} else { int v=int(s0); brush=QColor(v,v,v); }
			} else { int v=int(s0); brush=QColor(v,v,v); }
		}
		painter.setBrush(brush);
		painter.setPen( wireframe ? QPen( QColor( 90, 210, 190 ), 0.7 ) : QPen( brush, 0.7 ) );
		painter.drawPolygon( QPolygonF{ points[t[0]], points[t[1]], points[t[2]] } );
	}
	return image;
}

QPixmap comparePicture( const Mesh & original, const Mesh & result, bool wireframe, bool smooth, bool compareReduction=false )
{
	QPixmap image( 520, 370 ); image.fill( QColor( 27, 32, 39 ) );
	QPainter painter( &image );
	painter.drawPixmap( QRect( 0, 85, 260, 185 ), meshPicture( original, original, wireframe, smooth ) );
	painter.drawPixmap( QRect( 260, 85, 260, 185 ), meshPicture( result, original, wireframe, smooth ) );
	painter.setPen( QColor( 235, 235, 235 ) );
	painter.drawText( QRect( 0, 0, 260, 24 ), Qt::AlignCenter, compareReduction ? QObject::tr("Dense") : QObject::tr( "Original" ) );
	painter.drawText( QRect( 260, 0, 260, 24 ), Qt::AlignCenter, compareReduction ? QObject::tr("Reduced") : QObject::tr( "Displaced" ) );
	return image;
}

QPixmap uvPicture( const Mesh & original, const Mesh & working, const HeightField & field, const Displacement & mapping )
{
	float minU=std::numeric_limits<float>::infinity(), minV=minU, maxU=-minU, maxV=-minU;
	for (const auto &v : original.vertices) {
		minU=std::min(minU,v.uv[0]); maxU=std::max(maxU,v.uv[0]);
		minV=std::min(minV,v.uv[1]); maxV=std::max(maxV,v.uv[1]);
	}
	double centerU=(double(minU)+maxU)*0.5, centerV=(double(minV)+maxV)*0.5;
	double span=std::max({double(maxU)-minU,double(maxV)-minV,0.001})*1.1;
	Vec2 lower{float(centerU-span*0.5),float(centerV-span*0.5)};
	Vec2 upper{float(centerU+span*0.5),float(centerV+span*0.5)};
	HeightField pixels=sampleMappedGrid(field,lower,upper,340,340,mapping);
	QImage background(340,340,QImage::Format_RGB32);
	for (int y=0;y<340;++y) for (int x=0;x<340;++x) {
		int g=int(std::clamp(pixels.values[size_t(y)*340+size_t(x)],0.0f,1.0f)*150.0f);
		background.setPixel(x,y,qRgb(g,g,g));
	}
	QPixmap image(520,370); image.fill(QColor(27,32,39)); QPainter p(&image);
	p.drawImage(90,25,background);
	auto draw=[&](const Mesh &mesh, const QColor &color) {
		p.setPen(QPen(color,0.7));
		for (const auto &t : mesh.triangles) {
			QPointF points[3];
			for (int i=0;i<3;++i) {
				const auto &uv=mesh.vertices[t[i]].uv;
				points[i]=QPointF(90+(double(uv[0])-lower[0])*340/(double(upper[0])-lower[0]),
					25+(double(uv[1])-lower[1])*340/(double(upper[1])-lower[1]));
			}
			p.drawLine(points[0],points[1]); p.drawLine(points[1],points[2]); p.drawLine(points[2],points[0]);
		}
	};
	draw(working,QColor(90,210,190,180)); draw(original,QColor(255,190,70));
	p.setPen(QColor(235,235,235));
	p.drawText(QRect(0,0,520,23),Qt::AlignCenter,QObject::tr("Raw UVs: original gold, working teal; transformed height beneath"));
	return image;
}

QDoubleSpinBox * realControl( double min, double max, double value, double step = 0.05 )
{
	auto box = new QDoubleSpinBox;
	box->setKeyboardTracking(false);
	box->setDecimals( 3 ); box->setRange( min, max ); box->setSingleStep( step ); box->setValue( value );
	return box;
}

class PreviewDialog final : public QDialog
{
public:
	QVariantMap controlState() const {
		QVariantMap state; QVariantList values;
		for (auto box:findChildren<QDoubleSpinBox *>()) values.push_back(box->value());
		state["real"]=values; values.clear();
		for (auto box:findChildren<QSpinBox *>()) values.push_back(box->value());
		state["integer"]=values; values.clear();
		for (auto box:findChildren<QCheckBox *>()) values.push_back(box->isChecked());
		state["check"]=values; values.clear();
		for (auto box:findChildren<QComboBox *>()) values.push_back(box->currentIndex());
		state["choice"]=values;
		return state;
	}
	void restoreControls(const QVariantMap &state) {
		std::vector<std::unique_ptr<QSignalBlocker>> blockers;
		for (auto object:findChildren<QObject *>()) blockers.push_back(std::make_unique<QSignalBlocker>(object));
		auto restore=[&](const auto &boxes,const char *key,auto setter) {
			auto values=state.value(key).toList(); if (values.size()!=boxes.size()) return;
			for (qsizetype i=0;i<boxes.size();++i) setter(boxes[i],values[i]);
		};
		restore(findChildren<QDoubleSpinBox *>(),"real",[](auto box,const QVariant &value) { box->setValue(value.toDouble()); });
		restore(findChildren<QSpinBox *>(),"integer",[](auto box,const QVariant &value) { box->setValue(value.toInt()); });
		restore(findChildren<QCheckBox *>(),"check",[](auto box,const QVariant &value) { box->setChecked(value.toBool()); });
		restore(findChildren<QComboBox *>(),"choice",[](auto box,const QVariant &value) { box->setCurrentIndex(value.toInt()); });
	}
	PreviewDialog( NifModel * nif, ShapeSnapshot current,std::shared_ptr<VanillaOperation> op,bool continuing, QWidget * parent )
		: QDialog( parent ), snapshot(op->reference), live(std::move(current)),operation(std::move(op)),
		 original( snapshot.geometry ), nifModel(nif), displacement( snapshot.mapping )
	{
		setWindowTitle( tr( "Vanilla+ Geometry Preview" ) ); resize( 1050, 700 );
		auto main = new QVBoxLayout( this );
		auto content = new QHBoxLayout; main->addLayout( content );
		auto controls = new QWidget; auto left = new QVBoxLayout( controls );
		auto scroll = new QScrollArea; scroll->setWidgetResizable( true ); scroll->setWidget( controls );
		scroll->setMinimumWidth( 360 ); content->addWidget( scroll, 1 );
		auto right = new QVBoxLayout; content->addLayout( right, 2 );
		auto title = new QLabel( snapshot.writable ? tr( "Temporary preview. Apply changes the open NIF; saving remains manual." )
			: tr( "Preview only for this mesh format. Apply is not available yet." ) );
		title->setWordWrap( true ); left->addWidget( title );
		if(snapshot.animatedShader) {
			auto notice=new QLabel(tr("Animated shader: displacement samples the stored base textures and UV transform, not an animation frame. Animation can move texture detail away from baked geometry. Existing animation, stored UVs and UV-seam protections are preserved."));
			notice->setObjectName("geometryAnimationNotice"); notice->setWordWrap(true); left->addWidget(notice);
		}
		auto referenceNotice=new QLabel(continuing
			? tr("Refining the retained reference for this document and shape. Apply replaces the previous result; relief is not added again.")
			: tr("New Vanilla+ operation from the current mesh. No earlier undisplaced reference is available; this is new displacement, not cross-file-session refinement."));
		referenceNotice->setWordWrap(true); referenceNotice->setObjectName("vanillaReferenceStatus"); left->addWidget(referenceNotice);
		auto material = new QGroupBox( tr( "Material resources" ) ); auto materialLayout = new QVBoxLayout( material );
		left->addWidget( material );
		const auto & paths = snapshot.texturePaths;
		QString diffuseStatus, normalStatus, heightStatus;
		bool hasDiffuse = readHeightSource( nif, paths.value( 0 ), HeightSource::Diffuse, diffuse, diffuseHigh, diffuseStatus );
		bool hasNormal = readHeightSource( nif, paths.value( 1 ), HeightSource::Normal, normal, normalHigh, normalStatus );
		bool parallax = snapshot.shaderType == ShaderFlags::ST_Heightmap && ( snapshot.flags1 & ShaderFlags::SLSF1_Parallax );
		bool hasHeight = parallax && readHeightSource( nif, paths.value( 3 ), HeightSource::Height, existingHeight, heightHigh, heightStatus );
		if ( !parallax ) heightStatus = tr( "No active parallax height texture" );
		for ( const QString & line : { tr( "Diffuse: %1" ).arg( diffuseStatus ), tr( "Normal: %1" ).arg( normalStatus ),
			tr( "Height: %1" ).arg( heightStatus ) } ) {
			auto label = new QLabel( line ); label->setWordWrap( true ); label->setTextInteractionFlags( Qt::TextSelectableByMouse );
			materialLayout->addWidget( label );
		}
		auto uvInfo = new QLabel( tr( "UV transform: scale (%1, %2), offset (%3, %4); U %5, V %6. Open and seam boundaries are pinned." )
			.arg(displacement.uvScale[0]).arg(displacement.uvScale[1]).arg(displacement.uvOffset[0]).arg(displacement.uvOffset[1])
			.arg(displacement.u == Address::Wrap ? tr("wrap") : tr("clamp"))
			.arg(displacement.v == Address::Wrap ? tr("wrap") : tr("clamp")) );
		uvInfo->setWordWrap(true); materialLayout->addWidget(uvInfo);
		auto sourceGroup = new QGroupBox( tr( "Parallax Shader Preset" ) ); auto sources = new QFormLayout( sourceGroup ); left->addWidget( sourceGroup );
		auto generate = new QPushButton( tr( "Generate Height from Textures" ) );
		generate->setObjectName( "vanillaGenerateHeight" ); generate->setEnabled( hasDiffuse || hasNormal );
		generate->setToolTip( tr( "Combine available diffuse and normal cues into a temporary height reference and set a visible preview." ) );
		sources->addRow( generate );
		highPrecision = new QCheckBox; highPrecision->setObjectName("vanillaHighPrecision");
		highPrecision->setToolTip(tr("Use a 256 × 256 height field instead of 128 × 128. This keeps the same UV mapping and filter scale, but updates may take longer. It does not reconstruct true height from a normal map."));
		sources->addRow(tr("High precision height field"),highPrecision);
		diffuseWeight = realControl( 0, 1, hasDiffuse ? 1 : 0 ); diffuseWeight->setEnabled( hasDiffuse );
		normalWeight = realControl( 0, 1, !hasDiffuse && hasNormal ? 1 : 0 ); normalWeight->setEnabled( hasNormal );
		heightWeight = realControl( 0, 1, !hasDiffuse && !hasNormal && hasHeight ? 1 : 0 ); heightWeight->setEnabled( hasHeight );
		diffuseWeight->setObjectName( "vanillaDiffuseWeight" ); normalWeight->setObjectName( "vanillaNormalWeight" );
		diffuseMult = realControl( 0, 8, 1 ); diffuseMult->setEnabled( hasDiffuse );
		normalMult = realControl( 0, 8, 1 ); normalMult->setEnabled( hasNormal );
		heightMult = realControl( 0, 8, 1 ); heightMult->setEnabled( hasHeight );
		sources->addRow( tr( "Diffuse weight" ), diffuseWeight ); sources->addRow( tr( "Diffuse multiplier" ), diffuseMult );
		sources->addRow( tr( "Normal cue weight" ), normalWeight ); sources->addRow( tr( "Normal cue multiplier" ), normalMult );
		sources->addRow( tr( "Height weight" ), heightWeight ); sources->addRow( tr( "Height multiplier" ), heightMult );
		mode = new QComboBox; mode->addItems( { tr( "Add" ), tr( "Multiply" ), tr( "Screen" ), tr( "Overlay" ), tr( "Min" ), tr( "Max" ) } );
		sources->addRow( tr( "Blend" ), mode );
		auto normalNote = new QLabel( tr( "The normal cue is experimental; it is not reconstructed height." ) ); normalNote->setWordWrap( true ); sources->addRow( normalNote );
		auto conditioning = new QGroupBox( tr( "Height conditioning" ) ); auto form = new QFormLayout( conditioning ); left->addWidget( conditioning );
		black = realControl( 0, 1, 0 ); white = realControl( 0, 1, 1 ); midpoint = realControl( 0.001, 0.999, 0.5 );
		contrast = realControl( 0, 8, 1 ); outMin = realControl( -2, 2, 0 ); outMax = realControl( -2, 2, 1 ); invert = new QCheckBox;
		form->addRow( tr( "Black point" ), black ); form->addRow( tr( "White point" ), white ); form->addRow( tr( "Midpoint" ), midpoint );
		form->addRow( tr( "Contrast" ), contrast ); form->addRow( tr( "Output minimum" ), outMin ); form->addRow( tr( "Output maximum" ), outMax ); form->addRow( tr( "Invert" ), invert );
		auto smoothing = new QGroupBox( tr( "Smoothing and detail scale" ) ); auto smoothForm = new QFormLayout( smoothing ); left->addWidget( smoothing );
		spatialSigma = realControl( 0, 4, 2, 0.25 ); spatialSigma->setObjectName( "vanillaBilateralSpatial" );
		rangeSigma = realControl( 0.001, 2, 0.12, 0.01 ); rangeSigma->setObjectName( "vanillaBilateralRange" );
		spatialSigma->setToolTip( tr( "Spatial standard deviation in preview pixels. Larger values smooth over a wider area." ) );
		rangeSigma->setToolTip( tr( "Height difference standard deviation. Smaller values preserve sharper height boundaries." ) );
		fineSigma = realControl( 0.1, 7.9, 1, 0.25 ); coarseSigma = realControl( 0.2, 8, 4, 0.25 );
		mediumGain = realControl( 0, 2, 0.85, 0.1 ); fineGain = realControl( 0, 2, 0, 0.1 );
		fineGain->setObjectName( "vanillaFineGain" ); mediumGain->setObjectName( "vanillaMediumGain" );
		smoothForm->addRow( tr( "Bilateral spatial sigma (px)" ), spatialSigma );
		smoothForm->addRow( tr( "Bilateral range sigma" ), rangeSigma );
		smoothForm->addRow( tr( "Fine scale (pixels)" ), fineSigma ); smoothForm->addRow( tr( "Coarse scale (pixels)" ), coarseSigma );
		smoothForm->addRow( tr( "Geometry detail gain" ), mediumGain ); smoothForm->addRow( tr( "Fine detail gain" ), fineGain );
		auto smoothNote = new QLabel( tr( "Scale controls use 128 × 128 reference pixels; high precision preserves the same texture-space scale. Set spatial sigma to 0 to disable bilateral smoothing." ) );
		smoothNote->setWordWrap( true ); smoothForm->addRow( smoothNote );
		auto darkGroup=new QGroupBox(tr("Dark Feature Bias (experimental)")); auto darkForm=new QFormLayout(darkGroup); left->addWidget(darkGroup);
		darkEnabled=new QCheckBox; darkEnabled->setObjectName("vanillaDarkFeatureBias");
		darkPreview=new QCheckBox; darkPreview->setObjectName("vanillaDarkAttractionPreview");
		darkThreshold=realControl(0,1,0.01,0.005); darkThreshold->setObjectName("vanillaDarkThreshold");
		darkStrength=realControl(0,1,0.5,0.1); darkStrength->setObjectName("vanillaDarkStrength");
		darkShift=realControl(0,0.25,0.10,0.025); darkShift->setObjectName("vanillaDarkMaximumShift");
		darkShift->setToolTip(tr("Maximum movement as a fraction of the shortest original generated edge. Source vertices and reference-triangle edges stay fixed. Search is limited to half a local edge."));
		darkPreview->setToolTip(tr("Show local depressions detected in the conditioned height. Display brightness is amplified 4 times; the threshold uses unamplified height units."));
		darkForm->addRow(tr("Enable Dark Feature Bias"),darkEnabled); darkForm->addRow(tr("Attraction Preview"),darkPreview);
		darkForm->addRow(tr("Threshold"),darkThreshold); darkForm->addRow(tr("Attraction Strength"),darkStrength); darkForm->addRow(tr("Maximum Shift"),darkShift);
		auto geometry = new QGroupBox( tr( "Geometry" ) ); auto geo = new QFormLayout( geometry ); left->addWidget( geometry );
		surfaceProfile=new QComboBox; surfaceProfile->setObjectName("vanillaSurfaceProfile"); surfaceProfile->addItems({tr("Neutral"),tr("Rock"),tr("Wood"),tr("Masonry"),tr("Metal"),tr("Soil")});
		profilePreview=new QCheckBox; profilePreview->setObjectName("vanillaRockSuppressionPreview");
		profilePreview->setToolTip(tr("Show actual target amplitude attenuation (0–1), shared across profiles. Neutral bypasses morphology; Soil deliberately admits isotropic and irregular relief."));
		geo->addRow(tr("Surface Profile"),surfaceProfile); geo->addRow(tr("Show profile suppression"),profilePreview);
		reliefMode = new QComboBox; reliefMode->setObjectName("vanillaReliefBehavior");
		reliefMode->addItems({tr("Full relief"),tr("Shallow surface detail")});
		reliefMode->setToolTip(tr("Shallow surface detail removes broad texture-height bias before displacement. Select it before Generate Height for a smaller starting strength. Existing values are not changed when switching modes."));
		geo->addRow(tr("Relief behavior"),reliefMode);
		tessellation = new QComboBox; tessellation->setObjectName( "vanillaTessellationMode" );
		tessellation->addItems( { tr( "Height-adaptive" ), tr( "Uniform" ) } );
		gradientThreshold = realControl( 0.001, 2, 0.05, 0.01 ); gradientThreshold->setObjectName( "vanillaGradientThreshold" );
		gradientThreshold->setToolTip( tr( "Maximum sampled height representation error, in conditioned height units. Lower values add detail. Gradient and the fixed preview silhouette modestly weight the error; finite samples can miss fine detail." ) );
		depth = new QSpinBox; depth->setKeyboardTracking(false); depth->setRange( 0, 5 ); depth->setValue( 1 );
		depth->setObjectName( "vanillaSubdivisionDepth" );
		vertexLimit = new QSpinBox; vertexLimit->setKeyboardTracking(false); vertexLimit->setRange( 3, 65535 ); vertexLimit->setValue( 65535 );
		triangleLimit = new QSpinBox; triangleLimit->setKeyboardTracking(false); triangleLimit->setRange( 1, 65535 ); triangleLimit->setValue( 65535 );
		strength = realControl( -1000, 1000, 0, 0.1 ); neutral = realControl( -2, 2, 0.5 );
		strength->setObjectName( "vanillaDisplacementStrength" );
		positive = realControl( 0, 10000, 1, 0.1 ); negative = realControl( 0, 10000, 1, 0.1 );
		protectPinching = new QCheckBox; protectPinching->setChecked( true ); protectPinching->setObjectName( "vanillaProtectPinching" );
		removeBias=new QCheckBox; removeBias->setChecked(true); removeBias->setObjectName("vanillaRemoveDisplacementBias");
		removeBias->setToolTip(tr("Remove the area-weighted average signed displacement separately from each connected section. This suppresses uniform swelling while retaining height differences. Broad variation may still need Shallow surface detail."));
		qualityCleanup = new QCheckBox; qualityCleanup->setChecked( true ); qualityCleanup->setObjectName( "vanillaQualityCleanup" );
		qualityCleanup->setToolTip( tr( "Improve poor triangles by flipping safe interior edges. Vertices do not move; surface changes are limited." ) );
		maxSlope = realControl( 0.01, 2, 0.5, 0.05 ); maxSlope->setObjectName( "vanillaMaxSlope" );
		maxSlope->setToolTip( tr( "Maximum displacement difference relative to each 3D edge length. Lower values soften pinches and seam transitions." ) );
		geo->addRow( tr( "Tessellation" ), tessellation ); geo->addRow( tr( "Surface error tolerance" ), gradientThreshold );
		geo->addRow( tr( "Maximum subdivision depth" ), depth ); geo->addRow( tr( "Displacement strength" ), strength );
		geo->addRow( tr( "Neutral height" ), neutral ); geo->addRow( tr( "Positive limit" ), positive ); geo->addRow( tr( "Negative limit" ), negative );
		geo->addRow(tr("Remove average displacement bias"),removeBias);
		geo->addRow( tr( "Protect against pinching" ), protectPinching ); geo->addRow( tr( "Maximum displacement slope" ), maxSlope );
		geo->addRow( tr( "Improve triangle quality" ), qualityCleanup );
		patchRetopology = new QCheckBox; patchRetopology->setObjectName("vanillaPatchRetopology");
		patchRetopology->setToolTip(tr("Use verified coplanar source patches where supported. Unsupported patches retain their original triangles."));
		geo->addRow(tr("Retopologize compatible patches"),patchRetopology);
		legacyUVHandling=new QCheckBox; legacyUVHandling->setObjectName("vanillaOriginalUVHandling");
		legacyUVHandling->setToolTip(tr("Use the earlier tessellation behavior without separating UV islands. Original UV coordinates and the shader's wrap or clamp settings are preserved."));
		geo->addRow(tr("Use original UV handling"),legacyUVHandling);
		reduction = new QCheckBox; reduction->setObjectName("vanillaReduce");
		reductionTolerance=realControl(0,5,0.5,0.1); reductionTolerance->setObjectName("vanillaReductionTolerance");
		reductionTolerance->setToolTip(tr("Maximum sampled difference from the dense surface, as a percentage of the original shape's bounding-box diagonal. Lower values preserve more detail; finite sampling is not a guaranteed maximum."));
		geo->addRow(tr("Reduce reconstructed mesh"),reduction);
		geo->addRow(tr("Reconstruction error (%)"),reductionTolerance);
		geo->addRow( tr( "Vertex safety limit" ), vertexLimit ); geo->addRow( tr( "Triangle safety limit" ), triangleLimit );
		left->addStretch();
		view = new QComboBox; view->setObjectName( "vanillaPreviewMode" ); view->addItems( { tr( "Original" ), tr( "Tessellated" ), tr( "Displaced" ), tr( "Compare Original / Displaced" ), tr( "UV mapping" ), tr( "Reduced" ), tr( "Compare Dense / Reduced" ) } ); right->addWidget( view );
		auto viewOptions = new QHBoxLayout;
		wireframe = new QCheckBox( tr( "Show triangle edges" ) ); wireframe->setObjectName( "vanillaWireframe" ); viewOptions->addWidget( wireframe );
		smoothShading = new QCheckBox( tr( "Smooth shading" ) ); smoothShading->setChecked(true); smoothShading->setObjectName("vanillaSmoothShading"); viewOptions->addWidget(smoothShading); viewOptions->addStretch(); right->addLayout(viewOptions);
		meshLabel = new QLabel; meshLabel->setObjectName( "vanillaMeshPreview" ); meshLabel->setAlignment( Qt::AlignCenter ); meshLabel->setMinimumSize( 520, 370 ); right->addWidget( meshLabel );
		stats = new QLabel; stats->setObjectName( "vanillaPreviewStats" ); stats->setWordWrap( true ); right->addWidget( stats );
		auto images = new QHBoxLayout; right->addLayout( images );
		auto imageColumn = new QVBoxLayout; heightTitle=new QLabel; heightTitle->setObjectName("vanillaHeightResolution"); imageColumn->addWidget(heightTitle);
		heightLabel = new QLabel; heightLabel->setObjectName( "vanillaHeightPreview" ); heightLabel->setMinimumSize( 256, 256 ); heightLabel->setAlignment( Qt::AlignCenter ); imageColumn->addWidget( heightLabel ); images->addLayout( imageColumn );
		auto histColumn = new QVBoxLayout; histColumn->addWidget( new QLabel( tr( "Conditioned height histogram" ) ) ); histogramLabel = new QLabel; histColumn->addWidget( histogramLabel ); histColumn->addStretch(); images->addLayout( histColumn );
		statusLabel = new QLabel; statusLabel->setObjectName( "vanillaPreviewStatus" ); statusLabel->setWordWrap( true ); right->addWidget( statusLabel );
		auto buttons = new QDialogButtonBox( QDialogButtonBox::Cancel );
		auto stage = buttons->addButton( tr( "Check staged copy" ), QDialogButtonBox::ActionRole );
		stage->setObjectName( "vanillaStageCopy" );
		stage->setToolTip( tr( "Build and reopen a temporary in-memory copy. The open NIF is not modified." ) );
		connect(stage, &QPushButton::clicked, this, [this,nif]() {
			if (!previewValid) { statusLabel->setText(tr("Generate a valid displaced preview first.")); return; }
			QByteArray staged;
			QString error=stageOperation(nif,live,snapshot,previewMesh,staged);
			statusLabel->setText(error.isEmpty()
				? tr("Staged copy passed save/reopen checks. The open NIF has not been changed.")
				: tr("Staged copy could not be prepared: %1").arg(error));
		});
		applyButton = buttons->addButton( tr( "Apply" ), QDialogButtonBox::ApplyRole ); applyButton->setObjectName( "vanillaApply" ); applyButton->setEnabled( false );
		applyButton->setToolTip( tr( "Replace the selected shape in the open NIF with the checked preview. This does not save the file." ) );
		connect(applyButton, &QPushButton::clicked, this, [this,nif]() {
			if (!previewValid || !nif->undoStack) return;
			QByteArray staged;
			QString error=stageOperation(nif,live,snapshot,previewMesh,staged);
			if (!error.isEmpty()) { statusLabel->setText(tr("Apply failed: %1").arg(error)); return; }
			if (!operation->valid) {
				if (live.writeBytes!=snapshot.writeBytes) { statusLabel->setText(tr("The retained operation was invalidated by an external edit. Reopen the tool to begin an explicit new operation.")); return; }
				// A fresh-source dialog can still commit after an external edit was
				// exactly restored (verified above), but gets a new operation state.
				auto fresh=std::make_shared<VanillaOperation>(); fresh->reference=snapshot; fresh->currentWrite=live.writeBytes; fresh->currentShape=live.shapeBytes;
				operationStore(nif)->operations.push_back(fresh); operation=std::move(fresh);
			}
			QBuffer input(&staged); input.open(QIODevice::ReadOnly); NifModel copy;
			if (!copy.load(input)) { statusLabel->setText(tr("Apply failed: staged copy could not be reopened.")); return; }
			auto stagedShape=copy.getBlockIndex(snapshot.writeBlock); QByteArray newBytes;
			if (!serializeBlock(&copy,stagedShape,newBytes)
				|| !replaceShapeBytes(&copy,stagedShape,newBytes,live.writeBytes)
				|| !replaceShapeBytes(&copy,stagedShape,live.writeBytes,newBytes)) {
				statusLabel->setText(tr("Apply failed: replacement and rollback preflight failed.")); return;
			}
			auto shape=nif->getBlockIndex(snapshot.writeBlock);
			auto success=std::make_shared<bool>(false);
			nif->undoStack->push(new ReplaceShapeCommand(nif,shape,live.writeBytes,newBytes,success,operation,operation->settings,controlState()));
			if (!*success) { statusLabel->setText(tr("Apply failed. Check the selected shape before saving.")); return; }
			accept();
		});
		connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject ); main->addWidget( buttons );
		for ( auto box : { diffuseWeight, normalWeight, heightWeight, diffuseMult, normalMult, heightMult,
			black, white, midpoint, contrast, outMin, outMax, strength, neutral, positive, negative,
			spatialSigma, rangeSigma, fineSigma, coarseSigma, mediumGain, fineGain, gradientThreshold, maxSlope, reductionTolerance,
			darkThreshold,darkStrength,darkShift } )
			connect( box, qOverload<double>( &QDoubleSpinBox::valueChanged ), this, [this]() { updatePreview(); } );
		connect( depth, qOverload<int>( &QSpinBox::valueChanged ), this, [this]() { updatePreview(); } );
		connect( vertexLimit, qOverload<int>( &QSpinBox::valueChanged ), this, [this]() { updatePreview(); } );
		connect( triangleLimit, qOverload<int>( &QSpinBox::valueChanged ), this, [this]() { updatePreview(); } );
		connect( mode, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]() { updatePreview(); } );
		connect( tessellation, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]() { updatePreview(); } );
		connect( reliefMode, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]() { updatePreview(); } );
		connect( view, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]() { refreshDisplay(); } );
		connect( invert, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect( protectPinching, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect( removeBias, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect(darkEnabled,&QCheckBox::toggled,this,[this]() { updatePreview(); });
		connect(darkPreview,&QCheckBox::toggled,this,[this]() { refreshDisplay(); });
		connect(surfaceProfile,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]() { updatePreview(); });
		connect(profilePreview,&QCheckBox::toggled,this,[this]() { refreshDisplay(); });
		connect( qualityCleanup, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect( patchRetopology, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect( legacyUVHandling, &QCheckBox::toggled, this, [this]() { islandSeedReady=false; patchSeedReady=false; updatePreview(); } );
		connect( highPrecision, &QCheckBox::toggled, this, [this]() { updatePreview(); } );
		connect( reduction, &QCheckBox::toggled, this, [this](bool enabled) {
			if (enabled && view->currentIndex()!=5) { QSignalBlocker blocker(view); view->setCurrentIndex(5); }
			updatePreview();
		} );
		connect( wireframe, &QCheckBox::toggled, this, [this]() { refreshDisplay(); } );
		connect( smoothShading, &QCheckBox::toggled, this, [this]() { refreshDisplay(); } );
		connect( generate, &QPushButton::clicked, this, [this]() { generateHeight(); } );
		if (continuing) restoreControls(operation->settings);
		updatePreview();
	}

protected:
	void keyPressEvent(QKeyEvent *event) override {
		// A spin box has already committed its text before propagating Enter.
		// Do not let that same key activate the dialog's default action.
		if(event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter)
			for(auto widget=focusWidget();widget && widget!=this;widget=widget->parentWidget())
				if(qobject_cast<QAbstractSpinBox *>(widget)) { event->accept(); return; }
		QDialog::keyPressEvent(event);
	}
private:
	void generateHeight()
	{
		const auto &diffuseField=highPrecision->isChecked()?diffuseHigh:diffuse;
		const auto &normalField=highPrecision->isChecked()?normalHigh:normal;
		const float resolutionScale=highPrecision->isChecked()?2.0f:1.0f;
		const bool hasDiffuse = !diffuseField.values.empty(), hasNormal = !normalField.values.empty();
		if ( !hasDiffuse && !hasNormal ) return;
		try {
			float dw = hasDiffuse ? ( hasNormal ? 0.65f : 1.0f ) : 0.0f;
			float nw = hasNormal ? ( hasDiffuse ? 0.35f : 1.0f ) : 0.0f;
			HeightField combined = blend( { { hasDiffuse ? &diffuseField : nullptr, dw, 1 },
				{ hasNormal ? &normalField : nullptr, nw, 1 } }, Blend::Add );
			auto ordered = combined.values;
			std::sort( ordered.begin(), ordered.end() );
			float low = std::clamp( ordered[ordered.size() / 50], 0.0f, 1.0f );
			float high = std::clamp( ordered[ordered.size() * 49 / 50], 0.0f, 1.0f );
			if ( high - low < 0.002f ) { low = 0; high = 1; }
			// Controls round to three decimal places; keep black and white distinct.
			low = std::floor( low * 1000.0f ) / 1000.0f;
			high = std::ceil( high * 1000.0f ) / 1000.0f;
			Vec3 lo = original.vertices.front().position, hi = lo;
			for ( const auto & v : original.vertices ) for ( int i = 0; i < 3; ++i ) {
				lo[i] = std::min( lo[i], v.position[i] ); hi[i] = std::max( hi[i], v.position[i] );
			}
			double diagonal = std::hypot( double(hi[0])-lo[0], double(hi[1])-lo[1], double(hi[2])-lo[2] );
			double suggested = std::clamp( diagonal * (reliefMode->currentIndex()==1 ? 0.0025 : 0.025), 0.001, 1000.0 );
			Limits limits{ size_t( vertexLimit->value() ), size_t( triangleLimit->value() ) };
			Conditioning preset{ low, high, 0.5f, 1, 0, 1, false };
			HeightField prepared = condition( combined, preset );
			prepared = bilateral( prepared, 2.0f*resolutionScale, 0.12f, displacement.u, displacement.v );
			prepared = frequencyBands( prepared, resolutionScale, 4*resolutionScale, 0.85f, 0, displacement.u, displacement.v );
			if (reliefMode->currentIndex()==1) prepared=shallowSurfaceDetail(prepared,8*resolutionScale,0.5f,displacement.u,displacement.v);
			int chosenDepth = 3;
			displacement.profileTarget=nullptr;
			while ( chosenDepth > 0 ) {
				try { (void) subdivideAdaptive( original, prepared, unsigned( chosenDepth ), 0.05f, displacement, limits ); break; }
				catch ( const std::length_error & ) { --chosenDepth; }
			}
			{
				std::vector<std::unique_ptr<QSignalBlocker>> blockers;
				for ( QObject * obj : std::initializer_list<QObject *>{ diffuseWeight, normalWeight, heightWeight, diffuseMult,
					normalMult, heightMult, black, white, midpoint, contrast, outMin, outMax, strength, neutral,
					positive, negative, depth, mode, view, invert, tessellation, gradientThreshold, spatialSigma,
					rangeSigma, fineSigma, coarseSigma, mediumGain, fineGain, protectPinching, maxSlope } )
					blockers.emplace_back( std::make_unique<QSignalBlocker>( obj ) );
				diffuseWeight->setValue( dw ); normalWeight->setValue( nw ); heightWeight->setValue( 0 );
				diffuseMult->setValue( 1 ); normalMult->setValue( 1 ); heightMult->setValue( 1 );
				mode->setCurrentIndex( 0 ); black->setValue( low ); white->setValue( high ); midpoint->setValue( 0.5 );
				contrast->setValue( 1 ); outMin->setValue( 0 ); outMax->setValue( 1 ); invert->setChecked( false );
				strength->setValue( suggested ); neutral->setValue( 0.5 ); positive->setValue( suggested ); negative->setValue( suggested );
				spatialSigma->setValue( 2 ); rangeSigma->setValue( 0.12 ); fineSigma->setValue( 1 ); coarseSigma->setValue( 4 );
				mediumGain->setValue( 0.85 ); fineGain->setValue( 0 ); gradientThreshold->setValue( 0.05 ); tessellation->setCurrentIndex( 0 );
				protectPinching->setChecked( true ); maxSlope->setValue( 0.5 );
				depth->setValue( chosenDepth ); view->setCurrentIndex( 3 );
			}
			updatePreview();
		} catch ( const std::exception & e ) {
			statusLabel->setText( tr( "Could not generate height: %1" ).arg( QString::fromUtf8( e.what() ) ) );
		}
	}

	void renderPresentation()
	{
		bool hasSource=presentationHasSource;
		const auto &field=presentationField;
		heightTitle->setText(highPrecision->isChecked()?tr("Height field (256 × 256)"):tr("Height field (128 × 128)"));
		if (hasSource) {
			heightLabel->setPixmap(heightPicture(field)); histogramLabel->setPixmap(histogramPicture(field));
			if (darkPreview->isChecked()) {
				if (presentationAttraction.values.empty()) presentationAttraction=darkFeatureAttraction(field,highPrecision->isChecked()?4.0f:2.0f,displacement.u,displacement.v);
				auto display=presentationAttraction; for (auto &v:display.values) v=std::min(1.0f,v*4);
				heightLabel->setPixmap(heightPicture(display)); heightTitle->setText(tr("Dark attraction (brightness ×4)"));
			}
			if (profilePreview->isChecked()) {
				if (priorProfile>0) heightLabel->setPixmap(heightPicture(prior.suppression));
				else { heightLabel->clear(); heightLabel->setText(tr("Neutral: morphology bypassed")); }
				heightTitle->setText(tr("%1 suppression (0–1)").arg(surfaceProfile->currentText()));
			}
		} else { heightLabel->clear(); heightLabel->setText(tr("No active height source")); histogramLabel->clear(); }
		const Mesh &dense=presentationDenseUsesFinal?previewMesh:presentationDense;
		const Mesh &final=hasSource?previewMesh:dense;
		const Mesh &shown=view->currentIndex()==0?original:(view->currentIndex()==1?presentationWorking:(view->currentIndex()==2?dense:final));
		if(view->currentIndex()==4 && !hasSource) { meshLabel->clear(); meshLabel->setText(tr("Load a height source to show UV alignment.")); return; }
		int key=view->currentIndex()*4+int(wireframe->isChecked())*2+int(smoothShading->isChecked());
		auto found=std::find_if(presentationImages.begin(),presentationImages.end(),[key](const auto &entry) { return entry.first==key; });
		QPixmap image;
		if(found!=presentationImages.end()) { image=found->second; presentationImages.erase(found); }
		else image=view->currentIndex()==4?uvPicture(original,presentationWorking,field,displacement)
			:(view->currentIndex()==3?comparePicture(original,dense,wireframe->isChecked(),smoothShading->isChecked())
			:(view->currentIndex()==6?comparePicture(dense,final,wireframe->isChecked(),smoothShading->isChecked(),true)
			:meshPicture(shown,original,wireframe->isChecked(),smoothShading->isChecked())));
		// Four small images only; any processing change clears this entire LRU.
		if(presentationImages.size()==4) presentationImages.erase(presentationImages.begin());
		presentationImages.emplace_back(key,image); meshLabel->setPixmap(image);
	}
	void refreshDisplay()
	{
		if (!presentationReady) { updatePreview(); return; }
		try { renderPresentation(); }
		catch (const std::exception &e) {
			presentationReady=false; previewValid=false; applyButton->setEnabled(false);
			statusLabel->setText(tr("Preview could not be updated: %1").arg(QString::fromUtf8(e.what())));
		}
	}
	void updatePreview()
	{
		previewValid=false;
		presentationReady=false;
		presentationImages.clear();
		displacement.profileTarget=nullptr;
		displacement.strength=float(strength->value()); displacement.neutral=float(neutral->value());
		applyButton->setEnabled(false);
		try {
			const auto &diffuseField=highPrecision->isChecked()?diffuseHigh:diffuse;
			const auto &normalField=highPrecision->isChecked()?normalHigh:normal;
			const auto &heightField=highPrecision->isChecked()?heightHigh:existingHeight;
			const float resolutionScale=highPrecision->isChecked()?2.0f:1.0f;
			heightTitle->setText(highPrecision->isChecked()?tr("Height field (256 × 256)"):tr("Height field (128 × 128)"));
			Conditioning settings{ float( black->value() ), float( white->value() ), float( midpoint->value() ),
				float( contrast->value() ), float( outMin->value() ), float( outMax->value() ), invert->isChecked() };
			std::vector<Source> src{
				{ diffuseField.values.empty() ? nullptr : &diffuseField, float( diffuseWeight->value() ), float( diffuseMult->value() ) },
				{ normalField.values.empty() ? nullptr : &normalField, float( normalWeight->value() ), float( normalMult->value() ) },
				{ heightField.values.empty() ? nullptr : &heightField, float( heightWeight->value() ), float( heightMult->value() ) } };
			bool hasSource = std::any_of( src.begin(), src.end(), []( const Source & s ) { return s.field && s.weight > 0; } );
			HeightField field;
			HeightField attraction;
			if ( hasSource ) {
				// Decoded sources and addressing belong to the immutable dialog
				// snapshot. Geometry/view controls do not change this height recipe.
				std::vector<double> recipe{double(highPrecision->isChecked()),diffuseWeight->value(),normalWeight->value(),heightWeight->value(),
					diffuseMult->value(),normalMult->value(),heightMult->value(),double(mode->currentIndex()),
					black->value(),white->value(),midpoint->value(),contrast->value(),outMin->value(),outMax->value(),double(invert->isChecked()),
					spatialSigma->value(),rangeSigma->value(),fineSigma->value(),coarseSigma->value(),mediumGain->value(),fineGain->value(),
					double(reliefMode->currentIndex()),reliefMode->currentIndex()==1?neutral->value():0.0};
				if (!conditionedReady || recipe!=conditionedRecipe) {
					auto computed=condition(blend(src,static_cast<Blend>(mode->currentIndex())),settings);
					computed=bilateral(computed,float(spatialSigma->value())*resolutionScale,float(rangeSigma->value()),displacement.u,displacement.v);
					computed=frequencyBands(computed,float(fineSigma->value())*resolutionScale,float(coarseSigma->value())*resolutionScale,
						float(mediumGain->value()),float(fineGain->value()),displacement.u,displacement.v);
					if (reliefMode->currentIndex()==1) computed=shallowSurfaceDetail(computed,8*resolutionScale,float(neutral->value()),displacement.u,displacement.v);
					conditionedField=std::move(computed); conditionedRecipe=std::move(recipe); conditionedReady=true;
				}
				field=conditionedField;
				bool changed=priorInput.width!=field.width || priorInput.height!=field.height || priorInput.values!=field.values
					|| priorStrength!=float(strength->value()) || priorNeutral!=float(neutral->value());
				if (changed || priorProfile!=surfaceProfile->currentIndex()) {
					if (changed) morphologyReady=false;
					auto selected=static_cast<SurfaceProfile>(surfaceProfile->currentIndex());
					SurfacePrior computed;
					if (selected==SurfaceProfile::Neutral) computed=surfacePrior(field,float(neutral->value()),float(strength->value()),selected,displacement.u,displacement.v);
					else {
						if (!morphologyReady) { morphology=analyzeSurfaceMorphology(field,float(neutral->value()),float(strength->value()),displacement.u,displacement.v); morphologyReady=true; }
						computed=applySurfaceProfile(morphology,selected,displacement.u,displacement.v);
					}
					prior=std::move(computed); priorInput=field; priorStrength=float(strength->value()); priorNeutral=float(neutral->value()); priorProfile=surfaceProfile->currentIndex();
				}
				if (priorProfile>0) displacement.profileTarget=&prior.target;
				if (darkEnabled->isChecked()) {
					attraction=darkFeatureAttraction(field,2*resolutionScale,displacement.u,displacement.v);
				}
			}
			Limits limits{ size_t( vertexLimit->value() ), size_t( triangleLimit->value() ) };
			if (!islandSeedReady) {
				islandIssue.clear();
				if (legacyUVHandling->isChecked()) { islandSeed=original; islandIssue=tr("Selected manually"); }
				else {
					auto assessment=assessUVIsolation(original,displacement,limits);
					if (assessment.useLegacy) { islandSeed=original; islandIssue=QString::fromStdString(assessment.reason); }
					else try { islandSeed=isolateUVIslands(original,limits); }
					catch (const std::invalid_argument &e) { islandSeed=original; islandIssue=QString::fromUtf8(e.what()); }
				}
				islandSeedReady=true;
			}
			if (patchRetopology->isChecked() && !patchSeedReady) {
				Mesh input=islandSeed;
				auto islandAuthority=input.patchAuthority; input.patchAuthority.reset();
				patchSeed=retopologizeCoplanarPatches(input,patchReports,{},limits);
				if (islandAuthority) {
					auto authority=std::make_shared<PatchAuthority>(*patchSeed.patchAuthority);
					authority->original=islandAuthority->original; patchSeed.patchAuthority=std::move(authority);
				}
				(void)resolvePatchSources(patchSeed,limits);
				patchSeedReady=true;
			}
			const Mesh &seed=patchRetopology->isChecked() ? patchSeed : islandSeed;
			Mesh working = ( tessellation->currentIndex() == 0 && hasSource )
				? subdivideAdaptive( seed, field, unsigned( depth->value() ), float( gradientThreshold->value() ), displacement, limits )
				: ( tessellation->currentIndex() == 1 ? subdivide( seed, unsigned( depth->value() ), limits ) : seed );
			DarkFeatureStats darkStats;
			darkStats.enabled=darkEnabled->isChecked();
			if (hasSource && darkEnabled->isChecked()) {
				DarkFeatureSettings bias{true,float(darkThreshold->value()),float(darkStrength->value()),float(darkShift->value())};
				working=relocateDarkFeatures(working,attraction,displacement,bias,&darkStats,limits);
			}
			Mesh final = working;
			Mesh dense = working;
			DisplacementStats safetyStats;
			ReductionStats reductionStats;
			float reductionLimit=0;
			std::size_t qualityFlips = 0;
			bool cleanupBiasFallback=false,reductionBiasFallback=false;
			if ( hasSource ) {
				displacement.strength = float( strength->value() ); displacement.neutral = float( neutral->value() );
				displacement.positiveLimit = float( positive->value() ); displacement.negativeLimit = float( negative->value() );
				displacement.preventPinching = protectPinching->isChecked(); displacement.maxSlope = float(maxSlope->value());
				displacement.removeSectionBias=removeBias->isChecked();
				final = displace( working, field, displacement, limits, &safetyStats );
				Vec3 lo=original.vertices.front().position,hi=lo;
				for (const auto &v:original.vertices) for (int j=0;j<3;++j) {
					lo[j]=std::min(lo[j],v.position[j]); hi[j]=std::max(hi[j],v.position[j]);
				}
				double diagonal=std::hypot(double(hi[0])-lo[0],double(hi[1])-lo[1],double(hi[2])-lo[2]);
				double biasTolerance=std::max(1e-7,diagonal*1e-6);
				if ( qualityCleanup->isChecked() ) {
					auto cleaned=improveTriangleQuality(final,&qualityFlips,limits);
					if (displacement.removeSectionBias && maximumSectionDisplacementBias(cleaned,limits)
						> maximumSectionDisplacementBias(final,limits)+biasTolerance) {
						cleanupBiasFallback=true; qualityFlips=0;
					} else final=std::move(cleaned);
				}
				dense=final;
				if (reduction->isChecked()) {
					reductionLimit=float(diagonal*reductionTolerance->value()/100.0);
					final=reduceReconstruction(dense,reductionLimit,&reductionStats,limits);
					if (displacement.removeSectionBias && maximumSectionDisplacementBias(final,limits)
						>maximumSectionDisplacementBias(dense,limits)+biasTolerance) {
						final=dense; reductionStats.trianglesAfter=dense.triangles.size(); reductionStats.sampledError=0;
						++reductionStats.skippedRegions; reductionBiasFallback=true;
					}
				}
				if (displacement.removeSectionBias) safetyStats.maximumResidualBias=maximumSectionDisplacementBias(final,limits);
				previewMesh=final; previewValid=true;
				applyButton->setEnabled(snapshot.writable && bool(nifModel->undoStack));
			}
			QString statsText = tr( "Original: %1 vertices, %2 triangles. Working: %3 vertices, %4 triangles." )
				.arg( original.vertices.size() ).arg( original.triangles.size() ).arg( working.vertices.size() ).arg( working.triangles.size() );
			if (islandSeed.vertices.size()>original.vertices.size())
				statsText+=tr(" UV-island isolation added %1 seam vertices.").arg(islandSeed.vertices.size()-original.vertices.size());
			if (patchRetopology->isChecked()) {
				auto accepted=std::count_if(patchReports.begin(),patchReports.end(),[](const PatchRetopoReport &r) { return r.accepted; });
				auto changed=std::count_if(patchReports.begin(),patchReports.end(),[](const PatchRetopoReport &r) { return r.topologyChanged; });
				statsText+=tr(" Patches accepted: %1; changed: %2; unchanged: %3.").arg(accepted).arg(changed).arg(patchReports.size()-size_t(accepted));
			}
			if ( tessellation->currentIndex() == 0 && hasSource ) {
				qulonglong uniform = qulonglong( original.triangles.size() );
				for ( int i = 0; i < depth->value(); ++i ) uniform *= 4;
				double saved = uniform ? 100.0 * ( 1.0 - double( working.triangles.size() ) / double( uniform ) ) : 0;
				statsText += tr( " Uniform at this depth: %1 triangles (%2% fewer)." ).arg( uniform ).arg( saved, 0, 'f', 1 );
			}
			if (hasSource && displacement.preventPinching && safetyStats.limitedVertices)
				statsText += tr( " Pinch protection adjusted %1 vertices; global fallback scale %2%." )
					.arg(safetyStats.limitedVertices).arg(100.0*safetyStats.orientationScale,0,'f',1);
			if (hasSource && displacement.removeSectionBias)
				statsText+=tr(" Bias removed in %1 sections; maximum remaining mean %2 units.")
					.arg(safetyStats.centeredSections).arg(safetyStats.maximumResidualBias,0,'g',3);
			if (hasSource)
				statsText+=tr(" Displacement RMS: requested %1; applied %2 units before cleanup/reduction.")
					.arg(safetyStats.requestedRms,0,'g',4).arg(safetyStats.appliedRms,0,'g',4);
			if (hasSource && priorProfile>0)
				statsText+=tr(" %1 target: field RMS %2 → %3; broad energy %4 → %5; mean attenuation %6%.")
					.arg(surfaceProfile->currentText()).arg(prior.originalRms,0,'g',4).arg(prior.targetRms,0,'g',4).arg(prior.broadEnergyBefore,0,'g',4).arg(prior.broadEnergyAfter,0,'g',4).arg(prior.meanSuppression*100,0,'f',2);
			if (hasSource && priorProfile>0)
				statsText+=tr(" Fine-band energy %1 → %2; signed range [%3, %4] → [%5, %6].")
					.arg(prior.detailEnergyBefore,0,'g',4).arg(prior.detailEnergyAfter,0,'g',4).arg(prior.originalMin,0,'g',4).arg(prior.originalMax,0,'g',4).arg(prior.targetMin,0,'g',4).arg(prior.targetMax,0,'g',4);
			if (darkStats.enabled)
				statsText+=tr(" Dark bias enabled: %1 eligible; %2 near evidence above threshold; %3 proposals; %4 accepted; %5 rejected (%6 topology, %7 provenance/boundary); mean shift %8, maximum %9 units; %10 ambiguous samples held fixed.")
					.arg(darkStats.eligible).arg(darkStats.aboveThreshold).arg(darkStats.proposals).arg(darkStats.accepted).arg(darkStats.rejected)
					.arg(darkStats.topologyRejected).arg(darkStats.provenanceRejected).arg(darkStats.averageDistance,0,'g',4).arg(darkStats.maximumDistance,0,'g',4).arg(darkStats.provenanceSkipped);
			else statsText+=tr(" Dark bias disabled.");
			if (hasSource && qualityCleanup->isChecked())
				statsText += tr( " Triangle cleanup: %1 edges flipped." ).arg(qualityFlips);
			if (cleanupBiasFallback) statsText+=tr(" Cleanup retained the original connectivity to preserve section bias.");
			if (reductionBiasFallback) statsText+=tr(" Reduction retained the dense surface to preserve section bias.");
			if (hasSource && reduction->isChecked())
				statsText += tr(" Reduced: %1 vertices, %2 triangles; sampled error %3 of %4 allowed units. Regions skipped: %5.")
					.arg(final.vertices.size()).arg(final.triangles.size()).arg(reductionStats.sampledError,0,'g',4).arg(reductionLimit,0,'g',4).arg(reductionStats.skippedRegions);
			stats->setText( statsText );
			statusLabel->setText( hasSource ? (snapshot.writable
				? tr( "Preview only until Apply. Files are not saved automatically." )
				: tr( "Preview only: write-back for this mesh format is not available yet." ))
				: tr( "Configure NifSkope resources or enable a loaded height source to preview displacement." ) );
			if (!islandIssue.isEmpty()) {
				statusLabel->setText(statusLabel->text()+tr(" Using original UV handling: %1. UV values and addressing are unchanged.").arg(islandIssue));
			}
			presentationWorking=std::move(working);
			presentationDenseUsesFinal=hasSource && (!reduction->isChecked() || reductionBiasFallback);
			if (presentationDenseUsesFinal) presentationDense={}; else presentationDense=std::move(dense);
			presentationField=std::move(field); presentationAttraction=std::move(attraction); presentationHasSource=hasSource;
			renderPresentation(); presentationReady=true;
		} catch ( const std::exception & e ) {
			previewValid=false; presentationReady=false; applyButton->setEnabled(false);
			heightLabel->clear(); histogramLabel->clear();
			meshLabel->setPixmap( meshPicture( original, original, wireframe->isChecked(), smoothShading->isChecked() ) );
			stats->setText( tr( "Original: %1 vertices, %2 triangles." ).arg( original.vertices.size() ).arg( original.triangles.size() ) );
			statusLabel->setText( tr( "Preview could not be updated: %1" ).arg( QString::fromUtf8( e.what() ) ) );
		}
	}
	const ShapeSnapshot snapshot;
	const ShapeSnapshot live;
	std::shared_ptr<VanillaOperation> operation;
	const Mesh original;
	NifModel *nifModel;
	HeightField diffuse, normal, existingHeight, diffuseHigh, normalHigh, heightHigh;
	HeightField priorInput;
	HeightField conditionedField;
	std::vector<double> conditionedRecipe;
	bool conditionedReady=false;
	SurfacePrior prior;
	SurfacePrior morphology;
	bool morphologyReady=false;
	float priorStrength=std::numeric_limits<float>::quiet_NaN(),priorNeutral=0;
	int priorProfile=-1;
	Displacement displacement;
	Mesh previewMesh;
	Mesh presentationWorking,presentationDense;
	HeightField presentationField,presentationAttraction;
	bool presentationReady=false,presentationHasSource=false,presentationDenseUsesFinal=false;
	std::vector<std::pair<int,QPixmap>> presentationImages;
	Mesh patchSeed;
	Mesh islandSeed;
	std::vector<PatchRetopoReport> patchReports;
	bool patchSeedReady=false, islandSeedReady=false;
	QString islandIssue;
	bool previewValid=false;
	QDoubleSpinBox *diffuseWeight, *normalWeight, *heightWeight, *diffuseMult, *normalMult, *heightMult;
	QDoubleSpinBox *black, *white, *midpoint, *contrast, *outMin, *outMax, *strength, *neutral, *positive, *negative;
	QDoubleSpinBox *spatialSigma, *rangeSigma, *fineSigma, *coarseSigma, *mediumGain, *fineGain, *gradientThreshold, *maxSlope, *reductionTolerance;
	QCheckBox *invert, *wireframe, *protectPinching, *smoothShading, *qualityCleanup, *reduction, *patchRetopology, *highPrecision, *legacyUVHandling, *removeBias;
	QCheckBox *darkEnabled,*darkPreview;
	QDoubleSpinBox *darkThreshold,*darkStrength,*darkShift;
	QSpinBox *depth, *vertexLimit, *triangleLimit;
	QComboBox *mode, *view, *tessellation, *reliefMode;
	QComboBox *surfaceProfile;
	QCheckBox *profilePreview;
	QLabel *meshLabel, *heightLabel, *heightTitle, *histogramLabel, *stats, *statusLabel;
	QPushButton *applyButton;
};
} // namespace

namespace {
class SculptShapeCommand final : public QUndoCommand {
public:
	SculptShapeCommand(NifModel *nif,const QModelIndex &index,QByteArray before,QByteArray after,std::shared_ptr<bool> success)
		: model(nif),shape(index),oldBytes(std::move(before)),newBytes(std::move(after)),initial(std::move(success)) { setText(QObject::tr("Apply Sculpt")); }
	void redo() override { change(oldBytes,newBytes); }
	void undo() override { change(newBytes,oldBytes); }
private:
	void change(const QByteArray &expected,const QByteArray &replacement) {
		// Ordinary model notifications intentionally invalidate any older
		// Vanilla+ reference: later tessellation starts from this sculpted shape.
		bool ok=model && shape.isValid() && replaceShapeBytes(model,shape,expected,replacement);
		if (initial) { *initial=ok; initial.reset(); }
		if (!ok) { setObsolete(true); QMessageBox::warning(nullptr,QObject::tr("Sculpt"),QObject::tr("The shape changed. Sculpt replacement was refused; reopen the workspace.")); }
	}
	QPointer<NifModel> model; QPersistentModelIndex shape; QByteArray oldBytes,newBytes; std::shared_ptr<bool> initial;
};
}

void showSculptGeometry(NifModel *nif,const QModelIndex &selection,QWidget *parent)
{
	auto shape=nif && selection.isValid()?nif->getBlockIndex(selection):QModelIndex();
	ShapeSnapshot snapshot; QString error=inspectShape(nif,shape,snapshot);
	if (error.isEmpty() && !snapshot.writable) error=QObject::tr("Sculpt Apply currently supports static Skyrim SE BSTriShape and Skyrim NiTriShape geometry.");
	if (!error.isEmpty()) { QMessageBox::information(parent,QObject::tr("Sculpt"),error); return; }
	try {
		QByteArray document; QBuffer output(&document); output.open(QIODevice::WriteOnly);
		if (!nif->save(output)) throw std::runtime_error("Could not create the private sculpt document");
		NifModel preview; QBuffer input(&document); input.open(QIODevice::ReadOnly);
		if (!preview.load(input)) throw std::runtime_error("Could not open the private sculpt document");
		preview.refreshFileInfo(nif->getFileInfo().absoluteFilePath());
		preview.getGameResources().dataPaths=nif->getGameResources().dataPaths;
		QPointer<NifModel> live=nif;
		auto commit=[live,snapshot](const Mesh &mesh,bool apply) -> QString {
			if (!live) return QObject::tr("The source document closed.");
			if (apply && !live->undoStack) return QObject::tr("The source document has no Undo stack; sculpt Apply is unavailable.");
			if (live->getVersionNumber()!=snapshot.version || live->getUserVersion()!=snapshot.userVersion || live->getBSVersion()!=snapshot.bsVersion)
				return QObject::tr("The NIF header changed; reopen the sculpt workspace.");
			// Position-only editing must retain every original index and attribute.
			if (mesh.vertices.size()!=snapshot.geometry.vertices.size() || mesh.triangles!=snapshot.geometry.triangles || mesh.source!=snapshot.geometry.source)
				return QObject::tr("Sculpt topology or source authority changed; Apply refused.");
			for (std::size_t i=0;i<mesh.vertices.size();++i) {
				const auto &a=mesh.vertices[i],&b=snapshot.geometry.vertices[i];
				if (a.uv!=b.uv || a.normal!=b.normal || a.color!=b.color || a.boundary!=b.boundary) return QObject::tr("A protected sculpt attribute changed; Apply refused.");
			}
			QByteArray staged; QString error=stageShape(live,snapshot,mesh,staged); if (!error.isEmpty() || !apply) return error;
			NifModel copy; QBuffer reader(&staged); reader.open(QIODevice::ReadOnly);
			if (!copy.load(reader)) return QObject::tr("Could not reopen the staged sculpt document.");
			QByteArray replacement; auto index=copy.getBlockIndex(snapshot.writeBlock);
			if (!serializeBlock(&copy,index,replacement) || !replaceShapeBytes(&copy,index,replacement,snapshot.writeBytes)
				|| !replaceShapeBytes(&copy,index,snapshot.writeBytes,replacement)) return QObject::tr("Sculpt replacement/rollback preflight failed.");
			auto success=std::make_shared<bool>(false);
			live->undoStack->push(new SculptShapeCommand(live,live->getBlockIndex(snapshot.writeBlock),snapshot.writeBytes,replacement,success));
			return *success?QString():QObject::tr("Sculpt Apply failed; the source shape was retained.");
		};
		showSculptWorkspace(&preview,snapshot.shapeBlock,snapshot.writeBlock,snapshot.classic,snapshot.geometry,std::move(commit),parent,
			snapshot.animatedShader?QObject::tr("Animated shader: Sculpt edits geometry while preserving stored UVs, UV seams and existing shader animation. Animated textures may move over the edited surface."):QString());
	} catch (const std::exception &e) { QMessageBox::warning(parent,QObject::tr("Sculpt"),QString::fromUtf8(e.what())); }
}

void showVanillaPlusGeometry( NifModel * nif, const QModelIndex & selection, QWidget * parent )
{
	QModelIndex shape = nif && selection.isValid() ? nif->getBlockIndex( selection ) : QModelIndex();
	ShapeSnapshot snapshot;
	QString error = inspectShape( nif, shape, snapshot );
	if ( !error.isEmpty() ) { QMessageBox::information( parent, QObject::tr( "Vanilla+ Geometry" ), error ); return; }
	auto store=operationStore(nif); std::shared_ptr<VanillaOperation> operation;
	for (auto &op:store->operations) if (op->reference.shapeBlock==snapshot.shapeBlock) {
		if (operationMatches(nif,*op)) { operation=op; break; }
		op->valid=false;
	}
	bool continuing=bool(operation);
	if (!operation) {
		operation=std::make_shared<VanillaOperation>(); operation->reference=snapshot;
		operation->currentShape=snapshot.shapeBytes; operation->currentWrite=snapshot.writeBytes;
		store->operations.push_back(operation);
	}
	PreviewDialog dialog( nif, std::move( snapshot ),operation,continuing,parent );
	dialog.exec();
}
