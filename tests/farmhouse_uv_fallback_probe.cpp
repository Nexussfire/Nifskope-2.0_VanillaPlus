#include "model/nifmodel.h"
#include "geometry/vanillaplus.h"
#include "ui/vanillaplusdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUndoStack>
#include <algorithm>
#include <iostream>
#include <limits>

int main(int argc,char **argv)
{
	QApplication app(argc,argv);
	if (argc!=3 || !NifModel::loadXML()) return 1;
	NifModel nif;
	if (!nif.loadFromFile(QString::fromLocal8Bit(argv[1]))) return 1;
	nif.getGameResources().close_archives();
	nif.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
	QUndoStack undo; nif.undoStack=&undo;
	QByteArray before; QBuffer b(&before); b.open(QIODevice::WriteOnly); if (!nif.save(b)) return 1;
	for (int index:{35,38}) {
		auto shape=nif.getBlockIndex(index);
		auto shader=nif.getBlockIndex(nif.getLink(shape,"Shader Property"));
		auto vertices=nif.getIndex(shape,"Vertex Data");
		VanillaPlus::Mesh mesh; float minimum[2]={1e30f,1e30f},maximum[2]={-1e30f,-1e30f};
		for (int i=0;i<nif.rowCount(vertices);++i) {
			auto row=nif.getIndex(vertices,i); auto p=nif.get<Vector3>(row,"Vertex");
			auto n=nif.get<Vector3>(row,"Normal"); auto uv=nif.get<Vector2>(row,"UV");
			VanillaPlus::Vertex v; v.position={p[0],p[1],p[2]}; v.normal={n[0],n[1],n[2]}; v.uv={uv[0],uv[1]};
			for (int j=0;j<2;++j) { minimum[j]=std::min(minimum[j],uv[j]); maximum[j]=std::max(maximum[j],uv[j]); }
			mesh.vertices.push_back(v);
		}
		for (const auto &t:nif.getArray<Triangle>(shape,"Triangles")) mesh.triangles.push_back({t[0],t[1],t[2]});
		auto scale=nif.get<Vector2>(shader,"UV Scale"),offset=nif.get<Vector2>(shader,"UV Offset");
		VanillaPlus::Displacement mapping; mapping.uvScale={scale[0],scale[1]}; mapping.uvOffset={offset[0],offset[1]};
		auto assessment=VanillaPlus::assessUVIsolation(mesh,mapping);
		std::cout << "Shape " << index << " " << nif.get<QString>(shape,"Name").toStdString()
			<< " UV bounds " << minimum[0] << "," << minimum[1] << " to " << maximum[0] << "," << maximum[1]
			<< " fallback=" << assessment.useLegacy << " reason=" << assessment.reason << '\n';
		bool checked=false,safelyRejected=false;
		QTimer::singleShot(0,[&] {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
			auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
			auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
			if (!status || !depth || !strength || !stage) {
				for (auto label:dialog->findChildren<QLabel *>())
					safelyRejected |= label->text().contains("Non-manifold geometry cannot be previewed safely");
				if (!safelyRejected) std::cerr << "Shape " << index << " cannot enter preview for an unexpected reason\n";
				dialog->reject(); return;
			}
			depth->setValue(1); strength->setValue(0.1);
			bool fallbackShown=!assessment.useLegacy || status->text().contains("Using original UV handling");
			stage->click(); checked=fallbackShown && status->text().contains("Staged copy passed");
			if (!checked) std::cerr << "Shape " << index << " staging failed: " << status->text().toStdString() << '\n';
			dialog->reject();
		});
		showVanillaPlusGeometry(&nif,shape,nullptr);
		if (!checked && !safelyRejected) return 1;
		std::cout << "Shape " << index << (checked ? " fallback preview and staged copy passed\n" : " was safely rejected as non-manifold\n");
	}
	QByteArray after; QBuffer a(&after); a.open(QIODevice::WriteOnly);
	if (!nif.save(a) || before!=after) { std::cerr << "Cancel changed the source model\n"; return 1; }
	std::cout << "Both frame checks preserved the source model exactly\n";
	return 0;
}
