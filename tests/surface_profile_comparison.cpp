#include "geometry/vanillaplus.h"
#include "model/nifmodel.h"
#include "ui/vanillaplusdialog.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace VanillaPlus;
static const QStringList names{"Neutral","Rock","Wood","Masonry","Metal","Soil"};
static const QStringList regions{"broad bump","broad bowl","narrow pit","elongated ridge","elongated fissure",
	"saddle","parallel bands","incoherent roughness","planar unit with recessed boundaries","curved grain with radial knot"};

static QImage picture(const HeightField &field,bool signedValues)
{
	QImage image(int(field.width),int(field.height),QImage::Format_RGB32);
	for (size_t y=0;y<field.height;++y) for (size_t x=0;x<field.width;++x) {
		double value=field.values[y*field.width+x]+(signedValues?0.5:0);
		int gray=int(std::clamp(value,0.0,1.0)*255+0.5); image.setPixel(int(x),int(y),qRgb(gray,gray,gray));
	}
	return image;
}

static bool synthetic(const QString &output,QTextStream &csv)
{
	// One common square field, ten isolated motifs. All profiles see the same
	// texture-space scales, feature sizes, pixels, and sign convention.
	HeightField height{256,256,std::vector<float>(256*256)}; uint32_t random=19;
	for (size_t y=0;y<256;++y) for (size_t x=0;x<256;++x) {
		double u=(x+0.5)/256,v=(y+0.5)/256; int column=std::min(4,int(u*5)),row=std::min(1,int(v*2)),region=row*5+column;
		double a=u-(column+0.5)/5,b=v-(row+0.5)/2,r2=a*a+b*b;
		double window=std::exp(-std::pow(a/0.075,8)-std::pow(b/0.16,8));
		double broad=0.35*std::exp(-r2/(2*0.03*0.03)),value=0;
		random=random*1664525u+1013904223u;
		switch (region) {
		case 0: value=broad; break;
		case 1: value=-broad; break;
		case 2: value=-0.35*std::exp(-r2/(2*0.005*0.005)); break;
		case 3: value=0.3*std::exp(-a*a/(2*0.008*0.008)-b*b/(2*0.075*0.075)); break;
		case 4: value=-0.3*std::exp(-a*a/(2*0.008*0.008)-b*b/(2*0.075*0.075)); break;
		case 5: value=0.28*(a*a-b*b)/(0.035*0.035)*std::exp(-r2/(2*0.035*0.035)); break;
		case 6: value=0.2*std::cos(a*2*3.14159265358979323846/0.025); break;
		case 7: value=0.15*(2*double(random>>8)/0xFFFFFFu-1); break;
		case 8: value=0.18-0.28*std::max(std::exp(-std::pow((std::abs(a)-0.05)/0.004,2)),std::exp(-std::pow((std::abs(b)-0.09)/0.004,2))); break;
		case 9: value=0.15*std::sin(a*2*3.14159265358979323846/0.02+1.8*std::atan2(b,a)*std::exp(-r2/(0.025*0.025)))+0.22*std::exp(-r2/(2*0.008*0.008)); break;
		}
		height.values[y*256+x]=float(0.5+value*window);
	}
	auto measured=analyzeSurfaceMorphology(height,0.5f,1,Address::Clamp,Address::Clamp);
	if (!picture(measured.original,true).save(output+"/synthetic-source.png")) return false;
	for (int p=0;p<names.size();++p) {
		auto result=p==0?surfacePrior(height,0.5f,1,SurfaceProfile::Neutral,Address::Clamp,Address::Clamp)
			:applySurfaceProfile(measured,static_cast<SurfaceProfile>(p),Address::Clamp,Address::Clamp);
		auto mask=result.suppression; if (mask.values.empty()) mask={256,256,std::vector<float>(256*256,0)};
		if (!picture(result.target,true).save(output+"/synthetic-"+names[p]+".png") || !picture(mask,false).save(output+"/synthetic-"+names[p]+"-mask.png")) return false;
		for (int region=0;region<10;++region) {
			double before=0,after=0,suppression=0; size_t count=0;
			for (size_t y=0;y<256;++y) for (size_t x=0;x<256;++x) {
				if (int((y+0.5)*2/256)*5+std::min(4,int((x+0.5)*5/256))!=region) continue;
				size_t i=y*256+x; double original=result.original.values[i],target=result.target.values[i];
				if (target<std::min(0.0,original) || target>std::max(0.0,original)) return false;
				before+=original*original; after+=target*target; suppression+=mask.values[i]; ++count;
			}
			csv << "synthetic," << names[p] << ',' << regions[region] << ',' << std::sqrt(before/count) << ',' << std::sqrt(after/count) << ',' << (before>0?after/before:1) << ',' << suppression/count << '\n';
		}
	}
	return true;
}

static bool realMesh(const QString &root,const QString &relative,int block,const QString &label,const QString &output,QTextStream &log)
{
	NifModel nif; if (!nif.loadFromFile(root+"/Meshes Vanilla Test/meshes/"+relative)) return false;
	QString textureRoot=root+"/Textures Vanilla Test",nested=textureRoot+"/textures/dungeons/dwemerruins";
	nif.getGameResources().close_archives(); nif.getGameResources().dataPaths.prepend(textureRoot);
	for (const QString &suffix:{QString("/textures"),QString("/textures/textures")}) if (QDir(nested+suffix).exists()) nif.getGameResources().dataPaths.prepend(nested+suffix);
	if (block<0) for (int i=0;i<nif.getBlockCount();++i) if (nif.isNiBlock(nif.getBlockIndex(i),"BSTriShape")) { block=i; break; }
	QByteArray before; QBuffer original(&before); original.open(QIODevice::WriteOnly); if (!nif.save(original)) return false;
	bool passed=false;
	QTimer::singleShot(0,[&]() {
		auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
		auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight"); auto profile=dialog->findChild<QComboBox *>("vanillaSurfaceProfile");
		auto tess=dialog->findChild<QComboBox *>("vanillaTessellationMode"); auto depth=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
		auto view=dialog->findChild<QComboBox *>("vanillaPreviewMode"); auto preview=dialog->findChild<QLabel *>("vanillaMeshPreview");
		auto height=dialog->findChild<QLabel *>("vanillaHeightPreview"); auto stats=dialog->findChild<QLabel *>("vanillaPreviewStats");
		auto mask=dialog->findChild<QCheckBox *>("vanillaRockSuppressionPreview"); auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
		auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus"); auto strength=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
		if (!generate || !profile || !tess || !depth || !view || !preview || !height || !stats || !mask || !stage || !status || !strength || profile->count()!=6) { dialog->reject(); return; }
		bool diffuse=false,normal=false;
		for (auto text:dialog->findChildren<QLabel *>()) { diffuse|=text->text().contains("Diffuse: Loaded:"); normal|=text->text().contains("Normal: Loaded:"); }
		if (!diffuse || !normal) { std::cerr << label.toStdString() << " source textures missing\n"; dialog->reject(); return; }
		generate->click(); tess->setCurrentIndex(1); depth->setValue(2); view->setCurrentIndex(2);
		for (const char *name:{"vanillaDarkFeatureBias","vanillaQualityCleanup","vanillaReduce","vanillaPatchRetopology"}) if (auto box=dialog->findChild<QCheckBox *>(name)) box->setChecked(false);
		double fixedStrength=strength->value(); QString counts; QImage baseline;
		for (int p=0;p<6;++p) {
			mask->setChecked(false); profile->setCurrentIndex(p);
			QString count=stats->text().section(". ",0,1);
			if (p==0) { counts=count; baseline=preview->pixmap().toImage(); }
			else if (counts!=count || strength->value()!=fixedStrength) { dialog->reject(); return; }
			if (!preview->pixmap().save(output+"/"+label+"-"+names[p]+".png")) { dialog->reject(); return; }
			log << label << " / " << names[p] << " / strength " << fixedStrength << ": " << stats->text() << '\n';
			mask->setChecked(true);
			if (p==0) { QImage black(256,256,QImage::Format_RGB32); black.fill(Qt::black); black.save(output+"/"+label+"-"+names[p]+"-mask.png"); }
			else if (!height->pixmap().isNull()) height->pixmap().save(output+"/"+label+"-"+names[p]+"-mask.png");
			stage->click(); if (!status->text().contains("Staged copy passed")) { std::cerr << label.toStdString() << ": " << status->text().toStdString() << '\n'; dialog->reject(); return; }
		}
		mask->setChecked(false); profile->setCurrentIndex(0);
		passed=preview->pixmap().toImage()==baseline; dialog->reject();
	});
	showVanillaPlusGeometry(&nif,nif.getBlockIndex(block),nullptr);
	QByteArray after; QBuffer verify(&after); verify.open(QIODevice::WriteOnly);
	if (!passed || !nif.save(verify) || before!=after) return false;
	std::cout << label.toStdString() << ": six profiles, identical counts/settings, all staged; Neutral restored exactly; Cancel unchanged\n";
	return true;
}

int main(int argc,char **argv)
{
	QApplication app(argc,argv); if (argc!=2 || !NifModel::loadXML()) return 1;
	QString root=QString::fromLocal8Bit(argv[1]),output=QCoreApplication::applicationDirPath()+"/profile-comparison";
	if (!QDir().mkpath(output)) return 1;
	QFile csvFile(output+"/synthetic.csv"),logFile(output+"/real-meshes.txt");
	if (!csvFile.open(QIODevice::WriteOnly) || !logFile.open(QIODevice::WriteOnly)) return 1;
	QTextStream csv(&csvFile),log(&logFile); csv << "fixture,profile,region,input_rms,target_rms,retained_energy,mean_attenuation\n";
	if (!synthetic(output,csv)) return 1;
	if (!realMesh(root,"landscape/rocks/rockm01.nif",-1,"rock",output,log)
		|| !realMesh(root,"farmhouse02.nif",22,"wood",output,log)
		|| !realMesh(root,"farmhouse02.nif",15,"masonry",output,log)
		|| !realMesh(root,"dwewallsconce03.nif",5,"metal",output,log)) return 1;
	log << "No real dirt/ground mesh plus referenced texture pair is present. Soil is compared on the shared synthetic field and all four real fixtures.\n";
	QFile htmlFile(output+"/comparison.html"); if (!htmlFile.open(QIODevice::WriteOnly)) return 1; QTextStream html(&htmlFile);
	html << "<!doctype html><meta charset='utf-8'><title>Vanilla+ Surface Profile Comparison</title><style>body{font:16px system-ui;background:#20242a;color:#eee;margin:24px}img{max-width:32%;vertical-align:top;background:#15191d}select{font:inherit;margin:8px}table{border-collapse:collapse}td,th{padding:6px;border:1px solid #555}a{color:#9edbd0}</style>"
		<< "<h1>Surface Profile Comparison</h1><p>Same source, height, strength, uniform depth 2 and safety per real fixture. No Dark Feature Bias, cleanup or reduction. Images are grayscale geometry previews, not textured renders.</p>"
		<< "<label>Fixture <select id='fixture'><option>synthetic</option><option>rock</option><option>wood</option><option>masonry</option><option>metal</option></select></label><label>Profile <select id='profile'>";
	for (const auto &name:names) html << "<option>" << name << "</option>";
	html << "</select></label><p>Left: Neutral. Center: selected profile. Right: actual target attenuation (black = none, white = full).</p><img id='baseline'><img id='target'><img id='mask'><p>";
	for (int i=0;i<regions.size();++i) html << i+1 << ": " << regions[i] << (i==4?"<br>":"; ");
	html << "</p><p>Synthetic regions run left to right, then the next row. Soil deliberately admits the input morphology, so its target agrees with Neutral; it still uses shared descriptors for diagnostics. Real soil/ground assets were unavailable.</p>"
		<< "<p><a href='synthetic.csv'>Synthetic regional measurements</a> · <a href='real-meshes.txt'>Real fixture statistics</a></p><script>const f=document.querySelector('#fixture'),p=document.querySelector('#profile');function update(){let b=f.value,s=p.value;document.querySelector('#baseline').src=b+'-Neutral.png';document.querySelector('#target').src=b+'-'+s+'.png';document.querySelector('#mask').src=b+'-'+s+'-mask.png'}f.onchange=p.onchange=update;update()</script>";
	std::cout << "Comparison artifact: " << output.toStdString() << "/comparison.html\n";
}
