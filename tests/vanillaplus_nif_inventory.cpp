#include "model/nifmodel.h"
#include <QApplication>
#include <iostream>

int main(int argc,char **argv)
{
	QApplication app(argc,argv);
	if (argc!=2) { std::cerr << "Expected one NIF path, got " << argc << "\n"; return 1; }
	if (!NifModel::loadXML()) { std::cerr << "XML schema load failed\n"; return 1; }
	NifModel n;
	if (!n.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 2;
	std::cout << "Version " << n.getVersionNumber() << " user " << n.getUserVersion() << " BS " << n.getBSVersion() << "\n";
	for (int i=0;i<n.getBlockCount();++i) {
		auto b=n.getBlockIndex(i);
		std::cout << i << " " << n.itemName(b).toStdString() << " name=" << n.get<QString>(b,"Name").toStdString();
		if (n.isNiBlock(b,"NiTriShape") || n.isNiBlock(b,"NiTriStrips")) {
			auto d=n.getBlockIndex(n.getLink(b,"Data"));
			std::cout << " data=" << n.getBlockNumber(d) << " skin=" << n.getLink(b,"Skin Instance")
				<< " verts=" << n.get<int>(d,"Num Vertices") << " triangles=" << n.get<int>(d,"Num Triangles")
				<< " uvsets=" << n.rowCount(n.getIndex(d,"UV Sets")) << " colors=" << n.getArray<Color4>(d,"Vertex Colors").size()
				<< " additional=" << n.getLink(d,"Additional Data") << " flags=" << n.get<unsigned>(d,"BS Data Flags")
				<< " normals=" << n.getArray<Vector3>(d,"Normals").size()
				<< " tangents=" << n.getArray<Vector3>(d,"Tangents").size()
				<< " matchgroups=" << n.get<int>(d,"Num Match Groups")
				<< " parents=" << n.getParentLinks(n.getBlockNumber(d)).size();
		}
		if (n.isNiBlock(b,"BSTriShape")) std::cout << " verts=" << n.get<int>(b,"Num Vertices")
			<< " triangles=" << n.get<int>(b,"Num Triangles") << " skin=" << n.getLink(b,"Skin");
		if (n.isNiBlock(b,"BSShaderTextureSet")) for (const auto &path:n.getArray<QString>(b,"Textures"))
			if (!path.isEmpty()) std::cout << " [" << path.toStdString() << "]";
		std::cout << "\n";
	}
}
