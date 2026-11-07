#include "materialuvpreview.h"
#include <algorithm>
#include <cmath>
#include <vector>

QImage materialUvPreview(const QImage &source,std::array<double,2> scale,
	std::array<double,2> offset,bool wrapU,bool wrapV,int size)
{
	if (source.isNull() || size<=0 || !std::isfinite(scale[0]) || !std::isfinite(scale[1])
		|| !std::isfinite(offset[0]) || !std::isfinite(offset[1])) return {};
	QImage result(size,size,QImage::Format_RGB32);
	struct Axis { int a,b; double fraction; };
	auto axis=[](double value,int length,bool wrap) -> Axis {
		value=wrap ? value-std::floor(value) : std::clamp(value,0.0,1.0);
		double p=value*length-0.5, low=std::floor(p);
		auto index=[=](double x) {
			if (!wrap) return int(std::clamp(x,0.0,double(length-1)));
			if (x<0) return length-1;
			return x>=length ? 0 : int(x);
		};
		return {index(low),index(low+1),p-low};
	};
	std::vector<Axis> horizontal(size),vertical(size);
	for (int i=0;i<size;++i) {
		double transformedU=(i+0.5)/size*scale[0]+offset[0],transformedV=(i+0.5)/size*scale[1]+offset[1];
		if (!std::isfinite(transformedU) || !std::isfinite(transformedV)) return {};
		horizontal[i]=axis(transformedU,source.width(),wrapU); vertical[i]=axis(transformedV,source.height(),wrapV);
	}
	bool direct=source.format()==QImage::Format_RGB32 || source.format()==QImage::Format_ARGB32 || source.format()==QImage::Format_ARGB32_Premultiplied;
	for (int y=0;y<size;++y) {
		auto v=vertical[y]; auto output=reinterpret_cast<QRgb *>(result.scanLine(y));
		const QRgb *top=direct?reinterpret_cast<const QRgb *>(source.constScanLine(v.a)):nullptr;
		const QRgb *bottom=direct?reinterpret_cast<const QRgb *>(source.constScanLine(v.b)):nullptr;
		for (int x=0;x<size;++x) {
		auto u=horizontal[x];
		QRgb c[4];
		if (direct) { c[0]=top[u.a]; c[1]=top[u.b]; c[2]=bottom[u.a]; c[3]=bottom[u.b]; }
		else { c[0]=source.pixel(u.a,v.a); c[1]=source.pixel(u.b,v.a); c[2]=source.pixel(u.a,v.b); c[3]=source.pixel(u.b,v.b); }
		auto blend=[&](auto channel) {
			double a=std::lerp(double(channel(c[0])),double(channel(c[1])),u.fraction);
			double b=std::lerp(double(channel(c[2])),double(channel(c[3])),u.fraction);
			return int(std::clamp(std::lround(std::lerp(a,b,v.fraction)),0L,255L));
		};
		output[x]=qRgb(blend([](QRgb p){ return qRed(p); }),
			blend([](QRgb p){ return qGreen(p); }),blend([](QRgb p){ return qBlue(p); }));
		}
	}
	return result;
}
