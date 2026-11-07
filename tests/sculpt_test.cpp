#include "geometry/sculpt.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace VanillaPlus;
namespace {
int checks=0;
void check(bool condition,const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
Mesh grid()
{
	Mesh mesh;
	for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
		Vertex v; v.position={float(x),float(y),0}; v.uv={float(x)/8,float(y)/8}; v.color={.2f,.3f,.4f,.5f};
		if (x==0) v.uv[0]=-0.0f;
		mesh.vertices.push_back(v);
	}
	for (std::uint32_t y=0;y<8;++y) for (std::uint32_t x=0;x<8;++x) { auto a=y*9+x; mesh.triangles.push_back({a,a+1,a+10}); mesh.triangles.push_back({a,a+10,a+9}); }
	return captureSource(mesh);
}
}
int main()
{
	try {
		auto source=grid(); SculptSession session(source); auto baseline=session.positions(); SculptHit hit;
		check(session.pick({4,4,10},{4,4,-10},hit),"front ray missed"); check(!session.pick({4,4,-10},{4,4,10},hit),"back face accepted");
		check(!session.pick({20,4,10},{20,4,-10},hit),"outside ray accepted");
		check(!session.pick({NAN,4,10},{4,4,-10},hit),"nonfinite ray accepted");
		check(session.pick({4,4,10},{4,4,-10},hit),"front ray lost");
		session.beginStroke(hit); check(session.dab(hit,{SculptTool::Push,3,.8,2})>0,"push affected nothing"); check(session.endStroke(),"stroke not recorded");
		auto raised=session.positions(); check(raised[40][2]>.1,"push failed");
		for (std::size_t i=0;i<baseline.size();++i) {
			if (session.lockedVertices()[i]) check(raised[i]==baseline[i],"boundary moved");
			const auto &a=session.mesh().vertices[i],&b=source.vertices[i];
			check(a.normal==b.normal && a.color==b.color && a.boundary==b.boundary,"attribute changed");
			for (int j=0;j<2;++j) check(std::bit_cast<std::uint32_t>(a.uv[j])==std::bit_cast<std::uint32_t>(b.uv[j]),"UV bits changed");
		}
		check(session.mesh().triangles==source.triangles,"connectivity changed");
		check(session.undo() && session.positions()==baseline,"undo not exact"); check(session.redo() && session.positions()==raised,"redo not exact");
		session.pick({4,4,10},{4,4,-10},hit);
		session.beginStroke(hit); session.dab(hit,{SculptTool::Restore,3,1,2}); session.endStroke(); check(session.positions()[40]==baseline[40],"restore failed");
		check(session.restoreCheckpoint(raised) && session.positions()==raised,"checkpoint not exact");
		session.beginStroke(hit); session.dab(hit,{SculptTool::Smooth,3,1,2}); session.endStroke(); check(session.positions()[40][2]<raised[40][2],"smooth did not reduce peak");
		session.restoreCheckpoint(raised); hit.position={4,4,0}; session.beginStroke(hit); session.dab(hit,{SculptTool::Flatten,3,1,2}); session.endStroke(); check(session.positions()[40][2]<raised[40][2],"flatten anchor plane incorrect");
		session.restoreCheckpoint(baseline); session.beginStroke(hit); session.dab(hit,{SculptTool::Pull,3,1,2}); session.endStroke(); check(session.positions()[40][2]<0,"pull failed");
		auto invalid=baseline; invalid[40][0]=NAN; check(!session.restoreCheckpoint(invalid),"NaN checkpoint accepted");
		invalid=baseline; invalid[0][0]=1; check(!session.restoreCheckpoint(invalid),"locked checkpoint accepted");
		invalid=baseline; invalid[40][0]=100; check(!session.restoreCheckpoint(invalid),"inverted checkpoint accepted");
		bool rejected=false; try { session.beginStroke(hit); session.dab(hit,{SculptTool::Push,3,std::numeric_limits<double>::infinity(),2}); } catch(const std::invalid_argument &) { rejected=true; }
		session.endStroke(); check(rejected,"invalid brush accepted");
		// Disconnected coincident shells remain distinct and fully locked.
		auto shells=source; shells.source.reset(); shells.origins.clear(); auto count=std::uint32_t(source.vertices.size());
		shells.vertices.insert(shells.vertices.end(),source.vertices.begin(),source.vertices.end());
		for (auto t:source.triangles) { for (auto &i:t) i+=count; shells.triangles.push_back(t); }
		SculptSession overlap(captureSource(shells)); check(std::all_of(overlap.lockedVertices().begin(),overlap.lockedVertices().end(),[](bool v){return v;}),"coincident shells not locked");
		// A nearby disconnected back layer does not receive the brush.
		auto layers=shells; for (std::size_t i=count;i<layers.vertices.size();++i) layers.vertices[i].position[2]=-.01f;
		SculptSession separate(captureSource(layers)); separate.pick({4,4,10},{4,4,-10},hit); separate.beginStroke(hit); separate.dab(hit,{SculptTool::Push,3,1,2}); separate.endStroke();
		for (std::size_t i=count;i<layers.vertices.size();++i) check(separate.mesh().vertices[i].position==layers.vertices[i].position,"brush crossed disconnected surface");
		// Full depth strength is three times the previous maximum; optional
		// falloff smoothing softens the peak without changing topology/UVs.
		SculptSession unit(source),strong(source),soft(source),pull(source);
		unit.pick({4,4,10},{4,4,-10},hit);
		unit.beginStroke(hit); unit.dab(hit,{SculptTool::Push,3,1,2}); unit.endStroke();
		strong.beginStroke(hit); strong.dab(hit,{SculptTool::Push,3,3,2}); strong.endStroke();
		soft.beginStroke(hit); soft.dab(hit,{SculptTool::Push,3,3,2,true}); soft.endStroke();
		pull.beginStroke(hit); pull.dab(hit,{SculptTool::Pull,3,3,2,true}); pull.endStroke();
		check(std::abs(strong.positions()[40][2]-3*unit.positions()[40][2])<1e-6,"maximum depth strength not raised to three");
		check(soft.positions()[40][2]>unit.positions()[40][2] && soft.positions()[40][2]<strong.positions()[40][2],"falloff smoothing did not soften the peak");
		check(pull.positions()[40][2]<0 && std::abs(pull.positions()[40][2]+soft.positions()[40][2])<1e-6,"smoothed pull not symmetric");
		SculptSession rawRepeat(source),softRepeat(source); rawRepeat.restoreCheckpoint(raised); softRepeat.restoreCheckpoint(raised);
		rawRepeat.pick({4,4,10},{4,4,-10},hit);
		rawRepeat.beginStroke(hit); rawRepeat.dab(hit,{SculptTool::Push,3,.05,2}); rawRepeat.endStroke();
		softRepeat.beginStroke(hit); softRepeat.dab(hit,{SculptTool::Push,3,.05,2,true}); softRepeat.endStroke();
		check(softRepeat.positions()[40][2]<rawRepeat.positions()[40][2],"enabled stroke did not soften a previous sculpt poke");
		check(softRepeat.mesh().triangles==source.triangles,"stroke smoothing altered connectivity");
		for (std::size_t i=0;i<source.vertices.size();++i) {
			check(soft.mesh().vertices[i].uv==source.vertices[i].uv && soft.mesh().vertices[i].color==source.vertices[i].color,"falloff smoothing changed attributes");
			if (soft.lockedVertices()[i]) check(soft.positions()[i]==baseline[i],"falloff smoothing moved a lock");
		}
		auto softened=soft.positions(); check(soft.undo() && soft.positions()==baseline,"smoothed stroke undo failed"); check(soft.redo() && soft.positions()==softened,"smoothed stroke redo failed");
		strong.pick({4,4,10},{4,4,-10},hit); strong.beginStroke(hit); auto noOp=strong.positions();
		strong.dab(hit,{SculptTool::Push,3,0,2,true}); strong.endStroke(); check(strong.positions()==noOp,"zero strength smoothed existing edits");
		rejected=false; try { strong.beginStroke(hit); strong.dab(hit,{SculptTool::Push,3,3.01,2,true}); } catch(const std::invalid_argument &) { rejected=true; }
		strong.endStroke(); check(rejected,"out-of-range strength accepted");
		for (auto target:{SculptTool::Flatten,SculptTool::Restore,SculptTool::Smooth}) {
			SculptSession seeking(source); seeking.restoreCheckpoint(raised); hit.position={4,4,0}; hit.normal={0,0,1}; seeking.beginStroke(hit);
			seeking.dab(hit,{target,3,3,2,true}); seeking.endStroke();
			for (const auto &p:seeking.positions()) check(p[2]>=0 && p[2]<=raised[40][2],"high-strength target brush overshot");
		}
		// Sharp spikes can be released independently of permanent seam and
		// open-boundary locks. Re-locking preserves the edited revision.
		auto spike=source; spike.source.reset(); spike.origins.clear(); spike.vertices[40].position[2]=4; spike=captureSource(spike);
		SculptSession feature(spike); auto spiky=feature.positions(); check(feature.lockedVertices()[40],"test spike was not crease-locked");
		feature.pick({4,4,20},{4,4,-10},hit); feature.beginStroke(hit); feature.dab(hit,{SculptTool::Smooth,12,1,2}); feature.endStroke();
		check(feature.positions()[40]==spiky[40],"protected spike moved");
		feature.restoreCheckpoint(spiky); auto beforeRelease=feature.positions(); feature.releaseCreaseLocks(true);
		check(feature.positions()==beforeRelease && !feature.lockedVertices()[40],"release moved geometry or retained crease lock");
		for (std::size_t i=0;i<source.vertices.size();++i) if (source.vertices[i].boundary) check(feature.lockedVertices()[i],"open boundary released");
		feature.pick({4,4,20},{4,4,-10},hit); feature.beginStroke(hit); feature.dab(hit,{SculptTool::Smooth,12,1,2}); feature.endStroke();
		check(feature.positions()[40][2]<spiky[40][2],"released spike could not be smoothed"); auto repaired=feature.positions();
		feature.releaseCreaseLocks(false); check(feature.positions()==repaired && feature.lockedVertices()[40],"re-locking snapped repaired spike back");
		feature.pick({4,4,20},{4,4,-10},hit); feature.beginStroke(hit); feature.dab(hit,{SculptTool::Push,12,.2,2}); feature.endStroke();
		check(feature.positions()[40]==repaired[40],"relocked feature moved during brush stroke");
		check(feature.restoreCheckpoint(spiky) && feature.positions()==spiky,"checkpoint could not cross protection change");
		check(feature.undo() && feature.redo() && feature.positions()==spiky,"protection-toggle revision undo/redo failed");
		feature.releaseCreaseLocks(true); check(feature.restoreCheckpoint(repaired),"repair checkpoint failed");
		feature.releaseCreaseLocks(false); feature.pick({2,2,20},{2,2,-10},hit); feature.beginStroke(hit);
		check(feature.dab(hit,{SculptTool::Push,2,.1,2})>0,"re-locking froze unrelated editable vertices"); feature.endStroke();
		overlap.releaseCreaseLocks(true); check(std::all_of(overlap.lockedVertices().begin(),overlap.lockedVertices().end(),[](bool v){return v;}),"release unlocked coincident shells");
		check(feature.mesh().triangles==spike.triangles,"crease release changed topology");
		// Large flatten requests cannot invert the fixed triangle layout.
		session.restoreCheckpoint(baseline); hit.normal={1,0,0}; hit.position={-100,4,0}; session.beginStroke(hit); session.dab(hit,{SculptTool::Flatten,1000,1,.25}); session.endStroke(); validate(session.mesh());
		for (const auto &t:session.mesh().triangles) { const auto &a=session.mesh().vertices[t[0]].position,&b=session.mesh().vertices[t[1]].position,&c=session.mesh().vertices[t[2]].position; check((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])>0,"safety permitted inversion"); }
		std::cout << checks << " sculpt checks passed\n"; return 0;
	} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
