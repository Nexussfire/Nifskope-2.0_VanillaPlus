#include "sculpt.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <stdexcept>

namespace VanillaPlus {
namespace {
using D3=std::array<double,3>;
D3 sub(const Vec3 &a,const Vec3 &b) { return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]}; }
D3 cross(const D3 &a,const D3 &b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(const D3 &a,const D3 &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
double length(const D3 &a) { return std::sqrt(dot(a,a)); }
D3 face(const std::vector<Vec3> &p,const Triangle &t) { return cross(sub(p[t[1]],p[t[0]]),sub(p[t[2]],p[t[0]])); }
}
SculptSession::SculptSession(const Mesh &source) : baseline(source),current(source)
{
	validate(source);
	if (!source.source || source.origins.size()!=source.triangles.size()) throw std::invalid_argument("Sculpt needs captured source geometry");
	neighbors.resize(source.vertices.size()); locked.resize(source.vertices.size(),false);
	permanentLocks.resize(source.vertices.size(),false); creaseLocks.resize(source.vertices.size(),false);
	using Edge=std::array<std::uint32_t,2>;
	std::map<Edge,std::vector<std::size_t>> edges;
	std::map<Vec3,std::vector<std::size_t>> coincident;
	for (std::size_t i=0;i<source.vertices.size();++i) { coincident[source.vertices[i].position].push_back(i); permanentLocks[i]=source.vertices[i].boundary; }
	// Separate vertices are never welded. Lock them even if an unrelated shell
	// happens to share their coordinates; do not guess seam equivalence.
	for (const auto &entry:coincident) if (entry.second.size()>1) for (auto i:entry.second) permanentLocks[i]=true;
	for (std::size_t f=0;f<source.triangles.size();++f) {
		const auto &t=source.triangles[f];
		for (int k=0;k<3;++k) {
			auto a=t[k],b=t[(k+1)%3]; neighbors[a].push_back(b); neighbors[b].push_back(a);
			if (a>b) std::swap(a,b);
			edges[{a,b}].push_back(f);
		}
	}
	auto p=positions();
	referenceFaces.reserve(source.triangles.size());
	for(const auto &triangle:source.triangles) { auto n=face(p,triangle); referenceFaces.push_back({n,length(n)}); }
	for (const auto &entry:edges) {
		if (entry.second.size()!=2) {
			for (auto i:entry.first) permanentLocks[i]=true;
		} else {
			auto a=face(p,source.triangles[entry.second[0]]), b=face(p,source.triangles[entry.second[1]]);
			if (dot(a,b)<.5*length(a)*length(b)) // conservative geometric crease >=60 degrees
				for (auto i:entry.first) creaseLocks[i]=true;
		}
	}
	for (std::size_t i=0;i<locked.size();++i) locked[i]=permanentLocks[i] || creaseLocks[i];
	lockPositions=p;
	for (auto &list:neighbors) { std::sort(list.begin(),list.end()); list.erase(std::unique(list.begin(),list.end()),list.end()); }
	historyLimit=std::max<std::size_t>(1,std::min<std::size_t>(32,(64*1024*1024)/(sizeof(Vec3)*source.vertices.size())));
}
std::vector<Vec3> SculptSession::positions() const
{
	Positions p; p.reserve(current.vertices.size()); for (const auto &v:current.vertices) p.push_back(v.position); return p;
}
void SculptSession::setPositions(const Positions &p)
{
	for (std::size_t i=0;i<p.size();++i) {
		current.vertices[i].position=p[i];
		// Undo/Redo can cross a protection toggle. Keep subsequent brush
		// checks anchored to the explicitly restored revision.
		if (locked[i]) lockPositions[i]=p[i];
	}
}
void SculptSession::releaseCreaseLocks(bool release)
{
	endStroke();
	for (std::size_t i=0;i<locked.size();++i) {
		bool protect=permanentLocks[i] || (!release && creaseLocks[i]);
		if (protect && !locked[i]) lockPositions[i]=current.vertices[i].position;
		locked[i]=protect;
	}
}
bool SculptSession::pick(const Vec3 &nearPoint,const Vec3 &farPoint,SculptHit &hit) const
{
	for (int j=0;j<3;++j) if (!std::isfinite(nearPoint[j]) || !std::isfinite(farPoint[j])) return false;
	auto direction=sub(farPoint,nearPoint); double best=1.0; bool found=false;
	for (std::size_t i=0;i<current.triangles.size();++i) {
		const auto &t=current.triangles[i]; const auto &a=current.vertices[t[0]].position;
		auto e1=sub(current.vertices[t[1]].position,a),e2=sub(current.vertices[t[2]].position,a);
		auto n=cross(e1,e2); if (dot(n,direction)>=0) continue;
		auto h=cross(direction,e2); double determinant=dot(e1,h);
		if (std::abs(determinant)<1e-16) continue;
		auto s=sub(nearPoint,a); double u=dot(s,h)/determinant; if (u<0 || u>1) continue;
		auto q=cross(s,e1); double v=dot(direction,q)/determinant; if (v<0 || u+v>1) continue;
		double d=dot(e2,q)/determinant; if (d<0 || d>best) continue;
		best=d; found=true; hit.triangle=i; hit.distance=d*length(direction);
		for (int j=0;j<3;++j) { hit.position[j]=float(nearPoint[j]+direction[j]*d); hit.normal[j]=float(n[j]/length(n)); }
	}
	return found;
}
void SculptSession::beginStroke(const SculptHit &hit)
{
	if (stroking) endStroke();
	if (hit.triangle>=current.triangles.size()) throw std::invalid_argument("Invalid sculpt hit");
	for (int j=0;j<3;++j) if (!std::isfinite(hit.position[j]) || !std::isfinite(hit.normal[j])) throw std::invalid_argument("Nonfinite sculpt hit");
	double n=std::hypot(hit.normal[0],hit.normal[1],hit.normal[2]);
	if (n<1e-12) throw std::invalid_argument("Invalid sculpt plane normal");
	strokeStart=positions(); anchor=hit; for (int j=0;j<3;++j) anchor.normal[j]=float(hit.normal[j]/n); stroking=true;
}
bool SculptSession::safe(const Positions &p,bool revision,const Positions *beforePositions) const
{
	if (p.size()!=current.vertices.size()) return false;
	for (std::size_t i=0;i<p.size();++i) {
		for (float v:p[i]) if (!std::isfinite(v)) return false;
		if (locked[i] && (!revision || permanentLocks[i]) && p[i]!=lockPositions[i]) return false;
	}
	Positions snapshot; if(!beforePositions) { snapshot=positions(); beforePositions=&snapshot; }
	for (std::size_t i=0;i<current.triangles.size();++i) {
		const auto &t=current.triangles[i]; auto proposed=face(p,t),before=face(*beforePositions,t); const auto &original=referenceFaces[i];
		double proposedLength=length(proposed);
		if (proposedLength<original.length*.05 || dot(proposed,before)<=.05*length(before)*proposedLength
			|| dot(proposed,original.normal)<=.05*original.length*proposedLength) return false;
	}
	return true;
}
std::size_t SculptSession::dab(const SculptHit &hit,const SculptBrush &brush)
{
	if (!stroking || hit.triangle>=current.triangles.size()) return 0;
	for (float coordinate:hit.position) if (!std::isfinite(coordinate)) throw std::invalid_argument("Nonfinite sculpt hit");
	if (brush.tool<SculptTool::Push || brush.tool>SculptTool::Restore) throw std::invalid_argument("Unknown sculpt tool");
	if (!std::isfinite(brush.radius) || brush.radius<=0 || !std::isfinite(brush.strength) || brush.strength<0 || brush.strength>3
		|| !std::isfinite(brush.falloff) || brush.falloff<.25 || brush.falloff>8) throw std::invalid_argument("Invalid sculpt brush");
	if(brush.strength==0) return 0;
	Positions before=positions(),proposed=before;
	std::vector<double> distances(before.size(),std::numeric_limits<double>::infinity());
	using Item=std::pair<double,std::uint32_t>; std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
	for (auto i:current.triangles[hit.triangle]) { distances[i]=length(sub(before[i],hit.position)); queue.push({distances[i],i}); }
	bool hasEditableFootprint=false;
	while (!queue.empty()) {
		auto [distance,i]=queue.top(); queue.pop(); if (distance!=distances[i] || distance>=brush.radius) continue;
		if (locked[i]) continue;
		hasEditableFootprint=true;
		for (auto j:neighbors[i]) {
			double next=distance+length(sub(before[j],before[i]));
			if (next<distances[j] && next<brush.radius) { distances[j]=next; queue.push({next,j}); }
		}
	}
	if(!hasEditableFootprint) return 0;
	std::vector<Vec3> normals;
	if(brush.tool==SculptTool::Push || brush.tool==SculptTool::Pull) normals=shadingNormals(current);
	for (std::size_t i=0;i<before.size();++i) if (!locked[i] && distances[i]<brush.radius) {
		double falloff=std::pow(1-distances[i]/brush.radius,brush.falloff);
		double weight=falloff*brush.strength;
		// Depth brushes can exceed unit strength. Target-seeking brushes
		// converge faster without passing through their target or oscillating.
		if (brush.strength>1 && brush.tool!=SculptTool::Push && brush.tool!=SculptTool::Pull)
			weight=1-std::pow(1-falloff,brush.strength);
		D3 delta{};
		if (brush.tool==SculptTool::Push || brush.tool==SculptTool::Pull) {
			double amount=brush.radius*.1*(brush.tool==SculptTool::Pull?-1:1);
			for (int j=0;j<3;++j) delta[j]=normals[i][j]*amount;
		} else if (brush.tool==SculptTool::Flatten) {
			D3 n{anchor.normal[0],anchor.normal[1],anchor.normal[2]}; double d=dot(sub(before[i],anchor.position),n);
			for (int j=0;j<3;++j) delta[j]=-n[j]*d;
		} else if (brush.tool==SculptTool::Restore) delta=sub(baseline.vertices[i].position,before[i]);
		else if (!neighbors[i].empty()) {
			for (auto k:neighbors[i]) for (int j=0;j<3;++j) delta[j]+=double(before[k][j])-before[i][j];
			for (double &d:delta) d/=double(neighbors[i].size());
		}
		for (int j=0;j<3;++j) proposed[i][j]=float(double(before[i][j])+delta[j]*weight);
	}
	if (brush.smoothFalloff && (brush.tool==SculptTool::Push || brush.tool==SculptTool::Pull) && brush.strength>0) {
		// Relax accumulated sculpt offsets, rather than the authored surface.
		// A Jacobi pass avoids ordering bias and only touches this connected
		// brush footprint. Locked seams/boundaries stay fixed. Explicit Smooth,
		// Flatten and Restore retain their own target semantics.
		Positions softened=proposed;
		for (std::size_t i=0;i<before.size();++i) if (!locked[i] && distances[i]<brush.radius && !neighbors[i].empty()) {
			double weight=.35*std::min(brush.strength,1.0)*std::pow(1-distances[i]/brush.radius,brush.falloff);
			for (int j=0;j<3;++j) {
				double average=0;
				for (auto k:neighbors[i]) average+=double(proposed[k][j])-baseline.vertices[k].position[j];
				average/=double(neighbors[i].size());
				double offset=double(proposed[i][j])-baseline.vertices[i].position[j];
				softened[i][j]=float(double(proposed[i][j])+weight*(average-offset));
			}
		}
		proposed=std::move(softened);
	}
	// A dab is atomic. Backtrack all its movements together; no partial cracks.
	if(proposed==before) return 0;
	for (int attempt=0;attempt<10;++attempt) {
		if (safe(proposed,false,&before)) {
			std::size_t changed=0; for (std::size_t i=0;i<before.size();++i) changed+=proposed[i]!=before[i];
			setPositions(proposed); return changed;
		}
		for (std::size_t i=0;i<before.size();++i) for (int j=0;j<3;++j) proposed[i][j]=std::midpoint(before[i][j],proposed[i][j]);
	}
	return 0;
}
void SculptSession::remember(Positions p)
{
	if (past.size()>=historyLimit) past.erase(past.begin());
	past.push_back(std::move(p)); future.clear();
}
bool SculptSession::endStroke()
{
	if (!stroking) return false;
	stroking=false;
	if (positions()==strokeStart) { strokeStart.clear(); return false; }
	remember(std::move(strokeStart)); return true;
}
bool SculptSession::undo()
{
	endStroke(); if (past.empty()) return false; future.push_back(positions()); setPositions(past.back()); past.pop_back(); return true;
}
bool SculptSession::redo()
{
	endStroke(); if (future.empty()) return false; past.push_back(positions()); setPositions(future.back()); future.pop_back(); return true;
}
bool SculptSession::restoreCheckpoint(const Positions &p)
{
	endStroke(); if (positions()==p) return false; if (!safe(p,true)) return false;
	remember(positions()); setPositions(p); return true;
}
}
