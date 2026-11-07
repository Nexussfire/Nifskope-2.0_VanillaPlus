#include "geometry/vanillaplus.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace VanillaPlus;

// Synthetic demonstration only. No NIF, texture resources, or reduction.
int main(int argc, char **argv)
{
	try {
		if (argc != 2)
			throw std::invalid_argument("Usage: vanillaplus_demo output.svg");
		HeightField height{32, 32, {}};
		for (std::size_t y = 0; y < height.height; ++y) {
			for (std::size_t x = 0; x < height.width; ++x) {
				double u = (double(x) + 0.5) / double(height.width);
				double v = (double(y) + 0.5) / double(height.height);
				double edge = std::sin(u * 3.141592653589793) * std::sin(v * 3.141592653589793);
				height.values.push_back(float(0.5 + 0.45 * edge * std::sin(u * 15 + v * 9)));
			}
		}
		Mesh original{{{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}},
			{{1, 1, 0}, {0, 0, 1}, {1, 1}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}},
			{{0, 1, 2}, {0, 2, 3}}};
		auto tessellated = subdivide(original, 4);
		Displacement settings;
		settings.strength = 0.5;
		settings.u = settings.v = Address::Clamp;
		auto displaced = displace(tessellated, condition(height, {}), settings);
		std::ofstream svg(argv[1]);
		svg.exceptions(std::ios::failbit | std::ios::badbit);
		svg << "<svg xmlns='http://www.w3.org/2000/svg' width='1200' height='510' viewBox='0 0 1200 510'>"
			"<rect width='1200' height='510' fill='#101923'/>"
			"<g fill='#edf4fa' font-family='sans-serif'><text x='32' y='40' font-size='24'>Vanilla+ geometry core demonstration</text>"
			"<text x='32' y='69' font-size='14'>Synthetic height field / uniform subdivision / bounded displacement. No NIF integration or reduction yet.</text>"
			"<text x='32' y='112'>Original: 4 vertices, 2 triangles</text>"
			"<text x='425' y='112'>Height field: 32 x 32 samples</text>"
			"<text x='815' y='112'>Displaced: 289 vertices, 512 triangles</text></g>";
		for (std::size_t y = 0; y < height.height; ++y) {
			for (std::size_t x = 0; x < height.width; ++x) {
				int value = int(height.values[y * height.width + x] * 255);
				svg << "<rect x='" << 432 + x * 10 << "' y='" << 140 + y * 10
					<< "' width='10' height='10' fill='rgb(" << value << ',' << value << ',' << value << ")'/>";
			}
		}
		auto drawMesh = [&svg](const Mesh &mesh, double origin) {
			svg << "<g fill='none' stroke='#72d8c4' stroke-width='0.7'>";
			for (const auto &triangle : mesh.triangles) {
				svg << "<polygon points='";
				for (auto index : triangle) {
					const auto &p = mesh.vertices[index].position;
					svg << origin + (double(p[0]) - p[1]) * 170 << ','
						<< 270 + (double(p[0]) + p[1]) * 75 - p[2] * 340 << ' ';
				}
				svg << "'/>";
			}
			svg << "</g>";
		};
		drawMesh(original, 205);
		drawMesh(displaced, 995);
		svg << "</svg>\n";
		svg.close();
		std::cout << "Wrote synthetic demonstration to " << argv[1] << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
