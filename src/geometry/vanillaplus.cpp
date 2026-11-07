#include "vanillaplus.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace VanillaPlus {
namespace {
void require(bool ok, const char *message)
{
	if (!ok)
		throw std::invalid_argument(message);
}

template<std::size_t N> bool finite(const std::array<float, N> &v)
{
	return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

double length(const Vec3 &v)
{
	return std::hypot(double(v[0]), double(v[1]), double(v[2]));
}

Vec3 normalized(Vec3 v)
{
	double n = length(v);
	require(n > 1.0e-12 && std::isfinite(n), "Missing or opposing vertex normals");
	for (float &x : v)
		x = float(double(x) / n);
	return v;
}

template<std::size_t N>
std::array<float, N> midpoint(const std::array<float, N> &a, const std::array<float, N> &b)
{
	std::array<float, N> result;
	for (std::size_t i = 0; i < N; ++i)
		result[i] = float((double(a[i]) + double(b[i])) * 0.5);
	return result;
}

void checkLimits(const Mesh &mesh, Limits limits)
{
	if (mesh.vertices.size() > limits.vertices || mesh.triangles.size() > limits.triangles
		|| mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::length_error("Working geometry exceeds the configured vertex/triangle budget");
}

// Texture-center convention: u=(x+0.5)/width samples texel x exactly.
struct SampleAxis { std::size_t a, b; float fraction; };
SampleAxis axis(double coordinate, std::size_t size, Address address)
{
	if (address == Address::Wrap)
		coordinate -= std::floor(coordinate);
	else
		coordinate = std::clamp(coordinate, 0.0, 1.0);
	double p = coordinate * double(size) - 0.5;
	double lower = std::floor(p);
	auto index = [size, address](double x) -> std::size_t {
		if (address == Address::Clamp)
			return std::size_t(std::clamp(x, 0.0, double(size - 1)));
		if (x < 0)
			return size - 1;
		return x >= double(size) ? 0 : std::size_t(x);
	};
	return {index(lower), index(lower + 1), float(p - lower)};
}

float sampleValidated(const HeightField &field, double u, double v, Address au, Address av)
{
	auto x = axis(u, field.width, au);
	auto y = axis(v, field.height, av);
	auto at = [&field](std::size_t a, std::size_t b) { return double(field.values[b * field.width + a]); };
	double top = std::lerp(at(x.a, y.a), at(x.b, y.a), double(x.fraction));
	double bottom = std::lerp(at(x.a, y.b), at(x.b, y.b), double(x.fraction));
	return float(std::lerp(top, bottom, double(y.fraction)));
}

float sampleMappedValidated(const HeightField &field, double u, double v, const Displacement &mapping)
{
	return sampleValidated(field, u * mapping.uvScale[0] + mapping.uvOffset[0],
		v * mapping.uvScale[1] + mapping.uvOffset[1], mapping.u, mapping.v);
}

double requestedDistance(const HeightField &field,double u,double v,const Displacement &mapping)
{
	double original=(double(sampleMappedValidated(field,u,v,mapping))-mapping.neutral)*mapping.strength;
	if (!mapping.profileTarget) return original;
	double candidate=sampleMappedValidated(*mapping.profileTarget,u,v,mapping);
	// Repeat the signed constraint after bilinear interpolation: texel-wise
	// attenuation alone can reverse sign between opposing neighboring values.
	return std::clamp(candidate,std::min(0.0,original),std::max(0.0,original));
}

float combine(float a, float b, Blend mode)
{
	switch (mode) {
	case Blend::Add: return a + b;
	case Blend::Multiply: return a * b;
	case Blend::Screen: return 1 - (1 - a) * (1 - b);
	case Blend::Overlay: return a < 0.5f ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b);
	case Blend::Min: return std::min(a, b);
	case Blend::Max: return std::max(a, b);
	}
	throw std::invalid_argument("Unknown blend mode");
}
} // namespace

void validate(const Mesh &mesh, Limits limits)
{
	checkLimits(mesh, limits);
	require(!mesh.vertices.empty() && !mesh.triangles.empty(), "Empty geometry");
	for (const auto &v : mesh.vertices) {
		require(finite(v.position) && finite(v.normal) && finite(v.uv) && finite(v.color),
			"Non-finite vertex attribute");
		require(length(v.normal) > 1.0e-12, "Zero vertex normal");
	}
	for (const auto &t : mesh.triangles) {
		for (auto i : t)
			require(i < mesh.vertices.size(), "Triangle index outside vertex array");
		require(t[0] != t[1] && t[1] != t[2] && t[2] != t[0], "Repeated triangle index");
		const auto &a = mesh.vertices[t[0]].position;
		const auto &b = mesh.vertices[t[1]].position;
		const auto &c = mesh.vertices[t[2]].position;
		std::array<double, 3> ab, ac, cross;
		for (int i = 0; i < 3; ++i) {
			ab[i] = double(b[i]) - a[i];
			ac[i] = double(c[i]) - a[i];
		}
		for (int i = 0; i < 3; ++i)
			cross[i] = ab[(i + 1) % 3] * ac[(i + 2) % 3] - ab[(i + 2) % 3] * ac[(i + 1) % 3];
		double area = std::hypot(cross[0], cross[1], cross[2]);
		require(area > 0 && std::isfinite(area), "Degenerate triangle");
	}
}

Mesh captureSource(const Mesh &mesh, Limits limits)
{
	validate(mesh, limits);
	if (mesh.source) {
		require(mesh.origins.size() == mesh.triangles.size(), "Missing triangle provenance");
		return mesh;
	}
	Mesh result = mesh;
	std::map<std::pair<std::uint32_t, std::uint32_t>, unsigned> edges;
	for (const auto &t : result.triangles) for (int i = 0; i < 3; ++i)
		++edges[{std::min(t[i],t[(i+1)%3]), std::max(t[i],t[(i+1)%3])}];
	for (const auto &[edge, count] : edges) {
		require(count <= 2, "Non-manifold geometry cannot be previewed safely");
		if (count == 1) result.vertices[edge.first].boundary = result.vertices[edge.second].boundary = true;
	}
	result.source = std::make_shared<const SourceGeometry>(SourceGeometry{result.vertices, result.triangles});
	result.origins.reserve(result.triangles.size());
	for (size_t i = 0; i < result.triangles.size(); ++i)
		result.origins.push_back({std::uint32_t(i), {{{1,0,0},{0,1,0},{0,0,1}}}});
	return result;
}

std::vector<UVIsland> identifyUVIslands(const Mesh &mesh,Limits limits)
{
	validate(mesh,limits);
	using Edge=std::pair<std::uint32_t,std::uint32_t>;
	struct Use { std::uint32_t face,from,to; };
	std::map<Edge,std::vector<Use>> edges;
	std::vector<int> orientation(mesh.triangles.size());
	for (size_t f=0;f<mesh.triangles.size();++f) {
		const auto &t=mesh.triangles[f];
		const auto &a=mesh.vertices[t[0]].uv,&b=mesh.vertices[t[1]].uv,&c=mesh.vertices[t[2]].uv;
		double area=(double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]);
		require(std::isfinite(area) && std::abs(area)>1e-15, "Degenerate UV triangle cannot form an island");
		orientation[f]=area>0 ? 1 : -1;
		for (int i=0;i<3;++i) {
			auto x=t[i],y=t[(i+1)%3];
			edges[{std::min(x,y),std::max(x,y)}].push_back({std::uint32_t(f),x,y});
		}
	}
	std::vector<std::uint32_t> parent(mesh.triangles.size()); std::iota(parent.begin(),parent.end(),0);
	auto root=[&](std::uint32_t i) { while (parent[i]!=i) i=parent[i]; return i; };
	for (const auto &[edge,uses]:edges) {
		(void)edge;
		require(uses.size()<=2, "Non-manifold edge cannot form UV islands");
		if (uses.size()!=2) continue;
		const auto &a=uses[0],&b=uses[1];
		require(a.from==b.to && a.to==b.from, "Inconsistent winding cannot form UV islands");
		if (orientation[a.face]!=orientation[b.face]) continue;
		auto ra=root(a.face),rb=root(b.face); if (ra!=rb) parent[rb]=ra;
	}
	std::map<std::uint32_t,UVIsland> groups;
	for (std::uint32_t f=0;f<mesh.triangles.size();++f) groups[root(f)].triangles.push_back(f);
	std::vector<UVIsland> result; result.reserve(groups.size());
	for (auto &[id,island]:groups) { (void)id; result.push_back(std::move(island)); }
	return result;
}

Mesh isolateUVIslands(const Mesh &mesh,Limits limits)
{
	Mesh original=captureSource(mesh,limits);
	require(!original.patchAuthority && original.triangles==original.source->triangles,
		"UV island isolation requires the original working shape");
	auto islands=identifyUVIslands(original,limits);
	std::vector<std::uint32_t> faceIsland(original.triangles.size());
	for (std::uint32_t i=0;i<islands.size();++i) for (auto f:islands[i].triangles) faceIsland[f]=i;
	Mesh working=original; working.source.reset(); working.origins.clear();
	std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> vertices;
	std::vector<bool> assigned(original.vertices.size(),false);
	bool changed=false;
	for (size_t f=0;f<working.triangles.size();++f) for (int j=0;j<3;++j) {
		auto originalIndex=original.triangles[f][j]; auto key=std::make_pair(originalIndex,faceIsland[f]);
		auto it=vertices.find(key);
		if (it==vertices.end()) {
			std::uint32_t mapped=originalIndex;
			if (assigned[originalIndex]) {
				if (working.vertices.size()>=limits.vertices || working.vertices.size()>=std::numeric_limits<std::uint32_t>::max())
					throw std::length_error("UV island isolation exceeds the vertex budget");
				mapped=std::uint32_t(working.vertices.size()); working.vertices.push_back(original.vertices[originalIndex]); changed=true;
			} else assigned[originalIndex]=true;
			it=vertices.emplace(key,mapped).first;
		}
		working.triangles[f][j]=it->second;
	}
	if (!changed) return original;
	working=captureSource(working,limits);
	auto authority=std::make_shared<PatchAuthority>(); authority->original=original.source;
	for (std::uint32_t f=0;f<original.triangles.size();++f) {
		authority->patches.push_back(SourcePatch{{f}}); authority->workingFacePatch.push_back(f);
	}
	working.patchAuthority=std::move(authority);
	return working;
}

UVIsolationAssessment assessUVIsolation(const Mesh &mesh,const Displacement &mapping,Limits limits)
{
	UVIsolationAssessment result;
	auto fallback=[&](const char *reason) { result.useLegacy=true; result.reason=reason; return result; };
	try {
		validate(mesh,limits);
		if (!finite(mapping.uvScale) || !finite(mapping.uvOffset)) return fallback("Non-finite UV transform");
		if (mapping.uvScale[0]==0 || mapping.uvScale[1]==0) return fallback("The UV transform collapses a texture axis");
		auto islands=identifyUVIslands(mesh,limits); result.islands=islands.size();
		using Point=std::array<double,2>;
		struct Face { std::array<Point,3> uv; double minX,maxX,minY,maxY; };
		std::vector<Face> faces; faces.reserve(mesh.triangles.size());
		for (const auto &t:mesh.triangles) {
			Face f{{},1,0,1,0};
			for (int i=0;i<3;++i) {
				for (int j=0;j<2;++j) {
					double value=double(mesh.vertices[t[i]].uv[j])*mapping.uvScale[j]+mapping.uvOffset[j];
					if (!std::isfinite(value)) return fallback("Non-finite transformed UV");
					if (value < -1e-7 || value > 1+1e-7) return fallback("Tiled or out-of-range material UVs");
					f.uv[i][j]=value;
				}
				f.minX=std::min(f.minX,f.uv[i][0]); f.maxX=std::max(f.maxX,f.uv[i][0]);
				f.minY=std::min(f.minY,f.uv[i][1]); f.maxY=std::max(f.maxY,f.uv[i][1]);
			}
			faces.push_back(f);
		}
		std::sort(faces.begin(),faces.end(),[](const Face &a,const Face &b) { return a.minX<b.minX; });
		auto orient=[](Point a,Point b,Point c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); };
		std::size_t comparisons=0;
		for (size_t i=0;i<faces.size();++i) for (size_t j=i+1;j<faces.size();++j) {
			if (faces[j].minX>=faces[i].maxX-1e-12) break;
			if (++comparisons>250000) return fallback("UV overlap analysis exceeded its safety budget");
			const auto &a=faces[i],&b=faces[j];
			if (a.maxY<=b.minY+1e-12 || b.maxY<=a.minY+1e-12) continue;
			double sign=orient(b.uv[0],b.uv[1],b.uv[2])>=0 ? 1 : -1;
			std::vector<Point> polygon{a.uv[0],a.uv[1],a.uv[2]};
			for (int edge=0;edge<3 && !polygon.empty();++edge) {
				std::vector<Point> next; Point p=b.uv[edge],q=b.uv[(edge+1)%3];
				for (size_t k=0;k<polygon.size();++k) {
					Point x=polygon[k],y=polygon[(k+1)%polygon.size()];
					double dx=sign*orient(p,q,x),dy=sign*orient(p,q,y);
					bool insideX=dx>=0,insideY=dy>=0;
					if (insideX) next.push_back(x);
					if (insideX!=insideY) {
						double d=dx-dy; if (std::abs(d)<1e-20) return fallback("Ambiguous UV overlap");
						double w=dx/d; next.push_back({x[0]+w*(y[0]-x[0]),x[1]+w*(y[1]-x[1])});
					}
				}
				polygon=std::move(next);
			}
			if (polygon.size()>=3) {
				double area=0; for (size_t k=0;k<polygon.size();++k)
					area+=polygon[k][0]*polygon[(k+1)%polygon.size()][1]-polygon[k][1]*polygon[(k+1)%polygon.size()][0];
				if (std::abs(area)>2e-12) return fallback("Overlapping material UV regions");
			}
		}
		return result;
	} catch (const std::invalid_argument &e) { result.useLegacy=true; result.reason=e.what(); return result; }
	catch (const std::length_error &e) { result.useLegacy=true; result.reason=e.what(); return result; }
}

namespace {
// Project onto the source triangle plane; weights may lie outside it. The
// plane tolerance is deliberately strict: this is eligibility, not fitting.
bool sourceWeights(const SourceGeometry &s, std::uint32_t face, const Vec3 &p, Barycentric &w)
{
	if (face>=s.triangles.size()) return false;
	const auto &t=s.triangles[face];
	const auto &a=s.vertices[t[0]].position, &b=s.vertices[t[1]].position, &c=s.vertices[t[2]].position;
	double u[3],v[3],q[3],n[3];
	for (int i=0;i<3;++i) { u[i]=double(b[i])-a[i]; v[i]=double(c[i])-a[i]; q[i]=double(p[i])-a[i]; }
	for (int i=0;i<3;++i) n[i]=u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3];
	double uu=0,uv=0,vv=0,qu=0,qv=0,nn=0,qn=0;
	for (int i=0;i<3;++i) { uu+=u[i]*u[i]; uv+=u[i]*v[i]; vv+=v[i]*v[i]; qu+=q[i]*u[i]; qv+=q[i]*v[i]; nn+=n[i]*n[i]; qn+=q[i]*n[i]; }
	double den=uu*vv-uv*uv, scale=std::max(uu,vv);
	if (!(den>0) || !(scale>0) || qn*qn>nn*scale*1e-12) return false;
	w[1]=(qu*vv-qv*uv)/den; w[2]=(qv*uu-qu*uv)/den; w[0]=1-w[1]-w[2];
	return std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]);
}
bool affineMatch(const SourceGeometry &s,std::uint32_t face,std::uint32_t index)
{
	const auto &v=s.vertices[index]; Barycentric w;
	if (!sourceWeights(s,face,v.position,w)) return false;
	const auto &t=s.triangles[face];
	auto close=[](double a,double b) { return std::abs(a-b)<=1e-6*std::max({1.0,std::abs(a),std::abs(b)}); };
	for (int j=0;j<2;++j) {
		double x=0; for (int i=0;i<3;++i) x+=w[i]*s.vertices[t[i]].uv[j];
		if (!close(x,v.uv[j])) return false;
	}
	for (int j=0;j<3;++j) {
		double x=0; for (int i=0;i<3;++i) x+=w[i]*s.vertices[t[i]].normal[j];
		if (!close(x,v.normal[j])) return false;
	}
	for (int j=0;j<4;++j) {
		double x=0; for (int i=0;i<3;++i) x+=w[i]*s.vertices[t[i]].color[j];
		if (!close(x,v.color[j])) return false;
	}
	return true;
}
}

std::vector<SourcePatch> identifySourcePatches(const Mesh &mesh,
	const std::vector<std::array<std::uint32_t,2>> &protectedEdges)
{
	Mesh captured=captureSource(mesh); const auto &s=*captured.source;
	using Edge=std::pair<std::uint32_t,std::uint32_t>;
	auto key=[](std::uint32_t a,std::uint32_t b) { return Edge{std::min(a,b),std::max(a,b)}; };
	std::set<Edge> protectedSet;
	for (auto e:protectedEdges) {
		require(e[0]<s.vertices.size() && e[1]<s.vertices.size() && e[0]!=e[1], "Invalid protected edge");
		protectedSet.insert(key(e[0],e[1]));
	}
	struct Use { std::uint32_t face, a, b, opposite; };
	std::map<Edge,std::vector<Use>> edges;
	for (std::uint32_t f=0;f<s.triangles.size();++f) for (int i=0;i<3;++i) {
		const auto &t=s.triangles[f]; edges[key(t[i],t[(i+1)%3])].push_back({f,t[i],t[(i+1)%3],t[(i+2)%3]});
	}
	std::vector<std::uint32_t> parent(s.triangles.size()); std::iota(parent.begin(),parent.end(),0);
	auto root=[&](std::uint32_t x) { while (parent[x]!=x) x=parent[x]; return x; };
	std::vector<std::pair<std::uint32_t,std::uint32_t>> allowed, forbidden;
	for (const auto &[edge,uses]:edges) {
		if (uses.size()!=2) continue;
		const auto &a=uses[0], &b=uses[1];
		bool compatible=!protectedSet.count(edge) && a.a==b.b && a.b==b.a
			&& affineMatch(s,a.face,b.opposite) && affineMatch(s,b.face,a.opposite);
		(compatible ? allowed : forbidden).emplace_back(a.face,b.face);
	}
	for (const auto &[a,b]:allowed) {
		auto ra=root(a), rb=root(b); if (ra==rb) continue;
		bool crosses=false;
		for (const auto &[x,y]:forbidden) {
			auto rx=root(x), ry=root(y);
			if ((rx==ra && ry==rb) || (rx==rb && ry==ra)) { crosses=true; break; }
		}
		if (!crosses) parent[rb]=ra;
	}
	std::map<std::uint32_t,SourcePatch> groups;
	for (std::uint32_t f=0;f<s.triangles.size();++f) groups[root(f)].triangles.push_back(f);
	std::vector<SourcePatch> patches; patches.reserve(groups.size());
	for (auto &[id,p]:groups) { (void)id; patches.push_back(std::move(p)); }
	return patches;
}

PatchBoundary recoverPatchBoundary(const Mesh &mesh,const SourcePatch &patch,
	const std::vector<std::array<std::uint32_t,2>> &protectedEdges)
{
	Mesh captured=captureSource(mesh); const auto &s=*captured.source;
	require(!patch.triangles.empty(), "Empty source patch");
	using Edge=std::pair<std::uint32_t,std::uint32_t>;
	auto key=[](std::uint32_t a,std::uint32_t b) { return Edge{std::min(a,b),std::max(a,b)}; };
	std::set<Edge> protectedSet;
	for (auto e:protectedEdges) {
		require(e[0]<s.vertices.size() && e[1]<s.vertices.size() && e[0]!=e[1], "Invalid protected edge");
		protectedSet.insert(key(e[0],e[1]));
	}
	struct Use { std::uint32_t face,from,to,opposite; };
	std::map<Edge,std::vector<Use>> uses;
	std::set<std::uint32_t> faces;
	for (auto f:patch.triangles) {
		require(f<s.triangles.size() && faces.insert(f).second, "Invalid or repeated patch triangle");
		const auto &t=s.triangles[f];
		for (int i=0;i<3;++i) uses[key(t[i],t[(i+1)%3])].push_back({f,t[i],t[(i+1)%3],t[(i+2)%3]});
	}
	std::map<std::uint32_t,std::vector<std::uint32_t>> adjacent;
	std::map<std::uint32_t,PatchBoundaryEdge> outgoing;
	std::map<std::uint32_t,unsigned> incoming;
	for (const auto &[edge,incidents]:uses) {
		require(incidents.size()<=2, "Non-manifold patch edge");
		if (incidents.size()==2) {
			const auto &a=incidents[0],&b=incidents[1];
			require(!protectedSet.count(edge), "Protected edge lies inside the patch");
			require(a.from==b.to && a.to==b.from, "Inconsistent patch winding");
			require(affineMatch(s,a.face,b.opposite) && affineMatch(s,b.face,a.opposite), "Incompatible interior patch edge");
			adjacent[a.face].push_back(b.face); adjacent[b.face].push_back(a.face);
		} else {
			const auto &a=incidents[0];
			require(outgoing.emplace(a.from,PatchBoundaryEdge{a.from,a.to,protectedSet.count(edge)!=0}).second,
				"Branching patch boundary");
			require(++incoming[a.to]==1, "Branching patch boundary");
		}
	}
	std::set<std::uint32_t> visitedFaces; std::vector<std::uint32_t> pending{*faces.begin()};
	while (!pending.empty()) {
		auto f=pending.back(); pending.pop_back();
		if (!visitedFaces.insert(f).second) continue;
		for (auto n:adjacent[f]) pending.push_back(n);
	}
	require(visitedFaces.size()==faces.size(), "Disconnected patch triangles");
	require(!outgoing.empty() && outgoing.size()==incoming.size(), "Missing or open patch boundary");
	for (const auto &[vertex,edge]:outgoing) { (void)edge; require(incoming.count(vertex)==1, "Open patch boundary"); }
	PatchBoundary result;
	while (!outgoing.empty()) {
		auto start=outgoing.begin()->first, current=start;
		std::vector<PatchBoundaryEdge> loop;
		do {
			auto it=outgoing.find(current); require(it!=outgoing.end(), "Open or intersecting patch boundary");
			loop.push_back(it->second); current=it->second.to; outgoing.erase(it);
		} while (current!=start);
		require(loop.size()>=3, "Degenerate patch boundary loop");
		result.loops.push_back(std::move(loop));
	}
	// Patches currently admitted by identifySourcePatches are coplanar. Project
	// onto their most stable coordinate plane and reject crossing or touching
	// non-adjacent boundaries, including intersections between separate loops.
	const auto &t=s.triangles[*faces.begin()];
	const auto &p=s.vertices[t[0]].position,&q=s.vertices[t[1]].position,&r=s.vertices[t[2]].position;
	double u[3],v[3],n[3];
	for (int i=0;i<3;++i) { u[i]=double(q[i])-p[i]; v[i]=double(r[i])-p[i]; }
	for (int i=0;i<3;++i) n[i]=u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3];
	int drop=0; for (int i=1;i<3;++i) if (std::abs(n[i])>std::abs(n[drop])) drop=i;
	int x=(drop+1)%3,y=(drop+2)%3;
	auto orient=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c) {
		const auto &pa=s.vertices[a].position,&pb=s.vertices[b].position,&pc=s.vertices[c].position;
		return (double(pb[x])-pa[x])*(double(pc[y])-pa[y])-(double(pb[y])-pa[y])*(double(pc[x])-pa[x]);
	};
	for (size_t li=0;li<result.loops.size();++li) for (size_t i=0;i<result.loops[li].size();++i) {
		const auto &a=result.loops[li][i];
		for (size_t lj=li;lj<result.loops.size();++lj) for (size_t j=(lj==li ? i+1 : 0);j<result.loops[lj].size();++j) {
			const auto &b=result.loops[lj][j];
			if (li==lj && (a.to==b.from || b.to==a.from)) continue;
			const auto &a0=s.vertices[a.from].position,&a1=s.vertices[a.to].position;
			const auto &b0=s.vertices[b.from].position,&b1=s.vertices[b.to].position;
			if (std::max(double(a0[x]),double(a1[x])) < std::min(double(b0[x]),double(b1[x])) ||
				std::max(double(b0[x]),double(b1[x])) < std::min(double(a0[x]),double(a1[x])) ||
				std::max(double(a0[y]),double(a1[y])) < std::min(double(b0[y]),double(b1[y])) ||
				std::max(double(b0[y]),double(b1[y])) < std::min(double(a0[y]),double(a1[y]))) continue;
			double o1=orient(a.from,a.to,b.from),o2=orient(a.from,a.to,b.to);
			double o3=orient(b.from,b.to,a.from),o4=orient(b.from,b.to,a.to);
			require(((o1>0 && o2>0) || (o1<0 && o2<0) || (o3>0 && o4>0) || (o3<0 && o4<0)),
				"Intersecting patch boundary loops");
		}
	}
	return result;
}

void validatePatchDomain(const Mesh &mesh,const SourcePatch &patch,
	const std::vector<std::array<std::uint32_t,2>> &protectedEdges)
{
	(void)recoverPatchBoundary(mesh,patch,protectedEdges);
	Mesh captured=captureSource(mesh); const auto &s=*captured.source;
	const auto &first=s.triangles[patch.triangles[0]];
	const auto &p=s.vertices[first[0]].position, &q=s.vertices[first[1]].position, &r=s.vertices[first[2]].position;
	double u[3],v[3],n[3];
	for (int i=0;i<3;++i) { u[i]=double(q[i])-p[i]; v[i]=double(r[i])-p[i]; }
	for (int i=0;i<3;++i) n[i]=u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3];
	int drop=0; for (int i=1;i<3;++i) if (std::abs(n[i])>std::abs(n[drop])) drop=i;
	int x=(drop+1)%3,y=(drop+2)%3;
	auto orient=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c) {
		const auto &aa=s.vertices[a].position,&bb=s.vertices[b].position,&cc=s.vertices[c].position;
		return (double(bb[x])-aa[x])*(double(cc[y])-aa[y])-(double(bb[y])-aa[y])*(double(cc[x])-aa[x]);
	};
	const double firstArea=orient(first[0],first[1],first[2]);
	for (auto f:patch.triangles) {
		const auto &t=s.triangles[f];
		for (auto i:t) { Barycentric w; require(sourceWeights(s,patch.triangles[0],s.vertices[i].position,w), "Nonplanar patch domain"); }
		require(orient(t[0],t[1],t[2])*firstArea>0, "Folded or degenerate patch triangle");
	}
	for (size_t a=0;a<patch.triangles.size();++a) for (size_t b=a+1;b<patch.triangles.size();++b) {
		const auto &ta=s.triangles[patch.triangles[a]], &tb=s.triangles[patch.triangles[b]];
		for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
			auto a0=ta[i],a1=ta[(i+1)%3],b0=tb[j],b1=tb[(j+1)%3];
			if ((a0==b0 && a1==b1) || (a0==b1 && a1==b0)) continue;
			bool shared=a0==b0 || a0==b1 || a1==b0 || a1==b1;
			double o1=orient(a0,a1,b0),o2=orient(a0,a1,b1),o3=orient(b0,b1,a0),o4=orient(b0,b1,a1);
			if (shared) {
				if (o1==0 && o2==0) {
					auto common=(a0==b0 || a0==b1) ? a0 : a1;
					auto ao=(a0==common ? a1 : a0),bo=(b0==common ? b1 : b0);
					const auto &c=s.vertices[common].position,&aa=s.vertices[ao].position,&bb=s.vertices[bo].position;
					double dot=(double(aa[x])-c[x])*(double(bb[x])-c[x])+(double(aa[y])-c[y])*(double(bb[y])-c[y]);
					require(dot<=0, "Overlapping patch edges");
				}
				continue;
			}
			const auto &aa=s.vertices[a0].position,&ab=s.vertices[a1].position,&ba=s.vertices[b0].position,&bb=s.vertices[b1].position;
			bool separated=std::max(double(aa[x]),double(ab[x]))<std::min(double(ba[x]),double(bb[x])) ||
				std::max(double(ba[x]),double(bb[x]))<std::min(double(aa[x]),double(ab[x])) ||
				std::max(double(aa[y]),double(ab[y]))<std::min(double(ba[y]),double(bb[y])) ||
				std::max(double(ba[y]),double(bb[y]))<std::min(double(aa[y]),double(ab[y]));
			bool sameSide=(o1>0 && o2>0)||(o1<0 && o2<0)||(o3>0 && o4>0)||(o3<0 && o4<0);
			require(separated || sameSide, "Intersecting patch triangles");
		}
		auto inside=[&](std::uint32_t point,const Triangle &t) {
			double a1=orient(t[0],t[1],point),a2=orient(t[1],t[2],point),a3=orient(t[2],t[0],point);
			return (a1>0 && a2>0 && a3>0)||(a1<0 && a2<0 && a3<0);
		};
		for (auto i:ta) require(!inside(i,tb), "Overlapping patch interiors");
		for (auto i:tb) require(!inside(i,ta), "Overlapping patch interiors");
	}
}

namespace {
bool locateInSource(const SourceGeometry &s,const SourcePatch &patch,Vec3 point,SourceSurfaceLocation &location)
{
	require(finite(point), "Non-finite surface query");
	bool found=false,interior=false; SourceSurfaceLocation result;
	for (auto face:patch.triangles) {
		require(face<s.triangles.size(), "Invalid patch triangle"); Barycentric w;
		if (!sourceWeights(s,face,point,w) || *std::min_element(w.begin(),w.end()) < -1e-4) continue;
		bool inside=*std::min_element(w.begin(),w.end())>1e-6;
		double total=0; for (double &v:w) { v=std::max(0.0,v); total+=v; }
		if (!(total>0)) continue;
		for (double &v:w) v/=total;
		// Float positions at large world coordinates can round a point just
		// outside an edge. Accept clamping only when the reconstructed source
		// position remains within the same geometric tolerance as verification.
		const auto &tri=s.triangles[face]; double distance=0,extent=0;
		for (int j=0;j<3;++j) {
			double value=0; for (int k=0;k<3;++k) value+=w[k]*s.vertices[tri[k]].position[j];
			distance+=(value-point[j])*(value-point[j]);
			for (int k=0;k<3;++k) extent=std::max(extent,std::abs(double(s.vertices[tri[k]].position[j])-s.vertices[tri[(k+1)%3]].position[j]));
		}
		double tolerance=std::max(1.0,extent)*1e-6;
		if (distance>tolerance*tolerance) continue;
		if (found) {
			if (inside || interior) return false;
			const auto &a=s.triangles[result.triangle],&b=s.triangles[face];
			unsigned shared=0; for (auto i:a) for (auto j:b) if (i==j) ++shared;
			if (shared!=2) return false;
			for (auto i:b) if (std::find(a.begin(),a.end(),i)==a.end() && !affineMatch(s,result.triangle,i)) return false;
			for (auto i:a) if (std::find(b.begin(),b.end(),i)==b.end() && !affineMatch(s,face,i)) return false;
		}
		if (!found || face<result.triangle) result={face,w};
		found=true; interior=interior || inside;
	}
	if (found) location=result;
	return found;
}
}

bool locateOnSourcePatch(const Mesh &mesh,const SourcePatch &patch,Vec3 point,SourceSurfaceLocation &location)
{
	Mesh captured=captureSource(mesh);
	return locateInSource(*captured.source,patch,point,location);
}

bool remeshFourEdgePatch(const Mesh &mesh,const SourcePatch &patch,unsigned divisions,
	PatchRemesh &output,const std::vector<std::array<std::uint32_t,2>> &protectedEdges,std::string *rejectionReason)
{
	auto reject=[&](const char *reason) { if (rejectionReason) *rejectionReason=reason; return false; };
	if (rejectionReason) rejectionReason->clear();
	if (!divisions || divisions>8) return reject("Unsupported lattice divisions (supported: 1 to 8)");
	try {
		validatePatchDomain(mesh,patch,protectedEdges);
		auto boundary=recoverPatchBoundary(mesh,patch,protectedEdges);
		if (boundary.loops.size()!=1 || boundary.loops[0].size()!=4) return reject("Four-edge boundary required");
		Mesh captured=captureSource(mesh); const auto &s=*captured.source;
		const auto &loop=boundary.loops[0];
		const auto &ref=s.triangles[patch.triangles[0]];
		auto cross=[](const Vec3 &a,const Vec3 &b,const Vec3 &c) {
			std::array<double,3> u{},v{},n{};
			for (int i=0;i<3;++i) { u[i]=double(b[i])-a[i]; v[i]=double(c[i])-a[i]; }
			for (int i=0;i<3;++i) n[i]=u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3];
			return n;
		};
		auto normal=cross(s.vertices[ref[0]].position,s.vertices[ref[1]].position,s.vertices[ref[2]].position);
		auto positive=[&](const Vec3 &a,const Vec3 &b,const Vec3 &c) {
			auto n=cross(a,b,c); double dot=0; for (int i=0;i<3;++i) dot+=n[i]*normal[i]; return dot>1e-12;
		};
		Vec3 corners[4]; for (int i=0;i<4;++i) corners[i]=s.vertices[loop[size_t(i)].from].position;
		for (int i=0;i<4;++i) if (!positive(corners[i],corners[(i+1)%4],corners[(i+2)%4])) return reject("Boundary is not strictly convex");
		PatchRemesh candidate; candidate.sourcePatch=patch; size_t width=size_t(divisions)+1;
		candidate.vertices.reserve(width*width); candidate.sourceLocations.reserve(width*width);
		for (unsigned y=0;y<=divisions;++y) for (unsigned x=0;x<=divisions;++x) {
			double u=double(x)/divisions,v=double(y)/divisions;
			double w[4]={(1-u)*(1-v),u*(1-v),u*v,(1-u)*v}; Vec3 p{};
			for (int j=0;j<3;++j) { double value=0; for (int k=0;k<4;++k) value+=w[k]*corners[k][j]; p[j]=float(value); }
			SourceSurfaceLocation location;
			if (!locateOnSourcePatch(captured,patch,p,location)) return reject("Generated vertex has no unique source location");
			Vertex vertex; vertex.position=p; vertex.normal={0,0,0}; vertex.uv={0,0}; vertex.color={0,0,0,0};
			const auto &source=s.triangles[location.triangle];
			for (int k=0;k<3;++k) {
				const auto &sv=s.vertices[source[k]]; double b=location.barycentric[k];
				for (int j=0;j<3;++j) vertex.normal[j]+=float(b*sv.normal[j]);
				for (int j=0;j<2;++j) vertex.uv[j]+=float(b*sv.uv[j]);
				for (int j=0;j<4;++j) vertex.color[j]+=float(b*sv.color[j]);
			}
			vertex.boundary=(x==0 || y==0 || x==divisions || y==divisions);
			candidate.vertices.push_back(vertex); candidate.sourceLocations.push_back(location);
		}
		for (unsigned y=0;y<divisions;++y) for (unsigned x=0;x<divisions;++x) {
			auto a=std::uint32_t(size_t(y)*width+x),b=a+1,d=std::uint32_t(size_t(y+1)*width+x),c=d+1;
			candidate.triangles.push_back({a,b,c}); candidate.triangles.push_back({a,c,d});
		}
		for (const auto &t:candidate.triangles) {
			if (!positive(candidate.vertices[t[0]].position,candidate.vertices[t[1]].position,candidate.vertices[t[2]].position)) return reject("Candidate has an inverted or degenerate face");
			// Compare the new face's interpolated UVs to authoritative original
			// UVs at the centroid and edge midpoints before accepting it.
			for (const auto &weights:std::array<std::array<double,3>,4>{{{{1.0/3,1.0/3,1.0/3}},{{0.5,0.5,0}},{{0,0.5,0.5}},{{0.5,0,0.5}}}}) {
				Vec3 p{},normalSample{}; Vec2 uv{}; Color colorSample{};
				for (int k=0;k<3;++k) {
					const auto &vert=candidate.vertices[t[k]];
					for (int j=0;j<3;++j) { p[j]+=float(weights[k]*vert.position[j]); normalSample[j]+=float(weights[k]*vert.normal[j]); }
					for (int j=0;j<2;++j) uv[j]+=float(weights[k]*vert.uv[j]);
					for (int j=0;j<4;++j) colorSample[j]+=float(weights[k]*vert.color[j]);
				}
				SourceSurfaceLocation loc; if (!locateOnSourcePatch(captured,patch,p,loc)) return reject("Candidate sample has no unique source location");
				const auto &source=s.triangles[loc.triangle];
				for (int j=0;j<2;++j) {
					double expected=0; for (int k=0;k<3;++k) expected+=loc.barycentric[k]*s.vertices[source[k]].uv[j];
					if (std::abs(expected-uv[j])>1e-5*std::max(1.0,std::abs(expected))) return reject("Sampled UV correspondence failed");
				}
				for (int j=0;j<3;++j) {
					double position=0,n=0;
					for (int k=0;k<3;++k) { position+=loc.barycentric[k]*s.vertices[source[k]].position[j]; n+=loc.barycentric[k]*s.vertices[source[k]].normal[j]; }
					if (std::abs(position-p[j])>1e-5*std::max(1.0,std::abs(position)) ||
						std::abs(n-normalSample[j])>1e-5*std::max(1.0,std::abs(n))) return reject("Sampled position or normal correspondence failed");
				}
				for (int j=0;j<4;++j) {
					double expected=0; for (int k=0;k<3;++k) expected+=loc.barycentric[k]*s.vertices[source[k]].color[j];
					if (std::abs(expected-colorSample[j])>1e-5*std::max(1.0,std::abs(expected))) return reject("Sampled vertex-color correspondence failed");
				}
			}
		}
		output=std::move(candidate); return true;
	} catch (const std::invalid_argument &e) { if (rejectionReason) *rejectionReason=e.what(); return false; }
}

PatchRetopoReport verifyRetopologyCandidate(const Mesh &mesh,const SourcePatch &patch,unsigned divisions,
	const PatchRemesh &candidate,const std::vector<std::array<std::uint32_t,2>> &protectedEdges)
{
	PatchRetopoReport report; report.sourceFaces=patch.triangles.size();
	try {
		if (!divisions || divisions>8) { report.reason="Unsupported lattice divisions (supported: 1 to 8)"; return report; }
		auto boundary=recoverPatchBoundary(mesh,patch,protectedEdges);
		report.loops=boundary.loops.size();
		for (const auto &loop:boundary.loops) for (const auto &e:loop) {
			++report.boundaryEdges; if (e.protectedEdge) ++report.protectedEdges;
		}
		validatePatchDomain(mesh,patch,protectedEdges);
		if (boundary.loops.size()!=1) { report.reason="Multiple boundary loops or holes are not supported"; return report; }
		if (boundary.loops[0].size()!=4) { report.reason="Boundary does not have exactly four edges"; return report; }
		report.generatedVertices=candidate.vertices.size(); report.generatedTriangles=candidate.triangles.size();
		if (candidate.sourcePatch.triangles!=patch.triangles) { report.reason="Candidate source patch identity differs"; return report; }
		if (candidate.vertices.size()!=candidate.sourceLocations.size()) {
			report.reason="Missing generated vertex correspondence"; return report;
		}
		if (candidate.vertices.size()>65535 || candidate.triangles.size()>65535) {
			report.reason="Candidate exceeds NIF triangle or vertex limits"; return report;
		}
		std::vector<bool> used(candidate.vertices.size(),false);
		for (const auto &t:candidate.triangles) for (auto i:t) {
			if (i>=used.size()) { report.reason="Candidate index is out of range"; return report; }
			used[i]=true;
		}
		if (std::find(used.begin(),used.end(),false)!=used.end()) { report.reason="Candidate has unreferenced vertices"; return report; }
		Mesh original=captureSource(mesh); const auto &source=*original.source;
		std::set<std::uint32_t> faces(patch.triangles.begin(),patch.triangles.end());
		// Prove the strict coplanar case against one global affine field.
		// On each triangle an affine difference reaches its component extrema
		// at vertices, so bounding every source and candidate vertex also bounds
		// interpolation across triangles that cross the former internal edge.
		const auto reference=patch.triangles.front(); const auto &referenceFace=source.triangles[reference];
		auto globallyAffine=[&](const Vertex &v) {
			Barycentric weights; if (!sourceWeights(source,reference,v.position,weights)) return false;
			auto close=[](double a,double b) { return std::abs(a-b)<=1e-6*std::max({1.0,std::abs(a),std::abs(b)}); };
			for (int j=0;j<2;++j) { double value=0; for (int k=0;k<3;++k) value+=weights[k]*source.vertices[referenceFace[k]].uv[j]; if (!close(value,v.uv[j])) return false; }
			for (int j=0;j<3;++j) { double value=0; for (int k=0;k<3;++k) value+=weights[k]*source.vertices[referenceFace[k]].normal[j]; if (!close(value,v.normal[j])) return false; }
			for (int j=0;j<4;++j) { double value=0; for (int k=0;k<3;++k) value+=weights[k]*source.vertices[referenceFace[k]].color[j]; if (!close(value,v.color[j])) return false; }
			return true;
		};
		for (auto f:patch.triangles) for (auto i:source.triangles[f]) if (!globallyAffine(source.vertices[i])) {
			report.reason="Source attributes are not globally affine within tolerance"; return report;
		}
		for (size_t i=0;i<candidate.vertices.size();++i) {
			const auto &v=candidate.vertices[i]; const auto &loc=candidate.sourceLocations[i];
			if (!globallyAffine(v)) { report.reason="Candidate attributes are not globally affine within tolerance"; return report; }
			if (!faces.count(loc.triangle) || loc.triangle>=source.triangles.size()) {
				report.reason="Vertex correspondence lies outside source patch"; return report;
			}
			double sum=0; for (double w:loc.barycentric) {
				if (!std::isfinite(w) || w < -1e-8 || w > 1+1e-8) { report.reason="Invalid source barycentric coordinates"; return report; }
				sum+=w;
			}
			if (std::abs(sum-1)>1e-8) { report.reason="Source barycentric coordinates do not sum to one"; return report; }
			const auto &t=source.triangles[loc.triangle]; double deviation=0;
			for (int j=0;j<3;++j) {
				double position=0,normal=0;
				for (int k=0;k<3;++k) { position+=loc.barycentric[k]*source.vertices[t[k]].position[j]; normal+=loc.barycentric[k]*source.vertices[t[k]].normal[j]; }
				deviation+=(position-v.position[j])*(position-v.position[j]);
				if (std::abs(normal-v.normal[j])>1e-5*std::max(1.0,std::abs(normal))) { report.reason="Normal correspondence failed"; return report; }
			}
			report.maximumSourceDeviation=std::max(report.maximumSourceDeviation,std::sqrt(deviation));
			for (int j=0;j<2;++j) {
				double expected=0; for (int k=0;k<3;++k) expected+=loc.barycentric[k]*source.vertices[t[k]].uv[j];
				if (std::abs(expected-v.uv[j])>1e-5*std::max(1.0,std::abs(expected))) { report.reason="UV correspondence failed"; return report; }
			}
			for (int j=0;j<4;++j) {
				double expected=0; for (int k=0;k<3;++k) expected+=loc.barycentric[k]*source.vertices[t[k]].color[j];
				if (std::abs(expected-v.color[j])>1e-5*std::max(1.0,std::abs(expected))) { report.reason="Vertex color correspondence failed"; return report; }
			}
		}
		// Validate the complete candidate triangulation as a single coplanar,
		// affine domain, including all edges and triangle-pair intersections.
		Mesh candidateMesh; candidateMesh.vertices=candidate.vertices; candidateMesh.triangles=candidate.triangles;
		SourcePatch allFaces; for (std::uint32_t i=0;i<candidate.triangles.size();++i) allFaces.triangles.push_back(i);
		validatePatchDomain(candidateMesh,allFaces);
		auto newBoundary=recoverPatchBoundary(candidateMesh,allFaces);
		if (newBoundary.loops.size()!=1 || newBoundary.loops[0].size()!=size_t(divisions)*4) {
			report.reason="Candidate boundary topology differs"; return report;
		}
		// Every candidate edge must lie on one original boundary segment, with
		// positive orientation and no gaps; each original corner must occur.
		const auto &oldLoop=boundary.loops[0];
		std::array<double,4> covered{};
		std::set<std::pair<std::uint32_t,std::uint32_t>> expectedProtected;
		for (const auto &e:newBoundary.loops[0]) {
			const auto &a=candidate.vertices[e.from].position,&b=candidate.vertices[e.to].position;
			bool found=false;
			for (size_t j=0;j<4 && !found;++j) {
				const auto &p=source.vertices[oldLoop[j].from].position,&q=source.vertices[oldLoop[j].to].position;
				double len=0,pa=0,pb=0,da=0,db=0;
				for (int k=0;k<3;++k) { double d=double(q[k])-p[k]; len+=d*d; pa+=(double(a[k])-p[k])*d; pb+=(double(b[k])-p[k])*d; }
				if (!(len>0)) continue;
				pa/=len; pb/=len;
				for (int k=0;k<3;++k) { double d=double(q[k])-p[k]; da+=std::pow(double(a[k])-p[k]-pa*d,2); db+=std::pow(double(b[k])-p[k]-pb*d,2); }
				if (pa>=-1e-6 && pb<=1+1e-6 && pb>pa && da<=len*1e-12 && db<=len*1e-12) {
					covered[j]+=pb-pa; found=true;
					if (oldLoop[j].protectedEdge) expectedProtected.emplace(std::min(e.from,e.to),std::max(e.from,e.to));
				}
			}
			if (!found) { report.reason="Candidate boundary does not match the source"; return report; }
		}
		for (double length:covered) if (std::abs(length-1)>1e-5) { report.reason="Candidate boundary has a gap or overlap"; return report; }
		std::set<std::pair<std::uint32_t,std::uint32_t>> actualProtected;
		for (auto e:candidate.protectedBoundaryEdges)
			if (!actualProtected.emplace(std::min(e[0],e[1]),std::max(e[0],e[1])).second) {
				report.reason="Duplicate protected candidate edge"; return report;
			}
		if (actualProtected!=expectedProtected) { report.reason="Protected candidate edges differ from source"; return report; }
		// Conservative absolute scale relative to source patch extent.
		double extent=0;
		for (const auto &e:oldLoop) for (int j=0;j<3;++j) extent=std::max(extent,std::abs(double(source.vertices[e.to].position[j])-source.vertices[e.from].position[j]));
		if (report.maximumSourceDeviation>std::max(1.0,extent)*1e-6) { report.reason="Source surface deviation exceeds tolerance"; return report; }
		report.accepted=true; report.fallback=false; report.reason="Accepted"; return report;
	} catch (const std::invalid_argument &e) { report.reason=e.what(); return report; }
	catch (const std::length_error &e) { report.reason=e.what(); return report; }
}

PatchRetopoReport tryRetopologizePatch(const Mesh &mesh,const SourcePatch &patch,unsigned divisions,
	PatchRemesh &output,const std::vector<std::array<std::uint32_t,2>> &protectedEdges)
{
	PatchRetopoReport report; report.sourceFaces=patch.triangles.size();
	try {
		if (!divisions || divisions>8) { report.reason="Unsupported lattice divisions (supported: 1 to 8)"; return report; }
		auto boundary=recoverPatchBoundary(mesh,patch,protectedEdges); report.loops=boundary.loops.size();
		for (const auto &loop:boundary.loops) for (const auto &e:loop) { ++report.boundaryEdges; if (e.protectedEdge) ++report.protectedEdges; }
		validatePatchDomain(mesh,patch,protectedEdges);
		if (boundary.loops.size()!=1) { report.reason="Multiple boundary loops or holes are not supported"; return report; }
		if (boundary.loops[0].size()!=4) { report.reason="Boundary does not have exactly four edges"; return report; }
		PatchRemesh candidate;
		if (!remeshFourEdgePatch(mesh,patch,divisions,candidate,protectedEdges,&report.reason)) {
			return report;
		}
		// Grid order follows the four oriented source boundary chains.
		std::uint32_t width=divisions+1;
		for (unsigned i=0;i<divisions;++i) {
			if (boundary.loops[0][0].protectedEdge) candidate.protectedBoundaryEdges.push_back({i,i+1});
			if (boundary.loops[0][1].protectedEdge) candidate.protectedBoundaryEdges.push_back({i*width+divisions,(i+1)*width+divisions});
			if (boundary.loops[0][2].protectedEdge) candidate.protectedBoundaryEdges.push_back({divisions*width+divisions-i,divisions*width+divisions-i-1});
			if (boundary.loops[0][3].protectedEdge) candidate.protectedBoundaryEdges.push_back({(divisions-i)*width,(divisions-i-1)*width});
		}
		report=verifyRetopologyCandidate(mesh,patch,divisions,candidate,protectedEdges);
		if (report.accepted) output=std::move(candidate);
		return report;
	} catch (const std::invalid_argument &e) { report.reason=e.what(); return report; }
	catch (const std::length_error &e) { report.reason=e.what(); return report; }
}

Mesh retopologizeCoplanarPatches(const Mesh &mesh,std::vector<PatchRetopoReport> &reports,
	const std::vector<std::array<std::uint32_t,2>> &protectedEdges,Limits limits)
{
	Mesh original=captureSource(mesh,limits);
	require(!original.patchAuthority && original.triangles==original.source->triangles,
		"Patch retopology requires the original working shape");
	const auto patches=identifySourcePatches(original,protectedEdges);
	Mesh working=mesh; working.source.reset(); working.origins.clear(); working.patchAuthority.reset();
	auto authority=std::make_shared<PatchAuthority>(); authority->original=original.source; authority->patches=patches;
	authority->workingFacePatch.resize(working.triangles.size());
	reports.clear(); reports.reserve(patches.size());
	for (std::uint32_t pi=0;pi<patches.size();++pi) {
		const auto &patch=patches[pi];
		for (auto f:patch.triangles) authority->workingFacePatch.at(f)=pi;
		PatchRemesh candidate; auto report=tryRetopologizePatch(original,patch,1,candidate,protectedEdges);
		// Initial integration changes connectivity only. This preserves every
		// existing vertex and every boundary edge shared with fallback patches.
		if (report.accepted && candidate.triangles.size()==patch.triangles.size()) {
			std::vector<std::uint32_t> mapped; bool valid=true;
			std::set<std::uint32_t> sourceIndices;
			for (auto f:patch.triangles) for (auto i:original.source->triangles[f]) sourceIndices.insert(i);
			for (const auto &v:candidate.vertices) {
				std::uint32_t match=0; unsigned matches=0;
				for (auto i:sourceIndices) {
					const auto &sv=original.source->vertices[i];
					if (sv.position!=v.position || sv.uv!=v.uv) continue;
					bool same=true;
					for (int j=0;j<3;++j) same &= std::abs(double(sv.normal[j])-v.normal[j])<=1e-6;
					for (int j=0;j<4;++j) same &= std::abs(double(sv.color[j])-v.color[j])<=1e-6;
					if (same) { match=i; ++matches; }
				}
				if (matches!=1) { valid=false; break; }
				mapped.push_back(match);
			}
			if (valid) {
				std::set<Triangle> before,after;
				for (size_t i=0;i<patch.triangles.size();++i) {
					const auto &t=candidate.triangles[i];
					Triangle old=working.triangles[patch.triangles[i]],replacement{mapped.at(t[0]),mapped.at(t[1]),mapped.at(t[2])};
					auto oldKey=old,newKey=replacement; std::sort(oldKey.begin(),oldKey.end()); std::sort(newKey.begin(),newKey.end());
					before.insert(oldKey); after.insert(newKey); working.triangles[patch.triangles[i]]=replacement;
				}
				report.topologyChanged=before!=after;
			}
			if (!valid) { report.accepted=false; report.fallback=true; report.reason="Boundary vertex identity could not be preserved exactly"; }
		} else if (report.accepted) {
			report.accepted=false; report.fallback=true; report.reason="Patch would alter the working face count";
		}
		reports.push_back(std::move(report));
	}
	working=captureSource(working,limits); working.patchAuthority=std::move(authority);
	return working;
}

std::vector<SourceSample> resolvePatchSources(const Mesh &mesh,Limits limits)
{
	validate(mesh,limits);
	require(mesh.source && mesh.patchAuthority && mesh.patchAuthority->original && mesh.origins.size()==mesh.triangles.size()
		&& mesh.patchAuthority->workingFacePatch.size()==mesh.source->triangles.size(), "Missing patch-aware source correspondence");
	const auto &authority=*mesh.patchAuthority;
	std::vector<SourceSample> result(mesh.vertices.size()); std::vector<bool> seen(mesh.vertices.size(),false);
	for (size_t f=0;f<mesh.triangles.size();++f) {
		const auto &origin=mesh.origins[f];
		require(origin.triangle<mesh.source->triangles.size(), "Invalid working face correspondence");
		auto pi=authority.workingFacePatch[origin.triangle];
		require(pi<authority.patches.size(), "Invalid patch identity");
		const auto &base=mesh.source->triangles[origin.triangle];
		for (int corner=0;corner<3;++corner) {
			Vec3 point{};
			for (int j=0;j<3;++j) { double v=0; for (int k=0;k<3;++k) v+=origin.corners[corner][k]*mesh.source->vertices[base[k]].position[j]; point[j]=float(v); }
			SourceSurfaceLocation loc;
			if (!locateInSource(*authority.original,authority.patches[pi],point,loc))
				throw std::invalid_argument("Working vertex cannot be mapped to original patch " + std::to_string(pi)
					+ " (working face " + std::to_string(f) + ", corner " + std::to_string(corner) + ", position "
					+ std::to_string(point[0])+","+std::to_string(point[1])+","+std::to_string(point[2])+")");
			auto index=mesh.triangles[f][corner]; const auto &v=mesh.vertices[index]; const auto &t=authority.original->triangles[loc.triangle];
			for (int j=0;j<2;++j) {
				double uv=0; for (int k=0;k<3;++k) uv+=loc.barycentric[k]*authority.original->vertices[t[k]].uv[j];
				require(std::abs(uv-v.uv[j])<=1e-5*std::max(1.0,std::abs(uv)), "Working UV differs from original source");
			}
			for (int j=0;j<4;++j) {
				double color=0; for (int k=0;k<3;++k) color+=loc.barycentric[k]*authority.original->vertices[t[k]].color[j];
				require(std::abs(color-v.color[j])<=1e-5*std::max(1.0,std::abs(color)), "Working vertex color differs from original source");
			}
			Vec3 sourceNormal{};
			for (int j=0;j<3;++j) for (int k=0;k<3;++k)
				sourceNormal[j]+=float(loc.barycentric[k]*authority.original->vertices[t[k]].normal[j]);
			auto expectedNormal=normalized(sourceNormal),actualNormal=normalized(v.normal);
			for (int j=0;j<3;++j) require(std::abs(expectedNormal[j]-actualNormal[j])<=1e-5,
				"Working normal differs from original source");
			if (!seen[index] || pi<result[index].patch) result[index]={pi,loc};
			seen[index]=true;
		}
	}
	return result;
}

namespace {
Barycentric middleBary(const Barycentric &a, const Barycentric &b)
{
	return {(a[0]+b[0])*0.5, (a[1]+b[1])*0.5, (a[2]+b[2])*0.5};
}
Vertex sourceVertex(const SourceGeometry &source, std::uint32_t face, const Barycentric &weights, bool boundary)
{
	const auto &t = source.triangles.at(face);
	Vertex out;
	for (int c = 0; c < 3; ++c) {
		double p=0, n=0;
		for (int i = 0; i < 3; ++i) { p += weights[i]*source.vertices[t[i]].position[c]; n += weights[i]*source.vertices[t[i]].normal[c]; }
		out.position[c]=float(p); out.normal[c]=float(n);
	}
	out.normal = normalized(out.normal);
	for (int c = 0; c < 2; ++c) {
		double value=0; for (int i=0;i<3;++i) value += weights[i]*source.vertices[t[i]].uv[c];
		out.uv[c]=float(value);
	}
	for (int c = 0; c < 4; ++c) {
		double value=0; for (int i=0;i<3;++i) value += weights[i]*source.vertices[t[i]].color[c];
		out.color[c]=float(value);
	}
	out.boundary=boundary;
	return out;
}
}

void validate(const HeightField &field)
{
	require(field.width && field.height, "Empty height field");
	require(field.height <= std::numeric_limits<std::size_t>::max() / field.width,
		"Height dimensions overflow");
	require(field.width * field.height == field.values.size(), "Height dimensions do not match pixels");
	for (float h : field.values)
		require(std::isfinite(h), "Non-finite height sample");
}

HeightField areaResample(const HeightField &field, std::size_t width, std::size_t height)
{
	validate(field);
	require(width && height && height <= std::numeric_limits<std::size_t>::max() / width,
		"Invalid resampled height dimensions");
	HeightField out{width, height, std::vector<float>(width * height)};
	for (std::size_t y = 0; y < height; ++y) {
		double y0 = double(y) * field.height / height, y1 = double(y + 1) * field.height / height;
		for (std::size_t x = 0; x < width; ++x) {
			double x0 = double(x) * field.width / width, x1 = double(x + 1) * field.width / width;
			double total = 0, weight = 0;
			for (std::size_t sy = std::size_t(y0); sy < std::min(field.height, std::size_t(std::ceil(y1))); ++sy) {
				double wy = std::min(y1, double(sy + 1)) - std::max(y0, double(sy));
				for (std::size_t sx = std::size_t(x0); sx < std::min(field.width, std::size_t(std::ceil(x1))); ++sx) {
					double w = wy * (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)));
					total += field.values[sy * field.width + sx] * w; weight += w;
				}
			}
			out.values[y * width + x] = float(total / weight);
		}
	}
	return out;
}

float diffuseScalar(Color rgba)
{
	require(finite(rgba), "Non-finite diffuse sample");
	return float(0.2126 * double(rgba[0]) + 0.7152 * double(rgba[1]) + 0.0722 * double(rgba[2]));
}

float normalReliefCue(Color rgba)
{
	require(finite(rgba), "Non-finite normal sample");
	for (int i = 0; i < 3; ++i)
		require(rgba[i] >= 0 && rgba[i] <= 1, "Expected unsigned normal RGB");
	Vec3 n{rgba[0] * 2 - 1, rgba[1] * 2 - 1, rgba[2] * 2 - 1};
	n = normalized(n);
	// A directional cue only; kept separate so an integrator can replace it.
	return std::clamp(0.5f + (n[0] + n[1]) * 0.25f, 0.0f, 1.0f);
}

Color residualNormalSample(Color source,const NormalBasis &sourceBasis,const NormalBasis &displacedBasis)
{
	require(finite(source), "Non-finite normal sample");
	for (float v:source) require(v>=0 && v<=1, "Normal sample outside unsigned range");
	auto cross=[](const Vec3 &a,const Vec3 &b) -> Vec3 {
		return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
	};
	auto dot=[](const Vec3 &a,const Vec3 &b) -> double {
		return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];
	};
	auto basis=[&](const NormalBasis &input) {
		require(finite(input.bitangent) && finite(input.tangent) && finite(input.normal), "Non-finite normal basis");
		std::array<Vec3,3> b{normalized(input.bitangent),normalized(input.tangent),normalized(input.normal)};
		require(std::abs(dot(b[0],cross(b[1],b[2])))>1e-5, "Degenerate normal basis");
		return b;
	};
	auto old=basis(sourceBasis), current=basis(displacedBasis);
	Vec3 sample{source[0]*2-1,source[1]*2-1,source[2]*2-1};
	require(length(sample)>1e-12, "Zero normal sample");
	Vec3 world{};
	for (int j=0;j<3;++j) for (int i=0;i<3;++i) world[j]+=old[i][j]*sample[i];
	world=normalized(world);
	double determinant=dot(current[0],cross(current[1],current[2]));
	Vec3 residual{float(dot(world,cross(current[1],current[2]))/determinant),
		float(dot(world,cross(current[2],current[0]))/determinant),
		float(dot(world,cross(current[0],current[1]))/determinant)};
	residual=normalized(residual);
	return {std::clamp(residual[0]*0.5f+0.5f,0.0f,1.0f),
		std::clamp(residual[1]*0.5f+0.5f,0.0f,1.0f),
		std::clamp(residual[2]*0.5f+0.5f,0.0f,1.0f),source[3]};
}

float sample(const HeightField &field, Vec2 uv, Address u, Address v)
{
	validate(field);
	require(finite(uv), "Non-finite UV coordinates");
	return sampleValidated(field, uv[0], uv[1], u, v);
}

float sampleMapped(const HeightField &field, Vec2 uv, const Displacement &mapping)
{
	validate(field);
	require(finite(uv) && finite(mapping.uvScale) && finite(mapping.uvOffset), "Non-finite UV mapping");
	return sampleMappedValidated(field, uv[0], uv[1], mapping);
}

HeightField sampleMappedGrid(const HeightField &field, Vec2 minimum, Vec2 maximum,
	std::size_t width, std::size_t height, const Displacement &mapping)
{
	validate(field);
	require(finite(minimum) && finite(maximum) && finite(mapping.uvScale) && finite(mapping.uvOffset)
		&& minimum[0] < maximum[0] && minimum[1] < maximum[1]
		&& width > 0 && height > 0 && width <= 512 && height <= 512, "Invalid UV preview grid");
	HeightField output{width,height,{}};
	output.values.reserve(width*height);
	for (std::size_t y=0;y<height;++y) for (std::size_t x=0;x<width;++x) {
		double u=std::lerp(double(minimum[0]),double(maximum[0]),(double(x)+0.5)/double(width));
		double v=std::lerp(double(minimum[1]),double(maximum[1]),(double(y)+0.5)/double(height));
		output.values.push_back(sampleMappedValidated(field,u,v,mapping));
	}
	return output;
}

HeightField blend(const std::vector<Source> &sources, Blend mode)
{
	HeightField result;
	for (const auto &source : sources) {
		require(std::isfinite(source.weight) && source.weight >= 0 && source.weight <= 1
			&& std::isfinite(source.multiplier) && source.multiplier >= 0, "Invalid source weight/multiplier");
		if (!source.field || source.weight == 0)
			continue;
		validate(*source.field);
		bool first = result.values.empty();
		if (first) {
			result.width = source.field->width;
			result.height = source.field->height;
			result.values.resize(source.field->values.size());
		}
		require(result.width == source.field->width && result.height == source.field->height,
			"Height sources require matching dimensions");
		for (std::size_t i = 0; i < result.values.size(); ++i) {
			float value = source.field->values[i] * source.multiplier;
			float base = result.values[i];
			result.values[i] = first ? value * source.weight
				: std::lerp(base, combine(base, value, mode), source.weight);
		}
	}
	validate(result);
	return result;
}

HeightField condition(const HeightField &field, const Conditioning &s)
{
	validate(field);
	require(std::isfinite(s.black) && std::isfinite(s.white) && s.black < s.white
		&& std::isfinite(s.midpoint) && s.midpoint > 0 && s.midpoint < 1
		&& std::isfinite(s.contrast) && s.contrast >= 0
		&& std::isfinite(s.outputMin) && std::isfinite(s.outputMax) && s.outputMin <= s.outputMax,
		"Invalid height conditioning settings");
	HeightField result = field;
	double gamma = std::log(0.5) / std::log(double(s.midpoint));
	for (float &h : result.values) {
		double x = std::clamp((double(h) - s.black) / (double(s.white) - s.black), 0.0, 1.0);
		x = std::pow(x, gamma);
		x = std::clamp((x - 0.5) * s.contrast + 0.5, 0.0, 1.0);
		if (s.invert)
			x = 1 - x;
		h = float(std::lerp(double(s.outputMin), double(s.outputMax), x));
	}
	validate(result);
	return result;
}

namespace {
std::size_t pixelIndex(long long i, std::size_t size, Address address)
{
	long long n = static_cast<long long>(size);
	if (address == Address::Clamp)
		return static_cast<std::size_t>(std::clamp(i, 0LL, n - 1));
	i %= n;
	if (i < 0) i += n;
	return static_cast<std::size_t>(i);
}

HeightField gaussian(const HeightField &field, float sigma, Address u, Address v)
{
	if (sigma == 0) return field;
	int radius = int(std::ceil(3 * sigma));
	std::vector<double> weights(size_t(radius * 2 + 1));
	double sum = 0;
	for (int i = -radius; i <= radius; ++i) {
		double w = std::exp(-double(i * i) / (2 * double(sigma) * sigma));
		weights[size_t(i + radius)] = w;
		sum += w;
	}
	for (auto &w : weights) w /= sum;
	HeightField tmp = field, result = field;
	for (size_t y = 0; y < field.height; ++y) for (size_t x = 0; x < field.width; ++x) {
		double value = 0;
		for (int i = -radius; i <= radius; ++i)
			value += weights[size_t(i + radius)] * field.values[y * field.width + pixelIndex(static_cast<long long>(x) + i, field.width, u)];
		tmp.values[y * field.width + x] = float(value);
	}
	for (size_t y = 0; y < field.height; ++y) for (size_t x = 0; x < field.width; ++x) {
		double value = 0;
		for (int i = -radius; i <= radius; ++i)
			value += weights[size_t(i + radius)] * tmp.values[pixelIndex(static_cast<long long>(y) + i, field.height, v) * field.width + x];
		result.values[y * field.width + x] = float(value);
	}
	return result;
}
}

HeightField bilateral(const HeightField &field, float spatialSigma, float rangeSigma, Address u, Address v)
{
	validate(field);
	require(std::isfinite(spatialSigma) && spatialSigma >= 0 && spatialSigma <= 8
		&& std::isfinite(rangeSigma) && rangeSigma > 0, "Invalid bilateral smoothing settings");
	if (spatialSigma == 0) return field;
	int radius = int(std::ceil(2 * spatialSigma));
	std::vector<double> spatial(size_t((radius * 2 + 1) * (radius * 2 + 1)));
	for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx)
		spatial[size_t((dy + radius) * (radius * 2 + 1) + dx + radius)] =
			std::exp(-double(dx * dx + dy * dy) / (2 * double(spatialSigma) * spatialSigma));
	HeightField result = field;
	for (size_t y = 0; y < field.height; ++y) for (size_t x = 0; x < field.width; ++x) {
		double center = field.values[y * field.width + x], sum = 0, weighted = 0;
		for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
			size_t px = pixelIndex(static_cast<long long>(x) + dx, field.width, u);
			size_t py = pixelIndex(static_cast<long long>(y) + dy, field.height, v);
			double value = field.values[py * field.width + px], delta = value - center;
			double weight = spatial[size_t((dy + radius) * (radius * 2 + 1) + dx + radius)]
				* std::exp(-(delta * delta) / (2 * double(rangeSigma) * rangeSigma));
			weighted += weight * value; sum += weight;
		}
		result.values[y * field.width + x] = float(weighted / sum);
	}
	validate(result);
	return result;
}

HeightField frequencyBands(const HeightField &field, float fineSigma, float coarseSigma,
	float mediumGain, float fineGain, Address u, Address v)
{
	validate(field);
	require(std::isfinite(fineSigma) && fineSigma > 0 && std::isfinite(coarseSigma)
		&& coarseSigma > fineSigma && coarseSigma <= 16 && std::isfinite(mediumGain)
		&& mediumGain >= 0 && mediumGain <= 2 && std::isfinite(fineGain) && fineGain >= 0 && fineGain <= 2,
		"Invalid frequency band settings");
	if (mediumGain == 1 && fineGain == 1) return field;
	HeightField fine = gaussian(field, fineSigma, u, v);
	HeightField coarse = gaussian(field, coarseSigma, u, v);
	HeightField result = field;
	for (size_t i = 0; i < result.values.size(); ++i)
		result.values[i] = float(double(coarse.values[i]) + mediumGain * (double(fine.values[i]) - coarse.values[i])
			+ fineGain * (double(field.values[i]) - fine.values[i]));
	validate(result);
	return result;
}

HeightField shallowSurfaceDetail(const HeightField &field, float sigma, float neutral, Address u, Address v)
{
	validate(field);
	require(std::isfinite(sigma) && sigma>0 && sigma<=16 && std::isfinite(neutral), "Invalid shallow surface settings");
	auto broad=gaussian(field,sigma,u,v);
	HeightField result=field;
	for (size_t i=0;i<result.values.size();++i)
		result.values[i]=float(double(neutral)+double(field.values[i])-double(broad.values[i]));
	validate(result);
	return result;
}

std::array<std::size_t, 256> histogram(const HeightField &field)
{
	validate(field);
	std::array<std::size_t, 256> bins{};
	for (float h : field.values)
		++bins[std::size_t(std::clamp(h, 0.0f, 1.0f) * 255.0f)];
	return bins;
}

Mesh subdivide(const Mesh &mesh, unsigned depth, Limits limits)
{
	validate(mesh, limits);
	// Reject excessive depth before copying/allocating working arrays.
	std::size_t count = mesh.triangles.size();
	for (unsigned level = 0; level < depth; ++level) {
		if (count > limits.triangles / 4)
			throw std::length_error("Subdivision exceeds triangle budget");
		count *= 4;
	}
	Mesh result = captureSource(mesh, limits);
	for (unsigned level = 0; level < depth; ++level) {
		struct EdgeInfo { std::uint32_t index = 0, face = 0; Barycentric bary{}; unsigned uses = 0; };
		std::map<std::pair<std::uint32_t, std::uint32_t>, EdgeInfo> edges;
		auto edgeKey = [](std::uint32_t a, std::uint32_t b) {
			return std::make_pair(std::min(a, b), std::max(a, b));
		};
		for (size_t ti=0; ti<result.triangles.size(); ++ti) {
			const auto &t = result.triangles[ti];
			for (int i = 0; i < 3; ++i) {
				auto [it, inserted] = edges.emplace(edgeKey(t[i], t[(i + 1) % 3]), EdgeInfo{});
				if (inserted) { it->second.face = result.origins[ti].triangle; it->second.bary = middleBary(result.origins[ti].corners[i], result.origins[ti].corners[(i+1)%3]); }
				++it->second.uses;
				if (edges.size() > limits.vertices - result.vertices.size()
					|| edges.size() > std::numeric_limits<std::uint32_t>::max() - result.vertices.size())
					throw std::length_error("Subdivision exceeds vertex budget");
			}
		}
		result.vertices.reserve(result.vertices.size() + edges.size());
		for (auto &[key, info] : edges) {
			(void) key;
			Vertex v = sourceVertex(*result.source, info.face, info.bary, info.uses == 1);
			info.index = std::uint32_t(result.vertices.size());
			result.vertices.push_back(v);
		}
		std::vector<Triangle> triangles;
		std::vector<TriangleOrigin> origins;
		triangles.reserve(result.triangles.size() * 4);
		origins.reserve(result.triangles.size() * 4);
		for (size_t ti=0;ti<result.triangles.size();++ti) {
			const auto &t=result.triangles[ti]; const auto &o=result.origins[ti];
			auto ab = edges.at(edgeKey(t[0], t[1])).index;
			auto bc = edges.at(edgeKey(t[1], t[2])).index;
			auto ca = edges.at(edgeKey(t[2], t[0])).index;
			auto mab=middleBary(o.corners[0],o.corners[1]), mbc=middleBary(o.corners[1],o.corners[2]), mca=middleBary(o.corners[2],o.corners[0]);
			triangles.push_back({t[0], ab, ca});
			triangles.push_back({ab, t[1], bc});
			triangles.push_back({ca, bc, t[2]});
			triangles.push_back({ab, bc, ca});
			origins.push_back({o.triangle,{o.corners[0],mab,mca}});
			origins.push_back({o.triangle,{mab,o.corners[1],mbc}});
			origins.push_back({o.triangle,{mca,mbc,o.corners[2]}});
			origins.push_back({o.triangle,{mab,mbc,mca}});
		}
		result.triangles = std::move(triangles);
		result.origins = std::move(origins);
		validate(result, limits);
	}
	return result;
}

Mesh subdivideAdaptive(const Mesh &mesh, const HeightField &field, unsigned depth,
	float threshold, const Displacement &mapping, Limits limits)
{
	validate(mesh, limits);
	validate(field);
	require(depth <= 16 && std::isfinite(threshold) && threshold > 0
		&& finite(mapping.uvScale) && finite(mapping.uvOffset), "Invalid adaptive subdivision settings");
	if (mapping.profileTarget) { validate(*mapping.profileTarget); require(std::isfinite(mapping.strength) && std::isfinite(mapping.neutral),"Invalid profile sampling settings"); }
	Mesh result = captureSource(mesh, limits);
	using Edge = std::pair<std::uint32_t, std::uint32_t>;
	auto key = [](std::uint32_t a, std::uint32_t b) { return std::make_pair(std::min(a,b), std::max(a,b)); };
	auto heightAt = [&field, &mapping](double u, double v) {
		if (!mapping.profileTarget) return double(sampleMappedValidated(field,u,v,mapping));
		return mapping.strength!=0 ? mapping.neutral+requestedDistance(field,u,v,mapping)/mapping.strength : double(mapping.neutral);
	};
	for (unsigned level = 0; level < depth; ++level) {
		struct EdgeInfo { bool marked=false; unsigned uses=0; std::uint32_t face=0, index=0; Barycentric bary{}; };
		std::map<Edge, EdgeInfo> edges;
		for (size_t ti=0;ti<result.triangles.size();++ti) {
			const auto &t=result.triangles[ti]; const auto &o=result.origins[ti];
			for (int i=0;i<3;++i) {
				auto [it, inserted]=edges.emplace(key(t[i],t[(i+1)%3]),EdgeInfo{});
				if (inserted) { it->second.face=o.triangle; it->second.bary=middleBary(o.corners[i],o.corners[(i+1)%3]); }
				++it->second.uses;
			}
		}
		// Sample residual from the height plane representable by each triangle.
		// Residual is itself a scale-dependent curvature cue: an affine ramp
		// produces zero error even when its gradient is large. Modest gradient
		// and preview-silhouette weights only amplify an existing residual.
		for (const auto &t : result.triangles) {
			std::array<double,3> h;
			for (int i=0;i<3;++i) { const auto &uv=result.vertices[t[i]].uv; h[i]=heightAt(uv[0],uv[1]); }
			const auto &p=result.vertices[t[0]].position, &q=result.vertices[t[1]].position, &r=result.vertices[t[2]].position;
			double a[3],b[3],n[3];
			for (int i=0;i<3;++i) { a[i]=double(q[i])-p[i]; b[i]=double(r[i])-p[i]; }
			for (int i=0;i<3;++i) n[i]=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];
			double normalLength=std::hypot(n[0],n[1],n[2]);
			// This fixed direction matches the dialog's current orthographic projection.
			double facing=std::abs(n[0]+n[1]+0.9*n[2])/(normalLength*std::hypot(1.0,1.0,0.9));
			double multiplier=1+0.25*std::min(1.0,*std::max_element(h.begin(),h.end())-*std::min_element(h.begin(),h.end()))
				+0.25*(1-std::clamp(facing,0.0,1.0));
			auto error = [&](double w0,double w1,double w2) {
				double w[3]={w0,w1,w2}, u=0,v=0,predicted=0;
				for (int i=0;i<3;++i) { const auto &uv=result.vertices[t[i]].uv; u+=w[i]*uv[0]; v+=w[i]*uv[1]; predicted+=w[i]*h[i]; }
				return std::abs(heightAt(u,v)-predicted)*multiplier;
			};
			bool significant=false;
			for (int i=0;i<3;++i) {
				// A topological boundary remains pinned along its full length,
				// including new midpoint vertices. Texture variation on that edge
				// cannot be expressed there and should not drive subdivision.
				if (mapping.pinBoundaries && edges.at(key(t[i],t[(i+1)%3])).uses==1)
					continue;
				double maximum=0;
				for (double fraction : {0.25,0.5,0.75}) {
					double w[3]={0,0,0}; w[i]=1-fraction; w[(i+1)%3]=fraction;
					maximum=std::max(maximum,error(w[0],w[1],w[2]));
				}
				if (maximum>threshold) significant=true;
			}
			double interior=std::max({error(1.0/3,1.0/3,1.0/3),error(0.5,0.25,0.25),
				error(0.25,0.5,0.25),error(0.25,0.25,0.5)});
			// Split all three sides of a significant face. Neighbours still
			// share midpoint indices, but avoid creating needles inside the
			// area where relief matters most.
			if (significant || interior > threshold)
				for (int i=0;i<3;++i) edges.at(key(t[i],t[(i+1)%3])).marked=true;
		}
		std::map<Edge, EdgeInfo> midpointIndices;
		for (const auto &[edge, info] : edges) if (info.marked) midpointIndices.emplace(edge, info);
		if (midpointIndices.empty()) break;
		if (midpointIndices.size() > limits.vertices - result.vertices.size()
			|| midpointIndices.size() > std::numeric_limits<std::uint32_t>::max() - result.vertices.size())
			throw std::length_error("Adaptive subdivision exceeds vertex budget");
		size_t triangleCount = result.triangles.size();
		for (const auto &t : result.triangles) for (int i = 0; i < 3; ++i) {
			if (midpointIndices.count(key(t[i], t[(i+1)%3]))) {
				if (triangleCount >= limits.triangles) throw std::length_error("Adaptive subdivision exceeds triangle budget");
				++triangleCount;
			}
		}
		result.vertices.reserve(result.vertices.size() + midpointIndices.size());
		for (auto &[edge, info] : midpointIndices) {
			(void) edge;
			Vertex v=sourceVertex(*result.source, info.face, info.bary, info.uses==1);
			info.index = std::uint32_t(result.vertices.size());
			result.vertices.push_back(v);
		}
		std::vector<Triangle> triangles;
		std::vector<TriangleOrigin> origins;
		triangles.reserve(triangleCount);
		origins.reserve(triangleCount);
		for (size_t ti=0;ti<result.triangles.size();++ti) {
			const auto &t=result.triangles[ti]; const auto &origin=result.origins[ti];
			auto ab=midpointIndices.find(key(t[0],t[1])), bc=midpointIndices.find(key(t[1],t[2])), ca=midpointIndices.find(key(t[2],t[0]));
			if (ab!=midpointIndices.end() && bc!=midpointIndices.end() && ca!=midpointIndices.end()) {
				auto a=ab->second.index, b=bc->second.index, c=ca->second.index;
				auto mab=middleBary(origin.corners[0],origin.corners[1]), mbc=middleBary(origin.corners[1],origin.corners[2]), mca=middleBary(origin.corners[2],origin.corners[0]);
				triangles.insert(triangles.end(),{{t[0],a,c},{a,t[1],b},{c,b,t[2]},{a,b,c}});
				origins.push_back({origin.triangle,{origin.corners[0],mab,mca}});
				origins.push_back({origin.triangle,{mab,origin.corners[1],mbc}});
				origins.push_back({origin.triangle,{mca,mbc,origin.corners[2]}});
				origins.push_back({origin.triangle,{mab,mbc,mca}});
				continue;
			}
			struct Piece { Triangle triangle; TriangleOrigin origin; };
			std::vector<Piece> pieces{{t,origin}};
			for (int e = 0; e < 3; ++e) {
				auto found = midpointIndices.find(key(t[e], t[(e+1)%3]));
				if (found == midpointIndices.end()) continue;
				for (size_t j = 0; j < pieces.size(); ++j) {
					bool done = false;
					for (int i = 0; i < 3; ++i) {
						auto a = pieces[j].triangle[i], b = pieces[j].triangle[(i+1)%3], c = pieces[j].triangle[(i+2)%3];
						if (key(a,b) == found->first) {
							auto old=pieces[j].origin;
							auto m=middleBary(old.corners[i],old.corners[(i+1)%3]);
							auto mid=found->second.index;
							pieces[j] = {{a,mid,c},{old.triangle,{old.corners[i],m,old.corners[(i+2)%3]}}};
							pieces.push_back({{mid,b,c},{old.triangle,{m,old.corners[(i+1)%3],old.corners[(i+2)%3]}}});
							done = true; break;
						}
					}
					if (done) break;
				}
			}
			for (const auto &piece : pieces) { triangles.push_back(piece.triangle); origins.push_back(piece.origin); }
		}
		result.triangles = std::move(triangles);
		result.origins = std::move(origins);
		validate(result, limits);
	}
	return result;
}

HeightField darkFeatureAttraction(const HeightField &field,float sigma,Address u,Address v)
{
	validate(field);
	require(std::isfinite(sigma) && sigma>=0 && sigma<=64,"Invalid dark feature detector scale");
	auto out=gaussian(field,sigma,u,v);
	for (size_t i=0;i<out.values.size();++i) out.values[i]=std::max(0.0f,out.values[i]-field.values[i]);
	return out;
}

const SurfaceProfileParams &surfaceProfileParams(SurfaceProfile profile)
{
	// One response operator; these are priors, not physical material constants.
	static constexpr std::array<SurfaceProfileParams,6> parameters{{
		{0,0,0, 0,0,0,0},             // Neutral: analysis bypassed by the wrapper.
		{0,0,0.65, 4,4,4,0},         // Rock: retain the established response.
		{0,0.55,0.25, 4,5,4,0.10},   // Wood: local orientation, not a global grain.
		{0.40,0.45,0.10, 2,10,4,0.12}, // Masonry: broad planarity, joint retention.
		{0.70,0.20,0.05, 2,2,2,0.65}, // Metal: conservative broad and incoherent fine relief.
		{0,0,0, 0,0,0,0}              // Soil: expressly admit isotropic/irregular evidence.
	}};
	auto index=static_cast<size_t>(profile); require(index<parameters.size(),"Unknown surface profile");
	return parameters[index];
}

namespace {
HeightField orientationCoherence(const HeightField &field,float sigma,Address u,Address v)
{
	HeightField xx=field,xy=field,yy=field;
	double step=128.0/field.width;
	auto at=[&](long long x,long long y) { return double(field.values[pixelIndex(y,field.height,v)*field.width+pixelIndex(x,field.width,u)]); };
	for (size_t y=0;y<field.height;++y) for (size_t x=0;x<field.width;++x) {
		auto a=static_cast<long long>(x),b=static_cast<long long>(y); size_t i=y*field.width+x;
		double gx=(at(a+1,b)-at(a-1,b))/(2*step),gy=(at(a,b+1)-at(a,b-1))/(2*step);
		xx.values[i]=float(gx*gx); xy.values[i]=float(gx*gy); yy.values[i]=float(gy*gy);
	}
	xx=gaussian(xx,sigma,u,v); xy=gaussian(xy,sigma,u,v); yy=gaussian(yy,sigma,u,v);
	HeightField out=field;
	for (size_t i=0;i<out.values.size();++i) {
		double trace=double(xx.values[i])+yy.values[i];
		out.values[i]=float(std::clamp(std::hypot(double(xx.values[i])-yy.values[i],2.0*xy.values[i])/(trace+1e-30),0.0,1.0));
	}
	return out;
}

SurfacePrior signedSurfaceField(const HeightField &height,float neutral,float strength)
{
	validate(height); require(std::isfinite(neutral) && std::isfinite(strength),"Invalid surface prior settings");
	SurfacePrior result; result.original=height;
	for (auto &d:result.original.values) { d=(d-neutral)*strength; require(std::isfinite(d),"Displacement overflow in surface prior"); result.originalRms+=double(d)*d; }
	result.originalRms=std::sqrt(result.originalRms/height.values.size()); result.target=result.original;
	auto originalRange=std::minmax_element(result.original.values.begin(),result.original.values.end());
	result.originalMin=result.targetMin=*originalRange.first; result.originalMax=result.targetMax=*originalRange.second;
	result.targetRms=result.originalRms;
	return result;
}
}

SurfacePrior analyzeSurfaceMorphology(const HeightField &height,float neutral,float strength,Address u,Address v)
{
	auto result=signedSurfaceField(height,neutral,strength);
	double amplitude=0; for (auto d:result.original.values) amplitude=std::max(amplitude,std::abs(double(d)));
	// All scales are normalized to the 128-pixel reference texture, never to
	// generated edge length, triangle count, or tessellation depth.
	float scale=float(height.width)/128.0f;
	require(height.width==height.height && height.width<=4096,"Surface morphology requires a bounded square preview field");
	result.broad=gaussian(result.original,8*scale,u,v); result.fine=result.original;
	auto small=gaussian(result.original,2*scale,u,v);
	result.ridge=result.valley=result.coherence=result.anisotropy=result.suppression=height;
	result.bumpBowl=height;
	result.coherence=orientationCoherence(result.broad,8*scale,u,v);
	result.fineCoherence=orientationCoherence(result.original,2*scale,u,v);
	auto at=[&](const HeightField &field,long long x,long long y) { return double(field.values[pixelIndex(y,field.height,v)*field.width+pixelIndex(x,field.width,u)]); };
	double step=128.0/height.width;
	for (size_t i=0;i<height.values.size();++i) {
		result.fine.values[i]-=result.broad.values[i];
		result.ridge.values[i]=std::max(0.0f,result.original.values[i]-small.values[i]);
		result.valley.values[i]=std::max(0.0f,small.values[i]-result.original.values[i]);
	}
	for (size_t y=0;y<height.height;++y) for (size_t x=0;x<height.width;++x) {
		size_t i=y*height.width+x; auto xx=static_cast<long long>(x),yy=static_cast<long long>(y);
		double center=result.broad.values[i];
		double hxx=(at(result.broad,xx+1,yy)-2*center+at(result.broad,xx-1,yy))/(step*step);
		double hyy=(at(result.broad,xx,yy+1)-2*center+at(result.broad,xx,yy-1))/(step*step);
		double hxy=(at(result.broad,xx+1,yy+1)-at(result.broad,xx+1,yy-1)-at(result.broad,xx-1,yy+1)+at(result.broad,xx-1,yy-1))/(4*step*step);
		double radius=std::hypot(hxx-hyy,2*hxy),l1=(hxx+hyy+radius)*0.5,l2=(hxx+hyy-radius)*0.5;
		double magnitude=std::abs(l1)+std::abs(l2);
		double anisotropy=std::abs(std::abs(l1)-std::abs(l2))/(magnitude+1e-30);
		result.bumpBowl.values[i]=(magnitude>amplitude*1e-5 && l1*l2>magnitude*magnitude*1e-4)?1.0f:0.0f;
		result.anisotropy.values[i]=float(anisotropy); result.suppression.values[i]=0;
		result.broadEnergyBefore+=center*center;
		double detail=double(result.ridge.values[i])-result.valley.values[i]; result.detailEnergyBefore+=detail*detail;
	}
	result.broadEnergyBefore/=height.values.size(); result.detailEnergyBefore/=height.values.size();
	return result;
}

SurfacePrior applySurfaceProfile(const SurfacePrior &morphology,SurfaceProfile profile,Address u,Address v)
{
	const auto &p=surfaceProfileParams(profile);
	if (profile==SurfaceProfile::Neutral) return signedSurfaceField(morphology.original,0,1);
	for (auto field:{&morphology.original,&morphology.broad,&morphology.fine,&morphology.coherence,
		&morphology.fineCoherence,&morphology.anisotropy,&morphology.ridge,&morphology.valley,&morphology.bumpBowl}) {
		validate(*field); require(field->width==morphology.original.width && field->height==morphology.original.height,"Inconsistent morphology dimensions");
	}
	SurfacePrior result=morphology;
	const auto &height=result.original; float scale=float(height.width)/128.0f;
	result.target=height; result.suppression=height;
	result.targetRms=result.broadEnergyAfter=result.detailEnergyAfter=result.meanSuppression=0;
	for (size_t i=0;i<height.values.size();++i) {
		double original=height.values[i],broad=result.broad.values[i],fine=result.fine.values[i];
		double coherence=std::clamp(double(result.coherence.values[i]),0.0,1.0),anisotropy=std::clamp(double(result.anisotropy.values[i]),0.0,1.0);
		double support=std::abs(broad)/(std::abs(broad)+p.fineGuard*std::abs(fine)+p.ridgeGuard*result.ridge.values[i]+p.valleyGuard*result.valley.values[i]+1e-30);
		double blob=result.bumpBowl.values[i]*(1-anisotropy)*(1-anisotropy)*(1-coherence)*support;
		double broadMask=std::clamp((p.broadAttenuation+p.incoherentBroadAttenuation*(1-coherence)*(1-coherence))*support+p.blobAttenuation*blob,0.0,1.0);
		double fineMask=p.fineAttenuation*(1-std::clamp(double(result.fineCoherence.values[i]),0.0,1.0));
		double candidate=original-broadMask*broad-fineMask*fine;
		result.target.values[i]=float(std::clamp(candidate,std::min(0.0,original),std::max(0.0,original)));
		// Comparable across profiles: actual signed-amplitude attenuation,
		// including the final clamp, rather than a material-specific blob score.
		double suppression=original!=0 ? std::clamp(1-std::abs(double(result.target.values[i])/original),0.0,1.0) : 0;
		result.suppression.values[i]=float(suppression); result.meanSuppression+=suppression;
		result.targetRms+=double(result.target.values[i])*result.target.values[i];
	}
	auto after=gaussian(result.target,8*scale,u,v);
	for (auto d:after.values) result.broadEnergyAfter+=double(d)*d;
	auto detailAfter=gaussian(result.target,2*scale,u,v);
	for (size_t i=0;i<height.values.size();++i) {
		double remaining=double(result.target.values[i])-detailAfter.values[i]; result.detailEnergyAfter+=remaining*remaining;
	}
	result.detailEnergyAfter/=height.values.size();
	auto targetRange=std::minmax_element(result.target.values.begin(),result.target.values.end());
	result.targetMin=*targetRange.first; result.targetMax=*targetRange.second;
	result.meanSuppression/=height.values.size(); result.targetRms=std::sqrt(result.targetRms/height.values.size());
	result.broadEnergyAfter/=height.values.size();
	return result;
}

SurfacePrior surfacePrior(const HeightField &height,float neutral,float strength,SurfaceProfile profile,Address u,Address v)
{
	(void)surfaceProfileParams(profile);
	if (profile==SurfaceProfile::Neutral) return signedSurfaceField(height,neutral,strength);
	return applySurfaceProfile(analyzeSurfaceMorphology(height,neutral,strength,u,v),profile,u,v);
}

Mesh relocateDarkFeatures(const Mesh &mesh,const HeightField &attraction,const Displacement &mapping,
	const DarkFeatureSettings &settings,DarkFeatureStats *stats,Limits limits)
{
	DarkFeatureStats report; report.enabled=settings.enabled;
	if (stats) *stats=report;
	// Exact identity for disabled/zero settings, including original UV bit patterns.
	if (!settings.enabled || settings.strength==0 || settings.maximumShift==0) return mesh;
	validate(mesh,limits); validate(attraction);
	require(finite(mapping.uvScale) && finite(mapping.uvOffset),"Non-finite UV mapping");
	auto attractionAt=[&](Vec2 uv) { require(finite(uv),"Non-finite UV mapping"); return sampleMappedValidated(attraction,uv[0],uv[1],mapping); };
	require(std::isfinite(settings.threshold) && settings.threshold>=0 && std::isfinite(settings.strength)
		&& settings.strength>=0 && settings.strength<=1 && std::isfinite(settings.maximumShift)
		&& settings.maximumShift>=0 && settings.maximumShift<=0.25f,"Invalid dark feature settings");
	require(mesh.source && mesh.origins.size()==mesh.triangles.size(),"Dark feature bias requires source correspondence");
	Mesh out=mesh;
	struct Use { size_t face; int corner; };
	std::vector<std::vector<Use>> incident(mesh.vertices.size());
	for (size_t f=0;f<mesh.triangles.size();++f) for (int c=0;c<3;++c) incident[mesh.triangles[f][c]].push_back({f,c});
	auto distance=[](const Vec3 &a,const Vec3 &b) { return std::hypot(double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]); };
	auto orient=[](const Barycentric &a,const Barycentric &b,const Barycentric &c) {
		return (b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
	};
	auto quality=[&](const Triangle &t,size_t moved,const Vertex &candidate) {
		std::array<Vec3,3> p; for (int j=0;j<3;++j) p[j]=t[j]==moved ? candidate.position : out.vertices[t[j]].position;
		double sum=0; for (int j=0;j<3;++j) { double d=distance(p[j],p[(j+1)%3]); sum+=d*d; }
		double a[3],b[3],n[3]; for (int j=0;j<3;++j) { a[j]=double(p[1][j])-p[0][j]; b[j]=double(p[2][j])-p[0][j]; }
		for (int j=0;j<3;++j) n[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];
		return 2*std::sqrt(3.0)*std::hypot(n[0],n[1],n[2])/sum;
	};
	for (size_t i=mesh.source->vertices.size();i<mesh.vertices.size();++i) {
		if (mesh.vertices[i].boundary || incident[i].empty()) continue;
		auto first=incident[i].front(); const auto &origin=out.origins[first.face];
		auto face=origin.triangle; auto weights=origin.corners[first.corner];
		if (face>=mesh.source->triangles.size()) { ++report.provenanceSkipped; continue; }
		// Samples on any reference edge are fixed, including original internal
		// edges on the legacy path. Patch seeds may span former source diagonals.
		if (*std::min_element(weights.begin(),weights.end())<=1e-8) continue;
		bool compatible=true;
		for (auto use:incident[i]) {
			const auto &o=out.origins[use.face]; compatible &= o.triangle==face;
			for (int j=0;j<3;++j) compatible &= std::abs(o.corners[use.corner][j]-weights[j])<=1e-9;
		}
		if (!compatible) { ++report.provenanceSkipped; continue; }
		auto expected=sourceVertex(*mesh.source,face,weights,false);
		for (int j=0;j<3;++j) compatible &= std::abs(expected.position[j]-mesh.vertices[i].position[j])<=1e-5*std::max(1.0f,std::abs(expected.position[j]));
		for (int j=0;j<2;++j) compatible &= std::abs(expected.uv[j]-mesh.vertices[i].uv[j])<=1e-5*std::max(1.0f,std::abs(expected.uv[j]));
		for (int j=0;j<4;++j) compatible &= std::abs(expected.color[j]-mesh.vertices[i].color[j])<=1e-5;
		for (int j=0;j<3;++j) compatible &= std::abs(expected.normal[j]-mesh.vertices[i].normal[j])<=1e-5;
		if (!compatible) { ++report.provenanceSkipped; continue; }
		++report.eligible;
		double spacing=std::numeric_limits<double>::infinity();
		for (auto use:incident[i]) for (auto j:mesh.triangles[use.face]) if (j!=i)
			spacing=std::min(spacing,distance(mesh.vertices[i].position,mesh.vertices[j].position));
		const auto &base=mesh.source->triangles[face];
		const auto &p0=mesh.source->vertices[base[0]].position,&p1=mesh.source->vertices[base[1]].position,&p2=mesh.source->vertices[base[2]].position;
		std::array<double,3> e{},f{},x{},y{}; double ee=0,ef=0,ff=0;
		for (int j=0;j<3;++j) { e[j]=double(p1[j])-p0[j]; f[j]=double(p2[j])-p0[j]; ee+=e[j]*e[j]; ef+=e[j]*f[j]; ff+=f[j]*f[j]; }
		double det=ee*ff-ef*ef;
		if (!(det>ee*ff*1e-12) || !(spacing>0)) { ++report.provenanceSkipped; continue; }
		for (int j=0;j<3;++j) { x[j]=e[j]/std::sqrt(ee); y[j]=f[j]-e[j]*ef/ee; }
		double yy=0; for (auto a:y) yy+=a*a; for (auto &a:y) a/=std::sqrt(yy);
		double current=attractionAt(out.vertices[i].uv),best=current;
		Barycentric bestWeights=weights; Vertex bestVertex=out.vertices[i];
		bool found=false;
		// Search up to half a local edge; movement itself remains <= maximumShift.
		for (int ring=1;ring<=2;++ring) for (int direction=0;direction<16;++direction) {
			double angle=direction*(2*3.14159265358979323846/16),radius=spacing*0.25*ring;
			double de=0,df=0;
			for (int j=0;j<3;++j) { double d=(x[j]*std::cos(angle)+y[j]*std::sin(angle))*radius; de+=d*e[j]; df+=d*f[j]; }
			double b=(de*ff-df*ef)/det,c=(df*ee-de*ef)/det;
			Barycentric target{weights[0]-b-c,weights[1]+b,weights[2]+c};
			if (*std::min_element(target.begin(),target.end())<=1e-8) continue;
			auto targetVertex=sourceVertex(*mesh.source,face,target,false);
			double evidence=attractionAt(targetVertex.uv);
			if (evidence<=settings.threshold || evidence<=current+1e-7) continue;
			found=true;
			double fraction=std::min(1.0,double(settings.maximumShift)*spacing/radius)*settings.strength;
			Barycentric candidateWeights; for (int j=0;j<3;++j) candidateWeights[j]=std::lerp(weights[j],target[j],fraction);
			auto candidate=sourceVertex(*mesh.source,face,candidateWeights,false);
			double value=attractionAt(candidate.uv);
			// A bounded step can approach a narrow trough without reaching its
			// nonzero residual on the first move. Rank the nearby target, while
			// disallowing any loss of already captured attraction at the sample.
			if (value+1e-7>=current && evidence>best+1e-7) { best=evidence; bestWeights=candidateWeights; bestVertex=candidate; }
		}
		if (found) ++report.aboveThreshold;
		if (best<=current+1e-7) continue;
		++report.proposals;
		bool provenance=true;
		if (mesh.patchAuthority) {
			const auto &a=*mesh.patchAuthority; SourceSurfaceLocation loc;
			if (face>=a.workingFacePatch.size() || a.workingFacePatch[face]>=a.patches.size() || !a.original
				|| !locateInSource(*a.original,a.patches[a.workingFacePatch[face]],bestVertex.position,loc)) provenance=false;
			else {
				auto authoritative=sourceVertex(*a.original,loc.triangle,loc.barycentric,false);
				for (int j=0;j<2;++j) provenance &= std::abs(authoritative.uv[j]-bestVertex.uv[j])<=1e-5*std::max(1.0f,std::abs(authoritative.uv[j]));
				for (int j=0;j<4;++j) provenance &= std::abs(authoritative.color[j]-bestVertex.color[j])<=1e-5;
				for (int j=0;j<3;++j) provenance &= std::abs(authoritative.normal[j]-bestVertex.normal[j])<=1e-5;
			}
		}
		if (!provenance) { ++report.rejected; ++report.provenanceRejected; continue; }
		bool safe=distance(mesh.vertices[i].position,bestVertex.position)<=settings.maximumShift*spacing*(1+1e-5);
		for (auto use:incident[i]) {
			const auto &o=out.origins[use.face]; auto corners=o.corners; corners[use.corner]=bestWeights;
			double before=orient(o.corners[0],o.corners[1],o.corners[2]),after=orient(corners[0],corners[1],corners[2]);
			// Staying in the positively oriented one-ring kernel maintains the
			// original triangle partition: no crossing edges or new intersections.
			safe &= before>0 && after>=before*0.25;
			double oldQuality=quality(out.triangles[use.face],i,out.vertices[i]);
			safe &= quality(out.triangles[use.face],i,bestVertex)>=std::min(0.10,oldQuality*0.8);
			// Check stored float geometry too; large source coordinates can
			// quantize a valid double-precision domain proposal onto a bad face.
			auto normal=[&](bool moved) {
				std::array<Vec3,3> p; const auto &t=out.triangles[use.face];
				for (int j=0;j<3;++j) p[j]=(moved && t[j]==i)?bestVertex.position:out.vertices[t[j]].position;
				std::array<double,3> a{},b{},n{};
				for (int j=0;j<3;++j) { a[j]=double(p[1][j])-p[0][j]; b[j]=double(p[2][j])-p[0][j]; }
				for (int j=0;j<3;++j) n[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];
				return n;
			};
			auto oldNormal=normal(false),newNormal=normal(true); double area=0,projection=0;
			for (int j=0;j<3;++j) { area+=oldNormal[j]*oldNormal[j]; projection+=oldNormal[j]*newNormal[j]; }
			safe &= projection>=area*0.1;
		}
		if (!safe) { ++report.rejected; ++report.topologyRejected; continue; }
		double shift=distance(mesh.vertices[i].position,bestVertex.position);
		out.vertices[i]=bestVertex;
		for (auto use:incident[i]) out.origins[use.face].corners[use.corner]=bestWeights;
		++report.accepted; report.averageDistance+=shift; report.maximumDistance=std::max(report.maximumDistance,shift);
	}
	if (report.accepted) report.averageDistance/=report.accepted;
	validate(out,limits);
	if (out.patchAuthority && report.accepted) (void)resolvePatchSources(out,limits);
	if (stats) *stats=report;
	return out;
}

Mesh displace(const Mesh &mesh, const HeightField &field, const Displacement &s, Limits limits, DisplacementStats *stats)
{
	validate(mesh, limits);
	validate(field);
	if (s.profileTarget) validate(*s.profileTarget);
	require(std::isfinite(s.neutral) && std::isfinite(s.strength)
		&& std::isfinite(s.positiveLimit) && s.positiveLimit >= 0
		&& std::isfinite(s.negativeLimit) && s.negativeLimit >= 0
		&& finite(s.uvScale) && finite(s.uvOffset)
		&& (!s.preventPinching || (std::isfinite(s.maxSlope) && s.maxSlope > 0)), "Invalid displacement settings");
	if (stats) *stats = {};
	Mesh result = mesh;
	std::vector<double> distances(mesh.vertices.size(),0), requested(mesh.vertices.size(),0);
	for (size_t i=0;i<mesh.vertices.size();++i) {
		const auto &v=mesh.vertices[i];
		if (s.pinBoundaries && v.boundary) continue;
		double distance=requestedDistance(field,v.uv[0],v.uv[1],s);
		distances[i] = s.removeSectionBias ? distance
			: std::clamp(distance,-double(s.negativeLimit),double(s.positiveLimit));
	}
	std::vector<std::vector<size_t>> biasSections;
	std::vector<double> areaWeights(mesh.vertices.size(),0);
	std::vector<bool> centered;
	if (s.removeSectionBias) {
		std::vector<size_t> parent(mesh.vertices.size()); std::iota(parent.begin(),parent.end(),size_t(0));
		auto root=[&](size_t i) { while (parent[i]!=i) { parent[i]=parent[parent[i]]; i=parent[i]; } return i; };
		for (const auto &t:mesh.triangles) {
			const auto &a=mesh.vertices[t[0]].position,&b=mesh.vertices[t[1]].position,&c=mesh.vertices[t[2]].position;
			double u[3],v[3],n[3];
			for (int j=0;j<3;++j) { u[j]=double(b[j])-a[j]; v[j]=double(c[j])-a[j]; }
			for (int j=0;j<3;++j) n[j]=u[(j+1)%3]*v[(j+2)%3]-u[(j+2)%3]*v[(j+1)%3];
			double weight=std::hypot(n[0],n[1],n[2])/6.0;
			for (auto i:t) { areaWeights[i]+=weight; auto r=root(i),base=root(t[0]); if (r!=base) parent[r]=base; }
		}
		std::map<size_t,std::vector<size_t>> sections;
		for (size_t i=0;i<mesh.vertices.size();++i)
			if (areaWeights[i]>0 && !(s.pinBoundaries && mesh.vertices[i].boundary)) sections[root(i)].push_back(i);
		for (auto &[r,indices]:sections) { (void)r; biasSections.push_back(std::move(indices)); }
		centered.resize(biasSections.size(),false);
	}
	auto removeBias=[&]() {
		for (size_t section=0;section<biasSections.size();++section) {
			const auto &indices=biasSections[section]; double weighted=0,weight=0,low=std::numeric_limits<double>::infinity(),high=-low;
			bool withinLimits=true;
			for (auto i:indices) {
				weighted+=areaWeights[i]*distances[i]; weight+=areaWeights[i];
				low=std::min(low,distances[i]-double(s.positiveLimit)); high=std::max(high,distances[i]+double(s.negativeLimit));
				withinLimits &= distances[i]>=-double(s.negativeLimit) && distances[i]<=double(s.positiveLimit);
			}
			double mean=weighted/weight;
			if (stats) stats->maximumRemovedBias=std::max(stats->maximumRemovedBias,std::abs(mean));
			if (std::abs(mean)<=1e-12 && withinLimits) continue;
			centered[section]=true;
			// Project onto zero area-weighted signed displacement subject to the
			// user's positive/negative limits. Zero is always a feasible result.
			for (int iteration=0;iteration<64;++iteration) {
				double shift=std::midpoint(low,high),sum=0;
				for (auto i:indices) sum+=areaWeights[i]*std::clamp(distances[i]-shift,-double(s.negativeLimit),double(s.positiveLimit));
				if (sum>0) low=shift; else high=shift;
			}
			double shift=std::midpoint(low,high);
			for (auto i:indices) distances[i]=std::clamp(distances[i]-shift,-double(s.negativeLimit),double(s.positiveLimit));
		}
	};
	if (s.removeSectionBias) {
		removeBias();
		for (auto &distance:distances) distance=std::clamp(distance,-double(s.negativeLimit),double(s.positiveLimit));
	}
	requested=distances;
	std::map<std::pair<std::uint32_t,std::uint32_t>,double> edges;
	auto relaxSlope=[&](int iterations) {
		for (int iteration=0;iteration<iterations;++iteration) {
			bool changed=false;
			for (const auto &[edge,cap] : edges) {
				auto a=edge.first,b=edge.second;
				double difference=distances[a]-distances[b];
				if (std::abs(difference)<=cap+1e-12) continue;
				double excess=difference-std::copysign(cap,difference);
				bool pa=s.pinBoundaries && mesh.vertices[a].boundary;
				bool pb=s.pinBoundaries && mesh.vertices[b].boundary;
				if (pa && !pb) distances[b]+=excess;
				else if (pb && !pa) distances[a]-=excess;
				else if (!pa && !pb) { distances[a]-=excess*0.5; distances[b]+=excess*0.5; }
				changed=true;
			}
			if (!changed) break;
		}
	};
	auto enforceSlope=[&]() {
		if (s.removeSectionBias && !edges.empty()) {
			bool violation=false;
			for (const auto &[edge,cap]:edges)
				violation |= std::abs(distances[edge.first]-distances[edge.second])>cap+1e-12;
			if (!violation) return;
			// A tiny constrained edge must not scale down the entire shape.
			// Shortest-path envelopes are slope-bounded above/below the requested
			// field. They agree with it away from violations. Clamp their pinned
			// values using distance-to-boundary cones, then blend per section to
			// preserve zero integrated displacement without another global scale.
			std::vector<std::vector<std::pair<size_t,double>>> adjacency(distances.size());
			for (const auto &[edge,cap]:edges) {
				adjacency[edge.first].emplace_back(edge.second,cap);
				adjacency[edge.second].emplace_back(edge.first,cap);
			}
			auto envelope=[&](std::vector<double> values) {
				using Entry=std::pair<double,size_t>;
				std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
				for (size_t i=0;i<values.size();++i) if (std::isfinite(values[i])) queue.emplace(values[i],i);
				while (!queue.empty()) {
					auto [value,i]=queue.top(); queue.pop();
					if (value!=values[i]) continue;
					for (auto [j,cap]:adjacency[i]) if (value+cap<values[j]) {
						values[j]=value+cap; queue.emplace(values[j],j);
					}
				}
				return values;
			};
			std::vector<double> toPin(distances.size(),std::numeric_limits<double>::infinity());
			if (s.pinBoundaries) for (size_t i=0;i<toPin.size();++i) if (mesh.vertices[i].boundary) toPin[i]=0;
			toPin=envelope(std::move(toPin));
			auto lower=envelope(distances),negative=distances;
			for (auto &d:negative) d=-d;
			auto upper=envelope(std::move(negative));
			for (size_t i=0;i<lower.size();++i) {
				lower[i]=std::clamp(std::max(lower[i],-toPin[i]),-double(s.negativeLimit),double(s.positiveLimit));
				upper[i]=std::clamp(std::min(-upper[i],toPin[i]),-double(s.negativeLimit),double(s.positiveLimit));
			}
			for (const auto &indices:biasSections) {
				double lo=0,hi=0;
				for (auto i:indices) { lo+=areaWeights[i]*lower[i]; hi+=areaWeights[i]*upper[i]; }
				// Pin/bounds projection can shift an envelope's mean past zero.
				// Min/max with the feasible zero field keeps its Lipschitz bound.
				if (lo>0) { lo=0; for (auto i:indices) { lower[i]=std::min(lower[i],0.0); lo+=areaWeights[i]*lower[i]; } }
				if (hi<0) { hi=0; for (auto i:indices) { upper[i]=std::max(upper[i],0.0); hi+=areaWeights[i]*upper[i]; } }
				double blend=hi>lo ? std::clamp(-lo/(hi-lo),0.0,1.0) : 0;
				for (auto i:indices) distances[i]=std::lerp(lower[i],upper[i],blend);
			}
			return;
		}
		double contraction=1;
		for (const auto &[edge,cap] : edges) {
			double difference=std::abs(distances[edge.first]-distances[edge.second]);
			if (difference>cap && difference>0) contraction=std::min(contraction,cap/difference);
		}
		for (auto &distance : distances) distance*=contraction;
	};
	if (s.preventPinching) {
		// Limit changes in displacement across each physical edge. Relaxation
		// naturally tapers detail into pinned UV seams without moving the seam.
		for (const auto &t : mesh.triangles) for (int i=0;i<3;++i) {
			auto a=std::min(t[i],t[(i+1)%3]), b=std::max(t[i],t[(i+1)%3]);
			const auto &p=mesh.vertices[a].position, &q=mesh.vertices[b].position;
			double len=std::hypot(double(p[0])-q[0],double(p[1])-q[1],double(p[2])-q[2]);
			edges.emplace(std::make_pair(a,b),double(s.maxSlope)*len);
		}
		relaxSlope(64);
		// A final uniform contraction guarantees the slope limit even if the
		// relaxation iteration limit was reached on a large, connected mesh.
		enforceSlope();
	}
	auto updatePositions=[&](double scale) {
		for (size_t i=0;i<mesh.vertices.size();++i) {
			Vec3 n=normalized(mesh.vertices[i].normal);
			for (int c=0;c<3;++c)
				result.vertices[i].position[c]=float(double(mesh.vertices[i].position[c])+double(n[c])*distances[i]*scale);
		}
	};
	auto orientationSafe=[&](std::vector<bool> *affected=nullptr) {
		bool safe=true;
		for (const auto &t : mesh.triangles) {
			auto cross=[](const Vec3 &a,const Vec3 &b,const Vec3 &c) {
				std::array<double,3> x{},y{},n{};
				for (int j=0;j<3;++j) { x[j]=double(b[j])-a[j]; y[j]=double(c[j])-a[j]; }
				for (int j=0;j<3;++j) n[j]=x[(j+1)%3]*y[(j+2)%3]-x[(j+2)%3]*y[(j+1)%3];
				return n;
			};
			auto a=cross(mesh.vertices[t[0]].position,mesh.vertices[t[1]].position,mesh.vertices[t[2]].position);
			auto b=cross(result.vertices[t[0]].position,result.vertices[t[1]].position,result.vertices[t[2]].position);
			double original=0, projected=0;
			for (int j=0;j<3;++j) { original+=a[j]*a[j]; projected+=a[j]*b[j]; }
			if (!std::isfinite(projected) || projected < original*0.1) {
				safe=false;
				if (!affected) return false;
				for (auto index : t) (*affected)[index]=true;
			}
		}
		return safe;
	};
	double scale=1;
	updatePositions(scale);
	if (s.preventPinching) {
		// Back off locally around problematic faces first, preserving detail in
		// unaffected regions. Reapply edge constraints after each local pass.
		for (int pass=0;pass<12;++pass) {
			std::vector<bool> affected(mesh.vertices.size(),false);
			if (orientationSafe(&affected)) break;
			for (size_t i=0;i<distances.size();++i)
				if (affected[i] && !(s.pinBoundaries && mesh.vertices[i].boundary)) distances[i]*=0.5;
			relaxSlope(16);
			enforceSlope();
			updatePositions(scale);
		}
		while (!orientationSafe() && scale>1.0/1048576.0) { scale*=0.5; updatePositions(scale); }
		if (!orientationSafe()) { scale=0; updatePositions(scale); }
	}
	if (s.removeSectionBias) {
		removeBias();
		if (s.preventPinching) enforceSlope();
		updatePositions(scale);
		if (s.preventPinching) {
			while (!orientationSafe() && scale>1.0/1048576.0) { scale*=0.5; updatePositions(scale); }
			if (!orientationSafe()) { scale=0; updatePositions(scale); }
		}
	}
	if (stats) {
		stats->orientationScale=float(scale);
		stats->centeredSections=size_t(std::count(centered.begin(),centered.end(),true));
		for (const auto &indices:biasSections) {
			double total=0,weight=0;
			for (auto i:indices) { total+=areaWeights[i]*distances[i]*scale; weight+=areaWeights[i]; }
			stats->maximumResidualBias=std::max(stats->maximumResidualBias,std::abs(total/weight));
		}
		for (size_t i=0;i<distances.size();++i)
			if (std::abs(distances[i]*scale-requested[i])>1e-7) ++stats->limitedVertices;
		double requestedSquared=0,appliedSquared=0;
		for (size_t i=0;i<distances.size();++i) {
			requestedSquared+=requested[i]*requested[i];
			for (int j=0;j<3;++j) {
				double delta=double(result.vertices[i].position[j])-mesh.vertices[i].position[j];
				appliedSquared+=delta*delta;
			}
		}
		stats->requestedRms=std::sqrt(requestedSquared/double(distances.size()));
		stats->appliedRms=std::sqrt(appliedSquared/double(distances.size()));
	}
	validate(result, limits);
	return result;
}
double maximumSectionDisplacementBias(const Mesh &mesh,Limits limits)
{
	validate(mesh,limits);
	require(mesh.source && mesh.origins.size()==mesh.triangles.size(), "Bias measurement requires source correspondence");
	std::vector<size_t> parent(mesh.vertices.size()); std::iota(parent.begin(),parent.end(),size_t(0));
	auto root=[&](size_t i) { while (parent[i]!=i) { parent[i]=parent[parent[i]]; i=parent[i]; } return i; };
	std::vector<double> integral(mesh.vertices.size(),0),area(mesh.vertices.size(),0);
	for (size_t f=0;f<mesh.triangles.size();++f) {
		const auto &t=mesh.triangles[f]; const auto &origin=mesh.origins[f];
		require(origin.triangle<mesh.source->triangles.size(), "Invalid bias source correspondence");
		const auto &source=mesh.source->triangles[origin.triangle]; Vec3 p[3];
		for (int c=0;c<3;++c) for (int j=0;j<3;++j) {
			double value=0; for (int k=0;k<3;++k) value+=origin.corners[c][k]*mesh.source->vertices[source[k]].position[j]; p[c][j]=float(value);
		}
		double a[3],b[3],n[3];
		for (int j=0;j<3;++j) { a[j]=double(p[1][j])-p[0][j]; b[j]=double(p[2][j])-p[0][j]; }
		for (int j=0;j<3;++j) n[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];
		double weight=std::hypot(n[0],n[1],n[2])/6.0;
		for (int c=0;c<3;++c) {
			auto i=t[c]; auto normal=normalized(mesh.vertices[i].normal); double distance=0;
			for (int j=0;j<3;++j) distance+=(double(mesh.vertices[i].position[j])-p[c][j])*normal[j];
			integral[i]+=weight*distance; area[i]+=weight;
			auto r=root(i),base=root(t[0]); if (r!=base) parent[r]=base;
		}
	}
	std::map<size_t,std::pair<double,double>> sections;
	for (size_t i=0;i<mesh.vertices.size();++i) { auto &s=sections[root(i)]; s.first+=integral[i]; s.second+=area[i]; }
	double maximum=0;
	for (const auto &[r,s]:sections) { (void)r; if (s.second>0) maximum=std::max(maximum,std::abs(s.first/s.second)); }
	return maximum;
}

std::vector<Vec3> shadingNormals(const Mesh &mesh, Limits limits)
{
	validate(mesh,limits);
	if (mesh.patchAuthority) (void)resolvePatchSources(mesh,limits);
	require(bool(mesh.source) && mesh.origins.size()==mesh.triangles.size(), "Shading requires source provenance");
	using D3=std::array<double,3>;
	std::vector<D3> base(mesh.vertices.size(),{0,0,0}), moved(mesh.vertices.size(),{0,0,0});
	std::vector<bool> changed(mesh.vertices.size(),false);
	for (size_t f=0;f<mesh.triangles.size();++f) {
		const auto &t=mesh.triangles[f]; const auto &origin=mesh.origins[f];
		require(origin.triangle<mesh.source->triangles.size(), "Invalid source triangle provenance");
		const auto &source=mesh.source->triangles[origin.triangle];
		Vec3 reference[3];
		for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
			double p=0;
			for (int k=0;k<3;++k) p+=origin.corners[i][k]*mesh.source->vertices[source[k]].position[j];
			reference[i][j]=float(p);
		}
		auto faceNormal=[](const Vec3 &a,const Vec3 &b,const Vec3 &c) -> D3 {
			D3 u{},v{},n{}; for(int i=0;i<3;++i) { u[i]=double(b[i])-a[i]; v[i]=double(c[i])-a[i]; }
			for(int i=0;i<3;++i) n[i]=u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3];
			return n;
		};
		auto before=faceNormal(reference[0],reference[1],reference[2]);
		auto after=faceNormal(mesh.vertices[t[0]].position,mesh.vertices[t[1]].position,mesh.vertices[t[2]].position);
		bool faceChanged=false;
		for (int i=0;i<3;++i) faceChanged |= reference[i]!=mesh.vertices[t[i]].position;
		for (auto index:t) {
			for (int j=0;j<3;++j) { base[index][j]+=before[j]; moved[index][j]+=after[j]; }
			changed[index]=changed[index] || faceChanged;
		}
	}
	std::vector<Vec3> result; result.reserve(mesh.vertices.size());
	for (size_t i=0;i<mesh.vertices.size();++i) {
		const auto &authored=mesh.vertices[i].normal;
		if (!changed[i]) { result.push_back(authored); continue; }
		double nb=std::hypot(base[i][0],base[i][1],base[i][2]);
		double nm=std::hypot(moved[i][0],moved[i][1],moved[i][2]);
		require(nb>1e-12 && nm>1e-12, "Cannot update shading at a degenerate or unreferenced vertex");
		Vec3 corrected;
		for (int j=0;j<3;++j) corrected[j]=float(double(authored[j])+moved[i][j]/nm-base[i][j]/nb);
		result.push_back(normalized(corrected));
	}
	return result;
}

Mesh improveTriangleQuality(const Mesh &mesh, std::size_t *flips, Limits limits)
{
	validate(mesh, limits);
	if (mesh.patchAuthority) (void)resolvePatchSources(mesh,limits);
	if (flips) *flips = 0;
	require(bool(mesh.source) && mesh.origins.size() == mesh.triangles.size(), "Triangle cleanup requires source provenance");
	Mesh out = mesh;
	using Edge = std::pair<std::uint32_t, std::uint32_t>;
	auto key = [](std::uint32_t a, std::uint32_t b) { return Edge{std::min(a,b),std::max(a,b)}; };
	struct Use { std::size_t face; int corner; };
	using D3 = std::array<double,3>;
	auto sub = [](const Vec3 &a, const Vec3 &b) -> D3 { return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]}; };
	auto cross = [](const D3 &a, const D3 &b) -> D3 { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; };
	auto dot = [](const D3 &a, const D3 &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
	auto normal = [&](const Triangle &t) { return cross(sub(out.vertices[t[1]].position,out.vertices[t[0]].position),sub(out.vertices[t[2]].position,out.vertices[t[0]].position)); };
	auto quality = [&](const Triangle &t) {
		double sum=0; for (int i=0;i<3;++i) { auto v=sub(out.vertices[t[i]].position,out.vertices[t[(i+1)%3]].position); sum+=dot(v,v); }
		auto n=normal(t); return 2*std::sqrt(3.0)*std::sqrt(dot(n,n))/sum;
	};
	std::size_t count=0;
	for (int pass=0; pass<2; ++pass) {
		std::map<Edge,std::vector<Use>> edges;
		for (std::size_t i=0;i<out.triangles.size();++i) for (int j=0;j<3;++j)
			edges[key(out.triangles[i][j],out.triangles[i][(j+1)%3])].push_back({i,j});
		std::vector<bool> touched(out.vertices.size(),false);
		std::size_t accepted=0;
		for (const auto &[edge, uses] : edges) {
			(void) edge;
			if (uses.size()!=2) continue;
			auto u=uses[0], v=uses[1]; const auto t=out.triangles[u.face], s=out.triangles[v.face];
			const auto ot=out.origins[u.face], os=out.origins[v.face];
			if (ot.triangle!=os.triangle) continue;
			auto a=t[u.corner], b=t[(u.corner+1)%3], c=t[(u.corner+2)%3];
			if (s[v.corner]!=b || s[(v.corner+1)%3]!=a) continue;
			auto d=s[(v.corner+2)%3];
			if (c==d || edges.count(key(c,d)) || touched[a] || touched[b] || touched[c] || touched[d]) continue;
			double oldQ=std::min(quality(t),quality(s)); if (oldQ>=0.35) continue;
			Triangle n1{c,d,b}, n2{d,c,a};
			if (std::min(quality(n1),quality(n2)) <= oldQ+1e-6) continue;
			auto ba=ot.corners[u.corner], bb=ot.corners[(u.corner+1)%3], bc=ot.corners[(u.corner+2)%3], bd=os.corners[(v.corner+2)%3];
			auto cross2=[](double x,double y,double z,double w) { return x*w-y*z; };
			double rx=bb[1]-ba[1], ry=bb[2]-ba[2], sx=bd[1]-bc[1], sy=bd[2]-bc[2];
			double den=cross2(rx,ry,sx,sy); if (std::abs(den)<1e-15) continue;
			double x=bc[1]-ba[1], y=bc[2]-ba[2];
			double alongOld=cross2(x,y,sx,sy)/den, alongNew=cross2(x,y,rx,ry)/den;
			if (alongOld<=1e-9 || alongOld>=1-1e-9 || alongNew<=1e-9 || alongNew>=1-1e-9) continue;
			double deviation=0, maxEdge=0;
			for (int k=0;k<3;++k) {
				double p=std::lerp(double(out.vertices[a].position[k]),double(out.vertices[b].position[k]),alongOld);
				double q=std::lerp(double(out.vertices[c].position[k]),double(out.vertices[d].position[k]),alongNew);
				deviation+=(p-q)*(p-q);
			}
			for (auto i : {a,b,c,d}) for (auto j : {a,b,c,d}) { auto delta=sub(out.vertices[i].position,out.vertices[j].position); maxEdge=std::max(maxEdge,dot(delta,delta)); }
			if (deviation>0.0001*maxEdge) continue; // At most 1% of local edge scale.
			auto old1=normal(t), old2=normal(s), new1=normal(n1), new2=normal(n2);
			if (dot(old1,new1)<=0 || dot(old1,new2)<=0 || dot(old2,new1)<=0 || dot(old2,new2)<=0) continue;
			out.triangles[u.face]=n1; out.triangles[v.face]=n2;
			out.origins[u.face]={ot.triangle,{bc,bd,bb}}; out.origins[v.face]={ot.triangle,{bd,bc,ba}};
			touched[a]=touched[b]=touched[c]=touched[d]=true;
			++accepted; ++count;
		}
		if (!accepted) break;
	}
	validate(out,limits);
	if (out.patchAuthority) (void)resolvePatchSources(out,limits);
	if (flips) *flips=count;
	return out;
}

} // namespace VanillaPlus
