#include "geometry/vanillaplus.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace VanillaPlus;

int main()
{
	try {
		Mesh plane;
		constexpr unsigned side=17;
		for (unsigned y=0;y<side;++y) for (unsigned x=0;x<side;++x) {
			Vertex v; v.position={float(x)/(side-1),float(y)/(side-1),0};
			v.uv={v.position[0],v.position[1]}; plane.vertices.push_back(v);
		}
		for (unsigned y=0;y+1<side;++y) for (unsigned x=0;x+1<side;++x) {
			auto a=y*side+x,b=a+1,c=a+side,d=c+1;
			plane.triangles.push_back({a,b,d}); plane.triangles.push_back({a,d,c});
		}
		HeightField field{128,128,std::vector<float>(128*128)};
		for (unsigned y=0;y<128;++y) for (unsigned x=0;x<128;++x)
			field.values[y*128+x]=float(0.5+0.3*std::sin(6*3.14159265358979323846*(x+0.5)/128)*std::sin(6*3.14159265358979323846*(y+0.5)/128));
		Displacement settings; settings.u=settings.v=Address::Clamp;
		settings.strength=0.15f; settings.positiveLimit=settings.negativeLimit=1;
		// No subdivision, adaptive curvature, bias, cleanup, reduction, or
		// reconstruction filters: isolate repeat displacement/normal feedback.
		double fixedLateral=0,updatedLateral=0,zeroMovement=0;
		for (int mode=0;mode<3;++mode) {
			Mesh current=plane;
			std::cout << (mode==0?"Zero displacement":mode==1?"Repeated relief, fixed normals":"Repeated relief, updated normals") << '\n';
			for (int pass=1;pass<=3;++pass) {
				// Reopening captures the applied geometry and its normals as the
				// next immutable source. Original-session provenance is absent.
				current.source.reset(); current.origins.clear(); current.patchAuthority.reset();
				if (mode!=2) for (auto &v:current.vertices) v.normal={0,0,1};
				auto source=captureSource(current);
				auto runSettings=settings; if (mode==0) runSettings.strength=0;
				auto output=displace(source,field,runSettings);
				auto normals=shadingNormals(output);
				double lateral=0,vertical=0,maxNormalError=0;
				// For an initially flat surface whose authored normals match its
				// geometric normals, corrected shading normals should equal the
				// area-weighted normals of the actual displaced surface.
				std::vector<std::array<double,3>> geometric(output.vertices.size());
				for (auto t:output.triangles) {
					double a[3],b[3],n[3];
					for (int j=0;j<3;++j) { a[j]=double(output.vertices[t[1]].position[j])-output.vertices[t[0]].position[j]; b[j]=double(output.vertices[t[2]].position[j])-output.vertices[t[0]].position[j]; }
					for (int j=0;j<3;++j) n[j]=a[(j+1)%3]*b[(j+2)%3]-a[(j+2)%3]*b[(j+1)%3];
					for (auto i:t) for (int j=0;j<3;++j) geometric[i][j]+=n[j];
				}
				for (size_t i=0;i<output.vertices.size();++i) {
					for (int j=0;j<2;++j) { double d=double(output.vertices[i].position[j])-plane.vertices[i].position[j]; lateral+=d*d; }
					double dz=double(output.vertices[i].position[2])-plane.vertices[i].position[2]; vertical+=dz*dz;
					double length=std::hypot(geometric[i][0],geometric[i][1],geometric[i][2]);
					if (mode==2 || pass==1) for (int j=0;j<3;++j)
						maxNormalError=std::max(maxNormalError,std::abs(double(normals[i][j])-geometric[i][j]/length));
					output.vertices[i].normal=normals[i];
				}
				lateral=std::sqrt(lateral/output.vertices.size()); vertical=std::sqrt(vertical/output.vertices.size());
				std::cout << "  pass " << pass << ": lateral RMS=" << lateral << ", vertical RMS=" << vertical << ", normal reconstruction error=" << maxNormalError << '\n';
				if (mode==2 && maxNormalError>1e-5) throw std::runtime_error("Normal correction differs from independently reconstructed surface normals");
				if (pass==3) { if (mode==0) zeroMovement=lateral+vertical; else if (mode==1) fixedLateral=lateral; else updatedLateral=lateral; }
				current=std::move(output);
			}
		}
		if (zeroMovement!=0 || fixedLateral!=0 || updatedLateral<0.001)
			throw std::runtime_error("Repeat-pass diagnostic did not isolate the expected geometric normal feedback");
		std::cout << "Repeat-pass feedback reproduced without tessellation or curvature weighting; no NIF or texture files changed.\n";
	} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
