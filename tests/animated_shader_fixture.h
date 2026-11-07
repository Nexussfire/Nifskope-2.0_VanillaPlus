#pragma once
#include "model/nifmodel.h"
#include <stdexcept>

inline void addAnimatedShaderFixture(NifModel &nif,int shaderId)
{
	auto shader=nif.getBlockIndex(shaderId); int next=nif.getLink(shader,"Controller");
	auto uv=nif.get<Vector2>(shader,"UV Offset");
	auto check=[](bool ok) { if(!ok) throw std::runtime_error("animated shader fixture failed"); };
	for(int axis=0;axis<3;++axis) {
		auto c=nif.insertNiBlock("BSLightingShaderPropertyFloatController"),interp=nif.insertNiBlock("NiFloatInterpolator"),data=nif.insertNiBlock("NiFloatData");
		check(c.isValid() && interp.isValid() && data.isValid());
		check(nif.setLink(c,"Target",shaderId) && nif.setLink(c,"Next Controller",next) && nif.setLink(c,"Interpolator",nif.getBlockNumber(interp)) && nif.setLink(interp,"Data",nif.getBlockNumber(data)));
		check(nif.set<quint32>(c,"Controlled Variable",axis==0?20u:axis==1?22u:11u) && nif.set<quint16>(c,"Flags",72) && nif.set<float>(c,"Frequency",1) && nif.set<float>(c,"Phase",0) && nif.set<float>(c,"Start Time",0) && nif.set<float>(c,"Stop Time",2));
		auto group=nif.getIndex(data,"Data"); check(nif.set<quint32>(group,"Num Keys",2) && nif.set<quint32>(group,"Interpolation",1) && nif.updateArraySize(group,"Keys"));
		auto keys=nif.getIndex(group,"Keys"); float start=axis<2?uv[axis]:nif.get<float>(shader,"Alpha");
		for(int k=0;k<2;++k) { auto key=nif.getIndex(keys,k); check(nif.set<float>(key,"Time",float(k*2)) && nif.set<float>(key,"Value",start+float(k)*(axis==0?.125f:axis==1?-.125f:-.25f))); }
		next=nif.getBlockNumber(c);
	}
	check(nif.setLink(shader,"Controller",next));
}
