#include "vanillaplus.h"
#include "meshoptimizer/src/meshoptimizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace VanillaPlus {
namespace {
using D3=std::array<double,3>;
using Edge=std::pair<std::uint32_t,std::uint32_t>;
Edge key(std::uint32_t a,std::uint32_t b) { return {std::min(a,b),std::max(a,b)}; }
D3 subtract(const Vec3 &a,const Vec3 &b) { return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]}; }
D3 cross(D3 a,D3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(D3 a,D3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
struct Face { Triangle t; std::array<Barycentric,3> b; };

Barycentric patchCoordinates(const SourceGeometry &source,std::uint32_t face,const Vec3 &point)
{
	const auto &t=source.triangles.at(face);
	D3 a=subtract(source.vertices[t[1]].position,source.vertices[t[0]].position);
	D3 b=subtract(source.vertices[t[2]].position,source.vertices[t[0]].position);
	D3 p=subtract(point,source.vertices[t[0]].position);
	double aa=dot(a,a),ab=dot(a,b),bb=dot(b,b),pa=dot(p,a),pb=dot(p,b),den=aa*bb-ab*ab;
	if (!(den>0)) throw std::invalid_argument("Degenerate patch reference coordinates");
	double x=(pa*bb-pb*ab)/den,y=(pb*aa-pa*ab)/den;
	return {1-x-y,x,y};
}

// Compare surfaces at corresponding original-triangle coordinates, avoiding
// false matches against nearby disconnected surfaces or overlapping UVs.
bool compareSamples(const Mesh &mesh,const std::vector<Face> &from,const std::vector<Face> &to,
	double tolerance,double &maximum,std::size_t &work)
{
	constexpr double samples[7][3]={{1,0,0},{0,1,0},{0,0,1},{0.5,0.5,0},{0,0.5,0.5},{0.5,0,0.5},{1.0/3,1.0/3,1.0/3}};
	for (const auto &f:from) for (const auto &weights:samples) {
		double x=0,y=0; D3 position{};
		for (int i=0;i<3;++i) {
			x+=weights[i]*f.b[i][1]; y+=weights[i]*f.b[i][2];
			for (int j=0;j<3;++j) position[j]+=weights[i]*mesh.vertices[f.t[i]].position[j];
		}
		bool found=false;
		for (const auto &g:to) {
			if (++work>20000000) return false;
			double ax=g.b[0][1], ay=g.b[0][2], bx=g.b[1][1], by=g.b[1][2], cx=g.b[2][1], cy=g.b[2][2];
			double den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);
			if (std::abs(den)<1e-15) continue;
			double wa=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/den;
			double wb=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/den, wc=1-wa-wb;
			if (wa < -1e-8 || wb < -1e-8 || wc < -1e-8) continue;
			D3 delta{}; const double w[3]={wa,wb,wc};
			for (int i=0;i<3;++i) for (int j=0;j<3;++j) delta[j]+=w[i]*mesh.vertices[g.t[i]].position[j];
			for (int j=0;j<3;++j) delta[j]-=position[j];
			double error=std::sqrt(dot(delta,delta)); maximum=std::max(maximum,error);
			if (error>tolerance+1e-9) return false;
			found=true; break;
		}
		if (!found) return false;
	}
	return true;
}

// Overlay the two planar parameter-space triangulations. On each overlap
// polygon the surface difference is affine, so its maximum norm occurs at a
// polygon vertex. Reject ambiguous coverage rather than sampling sparsely.
bool compareOverlay(const Mesh &mesh,const std::vector<Face> &from,const std::vector<Face> &to,
	double tolerance,double &maximum,std::size_t &work)
{
	using P=std::array<double,2>;
	auto orient=[](P a,P b,P c) { return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]); };
	auto triangle=[](const Face &f) { return std::array<P,3>{{{f.b[0][1],f.b[0][2]},{f.b[1][1],f.b[1][2]},{f.b[2][1],f.b[2][2]}}}; };
	auto position=[&](const Face &f,const std::array<P,3> &t,P p,D3 &out) {
		double den=orient(t[0],t[1],t[2]); if (!(den>1e-15)) return false;
		double w[3]={orient(t[1],t[2],p)/den,orient(t[2],t[0],p)/den,orient(t[0],t[1],p)/den};
		out={}; for (int i=0;i<3;++i) for (int j=0;j<3;++j) out[j]+=w[i]*mesh.vertices[f.t[i]].position[j];
		return true;
	};
	for (const auto &f:from) {
		auto ft=triangle(f); double area=orient(ft[0],ft[1],ft[2])*0.5,covered=0;
		if (!(area>1e-15)) return false;
		for (const auto &g:to) {
			if (++work>20000000) return false;
			auto gt=triangle(g); if (!(orient(gt[0],gt[1],gt[2])>1e-15)) return false;
			std::vector<P> poly{ft[0],ft[1],ft[2]};
			for (int e=0;e<3 && !poly.empty();++e) {
				std::vector<P> next; P a=gt[e],b=gt[(e+1)%3];
				for (size_t i=0;i<poly.size();++i) {
					P p=poly[i],q=poly[(i+1)%poly.size()]; double dp=orient(a,b,p),dq=orient(a,b,q);
					bool pin=dp>=-1e-12,qin=dq>=-1e-12;
					if (pin) next.push_back(p);
					if (pin!=qin) {
						double denominator=dp-dq; if (std::abs(denominator)<1e-20) return false;
						double z=dp/denominator; next.push_back({p[0]+z*(q[0]-p[0]),p[1]+z*(q[1]-p[1])});
					}
				}
				poly=std::move(next);
			}
			if (poly.size()<3) continue;
			double polygonArea=0;
			for (size_t i=0;i<poly.size();++i) polygonArea+=poly[i][0]*poly[(i+1)%poly.size()][1]-poly[i][1]*poly[(i+1)%poly.size()][0];
			polygonArea=std::abs(polygonArea)*0.5; if (polygonArea<=1e-15) continue;
			covered+=polygonArea;
			for (auto p:poly) {
				D3 a{},b{}; if (!position(f,ft,p,a) || !position(g,gt,p,b)) return false;
				for (int j=0;j<3;++j) a[j]-=b[j];
				double error=std::sqrt(dot(a,a));
				if (!std::isfinite(error) || error>tolerance+1e-9) return false;
				maximum=std::max(maximum,error);
			}
		}
		if (std::abs(covered-area)>std::max(1e-12,area*1e-7)) return false;
	}
	return true;
}

bool validPatchTriangulation(const Mesh &mesh,const std::vector<Face> &faces,Limits limits)
{
	try {
		Mesh domain; domain.vertices=mesh.vertices;
		for (auto &v:domain.vertices) { v.position={0,0,0}; v.normal={0,0,1}; v.uv={0,0}; v.color={1,1,1,1}; }
		for (const auto &face:faces) {
			domain.triangles.push_back(face.t);
			for (int i=0;i<3;++i) {
				auto &v=domain.vertices.at(face.t[i]);
				v.position={float(face.b[i][1]),float(face.b[i][2]),0}; v.uv={v.position[0],v.position[1]};
			}
		}
		validate(domain,limits);
		SourcePatch patch; for (std::uint32_t i=0;i<domain.triangles.size();++i) patch.triangles.push_back(i);
		validatePatchDomain(domain,patch);
		return true;
	} catch (const std::exception &) { return false; }
}
}

Mesh reduceReconstruction(const Mesh &dense, float tolerance, ReductionStats *stats, Limits limits)
{
	validate(dense,limits);
	if (dense.patchAuthority) (void)resolvePatchSources(dense,limits);
	if (!dense.source || dense.origins.size()!=dense.triangles.size() || !std::isfinite(tolerance) || tolerance<0)
		throw std::invalid_argument("Reduction requires source provenance and a nonnegative finite error tolerance");
	ReductionStats result; result.trianglesBefore=dense.triangles.size(); result.trianglesAfter=dense.triangles.size();
	if (tolerance==0) { if (stats) *stats=result; return dense; }
	struct Packed { float v[12]; };
	std::vector<Packed> vertices(dense.vertices.size());
	std::vector<unsigned char> locked(dense.vertices.size(),0);
	std::map<Edge,std::vector<D3>> edgeNormals;
	for (const auto &t:dense.triangles) {
		auto n=cross(subtract(dense.vertices[t[1]].position,dense.vertices[t[0]].position),subtract(dense.vertices[t[2]].position,dense.vertices[t[0]].position));
		double len=std::sqrt(dot(n,n)); for (double &x:n) x/=len;
		for (int i=0;i<3;++i) edgeNormals[key(t[i],t[(i+1)%3])].push_back(n);
	}
	const D3 previewDirection{1,1,0.9};
	for (const auto &[e,n]:edgeNormals)
		if (n.size()!=2 || dot(n[0],n[1])<0.7 || dot(n[0],previewDirection)*dot(n[1],previewDirection)<=0)
			locked[e.first]=locked[e.second]=1;
	for (std::size_t i=0;i<vertices.size();++i) {
		const auto &v=dense.vertices[i]; auto &p=vertices[i].v;
		for (int j=0;j<3;++j) { p[j]=v.position[j]; p[j+3]=v.normal[j]; }
		for (int j=0;j<2;++j) p[j+6]=v.uv[j];
		for (int j=0;j<4;++j) p[j+8]=v.color[j];
		if (v.boundary) locked[i]=1;
	}
	std::map<std::uint32_t,std::vector<Face>> groups;
	for (std::size_t i=0;i<dense.triangles.size();++i) {
		const auto &o=dense.origins[i];
		if (o.triangle>=dense.source->triangles.size()) throw std::invalid_argument("Invalid source triangle provenance");
		if (!dense.patchAuthority) groups[o.triangle].push_back({dense.triangles[i],o.corners});
		else {
			const auto &authority=*dense.patchAuthority;
			auto pi=authority.workingFacePatch.at(o.triangle);
			if (pi>=authority.patches.size() || authority.patches[pi].triangles.empty()) throw std::invalid_argument("Invalid reduction patch");
			const auto &base=dense.source->triangles[o.triangle]; Face face{dense.triangles[i],{}};
			for (int c=0;c<3;++c) {
				Vec3 point{};
				for (int j=0;j<3;++j) { double v=0; for (int k=0;k<3;++k) v+=o.corners[c][k]*dense.source->vertices[base[k]].position[j]; point[j]=float(v); }
				face.b[c]=patchCoordinates(*authority.original,authority.patches[pi].triangles.front(),point);
			}
			groups[pi].push_back(face);
		}
	}
	Mesh out=dense; out.triangles.clear(); out.origins.clear(); std::size_t work=0;
	std::vector<std::uint32_t> reducedFacePatches;
	const float weights[9]={0.5f,0.5f,0.5f,1,1,1,1,1,1};
	for (const auto &[origin,faces]:groups) {
		const auto &reference=dense.patchAuthority ? *dense.patchAuthority->original : *dense.source;
		auto sourceIndex=dense.patchAuthority ? dense.patchAuthority->patches[origin].triangles.front() : origin;
		const auto &sourceFace=reference.triangles[sourceIndex];
		auto sourceNormal=cross(subtract(reference.vertices[sourceFace[1]].position,reference.vertices[sourceFace[0]].position),
			subtract(reference.vertices[sourceFace[2]].position,reference.vertices[sourceFace[0]].position));
		std::map<std::uint32_t,Barycentric> bary;
		std::vector<unsigned int> input;
		for (const auto &face:faces) for (int i=0;i<3;++i) {
			auto [it,inserted]=bary.emplace(face.t[i],face.b[i]);
			if (!inserted) for (int j=0;j<3;++j)
				if (std::abs(it->second[j]-face.b[i][j])>(dense.patchAuthority ? 1e-6 : 1e-9)) throw std::invalid_argument("Inconsistent source coordinates");
			input.push_back(face.t[i]);
		}
		std::vector<unsigned int> indices(input.size()); float qemError=0;
		size_t count=meshopt_simplifyWithAttributes(indices.data(),input.data(),input.size(),
			vertices[0].v,vertices.size(),sizeof(Packed),vertices[0].v+3,sizeof(Packed),weights,9,
			locked.data(),3,tolerance,meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute | meshopt_SimplifySparse,&qemError);
		std::vector<Face> candidate;
		bool valid=count>=3 && count<=input.size() && count%3==0;
		for (size_t i=0;valid && i<count;i+=3) {
			Triangle t{indices[i],indices[i+1],indices[i+2]};
			if (!bary.count(t[0]) || !bary.count(t[1]) || !bary.count(t[2])) { valid=false; break; }
			Face f{t,{bary.at(t[0]),bary.at(t[1]),bary.at(t[2])}};
			double ax=f.b[1][1]-f.b[0][1],ay=f.b[1][2]-f.b[0][2],bx=f.b[2][1]-f.b[0][1],by=f.b[2][2]-f.b[0][2];
			if (ax*by-ay*bx<=1e-15) { valid=false; break; }
			auto n=cross(subtract(dense.vertices[t[1]].position,dense.vertices[t[0]].position),subtract(dense.vertices[t[2]].position,dense.vertices[t[0]].position));
			if (!(dot(n,n)>1e-24) || !(dot(n,sourceNormal)>0)) { valid=false; break; }
			candidate.push_back(f);
		}
		double error=0;
		if (valid && count<input.size()) {
			// Bound work to keep interactive previews responsive.
			if (faces.size()*candidate.size()>500000 || work>=20000000) { valid=false; ++result.skippedRegions; }
			else if (dense.patchAuthority) valid=validPatchTriangulation(dense,candidate,limits)
				&& compareOverlay(dense,faces,candidate,tolerance,error,work)
				&& compareOverlay(dense,candidate,faces,tolerance,error,work);
			else valid=compareSamples(dense,faces,candidate,tolerance,error,work)
				&& compareSamples(dense,candidate,faces,tolerance,error,work);
		}
		const auto &chosen=valid && count<input.size()?candidate:faces;
		if (&chosen==&candidate) result.sampledError=std::max(result.sampledError,float(error));
		for (const auto &face:chosen) {
			out.triangles.push_back(face.t);
			if (dense.patchAuthority) reducedFacePatches.push_back(origin);
			else out.origins.push_back({origin,face.b});
		}
	}
	// Keep original vertices at their original indices for exact NIF attribute
	// preservation; compact only generated vertices.
	std::size_t original=dense.source->vertices.size();
	if (original>dense.vertices.size()) throw std::invalid_argument("Original vertex array is missing");
	std::vector<bool> used(dense.vertices.size(),false);
	for (const auto &t:out.triangles) for (auto i:t) used[i]=true;
	std::vector<std::uint32_t> remap(dense.vertices.size(),0);
	std::vector<std::uint32_t> oldForNew;
	out.vertices.assign(dense.vertices.begin(),dense.vertices.begin()+original);
	for (std::size_t i=0;i<original;++i) { remap[i]=std::uint32_t(i); oldForNew.push_back(std::uint32_t(i)); }
	for (std::size_t i=original;i<dense.vertices.size();++i) if (used[i]) {
		remap[i]=std::uint32_t(out.vertices.size()); out.vertices.push_back(dense.vertices[i]); oldForNew.push_back(std::uint32_t(i));
	}
	for (auto &t:out.triangles) for (auto &i:t) i=remap[i];
	if (dense.patchAuthority) {
		const auto samples=resolvePatchSources(dense,limits);
		const auto &originalSource=*dense.patchAuthority->original;
		auto reference=std::make_shared<SourceGeometry>(); reference->vertices=out.vertices; reference->triangles=out.triangles;
		for (size_t i=0;i<reference->vertices.size();++i) {
			auto old=oldForNew[i];
			if (old<originalSource.vertices.size()) { reference->vertices[i].position=originalSource.vertices[old].position; continue; }
			const auto &loc=samples.at(old).location; const auto &t=originalSource.triangles.at(loc.triangle);
			for (int j=0;j<3;++j) {
				double value=0; for (int k=0;k<3;++k) value+=loc.barycentric[k]*originalSource.vertices[t[k]].position[j];
				reference->vertices[i].position[j]=float(value);
			}
		}
		auto authority=std::make_shared<PatchAuthority>(*dense.patchAuthority); authority->workingFacePatch=std::move(reducedFacePatches);
		out.source=std::move(reference); out.patchAuthority=std::move(authority); out.origins.clear();
		for (size_t i=0;i<out.triangles.size();++i) out.origins.push_back({std::uint32_t(i),{{{1,0,0},{0,1,0},{0,0,1}}}});
	}
	validate(out,limits);
	if (out.patchAuthority) (void)resolvePatchSources(out,limits);
	result.trianglesAfter=out.triangles.size();
	if (stats) *stats=result;
	return out;
}
}
