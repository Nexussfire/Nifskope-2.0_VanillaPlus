#include "geometry/vanillaplus.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

using namespace VanillaPlus;
namespace {
int checks = 0;
void check(bool condition, const char *message)
{
	++checks;
	if (!condition)
		throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
template<typename Exception, typename F> void rejects(F f, const char *message)
{
	bool caught = false;
	try { f(); } catch (const Exception &) { caught = true; }
	check(caught, message);
}
Mesh square()
{
	return {{{{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
		{{1, 0, 0}, {0, 0, 1}, {1, 0}, {0, 1, 0, 1}},
		{{1, 1, 0}, {0, 0, 1}, {1, 1}, {0, 0, 1, 1}},
		{{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1}}},
		{{0, 1, 2}, {0, 2, 3}}};
}
bool equal(const Mesh &a, const Mesh &b)
{
	if (a.vertices.size() != b.vertices.size() || a.triangles != b.triangles)
		return false;
	for (std::size_t i = 0; i < a.vertices.size(); ++i) {
		const auto &x = a.vertices[i];
		const auto &y = b.vertices[i];
		if (x.position != y.position || x.normal != y.normal || x.uv != y.uv || x.color != y.color)
			return false;
	}
	return true;
}

void subdivisionTests()
{
	auto original = square();
	auto before = original;
	auto divided = subdivide(original, 1);
	check(divided.vertices.size() == 9, "Shared edge must have exactly one midpoint");
	check(divided.triangles.size() == 8, "Triangle subdivision count");
	check(equal(original, before), "Subdivision changed source");
	check(equal(subdivide(original, 0), original), "Depth zero changed mesh");
	check(equal(subdivide(original, 3), subdivide(original, 3)), "Subdivision not deterministic");
	std::vector<bool> referenced(divided.vertices.size(), false);
	double area = 0;
	for (const auto &t : divided.triangles) {
		const auto &a = divided.vertices[t[0]].position;
		const auto &b = divided.vertices[t[1]].position;
		const auto &c = divided.vertices[t[2]].position;
		double signedArea = ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2;
		check(signedArea > 0, "Subdivision reversed winding");
		area += signedArea;
		for (auto i : t) referenced[i] = true;
	}
	check(near(area, 1), "Subdivision changed surface area");
	check(std::all_of(referenced.begin(), referenced.end(), [](bool b) { return b; }), "Unreferenced new vertex");
	int centers = 0;
	for (const auto &v : divided.vertices) {
		check(near(v.uv[0], v.position[0]) && near(v.uv[1], v.position[1]), "UV alignment changed");
		if (near(v.position[0], 0.5) && near(v.position[1], 0.5)) {
			++centers;
			check(v.color == Color{0.5, 0, 0.5, 1}, "Vertex color interpolation lost");
		}
	}
	check(centers == 1, "Duplicate shared-edge midpoint");
	// Duplicate positions deliberately represent a UV/hard-normal seam.
	auto seam = original;
	seam.vertices.push_back(seam.vertices[0]);
	seam.vertices.push_back(seam.vertices[2]);
	seam.vertices[4].uv = {2, 0};
	seam.vertices[5].uv = {3, 1};
	seam.vertices[4].normal = seam.vertices[5].normal = {0, 1, 0};
	seam.triangles[1] = {4, 5, 3};
	check(subdivide(seam, 1).vertices.size() == 12, "Distinct seam indices were welded");
	rejects<std::length_error>([&] { subdivide(original, 1, {8, 8}); }, "Vertex cap not enforced");
	rejects<std::length_error>([&] { subdivide(original, 1, {9, 7}); }, "Triangle cap not enforced");
	rejects<std::length_error>([&] { subdivide(original, 1000000); }, "Excessive depth not rejected");
	check(equal(original, before), "Failure changed original");
}

void heightTests()
{
	HeightField ramp{2, 2, {0, 1, 0, 1}};
	check(near(sample(ramp, {0.25, 0.25}, Address::Clamp, Address::Clamp), 0), "Texel center sampling");
	check(near(sample(ramp, {0.5, 0.5}, Address::Clamp, Address::Clamp), 0.5), "Bilinear interpolation");
	check(near(sample(ramp, {-0.25, 0.5}, Address::Wrap, Address::Clamp), 1), "Negative UV wrapping");
	check(near(sample(ramp, {-0.25, 0.5}, Address::Clamp, Address::Wrap), 0), "Independent axis clamp");
	check(near(sample(ramp, {1, 0.5}, Address::Wrap, Address::Wrap), 0.5), "Wrap seam interpolation");
	HeightField a{1, 1, {0.25}}, b{1, 1, {0.75}};
	for (auto [mode, expected] : {std::pair{Blend::Add, 1.0f}, {Blend::Multiply, 0.1875f},
		{Blend::Screen, 0.8125f}, {Blend::Overlay, 0.375f}, {Blend::Min, 0.25f}, {Blend::Max, 0.75f}})
		check(near(blend({{&a}, {&b}}, mode).values[0], expected), "Blend arithmetic");
	check(near(blend({{&a}, {&b, 0.5, 1}}, Blend::Add).values[0], 0.625), "Blend weight");
	check(near(blend({{nullptr}, {&a, 1, 2}}, Blend::Add).values[0], 0.5), "Missing source or multiplier");
	rejects<std::invalid_argument>([] { blend({}, Blend::Add); }, "Missing sources silently accepted");
	rejects<std::invalid_argument>([&] { blend({{&a}, {&ramp}}, Blend::Add); }, "Mismatched dimensions accepted");
	Conditioning s;
	check(condition(ramp, s).values == ramp.values, "Default conditioning changed height");
	s.midpoint = 0.25;
	check(near(condition(a, s).values[0], 0.5), "Midpoint mapping");
	s = {};
	s.invert = true;
	s.outputMin = -2;
	s.outputMax = 2;
	check(condition(ramp, s).values == std::vector<float>{2, -2, 2, -2}, "Output range/inversion");
	s.black = s.white;
	rejects<std::invalid_argument>([&] { condition(a, s); }, "Zero input range accepted");
	auto bins = histogram(ramp);
	check(bins[0] == 2 && bins[255] == 2 && std::accumulate(bins.begin(), bins.end(), std::size_t(0)) == 4,
		"Histogram lost samples");
	check(near(diffuseScalar({1, 1, 1, 0}), 1), "Diffuse grayscale");
	check(near(normalReliefCue({0.5, 0.5, 1, 1}), 0.5), "Flat normal must be neutral");
}

void displacementTests()
{
	auto source = square();
	auto before = source;
	HeightField high{1, 1, {1}}, low{1, 1, {0}};
	Displacement s;
	check(equal(displace(source, high, s), source), "Zero strength changed geometry");
	s.strength = 4;
	s.positiveLimit = 0.25;
	s.negativeLimit = 0.125;
	auto up = displace(source, high, s);
	auto down = displace(source, low, s);
	for (std::size_t i = 0; i < source.vertices.size(); ++i) {
		check(near(up.vertices[i].position[2], 0.25), "Positive displacement limit");
		check(near(down.vertices[i].position[2], -0.125), "Negative displacement limit");
		check(up.vertices[i].color == source.vertices[i].color && up.vertices[i].uv == source.vertices[i].uv,
			"Displacement changed attributes");
	}
	check(equal(source, before), "Displacement changed source");
	check(up.triangles == source.triangles, "Displacement changed topology");
	s.strength = 1;
	s.positiveLimit = s.negativeLimit = 10;
	HeightField ramp{2, 1, {0, 1}};
	s.u = Address::Clamp;
	s.uvScale = {0, 0};
	s.uvOffset = {0.75, 0.5};
	check(near(displace(source, ramp, s).vertices[0].position[2], 0.5), "Material UV transform ignored");
	// Large strength makes distinct vertices converge: output must be rejected.
	auto collapse = source;
	collapse.vertices[0].normal = {1, 0, 0};
	collapse.vertices[1].normal = {-1, 0, 0};
	s = {};
	s.strength = 1;
	rejects<std::invalid_argument>([&] { displace(collapse, high, s); }, "Collapsed triangle accepted");
	check(equal(source, before), "Rejected displacement changed input");
}

void filteringAndAdaptiveTests()
{
	HeightField checker{4, 4, {0,1,0,1, 1,0,1,0, 0,1,0,1, 1,0,1,0}};
	auto averaged = areaResample(checker, 2, 2);
	check(std::all_of(averaged.values.begin(), averaged.values.end(), [](float v) { return near(v, 0.5); }), "Area sampling aliased a checkerboard");
	HeightField thirds{3,1,{0,1,0}};
	auto partial = areaResample(thirds, 2, 1);
	check(near(partial.values[0], 1.0/3) && near(partial.values[1], 1.0/3), "Area sampling ignored partial texels");
	check(areaResample(checker, 4, 4).values == checker.values, "Identity area sampling changed data");
	rejects<std::invalid_argument>([&] { areaResample(checker, 0, 1); }, "Empty area sampling accepted");
	HeightField flat{8, 8, std::vector<float>(64, 0.4f)};
	auto shallowFlat=shallowSurfaceDetail(flat,4,0.5f,Address::Clamp,Address::Clamp);
	check(std::all_of(shallowFlat.values.begin(),shallowFlat.values.end(),[](float v) { return near(v,0.5); }),
		"Shallow surface converted a constant height bias into bulging");
	HeightField localizedDetail{17,17,std::vector<float>(289,0.7f)}; localizedDetail.values[8*17+8]=0.9f;
	auto shallowDetail=shallowSurfaceDetail(localizedDetail,4,0.5f,Address::Clamp,Address::Clamp);
	check(shallowDetail.values[8*17+8]>0.65f && std::abs(shallowDetail.values[0]-0.5f)<0.01f,
		"Shallow surface failed to retain local detail or suppress broad bias");
	HeightField seamSignal{9,1,{1,0,0,0,0,0,0,0,0}};
	auto wrappedDetail=shallowSurfaceDetail(seamSignal,1,0.5f,Address::Wrap,Address::Clamp);
	auto clampedDetail=shallowSurfaceDetail(seamSignal,1,0.5f,Address::Clamp,Address::Clamp);
	check(wrappedDetail.values.back()<clampedDetail.values.back(), "Shallow surface ignored the material wrap mode");
	rejects<std::invalid_argument>([&] { shallowSurfaceDetail(flat,0,0.5f); }, "Invalid shallow detail radius accepted");
	auto filtered = bilateral(flat, 2, 0.1f, Address::Clamp, Address::Clamp);
	auto highScale = bilateral(flat, 8, 0.1f, Address::Clamp, Address::Clamp);
	check(std::all_of(highScale.values.begin(),highScale.values.end(),[](float v) { return near(v,0.4); }), "High-resolution bilateral scale changed a constant field");
	auto highBands=frequencyBands(flat,2,16,0.85f,0,Address::Clamp,Address::Clamp);
	check(std::all_of(highBands.values.begin(),highBands.values.end(),[](float v) { return near(v,0.4); }), "High-resolution frequency scale changed a constant field");
	check(std::all_of(filtered.values.begin(), filtered.values.end(), [](float v) { return near(v, 0.4); }), "Bilateral changed a constant field");
	HeightField step{9, 1, {0,0,0,0,0,1,1,1,1}};
	auto edge = bilateral(step, 2, 0.08f, Address::Clamp, Address::Clamp);
	check(edge.values[4] < 0.01f && edge.values[5] > 0.99f, "Bilateral blurred across a strong edge");
	HeightField noise{5, 1, {0.5f,0.5f,0.55f,0.5f,0.5f}};
	auto clean = bilateral(noise, 1, 0.2f, Address::Clamp, Address::Clamp);
	check(clean.values[2] < 0.55f && clean.values[2] > 0.5f, "Bilateral did not smooth local noise");
	check(bilateral(noise, 0, 0.1f).values == noise.values, "Zero smoothing changed input");
	rejects<std::invalid_argument>([&] { bilateral(noise, 1, 0); }, "Zero range sigma accepted");
	check(frequencyBands(noise, 1, 3, 1, 1).values == noise.values, "Unity band gains changed input");
	auto limited = frequencyBands(noise, 1, 3, 1, 0, Address::Clamp, Address::Clamp);
	check(limited.values[2] < noise.values[2], "Fine band suppression did not reduce noise");
	filtered = frequencyBands(flat, 1, 3, 0, 0);
	check(std::all_of(filtered.values.begin(), filtered.values.end(), [](float v) { return near(v, 0.4); }), "Frequency filter changed constant field");
	rejects<std::invalid_argument>([&] { frequencyBands(noise, 3, 1, 1, 0); }, "Invalid band ordering accepted");

	auto source = square();
	Displacement mapping; mapping.u = mapping.v = Address::Clamp;
	HeightField borderOnly{9,9,std::vector<float>(81,0.5f)};
	borderOnly.values[2]=1.0f; borderOnly.values[6]=1.0f;
	Displacement pinned=mapping; pinned.pinBoundaries=true; pinned.neutral=0.5f;
	check(equal(subdivideAdaptive(source,borderOnly,1,0.05f,pinned),source),
		"Height on an immovable boundary caused needless subdivision");
	borderOnly.values[4*9+4]=1.0f;
	check(subdivideAdaptive(source,borderOnly,1,0.05f,pinned).triangles.size()>source.triangles.size(),
		"Pinned boundary rule suppressed interior detail");
	check(equal(subdivideAdaptive(source, flat, 3, 0.1f, mapping), source), "Flat height caused adaptive subdivision");
	HeightField ramp{8,8,std::vector<float>(64)};
	for (size_t y=0;y<8;++y) for (size_t x=0;x<8;++x) ramp.values[y*8+x]=float(x)/7;
	auto rampMesh=source;
	for (auto &v : rampMesh.vertices) for (float &uv : v.uv) uv=0.0625f+0.875f*uv;
	check(equal(subdivideAdaptive(rampMesh,ramp,3,0.001f,mapping),rampMesh), "Representable steep linear ramp was subdivided");
	auto mirrored=rampMesh;
	for (auto &v : mirrored.vertices) v.uv[0]=1-v.uv[0];
	check(equal(subdivideAdaptive(mirrored,ramp,3,0.001f,mapping),mirrored), "Mirrored linear ramp was subdivided");
	HeightField interior{9,9,std::vector<float>(81,0)};
	interior.values[3*9+6]=1; interior.values[6*9+3]=1;
	auto captured=subdivideAdaptive(source,interior,1,0.05f,mapping);
	check(captured.triangles.size()>source.triangles.size(), "Interior representation error was missed");
	check(equal(captured,subdivide(source,1)), "Fully refined faces do not use balanced uniform connectivity");
	check(equal(subdivideAdaptive(source,interior,1,2.0f,mapping),source), "High error tolerance did not suppress refinement");
	HeightField localized{8, 8, std::vector<float>(64, 0)};
	for (size_t y = 0; y < 3; ++y) for (size_t x = 0; x < 3; ++x) localized.values[y*8+x] = 1;
	auto adaptive = subdivideAdaptive(source, localized, 2, 0.2f, mapping);
	check(adaptive.triangles.size() > source.triangles.size(), "Adaptive subdivision missed detail");
	check(adaptive.triangles.size() < subdivide(source, 2).triangles.size(), "Adaptive subdivision was not selective");
	check(equal(adaptive, subdivideAdaptive(source, localized, 2, 0.2f, mapping)), "Adaptive subdivision not deterministic");
	auto measuredError = [&](const Mesh &m) {
		double total=0;
		for (int y=0;y<16;++y) for (int x=0;x<16;++x) {
			double px=(x+0.5)/16, py=(y+0.5)/16; bool found=false;
			for (const auto &t : m.triangles) {
				const auto &a=m.vertices[t[0]].position, &b=m.vertices[t[1]].position, &c=m.vertices[t[2]].position;
				double den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
				double wa=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/den;
				double wb=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/den, wc=1-wa-wb;
				if (wa < -1e-7 || wb < -1e-7 || wc < -1e-7) continue;
				double estimate=wa*sampleMapped(localized,m.vertices[t[0]].uv,mapping)
					+wb*sampleMapped(localized,m.vertices[t[1]].uv,mapping)
					+wc*sampleMapped(localized,m.vertices[t[2]].uv,mapping);
				double diff=estimate-sampleMapped(localized,{float(px),float(py)},mapping); total+=diff*diff; found=true; break;
			}
			check(found, "Adaptive measurement found a coverage gap");
		}
		return total;
	};
	check(measuredError(adaptive)<measuredError(source), "Adaptive refinement did not improve sampled height representation");
	double area = 0;
	std::map<std::pair<unsigned,unsigned>, int> edges;
	for (const auto &t : adaptive.triangles) {
		const auto &a=adaptive.vertices[t[0]].position, &b=adaptive.vertices[t[1]].position, &c=adaptive.vertices[t[2]].position;
		double signedArea = ((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2;
		check(signedArea > 0, "Adaptive triangle winding"); area += signedArea;
		for (int i=0;i<3;++i) { auto x=t[i], y=t[(i+1)%3]; ++edges[{std::min(x,y),std::max(x,y)}]; }
	}
	check(near(area, 1), "Adaptive subdivision changed surface area");
	for (const auto &[e, count] : edges) {
		const auto &a=adaptive.vertices[e.first].position, &b=adaptive.vertices[e.second].position;
		bool boundary = (near(a[0],0)&&near(b[0],0)) || (near(a[0],1)&&near(b[0],1))
			|| (near(a[1],0)&&near(b[1],0)) || (near(a[1],1)&&near(b[1],1));
		check(count == (boundary ? 1 : 2), "Adaptive mesh has a crack or unmatched edge");
	}
	auto seam = source;
	seam.vertices.push_back(seam.vertices[0]); seam.vertices.push_back(seam.vertices[2]);
	seam.vertices[4].normal = seam.vertices[5].normal = {0, 1, 0};
	seam.triangles[1] = {4, 5, 3};
	auto splitSeam = subdivideAdaptive(seam, localized, 1, 0.2f, mapping);
	int seamMidpoints = 0;
	for (const auto &v : splitSeam.vertices)
		if (near(v.position[0], 0.5) && near(v.position[1], 0.5)) ++seamMidpoints;
	check(seamMidpoints == 2, "Adaptive subdivision welded a hard seam");
	rejects<std::length_error>([&] { subdivideAdaptive(source, localized, 2, 0.2f, mapping, {4, 100}); }, "Adaptive vertex limit not enforced");
	rejects<std::length_error>([&] { subdivideAdaptive(source, localized, 2, 0.2f, mapping, {100, 2}); }, "Adaptive triangle limit not enforced");
	check(equal(source, square()), "Adaptive subdivision mutated input");
}

void uvAuthorityTests()
{
	Mesh mesh = square();
	mesh.vertices[0].uv={-3.0f,2.0f}; mesh.vertices[1].uv={4.0f,-1.0f};
	mesh.vertices[2].uv={1.0f,5.0f}; mesh.vertices[3].uv={-2.0f, -3.0f};
	auto original=mesh;
	HeightField texture{4,4,{0,0.1f,0.2f,0.3f, 0.1f,0.2f,0.3f,0.4f,
		0.2f,0.3f,0.4f,0.5f, 0.3f,0.4f,0.5f,0.6f}};
	Displacement mapping; mapping.uvScale={-1.5f,2.25f}; mapping.uvOffset={0.3f,-0.8f};
	for (auto u : {Address::Clamp,Address::Wrap}) for (auto v : {Address::Clamp,Address::Wrap}) {
		mapping.u=u; mapping.v=v;
		for (auto uv : {Vec2{-2.2f,3.7f},Vec2{0.3f,-0.2f},Vec2{2,4}}) {
			Vec2 transformed{uv[0]*mapping.uvScale[0]+mapping.uvOffset[0], uv[1]*mapping.uvScale[1]+mapping.uvOffset[1]};
			check(near(sampleMapped(texture,uv,mapping),sample(texture,transformed,u,v)), "UV transform/address ordering");
		}
		auto grid=sampleMappedGrid(texture,{-2,1},{2,5},2,2,mapping);
		check(near(grid.values[0],sampleMapped(texture,{-1,2},mapping))
			&& near(grid.values[3],sampleMapped(texture,{1,4},mapping)), "UV preview samples disagree with displacement mapping");
	}
	auto verify=[](const Mesh &result) {
		check(bool(result.source) && result.origins.size()==result.triangles.size(), "Missing source provenance");
		for (size_t f=0;f<result.triangles.size();++f) {
			const auto &o=result.origins[f];
			check(o.triangle<result.source->triangles.size(), "Invalid provenance triangle");
			const auto &t=result.source->triangles[o.triangle];
			for (int corner=0;corner<3;++corner) {
				const auto &b=o.corners[corner]; const auto &v=result.vertices[result.triangles[f][corner]];
				check(near(b[0]+b[1]+b[2],1), "Invalid barycentric sum");
				for (int j=0;j<2;++j) {
					double expected=0; for (int i=0;i<3;++i) expected+=b[i]*result.source->vertices[t[i]].uv[j];
					check(near(v.uv[j],expected), "UV does not follow original triangle");
				}
				for (int j=0;j<3;++j) {
					double expected=0; for (int i=0;i<3;++i) expected+=b[i]*result.source->vertices[t[i]].position[j];
					check(near(v.position[j],expected), "Position does not follow original triangle");
				}
				for (int j=0;j<4;++j) {
					double expected=0; for (int i=0;i<3;++i) expected+=b[i]*result.source->vertices[t[i]].color[j];
					check(near(v.color[j],expected), "Vertex color or alpha does not follow original triangle");
				}
			}
		}
	};
	verify(subdivide(mesh,3));
	verify(subdivideAdaptive(mesh,texture,2,0.08f,mapping));
	check(equal(mesh,original), "Original UV geometry changed");
	// Two adjacent triangles use duplicated edge vertices, as at a UV seam.
	Mesh seam=mesh;
	seam.vertices.push_back(seam.vertices[0]); seam.vertices.push_back(seam.vertices[2]);
	seam.vertices[4].uv={10,5}; seam.vertices[5].uv={12,7}; seam.triangles[1]={4,5,3};
	seam.vertices[4].color={0.1f,0.2f,0.3f,0.4f}; seam.vertices[5].color={0.5f,0.6f,0.7f,0.8f};
	auto working=subdivide(seam,2);
	mapping.pinBoundaries=true; mapping.strength=1; mapping.neutral=0; mapping.positiveLimit=2;
	HeightField high{1,1,{1}};
	auto displaced=displace(working,high,mapping);
	bool foundInterior=false;
	for (size_t i=0;i<working.vertices.size();++i) {
		if (working.vertices[i].boundary)
			check(displaced.vertices[i].position==working.vertices[i].position, "Boundary or seam moved");
		else if (displaced.vertices[i].position!=working.vertices[i].position) foundInterior=true;
		check(displaced.vertices[i].uv==working.vertices[i].uv, "Displacement changed UV");
	}
	check(foundInterior, "All interior vertices were pinned");
	verify(working);
	Mesh shells;
	for (int i=0;i<3;++i) shells.vertices.push_back(mesh.vertices[size_t(i)]);
	for (int i=0;i<3;++i) { auto v=mesh.vertices[size_t(i)]; v.uv[0]+=8; shells.vertices.push_back(v); }
	shells.triangles={{0,1,2},{3,4,5}};
	auto separate=subdivide(shells,1);
	check(separate.vertices.size()==12, "Coincident shells were welded");
	verify(separate);
}

void shadingNormalTests()
{
	Mesh m=square();
	m.vertices[0].normal=m.vertices[1].normal=m.vertices[2].normal={0,0.6f,0.8f};
	m.vertices.push_back(m.vertices[0]); m.vertices.push_back(m.vertices[2]);
	m.vertices[3].normal=m.vertices[4].normal=m.vertices[5].normal={0,-0.6f,0.8f};
	m.triangles[1]={4,5,3};
	auto working=subdivide(m,2);
	auto normal=shadingNormals(working);
	for (size_t i=0;i<working.vertices.size();++i)
		check(normal[i]==working.vertices[i].normal,"Undisplaced tessellation changed an authored shading normal");
	check(normal[0]!=normal[4] && working.vertices[0].position==working.vertices[4].position,
		"Hard-normal seam was merged");
	auto displaced=working; bool changed=false;
	for (auto &v:displaced.vertices) if (near(v.position[0],0.75) && near(v.position[1],0.25)) {
		v.position[2]+=0.1f; changed=true; break;
	}
	check(changed,"Missing shading test vertex");
	auto corrected=shadingNormals(displaced);
	bool updated=false;
	for (size_t i=0;i<corrected.size();++i) {
		if (corrected[i]!=normal[i]) updated=true;
		if (working.vertices[i].normal[1]<0) check(corrected[i]==normal[i],"Displacement changed another smoothing seam");
	}
	check(updated,"Displaced shading received no correction");
	check(equal(working,subdivide(m,2)),"Shading calculation changed geometry");
}

void reductionTests()
{
	auto original=square(); auto dense=subdivide(original,3); auto before=dense;
	ReductionStats stats;
	check(equal(reduceReconstruction(dense,0,&stats),dense), "Zero tolerance changed dense mesh");
	auto reduced=reduceReconstruction(dense,0.001f,&stats);
	check(stats.trianglesBefore==dense.triangles.size() && stats.trianglesAfter==reduced.triangles.size(), "Reduction statistics incorrect");
	check(reduced.triangles.size()<dense.triangles.size(), "Flat dense surface was not reduced");
	check(stats.sampledError<=0.001f, "Reduction exceeded sampled error limit");
	check(equal(dense,before), "Reduction mutated dense input");
	check(equal(reduced,reduceReconstruction(dense,0.001f)), "Reduction not deterministic");
	for (std::size_t i=0;i<original.vertices.size();++i)
		check(reduced.vertices[i].position==dense.vertices[i].position && reduced.vertices[i].uv==dense.vertices[i].uv
			&& reduced.vertices[i].color==dense.vertices[i].color, "Original vertex attributes changed");
	for (const auto &v:dense.vertices) if (v.boundary) {
		bool found=false;
		for (const auto &w:reduced.vertices) if (v.position==w.position && v.uv==w.uv && v.color==w.color) { found=true; break; }
		check(found, "Boundary vertex removed by reduction");
	}
	for (std::size_t i=0;i<reduced.triangles.size();++i) {
		const auto &o=reduced.origins[i]; const auto &source=reduced.source->triangles[o.triangle];
		for (int c=0;c<3;++c) for (int j=0;j<2;++j) {
			double expected=0; for (int k=0;k<3;++k) expected+=o.corners[c][k]*reduced.source->vertices[source[k]].uv[j];
			check(near(reduced.vertices[reduced.triangles[i][c]].uv[j],expected), "Reduction crossed UV mapping boundary");
		}
	}
	auto broken=dense; broken.origins.clear();
	rejects<std::invalid_argument>([&] { reduceReconstruction(broken,0.1f); }, "Reduction accepted missing provenance");
	auto seamed=original;
	seamed.vertices.push_back(seamed.vertices[0]); seamed.vertices.push_back(seamed.vertices[2]);
	seamed.vertices[4].uv={3,4}; seamed.vertices[5].uv={5,6};
	seamed.vertices[4].color={0,0,0,0}; seamed.vertices[5].color={0,0,0,0};
	seamed.triangles[1]={4,5,3};
	auto denseSeam=subdivide(seamed,3); auto reducedSeam=reduceReconstruction(denseSeam,0.001f);
	for (const auto &v:denseSeam.vertices) if (v.boundary) {
		bool found=false;
		for (const auto &w:reducedSeam.vertices) if (v.position==w.position && v.uv==w.uv && v.color==w.color) { found=true; break; }
		check(found,"Reduction removed or merged a seam boundary");
	}
	auto ridge=dense;
	bool ridgeSet=false;
	for (auto &v:ridge.vertices) if (near(v.position[0],0.5) && near(v.position[1],0.25)) {
		v.position[2]=1; ridgeSet=true; break;
	}
	check(ridgeSet,"Ridge test vertex missing");
	auto ridgeReduced=reduceReconstruction(ridge,0.5f);
	bool ridgeFound=false;
	for (const auto &v:ridgeReduced.vertices) if (near(v.position[0],0.5) && near(v.position[1],0.25) && near(v.position[2],1)) ridgeFound=true;
	check(ridgeFound,"Sharp reconstructed feature was removed");
}

void triangleCleanupTests()
{
	Mesh m;
	m.vertices={{{0.2f,0.4f,0},{0,0,1},{0.2f,0.4f}},
		{{0.8f,0.4f,0},{0,0,1},{0.8f,0.4f}},
		{{0.237f,0.411f,0},{0,0,1},{0.237f,0.411f}},
		{{0.314f,0.309f,0},{0,0,1},{0.314f,0.309f}}};
	m.triangles={{0,1,2},{1,0,3}};
	Mesh source{{{{0,0,0},{0,0,1},{0,0}},{{2,0,0},{0,0,1},{2,0}},{{0,2,0},{0,0,1},{0,2}}},{{0,1,2}}};
	m.source=std::make_shared<const SourceGeometry>(SourceGeometry{source.vertices,source.triangles});
	auto bary=[](const Vertex &v) -> Barycentric { return {1-double(v.uv[0])/2-double(v.uv[1])/2,double(v.uv[0])/2,double(v.uv[1])/2}; };
	m.origins={{0,{bary(m.vertices[0]),bary(m.vertices[1]),bary(m.vertices[2])}},
		{0,{bary(m.vertices[1]),bary(m.vertices[0]),bary(m.vertices[3])}}};
	auto before=m; std::size_t flips=0;
	auto improved=improveTriangleQuality(m,&flips);
	check(flips==1 && improved.triangles!=m.triangles, "Poor coplanar triangle pair was not improved");
	for (std::size_t i=0;i<m.vertices.size();++i)
		check(improved.vertices[i].position==m.vertices[i].position && improved.vertices[i].uv==m.vertices[i].uv,
			"Cleanup moved a vertex or changed UVs");
	check(equal(m,before), "Cleanup mutated input");
	check(equal(improved,improveTriangleQuality(m)), "Cleanup is not deterministic");
	auto protectedEdge=m; protectedEdge.origins[1].triangle=1;
	check(improveTriangleQuality(protectedEdge,&flips).triangles==m.triangles && flips==0, "Original edge was flipped");
	auto folded=m; folded.vertices[3].position[2]=1;
	check(improveTriangleQuality(folded,&flips).triangles==m.triangles && flips==0, "Large surface change was accepted");
	auto noSource=m; noSource.source.reset();
	rejects<std::invalid_argument>([&] { improveTriangleQuality(noSource); }, "Cleanup accepted missing provenance");
}

void pinchProtectionTests()
{
	auto base=subdivide(square(),3);
	HeightField checker{4,4,{0,1,0,1, 1,0,1,0, 0,1,0,1, 1,0,1,0}};
	Displacement s; s.u=s.v=Address::Clamp; s.strength=20; s.neutral=0.5f;
	s.positiveLimit=s.negativeLimit=20; s.pinBoundaries=true; s.preventPinching=true; s.maxSlope=0.4f;
	DisplacementStats stats;
	auto safe=displace(base,checker,s,{},&stats);
	check(stats.limitedVertices>0, "Pinch protection did not limit steep height changes");
	bool interiorMoved=false;
	for (size_t i=0;i<base.vertices.size();++i) {
		check(safe.vertices[i].uv==base.vertices[i].uv, "Pinch protection changed UVs");
		if (base.vertices[i].boundary) check(safe.vertices[i].position==base.vertices[i].position, "Pinch protection moved seam");
		else if (!near(safe.vertices[i].position[2],base.vertices[i].position[2])) interiorMoved=true;
	}
	check(interiorMoved, "Pinch protection discarded all detail");
	for (const auto &t : base.triangles) {
		const auto &a=safe.vertices[t[0]].position, &b=safe.vertices[t[1]].position, &c=safe.vertices[t[2]].position;
		check((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])>0, "Protected triangle flipped");
		for (int i=0;i<3;++i) {
			auto x=t[i],y=t[(i+1)%3]; const auto &p=base.vertices[x].position, &q=base.vertices[y].position;
			double len=std::hypot(double(p[0])-q[0],double(p[1])-q[1],double(p[2])-q[2]);
			double dh=(safe.vertices[x].position[2]-p[2])-(safe.vertices[y].position[2]-q[2]);
			check(std::abs(dh)<=double(s.maxSlope)*len+1e-5, "Displacement slope exceeds limit");
		}
	}
	Mesh fold{{{{0,0,0},{1,0,0},{0,0}}, {{1,0,0},{0,0,1},{1,0}}, {{0,1,0},{0,0,1},{0,1}}},{{0,1,2}}};
	HeightField high{1,1,{1}}; s.pinBoundaries=false; s.strength=4; s.neutral=0; s.maxSlope=100;
	auto protectedFold=displace(fold,high,s,{},&stats);
	check(stats.limitedVertices>0 && stats.orientationScale>0, "Flip prevention did not back off displacement");
	check(protectedFold.vertices[0].position[0]<0.9f, "Flip prevention left inverted triangle");
	Mesh separate=fold;
	separate.vertices.push_back({{10,0,0},{0,0,1},{0,0}});
	separate.vertices.push_back({{11,0,0},{0,0,1},{1,0}});
	separate.vertices.push_back({{10,1,0},{0,0,1},{0,1}});
	separate.triangles.push_back({3,4,5});
	auto protectedSeparate=displace(separate,high,s,{},&stats);
	check(stats.orientationScale==1 && near(protectedSeparate.vertices[3].position[2],4),
		"Local flip protection reduced displacement on an unrelated surface");
	Mesh constant=square(); for (auto &v : constant.vertices) v.normal={0,0,1};
	s.strength=1; s.maxSlope=0.5f;
	auto constantOut=displace(constant,high,s,{},&stats);
	check(stats.limitedVertices==0 && stats.orientationScale==1 && near(constantOut.vertices[0].position[2],1),
		"Pinch protection changed a safe constant displacement");
	check(equal(base,subdivide(square(),3)), "Pinch protection mutated source geometry");
}

void sourcePatchTests()
{
	auto mesh=square(); for (auto &v:mesh.vertices) v.color={1,1,1,1};
	auto patches=identifySourcePatches(mesh);
	check(patches.size()==1 && patches[0].triangles.size()==2, "Compatible coplanar triangles did not form a patch");
	auto boundary=recoverPatchBoundary(mesh,patches[0],{{0,1}});
	validatePatchDomain(mesh,patches[0],{{0,1}});
	check(boundary.loops.size()==1 && boundary.loops[0].size()==4, "Square boundary was not recovered");
	bool protectedFound=false;
	for (size_t i=0;i<boundary.loops[0].size();++i) {
		const auto &edge=boundary.loops[0][i], &next=boundary.loops[0][(i+1)%boundary.loops[0].size()];
		check(edge.to==next.from, "Boundary loop is not ordered and closed");
		if (edge.from==0 && edge.to==1) protectedFound=edge.protectedEdge;
	}
	check(protectedFound, "Protected boundary edge was lost");
	PatchRemesh remesh;
	auto alternate=mesh; alternate.triangles={{0,1,3},{1,2,3}};
	check(remeshFourEdgePatch(alternate,SourcePatch{{0,1}},2,remesh), "Four-edge patch remeshing failed");
	check(remesh.vertices.size()==9 && remesh.triangles.size()==8 && remesh.sourceLocations.size()==9,
		"Patch lattice has incorrect dimensions or correspondence");
	check(remesh.triangles[0]==Triangle{0,1,4}, "Patch triangulation unexpectedly retained the original diagonal");
	for (size_t i=0;i<remesh.vertices.size();++i) {
		const auto &v=remesh.vertices[i]; const auto &loc=remesh.sourceLocations[i];
		const auto &t=alternate.triangles[loc.triangle];
		for (int j=0;j<2;++j) {
			double expected=0; for (int k=0;k<3;++k) expected+=loc.barycentric[k]*alternate.vertices[t[k]].uv[j];
			check(near(v.uv[j],expected), "Patch lattice UV differs from source barycentrics");
		}
	}
	PatchRemesh untouched=remesh;
	check(!remeshFourEdgePatch(mesh,SourcePatch{{0}},2,remesh) && remesh.triangles==untouched.triangles,
		"Unsupported patch did not preserve the previous result");
	PatchRemesh verified;
	auto accepted=tryRetopologizePatch(alternate,SourcePatch{{0,1}},2,verified);
	check(accepted.accepted && !accepted.fallback && accepted.reason=="Accepted", "Verified patch transaction failed");
	check(accepted.sourceFaces==2 && accepted.loops==1 && accepted.boundaryEdges==4 && accepted.generatedTriangles==8,
		"Patch transaction diagnostics are incomplete");
	check(verified.sourcePatch.triangles==SourcePatch{{0,1}}.triangles, "Patch identity was not retained");
	std::vector<PatchRetopoReport> reports;
	auto patchWorking=retopologizeCoplanarPatches(alternate,reports);
	check(reports.size()==1 && reports[0].accepted && patchWorking.triangles!=alternate.triangles,
		"Verified patch did not replace the old internal diagonal");
	std::vector<PatchRetopoReport> fallbackReports;
	auto fallbackMesh=retopologizeCoplanarPatches(alternate,fallbackReports,{{1,3}});
	check(fallbackMesh.triangles==alternate.triangles && fallbackMesh.vertices.size()==alternate.vertices.size(),
		"Rejected protected patch changed source topology");
	check(patchWorking.vertices.size()==alternate.vertices.size(), "Patch replacement altered vertex count");
	for (size_t i=0;i<alternate.vertices.size();++i)
		check(patchWorking.vertices[i].position==alternate.vertices[i].position && patchWorking.vertices[i].uv==alternate.vertices[i].uv,
			"Patch replacement altered source position or UV");
	auto subdivided=subdivide(patchWorking,2);
	auto correspondence=resolvePatchSources(subdivided);
	check(correspondence.size()==subdivided.vertices.size(), "Subdivided patch lost source correspondence");
	Displacement d; d.strength=0;
	HeightField flat{1,1,{0.5f}};
	auto displaced=displace(subdivided,flat,d);
	(void)shadingNormals(displaced);
	auto reduced=reduceReconstruction(displaced,0.001f);
	check(resolvePatchSources(reduced).size()==reduced.vertices.size(), "Reduced patch lost source correspondence");
	PatchRemesh protectedResult;
	auto protectedReport=tryRetopologizePatch(alternate,SourcePatch{{0,1}},2,protectedResult,{{0,1}});
	check(protectedReport.accepted && protectedReport.protectedEdges==1 && protectedResult.protectedBoundaryEdges.size()==2,
		"Protected boundary edge was not retained in patch correspondence");
	auto damaged=verified; damaged.vertices[4].uv[0]+=0.1f;
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Altered UV candidate passed verification");
	damaged=verified; damaged.sourceLocations[4].barycentric[0]+=0.25;
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Altered provenance passed verification");
	damaged=verified; std::swap(damaged.triangles[0][1],damaged.triangles[0][2]);
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Inverted face passed verification");
	damaged=verified; damaged.triangles[1]=damaged.triangles[0];
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Overlapping candidate faces passed verification");
	damaged=verified; damaged.triangles[0][0]=999;
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Out-of-range candidate index passed verification");
	damaged=verified; damaged.vertices.push_back(damaged.vertices[0]); damaged.sourceLocations.push_back(damaged.sourceLocations[0]);
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Unreferenced candidate vertex passed verification");
	damaged=protectedResult; damaged.protectedBoundaryEdges.clear();
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged,{{0,1}}).accepted, "Lost protected edge passed verification");
	damaged=verified; damaged.vertices[1].position[1]+=0.1f;
	check(!verifyRetopologyCandidate(alternate,SourcePatch{{0,1}},2,damaged).accepted, "Altered boundary passed verification");
	auto unchanged=verified;
	auto rejected=tryRetopologizePatch(alternate,SourcePatch{{0}},2,verified);
	check(!rejected.accepted && rejected.fallback && !rejected.reason.empty() && verified.triangles==unchanged.triangles,
		"Unsupported patch transaction did not preserve output");
	auto budget=tryRetopologizePatch(alternate,SourcePatch{{0,1}},9,verified);
	check(!budget.accepted && budget.fallback && !budget.reason.empty() && verified.triangles==unchanged.triangles,
		"Unsupported lattice size did not fall back");
	auto forbidden=tryRetopologizePatch(alternate,SourcePatch{{0,1}},2,verified,{{1,3}});
	check(!forbidden.accepted && forbidden.fallback && !forbidden.reason.empty() && verified.triangles==unchanged.triangles,
		"Protected internal edge transaction did not fall back");
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(mesh,patches[0],{{0,2}}); }, "Protected internal edge was cancelled");
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(mesh,SourcePatch{{0,0}}); }, "Duplicate patch triangle accepted");
	Mesh ring;
	for (auto p:std::vector<Vec2>{{0,0},{3,0},{3,3},{0,3},{1,1},{2,1},{2,2},{1,2}}) {
		Vertex v; v.position={p[0],p[1],0}; v.uv=p; ring.vertices.push_back(v);
	}
	ring.triangles={{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
	auto ringBoundary=recoverPatchBoundary(ring,SourcePatch{{0,1,2,3,4,5,6,7}});
	validatePatchDomain(ring,SourcePatch{{0,1,2,3,4,5,6,7}});
	check(ringBoundary.loops.size()==2, "Patch hole was not retained as a separate boundary loop");
	check(ringBoundary.loops[0].size()==4 && ringBoundary.loops[1].size()==4, "Patch hole boundary has incorrect edges");
	Mesh straight;
	for (auto p:std::vector<Vec2>{{0,0},{0.5f,0},{1,0},{1,1},{0,1},{0.5f,0.5f}}) {
		Vertex v; v.position={p[0],p[1],0}; v.uv=p; straight.vertices.push_back(v);
	}
	straight.triangles={{0,1,5},{1,2,5},{2,3,5},{3,4,5},{4,0,5}};
	auto straightBoundary=recoverPatchBoundary(straight,SourcePatch{{0,1,2,3,4}},{{0,1},{1,2}});
	check(straightBoundary.loops.size()==1 && straightBoundary.loops[0].size()==5, "Straight boundary vertex was simplified");
	check(std::count_if(straightBoundary.loops[0].begin(),straightBoundary.loops[0].end(),
		[](const PatchBoundaryEdge &e) { return e.protectedEdge; })==2, "Straight protected edges were lost");
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(ring,SourcePatch{{0,4}}); }, "Disconnected patch accepted");
	auto reversed=mesh; reversed.triangles[1]={3,2,0};
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(reversed,SourcePatch{{0,1}}); }, "Inconsistent patch winding accepted");
	auto crossed=mesh;
	crossed.vertices[1].position={1,1,0}; crossed.vertices[1].uv={1,1};
	crossed.vertices[2].position={0,1,0}; crossed.vertices[2].uv={0,1};
	crossed.vertices[3].position={1,0,0}; crossed.vertices[3].uv={1,0};
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(crossed,SourcePatch{{0,1}}); }, "Self-intersecting boundary accepted");
	rejects<std::invalid_argument>([&] { validatePatchDomain(crossed,SourcePatch{{0,1}}); }, "Crossing patch domain accepted");
	SourceSurfaceLocation location;
	check(locateOnSourcePatch(mesh,patches[0],{0.2f,0.7f,0},location), "Patch location missed the source surface");
	check(location.triangle==1 && near(location.barycentric[0]+location.barycentric[1]+location.barycentric[2],1), "Incorrect source correspondence");
	check(locateOnSourcePatch(mesh,patches[0],{0.5f,0.5f,0},location) && location.triangle==0,
		"Equivalent shared-edge correspondence was not canonicalized");
	check(!locateOnSourcePatch(mesh,patches[0],{0.2f,0.7f,0.1f},location), "Patch location accepted an off-surface point");
	check(identifySourcePatches(mesh,{{0,2}}).size()==2, "Explicit feature edge was crossed");
	auto uv=mesh; uv.vertices[3].uv[0]+=0.25f;
	check(identifySourcePatches(uv).size()==2, "Piecewise UV mapping was treated as globally affine");
	check(!locateOnSourcePatch(uv,SourcePatch{{0,1}},{0.5f,0.5f,0},location),
		"Incompatible shared-edge correspondence was treated as equivalent");
	rejects<std::invalid_argument>([&] { recoverPatchBoundary(uv,SourcePatch{{0,1}}); }, "Incompatible UV edge was cancelled");
	auto bent=mesh; bent.vertices[3].position[2]=0.1f;
	check(identifySourcePatches(bent).size()==2, "Bent surface was merged into a planar patch");
	auto color=mesh; color.vertices[3].color[0]=0.5f;
	check(identifySourcePatches(color).size()==2, "Vertex attribute discontinuity was crossed");
	auto seam=mesh; seam.vertices.push_back(seam.vertices[0]); seam.vertices.push_back(seam.vertices[2]);
	seam.triangles[1]={4,5,3};
	check(identifySourcePatches(seam).size()==2, "Separate seam vertices were merged");
	auto fan=mesh; Vertex center=fan.vertices[0]; center.position={0.5f,0.5f,0}; center.uv={0.5f,0.5f};
	fan.vertices.push_back(center); fan.triangles={{0,1,4},{1,2,4},{2,3,4},{3,0,4}};
	validatePatchDomain(fan,SourcePatch{{0,1,2,3}});
	auto folded=fan; folded.vertices[4].position={0.5f,-0.25f,0}; folded.vertices[4].uv={0.5f,-0.25f};
	check(recoverPatchBoundary(folded,SourcePatch{{0,1,2,3}}).loops.size()==1, "Folded-domain fixture has no closed boundary");
	rejects<std::invalid_argument>([&] { validatePatchDomain(folded,SourcePatch{{0,1,2,3}}); }, "Folded patch interior accepted");
	auto fanPatches=identifySourcePatches(fan,{{0,4}});
	for (const auto &p:fanPatches) {
		bool first=std::find(p.triangles.begin(),p.triangles.end(),0u)!=p.triangles.end();
		bool last=std::find(p.triangles.begin(),p.triangles.end(),3u)!=p.triangles.end();
		check(!(first && last), "Protected edge was bypassed through another patch path");
	}
	auto overlap=mesh; overlap.triangles={mesh.triangles[0],mesh.triangles[0]};
	check(!locateOnSourcePatch(overlap,SourcePatch{{0,1}},{0.7f,0.2f,0},location), "Overlapping surface location was accepted");
	rejects<std::invalid_argument>([&] { identifySourcePatches(mesh,{{0,99}}); }, "Invalid protected edge accepted");
}

void uvIslandTests()
{
	auto mesh=square();
	auto islands=identifyUVIslands(mesh);
	check(islands.size()==1 && islands[0].triangles.size()==2, "Continuous UV faces were split");
	Displacement uvMapping;
	check(!assessUVIsolation(mesh,uvMapping).useLegacy, "Normal adjoining UV triangles triggered fallback");
	auto tiled=mesh; tiled.vertices[2].uv[0]=2;
	for (auto addressing:{Address::Wrap,Address::Clamp}) {
		uvMapping.u=addressing;
		auto decision=assessUVIsolation(tiled,uvMapping);
		check(decision.useLegacy && !decision.reason.empty(), "Out-of-range material UV did not select fallback");
		check(tiled.vertices[2].uv[0]==2, "UV assessment changed a tiled coordinate");
	}
	uvMapping.uvScale={0.5f,1};
	check(!assessUVIsolation(tiled,uvMapping).useLegacy, "Assessment ignored the shader UV transform");
	uvMapping.uvOffset={0.25f,0};
	check(assessUVIsolation(tiled,uvMapping).useLegacy, "Offset outside the material domain did not select fallback");
	uvMapping=Displacement{};
	auto seam=mesh; seam.vertices.push_back(seam.vertices[0]); seam.vertices.push_back(seam.vertices[2]);
	seam.triangles[1]={4,5,3};
	check(identifyUVIslands(seam).size()==2, "Separate overlapping islands were welded");
	auto mirrored=mesh; mirrored.vertices[3].uv={1,0};
	check(identifyUVIslands(mirrored).size()==2, "Mirrored UV orientation was not separated");
	auto isolated=isolateUVIslands(mirrored);
	check(isolated.vertices.size()==6 && isolated.patchAuthority, "Shared mirrored seam was not isolated");
	for (size_t i=0;i<mirrored.vertices.size();++i)
		check(isolated.vertices[i].uv==mirrored.vertices[i].uv && isolated.vertices[i].position==mirrored.vertices[i].position,
			"UV isolation changed an original vertex");
	check(isolated.triangles[0][0]!=isolated.triangles[1][0] && isolated.triangles[0][2]!=isolated.triangles[1][1],
		"Mirrored seam still shares vertex indices");
	check(isolated.vertices[isolated.triangles[0][0]].boundary && isolated.vertices[isolated.triangles[1][0]].boundary,
		"Isolated seam was not pinned");
	auto divided=subdivide(isolated,2);
	check(resolvePatchSources(divided).size()==divided.vertices.size(), "Island subdivision lost original source mapping");
	auto patchInput=isolated; auto islandAuthority=patchInput.patchAuthority; patchInput.patchAuthority.reset();
	std::vector<PatchRetopoReport> islandReports;
	auto patchSeed=retopologizeCoplanarPatches(patchInput,islandReports);
	auto patchAuthority=std::make_shared<PatchAuthority>(*patchSeed.patchAuthority);
	patchAuthority->original=islandAuthority->original; patchSeed.patchAuthority=std::move(patchAuthority);
	auto patchDivided=subdivide(patchSeed,1);
	check(resolvePatchSources(patchDivided).size()==patchDivided.vertices.size(),
		"Island isolation plus patch retopology lost original source mapping");
	Displacement mapping; mapping.strength=1; mapping.neutral=0; mapping.pinBoundaries=true;
	HeightField constant{1,1,{1}};
	auto displaced=displace(divided,constant,mapping);
	for (size_t i=0;i<divided.vertices.size();++i) if (divided.vertices[i].boundary)
		check(displaced.vertices[i].position==divided.vertices[i].position, "Island isolation moved a seam boundary");
	check(isolateUVIslands(seam).vertices.size()==seam.vertices.size(), "Already separate UV islands were duplicated");
	Mesh overlapping;
	for (int copy=0;copy<2;++copy) for (int i=0;i<3;++i) overlapping.vertices.push_back(mesh.vertices[size_t(i)]);
	overlapping.triangles={{0,1,2},{3,4,5}};
	check(assessUVIsolation(overlapping,uvMapping).useLegacy, "Overlapping UV regions did not select fallback");
	check(identifyUVIslands(overlapping).size()==2, "Overlapping shells were merged into one UV island");
	auto overlapWorking=subdivide(isolateUVIslands(overlapping),1);
	check(overlapWorking.vertices.size()==12 && overlapWorking.triangles.size()==8,
		"Tessellation welded overlapping UV islands");
	for (int i=0;i<3;++i) check(overlapWorking.vertices[size_t(i)].uv==overlapping.vertices[size_t(i)].uv
		&& overlapWorking.vertices[size_t(i+3)].uv==overlapping.vertices[size_t(i+3)].uv,
		"Overlapping-island tessellation changed original UV values");
	rejects<std::length_error>([&] { isolateUVIslands(mirrored,{4,10}); }, "Island isolation ignored vertex budget");
	auto degenerate=mesh; degenerate.vertices[1].uv=degenerate.vertices[0].uv;
	rejects<std::invalid_argument>([&] { identifyUVIslands(degenerate); }, "Degenerate UV face was accepted");
	check(assessUVIsolation(degenerate,uvMapping).useLegacy, "Ambiguous UV face did not select safe fallback");
}

void residualNormalTests()
{
	NormalBasis identity{{1,0,0},{0,1,0},{0,0,1}};
	Color source{0.5f,0.5f,1.0f,0.37f};
	auto same=residualNormalSample(source,identity,identity);
	check(near(same[0],0.5) && near(same[1],0.5) && near(same[2],1) && same[3]==source[3],
		"Unchanged basis did not preserve a flat normal and alpha");
	NormalBasis tilted{{0.8660254f,0,-0.5f},{0,1,0},{0.5f,0,0.8660254f}};
	auto residual=residualNormalSample(source,identity,tilted);
	check(near(residual[0],0.25) && near(residual[1],0.5) && near(residual[2],0.9330127),
		"Residual did not compensate for the displaced basis");
	NormalBasis skewed{{1,0,0},{0.2f,1,0},{0,0.3f,1}};
	residual=residualNormalSample(source,identity,skewed);
	Vec3 v{residual[0]*2-1,residual[1]*2-1,residual[2]*2-1};
	double tlen=std::hypot(0.2,1.0), nlen=std::hypot(0.3,1.0);
	double x=v[0]+v[1]*0.2/tlen, y=v[1]/tlen+v[2]*0.3/nlen, z=v[2]/nlen;
	check(std::abs(x)<1e-6 && std::abs(y)<1e-6 && z>0, "Residual did not invert a non-orthogonal shader basis");
	NormalBasis mirrored{{-1,0,0},{0,1,0},{0,0,1}};
	auto handed=residualNormalSample({1,0.5f,0.5f,0.8f},identity,mirrored);
	check(near(handed[0],0) && near(handed[1],0.5) && near(handed[2],0.5) && handed[3]==0.8f,
		"Residual lost mirrored basis handedness or alpha");
	NormalBasis degenerate{{1,0,0},{1,0,0},{0,0,1}};
	rejects<std::invalid_argument>([&] { residualNormalSample(source,identity,degenerate); }, "Degenerate normal basis accepted");
	rejects<std::invalid_argument>([&] { residualNormalSample({0.5f,0.5f,0.5f,1},identity,identity); }, "Zero normal accepted");
}

void displacementBiasTests()
{
	Mesh rounded;
	for (auto p:std::vector<Vec3>{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}) {
		Vertex v; v.position=p; v.normal=p; v.uv={0.25f,0.5f}; rounded.vertices.push_back(v);
	}
	rounded.triangles={{4,0,2},{4,2,1},{4,1,3},{4,3,0},{5,2,0},{5,1,2},{5,3,1},{5,0,3}};
	rounded=captureSource(rounded);
	Displacement s; s.strength=1; s.neutral=0.5f; s.u=s.v=Address::Clamp;
	HeightField biased{1,1,{0.8f}};
	auto inflated=displace(rounded,biased,s);
	check(inflated.vertices[0].position[0]>1.2f, "Radial inflation regression fixture is ineffective");
	check(maximumSectionDisplacementBias(inflated)>0.29, "Bias measurement missed radial inflation");
	s.removeSectionBias=true;
	DisplacementStats stats;
	auto centered=displace(rounded,biased,s,{},&stats);
	check(equal(centered,rounded), "Constant normal-height bias inflated a closed curved section");
	check(maximumSectionDisplacementBias(centered)<1e-8, "Bias measurement did not recognize the corrected surface");
	check(stats.centeredSections==1 && stats.maximumRemovedBias>0.29 && stats.maximumResidualBias<1e-10,
		"Displacement bias diagnostics are incorrect");
	auto two=rounded; two.source.reset(); two.origins.clear();
	for (auto v:rounded.vertices) { v.position[0]+=4; v.uv[0]=0.75f; two.vertices.push_back(v); }
	for (auto t:rounded.triangles) { for (auto &i:t) i+=6; two.triangles.push_back(t); }
	HeightField opposite{2,1,{0.8f,0.2f}};
	check(equal(displace(two,opposite,s),two), "Separate sections cancelled each other's displacement bias");
	auto panel=subdivide(square(),3);
	HeightField detail{9,9,std::vector<float>(81,0.7f)}; detail.values[4*9+4]=1;
	s.pinBoundaries=true; s.preventPinching=true; s.maxSlope=0.25f; s.positiveLimit=0.1f; s.negativeLimit=0.05f;
	auto result=displace(panel,detail,s,{},&stats);
	check(!equal(result,panel), "Bias removal erased all local relief");
	double integral=0,area=0;
	for (const auto &t:panel.triangles) {
		const auto &a=panel.vertices[t[0]].position,&b=panel.vertices[t[1]].position,&c=panel.vertices[t[2]].position;
		double faceArea=std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]))*0.5;
		for (auto i:t) integral+=faceArea*(double(result.vertices[i].position[2])-panel.vertices[i].position[2])/3;
		area+=faceArea;
	}
	check(std::abs(integral/area)<1e-8 && stats.maximumResidualBias<1e-9, "Safety constraints reintroduced average displacement bias");
	check(maximumSectionDisplacementBias(result)<1e-8, "Post-processing bias measurement disagrees with the integrated surface");
	for (size_t i=0;i<panel.vertices.size();++i) {
		double d=double(result.vertices[i].position[2])-panel.vertices[i].position[2];
		check(d<=0.100001 && d>=-0.050001, "Bias removal violated signed displacement limits");
		if (panel.vertices[i].boundary) check(result.vertices[i].position==panel.vertices[i].position, "Bias removal moved a pinned seam");
	}
	check(equal(result,displace(panel,detail,s)), "Bias removal is not deterministic");
	// A short edge near a pinned seam must not flatten relief elsewhere.
	auto uneven=panel;
	for (auto &v:uneven.vertices) if (near(v.position[0],0.125) && near(v.position[1],0.125))
		v.position={0.000001f,0.000001f,0};
	uneven.source.reset(); uneven.origins.clear(); uneven=captureSource(uneven);
	HeightField ramp{2,1,{0.2f,0.8f}};
	s.strength=0.2f; s.maxSlope=0.5f; s.positiveLimit=s.negativeLimit=1;
	auto local=displace(uneven,ramp,s,{},&stats);
	double remoteRelief=0;
	for (size_t i=0;i<uneven.vertices.size();++i)
		if (uneven.vertices[i].position[0]>0.6f && !uneven.vertices[i].boundary)
			remoteRelief=std::max(remoteRelief,std::abs(double(local.vertices[i].position[2])));
	std::cout << "Short-edge fixture remote relief: " << remoteRelief << '\n';
	check(remoteRelief>0.01, "A short pinned edge flattened distant valid surface detail");
	check(maximumSectionDisplacementBias(local)<1e-8, "Local slope limiting reintroduced section inflation");
	check(stats.appliedRms>0.01 && stats.requestedRms>0.01, "Relief diagnostics concealed flattened geometry");
	for (size_t i=0;i<uneven.vertices.size();++i) {
		check(local.vertices[i].uv==uneven.vertices[i].uv, "Local limiting changed source UVs");
		if (uneven.vertices[i].boundary) check(local.vertices[i].position==uneven.vertices[i].position, "Local limiting moved a pinned seam");
	}
	for (const auto &t:uneven.triangles) for (int j=0;j<3;++j) {
		auto a=t[j],b=t[(j+1)%3]; auto p=uneven.vertices[a].position,q=uneven.vertices[b].position;
		double cap=s.maxSlope*std::hypot(double(p[0])-q[0],double(p[1])-q[1],double(p[2])-q[2]);
		check(std::abs(double(local.vertices[a].position[2])-local.vertices[b].position[2])<=cap+1e-8,
			"Local limiting violated the edge slope constraint");
	}
	check(equal(local,displace(uneven,ramp,s)), "Local slope envelopes are not deterministic");
	// Independent sections must retain the same relief whether processed alone
	// or in a shape containing a differently sized constrained section.
	auto joined=uneven; joined.source.reset(); joined.origins.clear();
	size_t offset=joined.vertices.size();
	for (auto v:panel.vertices) { v.position[0]+=3; joined.vertices.push_back(v); }
	for (auto t:panel.triangles) { for (auto &i:t) i+=uint32_t(offset); joined.triangles.push_back(t); }
	auto alone=displace(panel,ramp,s),together=displace(joined,ramp,s);
	for (size_t i=0;i<panel.vertices.size();++i)
		check(near(together.vertices[offset+i].position[2],alone.vertices[i].position[2]),
			"A constrained section suppressed relief on an unrelated section");
}

void darkFeatureTests()
{
	Displacement mapping; mapping.u=mapping.v=Address::Clamp;
	DarkFeatureSettings settings; settings.enabled=true; settings.strength=1; settings.maximumShift=0.25f;
	auto mesh=subdivide(square(),4);
	for (float value:{0.0f,0.2f,1.0f}) {
		HeightField flat{64,64,std::vector<float>(4096,value)};
		auto attraction=darkFeatureAttraction(flat,2,Address::Clamp,Address::Clamp);
		for (auto v:attraction.values) check(v<1e-7,"Uniform height generated dark attraction");
		DarkFeatureStats stats;
		check(equal(mesh,relocateDarkFeatures(mesh,attraction,mapping,settings,&stats)) && stats.accepted==0,
			"Uniform darkness/brightness moved generated samples");
	}
	HeightField height{128,128,std::vector<float>(16384)};
	for (size_t y=0;y<128;++y) for (size_t x=0;x<128;++x) {
		double u=(x+0.5)/128;
		// Center the known fissure between the baseline's x=.375 and .4375
		// sample columns, rather than on an already captured valley sample.
		height.values[y*128+x]=float(0.8-0.6*std::exp(-std::pow((u-0.40625)/0.018,2)));
	}
	auto attraction=darkFeatureAttraction(height,2,Address::Clamp,Address::Clamp);
	check(sample(attraction,{0.40625f,0.5f},Address::Clamp,Address::Clamp)>0.1f,"Narrow trough did not produce centered attraction");
	DarkFeatureStats stats;
	auto biased=relocateDarkFeatures(mesh,attraction,mapping,settings,&stats);
	std::cout << "Dark fixture: eligible=" << stats.eligible << " above=" << stats.aboveThreshold << " proposals=" << stats.proposals << " accepted=" << stats.accepted << " topology=" << stats.topologyRejected << " provenance=" << stats.provenanceRejected << '\n';
	check(stats.accepted>0 && stats.eligible>stats.accepted && stats.maximumDistance>0,"No generated samples reached trough evidence");
	check(biased.triangles==mesh.triangles && biased.vertices.size()==mesh.vertices.size(),"Dark bias changed the tessellation budget");
	check(equal(biased,relocateDarkFeatures(mesh,attraction,mapping,settings)),"Dark relocation is not deterministic");
	settings.enabled=false; check(equal(mesh,relocateDarkFeatures(mesh,attraction,mapping,settings)),"Disabled dark bias changed geometry");
	settings.enabled=true; settings.strength=0; check(equal(mesh,relocateDarkFeatures(mesh,attraction,mapping,settings)),"Zero attraction strength changed geometry");
	settings.strength=1; settings.maximumShift=0; check(equal(mesh,relocateDarkFeatures(mesh,attraction,mapping,settings)),"Zero maximum shift changed geometry");
	settings.maximumShift=0.25f;
	for (size_t i=0;i<mesh.vertices.size();++i) {
		if (i<mesh.source->vertices.size() || mesh.vertices[i].boundary)
			check(mesh.vertices[i].position==biased.vertices[i].position && mesh.vertices[i].uv==biased.vertices[i].uv,"Dark bias moved a source vertex or protected boundary");
		check(near(biased.vertices[i].position[0],biased.vertices[i].uv[0]) && near(biased.vertices[i].position[1],biased.vertices[i].uv[1]),"Relocation dragged old UVs instead of resampling source mapping");
		check(biased.vertices[i].position[2]==0,"Dark bias added displacement");
		double nearest=1e20;
		for (auto t:mesh.triangles) if (std::find(t.begin(),t.end(),i)!=t.end()) for (auto j:t) if (j!=i)
			nearest=std::min(nearest,std::hypot(double(mesh.vertices[i].position[0])-mesh.vertices[j].position[0],double(mesh.vertices[i].position[1])-mesh.vertices[j].position[1]));
		check(std::hypot(double(biased.vertices[i].position[0])-mesh.vertices[i].position[0],double(biased.vertices[i].position[1])-mesh.vertices[i].position[1])<=nearest*0.25001,"Dark relocation exceeded its local budget");
	}
	// Fixed, independent source-domain samples compare actual displaced triangle
	// interpolation against the known trough, at exactly the same face count.
	mapping.strength=0.1f; mapping.positiveLimit=mapping.negativeLimit=1;
	auto baseDisplaced=displace(mesh,height,mapping),biasedDisplaced=displace(biased,height,mapping);
	auto error=[&](const Mesh &surface,bool troughOnly) {
		double sum=0; size_t count=0;
		for (int y=1;y<40;++y) for (int x=1;x<100;++x) {
			double u=x/100.0,v=y/40.0; if (u<0.34 || u>0.51) continue;
			if (troughOnly && std::abs(u-0.40625)>0.018) continue;
			for (auto t:surface.triangles) {
				auto a=surface.vertices[t[0]],b=surface.vertices[t[1]],c=surface.vertices[t[2]];
				double den=(b.uv[0]-a.uv[0])*(c.uv[1]-a.uv[1])-(b.uv[1]-a.uv[1])*(c.uv[0]-a.uv[0]);
				double wb=((u-a.uv[0])*(c.uv[1]-a.uv[1])-(v-a.uv[1])*(c.uv[0]-a.uv[0]))/den;
				double wc=((b.uv[0]-a.uv[0])*(v-a.uv[1])-(b.uv[1]-a.uv[1])*(u-a.uv[0]))/den;
				if (wb < -1e-7 || wc < -1e-7 || wb+wc>1+1e-7) continue;
				double expectedHeight=(sample(height,{float(u),float(v)},Address::Clamp,Address::Clamp)-mapping.neutral)*mapping.strength;
				double represented=(1-wb-wc)*a.position[2]+wb*b.position[2]+wc*c.position[2];
				sum+=std::abs(expectedHeight-represented); ++count; break;
			}
		}
		return sum/count;
	};
	double before=error(baseDisplaced,true),after=error(biasedDisplaced,true);
	std::cout << "Dark trough error at " << mesh.triangles.size() << " triangles: " << before << " -> " << after << "; moved " << stats.accepted << '\n';
	std::cout << "Dark trough wider neighborhood error: " << error(baseDisplaced,false) << " -> " << error(biasedDisplaced,false) << '\n';
	check(after<before,"Relocation did not improve the narrow displaced trough at fixed triangle count");
	// Patch-aware seeds retain original-source correspondence after relocation.
	std::vector<PatchRetopoReport> reports;
	auto patch=retopologizeCoplanarPatches(square(),reports);
	auto patchMesh=subdivide(patch,3),patchBiased=relocateDarkFeatures(patchMesh,attraction,mapping,settings,&stats);
	check(stats.accepted>0 && resolvePatchSources(patchBiased).size()==patchBiased.vertices.size(),"Patch relocation lost original surface correspondence");
	// An overlapping/incompatible source representation is not guessed. Each
	// failed proposal must leave its vertex, UV, and origins exactly untouched.
	auto ambiguous=patchMesh;
	auto authority=std::make_shared<PatchAuthority>(*ambiguous.patchAuthority);
	auto original=std::make_shared<SourceGeometry>(*authority->original);
	auto originalFaces=original->triangles;
	original->triangles.insert(original->triangles.end(),originalFaces.begin(),originalFaces.end()); authority->original=original;
	for (auto &p:authority->patches) for (size_t f=0;f<originalFaces.size();++f) p.triangles.push_back(uint32_t(originalFaces.size()+f));
	ambiguous.patchAuthority=authority;
	auto rejected=relocateDarkFeatures(ambiguous,attraction,mapping,settings,&stats);
	check(stats.provenanceRejected>0,"Ambiguous patch provenance was not rejected");
	check(equal(ambiguous,rejected) && stats.accepted==0,"Rejected ambiguous patch proposals changed baseline geometry");
	for (size_t f=0;f<ambiguous.origins.size();++f)
		check(ambiguous.origins[f].triangle==rejected.origins[f].triangle && ambiguous.origins[f].corners==rejected.origins[f].corners,
			"Rejected ambiguous patch proposals changed source correspondence");
	for (size_t i=0;i<ambiguous.vertices.size();++i) {
		if (rejected.vertices[i].position==ambiguous.vertices[i].position)
			check(rejected.vertices[i].uv==ambiguous.vertices[i].uv,"Rejected relocation partially changed UVs");
	}
	for (int address=0;address<4;++address) {
		auto transformed=square();
		Displacement transform=mapping; transform.uvScale={-2,0.5f}; transform.uvOffset={0.3f,-0.2f};
		transform.u=(address&1)?Address::Wrap:Address::Clamp; transform.v=(address&2)?Address::Wrap:Address::Clamp;
		for (auto &v:transformed.vertices) for (int j=0;j<2;++j) v.uv[j]=(v.uv[j]-transform.uvOffset[j])/transform.uvScale[j];
		auto sampled=subdivide(transformed,4);
		auto relocated=relocateDarkFeatures(sampled,attraction,transform,settings);
		for (size_t i=0;i<sampled.vertices.size();++i) for (int j=0;j<2;++j)
			check(std::abs((relocated.vertices[i].uv[j]*transform.uvScale[j]+transform.uvOffset[j])-relocated.vertices[i].position[j])<1e-6,
				"Dark relocation broke transformed/mirrored material UV authority");
	}
	HeightField ramp{128,128,std::vector<float>(16384)};
	for (size_t y=0;y<128;++y) for (size_t x=0;x<128;++x) ramp.values[y*128+x]=float(x)/127;
	auto broad=darkFeatureAttraction(ramp,2,Address::Clamp,Address::Clamp);
	check(sample(broad,{0.5f,0.5f},Address::Clamp,Address::Clamp)<1e-6,"Broad linear gradient generated interior trough evidence");
	settings.threshold=1; check(equal(mesh,relocateDarkFeatures(mesh,attraction,mapping,settings)),"Attraction threshold did not suppress weak evidence");
	settings.threshold=0.01f;
	// Put one generated sample very near an edge of its one-ring, while
	// retaining exact source coordinates. An attractive point beyond that edge
	// is inside the source triangle but would invert an incident face.
	auto crowded=mesh; size_t targetIndex=0; Barycentric movedWeights{}; Vec2 peak{};
	for (size_t i=mesh.source->vertices.size();i<mesh.vertices.size() && !targetIndex;++i) {
		if (!near(mesh.vertices[i].uv[0],0.375) || !near(mesh.vertices[i].uv[1],0.5)) continue;
		for (size_t f=0;f<mesh.triangles.size();++f) for (int c=0;c<3;++c) if (mesh.triangles[f][c]==i) {
			auto weights=mesh.origins[f].corners[c]; if (*std::min_element(weights.begin(),weights.end())<0.01) continue;
			auto b=mesh.origins[f].corners[(c+1)%3],d=mesh.origins[f].corners[(c+2)%3];
			for (int j=0;j<3;++j) movedWeights[j]=weights[j]*0.0001+(b[j]+d[j])*0.49995;
			auto face=mesh.origins[f].triangle; const auto &t=mesh.source->triangles[face]; Vertex vertex;
			vertex.position={}; vertex.uv={}; vertex.color={};
			for (int k=0;k<3;++k) {
				const auto &s=mesh.source->vertices[t[k]];
				for (int j=0;j<3;++j) vertex.position[j]+=float(movedWeights[k]*s.position[j]);
				for (int j=0;j<2;++j) vertex.uv[j]+=float(movedWeights[k]*s.uv[j]);
				for (int j=0;j<4;++j) vertex.color[j]+=float(movedWeights[k]*s.color[j]);
			}
			for (int j=0;j<2;++j) peak[j]=vertex.uv[j]+(vertex.uv[j]-mesh.vertices[i].uv[j])*0.25f;
			crowded.vertices[i]=vertex; targetIndex=i;
			break;
		}
	}
	check(targetIndex!=0,"Could not construct topology-rejection fixture");
	for (size_t f=0;f<crowded.triangles.size();++f) for (int c=0;c<3;++c)
		if (crowded.triangles[f][c]==targetIndex) crowded.origins[f].corners[c]=movedWeights;
	HeightField lure{128,128,std::vector<float>(16384)};
	for (size_t y=0;y<128;++y) for (size_t x=0;x<128;++x) {
		double dx=(x+0.5)/128-peak[0],dy=(y+0.5)/128-peak[1]; lure.values[y*128+x]=float(std::exp(-(dx*dx+dy*dy)/0.0001));
	}
	auto constrained=relocateDarkFeatures(crowded,lure,mapping,settings,&stats);
	check(stats.topologyRejected>0,"Topology guard did not reject a proposal crossing its one-ring boundary");
	check(constrained.vertices[targetIndex].position==crowded.vertices[targetIndex].position && constrained.vertices[targetIndex].uv==crowded.vertices[targetIndex].uv,
		"Rejected topology proposal partially moved the generated sample");
}

void surfacePriorTests()
{
	constexpr size_t size=128;
	auto field=[&](auto function) {
		HeightField out{size,size,std::vector<float>(size*size)};
		for (size_t y=0;y<size;++y) for (size_t x=0;x<size;++x) out.values[y*size+x]=float(function((double(x)+0.5)/size-0.5,(double(y)+0.5)/size-0.5));
		return out;
	};
	auto bump=field([](double x,double y) { return 0.5+0.4*std::exp(-(x*x+y*y)/(2*0.12*0.12)); });
	auto neutral=surfacePrior(bump,0.5f,1,SurfaceProfile::Neutral,Address::Clamp,Address::Clamp);
	check(neutral.target.values==neutral.original.values && neutral.broad.values.empty(),"Neutral did not bypass morphology exactly");
	auto rock=surfacePrior(bump,0.5f,1,SurfaceProfile::Rock,Address::Clamp,Address::Clamp);
	size_t center=64*size+64;
	std::cout << "Rock broad bump peak " << rock.original.values[center] << " -> " << rock.target.values[center] << "; broad energy " << rock.broadEnergyBefore << " -> " << rock.broadEnergyAfter << '\n';
	check(rock.target.values[center]<rock.original.values[center]*0.9f && rock.broadEnergyAfter<rock.broadEnergyBefore,"Rock did not attenuate a broad radial lobe");
	auto bowl=bump; for (auto &v:bowl.values) v=1-v;
	auto inward=surfacePrior(bowl,0.5f,1,SurfaceProfile::Rock,Address::Clamp,Address::Clamp);
	check(inward.target.values[center]<0 && inward.target.values[center]>inward.original.values[center],"Rock deepened or reversed a broad bowl");
	auto pit=field([](double x,double y) { return 0.5-0.4*std::exp(-(x*x+y*y)/(2*0.015*0.015)); });
	auto narrow=surfacePrior(pit,0.5f,1,SurfaceProfile::Rock,Address::Clamp,Address::Clamp);
	check(std::abs(narrow.target.values[center])>std::abs(narrow.original.values[center])*0.95,"Rock erased a narrow radial pit");
	for (double sign:{-1.0,1.0}) {
		auto ridge=field([&](double x,double) { return 0.5+sign*0.4*std::exp(-x*x/(2*0.035*0.035)); });
		auto directional=surfacePrior(ridge,0.5f,1,SurfaceProfile::Rock,Address::Clamp,Address::Clamp);
		check(directional.coherence.values[center]>0.99 && std::abs(directional.target.values[center])>std::abs(directional.original.values[center])*0.99,"Opposing fissure/ridge banks lost coherence or were suppressed");
	}
	auto saddle=field([](double x,double y) { return 0.5+0.5*(x*x-y*y); });
	auto saddlePrior=surfacePrior(saddle,0.5f,1,SurfaceProfile::Rock,Address::Clamp,Address::Clamp);
	check(saddlePrior.suppression.values[center]<1e-6,"Equal-magnitude saddle was classified as a blob");
	auto bands=field([](double x,double) { return 0.5+0.3*std::sin(x*12*3.14159265358979323846); });
	auto strata=surfacePrior(bands,0.5f,1,SurfaceProfile::Rock,Address::Wrap,Address::Clamp);
	check(strata.targetRms>strata.originalRms*0.99,"Rock erased coherent parallel bands");
	auto grain=surfacePrior(bands,0.5f,1,SurfaceProfile::Wood,Address::Wrap,Address::Clamp);
	check(grain.targetRms>grain.originalRms*0.99,"Wood used broad-scale coherence to attenuate coherent fine grain");
	uint32_t random=17; auto noise=bump;
	for (auto &v:noise.values) { random=random*1664525u+1013904223u; v=0.4f+float(random>>8)/float(0xFFFFFFu)*0.2f; }
	for (const auto &input:{bump,bowl,pit,saddle,bands,noise}) for (float strength:{-2.0f,0.0f,2.0f}) {
		auto measurements=analyzeSurfaceMorphology(input,0.5f,strength,Address::Clamp,Address::Wrap);
		auto coherent=measurements.coherence.values;
		for (auto profile:{SurfaceProfile::Rock,SurfaceProfile::Wood,SurfaceProfile::Masonry,SurfaceProfile::Metal,SurfaceProfile::Soil}) {
			auto result=applySurfaceProfile(measurements,profile,Address::Clamp,Address::Wrap);
			for (size_t i=0;i<input.values.size();++i) {
				double d=result.original.values[i],target=result.target.values[i];
				check(target>=std::min(0.0,d) && target<=std::max(0.0,d),"A surface profile reversed, amplified, or invented signed displacement");
			}
			check(measurements.coherence.values==coherent,"Profile interpretation mutated shared morphology");
			if (profile==SurfaceProfile::Soil) {
				check(result.target.values==measurements.original.values,"Soil penalized otherwise admissible isotropic/irregular evidence");
				check(!result.broad.values.empty() && result.fineCoherence.values==measurements.fineCoherence.values,
					"Soil discarded descriptive morphology instead of interpreting it permissively");
			}
		}
	}
	auto wood=surfacePrior(bump,0.5f,1,SurfaceProfile::Wood,Address::Clamp,Address::Clamp);
	auto metal=surfacePrior(bump,0.5f,1,SurfaceProfile::Metal,Address::Clamp,Address::Clamp);
	check(wood.target.values[center]<wood.original.values[center]*0.9f,"Wood failed to attenuate a broad independent lobe");
	check(metal.target.values[center]<metal.original.values[center]*0.75f,"Metal failed to resist broad manufactured-form swelling");
	auto metalNoise=surfacePrior(noise,0.5f,1,SurfaceProfile::Metal,Address::Clamp,Address::Clamp);
	check(metalNoise.targetRms<metalNoise.originalRms*0.9,"Metal did not attenuate incoherent fine perturbations");
	for (auto profile:{SurfaceProfile::Wood,SurfaceProfile::Masonry}) {
		auto groove=field([](double x,double) { return 0.5-0.4*std::exp(-x*x/(2*0.035*0.035)); });
		auto preserved=surfacePrior(groove,0.5f,1,profile,Address::Clamp,Address::Clamp);
		check(std::abs(preserved.target.values[center])>std::abs(preserved.original.values[center])*0.85,"Directional grain or masonry joints were erased");
	}
	auto knot=surfacePrior(pit,0.5f,1,SurfaceProfile::Wood,Address::Clamp,Address::Clamp);
	check(std::abs(knot.target.values[center])>std::abs(knot.original.values[center])*0.8,"Wood imposed a global direction and erased a localized radial feature");
	Displacement settings; settings.u=settings.v=Address::Clamp; settings.strength=1;
	HeightField opposing{2,1,{0.6f,0.1f}},attenuated{2,1,{0,-0.4f}};
	settings.profileTarget=&attenuated; auto sampling=subdivide(square(),1);
	for (auto &v:sampling.vertices) v.uv={0.3f,0.5f};
	auto constrained=displace(sampling,opposing,settings);
	for (const auto &v:constrained.vertices) check(v.position[2]>=0,"Bilinear profile interpolation reversed the original signed sample");
	settings.profileTarget=&rock.target;
	double previousError=0,firstEnvelope=-1;
	for (unsigned depth=0;depth<=4;++depth) {
		auto mesh=subdivide(square(),depth),output=displace(mesh,bump,settings);
		double envelope=0,squared=0;
		for (size_t i=0;i<output.vertices.size();++i) {
			envelope=std::max(envelope,std::abs(double(output.vertices[i].position[2])));
			check(output.vertices[i].position[0]==mesh.vertices[i].position[0] && output.vertices[i].position[1]==mesh.vertices[i].position[1],"Resolution introduced lateral drift from immutable planar reference");
		}
		for (auto t:output.triangles) {
			auto a=output.vertices[t[0]].position,b=output.vertices[t[1]].position,c=output.vertices[t[2]].position;
			double area=std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]))*0.5;
			squared+=area*(double(a[2])*a[2]+double(b[2])*b[2]+double(c[2])*c[2]+double(a[2])*b[2]+double(b[2])*c[2]+double(c[2])*a[2])/6;
		}
		double error=0; size_t count=0;
		for (int y=0;y<24;++y) for (int x=0;x<24;++x) {
			double u=(x+0.5)/24,v=(y+0.5)/24;
			for (auto t:output.triangles) {
				auto a=output.vertices[t[0]].position,b=output.vertices[t[1]].position,c=output.vertices[t[2]].position;
				double den=(double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]);
				double wb=((u-a[0])*(double(c[1])-a[1])-(v-a[1])*(double(c[0])-a[0]))/den;
				double wc=((double(b[0])-a[0])*(v-a[1])-(double(b[1])-a[1])*(u-a[0]))/den;
				if (wb<0 || wc<0 || wb+wc>1) continue;
				double expected=sample(rock.target,{float(u),float(v)},Address::Clamp,Address::Clamp);
				error+=std::abs((1-wb-wc)*a[2]+wb*b[2]+wc*c[2]-expected); ++count; break;
			}
		}
		error/=count;
		std::cout << "Rock fixed target depth " << depth << ": envelope=" << envelope << " RMS=" << std::sqrt(squared) << " surface error=" << error << '\n';
		if (depth==1) firstEnvelope=envelope;
		if (depth>1) check(std::abs(envelope-firstEnvelope)<1e-7,"Depth increased the fixed target displacement envelope");
		if (depth==4) check(error<previousError,"Higher resolution did not improve fixed-target approximation");
		previousError=error;
	}
	Mesh rounded;
	for (auto p:std::vector<Vec3>{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}) { Vertex v; v.position=p; v.normal=p; rounded.vertices.push_back(v); }
	rounded.triangles={{4,0,2},{4,2,1},{4,1,3},{4,3,0},{5,2,0},{5,1,2},{5,3,1},{5,0,3}};
	HeightField flat{128,128,std::vector<float>(16384,0.5f)}; auto zero=surfacePrior(flat,0.5f,1,SurfaceProfile::Rock);
	settings.profileTarget=&zero.target;
	check(equal(rounded,displace(rounded,flat,settings)),"Rock profile changed rounded reference geometry without requested displacement");
}

void malformedTests()
{
	auto mesh = square();
	mesh.triangles[0][0] = 99;
	rejects<std::invalid_argument>([&] { subdivide(mesh, 1); }, "Invalid index accepted");
	mesh = square();
	mesh.vertices[0].uv[0] = std::numeric_limits<float>::quiet_NaN();
	rejects<std::invalid_argument>([&] { validate(mesh); }, "NaN UV accepted");
	mesh = square();
	mesh.vertices[0].normal = {0, 0, 0};
	rejects<std::invalid_argument>([&] { validate(mesh); }, "Zero normal accepted");
	mesh = square();
	mesh.vertices[1].normal = {0, 0, -1};
	rejects<std::invalid_argument>([&] { subdivide(mesh, 1); }, "Opposing midpoint normals accepted");
	HeightField bad{1, 1, {std::numeric_limits<float>::infinity()}};
	rejects<std::invalid_argument>([&] { validate(bad); }, "Infinite height accepted");
	bad = {std::numeric_limits<std::size_t>::max(), 2, {0}};
	rejects<std::invalid_argument>([&] { validate(bad); }, "Dimension overflow accepted");
}
} // namespace

int main()
{
	try {
		subdivisionTests();
		heightTests();
		displacementTests();
		filteringAndAdaptiveTests();
		uvAuthorityTests();
		pinchProtectionTests();
		darkFeatureTests();
		surfacePriorTests();
		triangleCleanupTests();
		shadingNormalTests();
		reductionTests();
		sourcePatchTests();
		uvIslandTests();
		residualNormalTests();
		displacementBiasTests();
		malformedTests();
		std::cout << "Vanilla+ core: " << checks << " checks passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
		return 1;
	}
}
