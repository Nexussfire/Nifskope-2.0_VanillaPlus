#ifndef MATERIALUVPREVIEW_H
#define MATERIALUVPREVIEW_H
#include <QImage>
#include <array>

// 2D diagnostic sampling of a texture; not a complete material renderer.
QImage materialUvPreview(const QImage &source, std::array<double,2> scale,
	std::array<double,2> offset, bool wrapU, bool wrapV, int size);
#endif
