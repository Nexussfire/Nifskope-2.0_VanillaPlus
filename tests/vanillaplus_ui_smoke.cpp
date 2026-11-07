#include "gamemanager.h"
#include "model/nifmodel.h"
#include "model/kfmmodel.h"
#include "io/nifstream.h"
#include "nifskope.h"
#include "ui/vanillaplusdialog.h"
#include "ui/materialgraphdialog.h"
#include "ui/materialuvpreview.h"
#include "ui/widgets/filebrowser.h"
#include "gl/glcontroller.h"

#include <QApplication>
#include <QKeyEvent>
#include <QBuffer>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QLabel>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

#include <iostream>
#include <bit>
#include <cmath>

class TestMaterialController final : public Controller
{
public:
	using Controller::Controller;
	void updateTime(float) override {}
};

int main( int argc, char ** argv )
{
	QApplication app( argc, argv );
	{
		QImage source(2,2,QImage::Format_RGB32);
		source.setPixel(0,0,qRgb(255,0,0)); source.setPixel(1,0,qRgb(0,255,0));
		source.setPixel(0,1,qRgb(0,0,255)); source.setPixel(1,1,qRgb(255,255,255));
		const QRgb expected[4]={qRgb(0,0,255),qRgb(255,0,0),qRgb(255,255,255),qRgb(0,255,0)};
		for (int i=0;i<4;++i) {
			auto image=materialUvPreview(source,{0,0},{-0.25,1.25},bool(i&2),bool(i&1),1);
			if (image.isNull() || image.pixel(0,0)!=expected[i]) { std::cerr << "UV preview addressing failed\n"; return 1; }
		}
		auto normal=materialUvPreview(source,{1,1},{0,0},true,true,2);
		auto mirrored=materialUvPreview(source,{-1,1},{1,0},true,true,2);
		if (normal.pixel(0,0)!=qRgb(255,0,0) || mirrored.pixel(0,0)!=qRgb(0,255,0)
			|| mirrored.pixel(1,0)!=qRgb(255,0,0)) { std::cerr << "UV scale/offset preview order failed\n"; return 1; }
		if (!materialUvPreview(source,{1.7e308,1},{1.7e308,0},true,true,2).isNull()) {
			std::cerr << "UV preview accepted overflowing transformed coordinates\n"; return 1;
		}
		QImage colors(3,2,QImage::Format_ARGB32);
		for (int y=0;y<2;++y) for (int x=0;x<3;++x) colors.setPixel(x,y,qRgba(30+80*x,50+100*y,220-40*x,20+35*x+75*y));
		for (auto format:{QImage::Format_RGB32,QImage::Format_ARGB32,QImage::Format_ARGB32_Premultiplied,QImage::Format_RGB888,QImage::Format_Grayscale8}) {
			auto encoded=colors.convertToFormat(format); QImage reference(3,2,QImage::Format_RGB32);
			for (int y=0;y<2;++y) for (int x=0;x<3;++x) { auto pixel=encoded.pixel(x,y); reference.setPixel(x,y,qRgb(qRed(pixel),qGreen(pixel),qBlue(pixel))); }
			for (int address=0;address<4;++address)
				if (materialUvPreview(encoded,{-1.3,0.75},{-0.4,1.1},bool(address&1),bool(address&2),7)
					!=materialUvPreview(reference,{-1.3,0.75},{-0.4,1.1},bool(address&1),bool(address&2),7)) {
					std::cerr << "Fast UV preview changed raw-channel or image-format semantics\n"; return 1;
				}
		}
	}
	if ( argc != 3 ) { std::cerr << "Expected NIF and texture root paths\n"; return 1; }
	QTemporaryDir settingsDir;
	if ( !settingsDir.isValid() ) { std::cerr << "Could not isolate settings\n"; return 1; }
	QSettings::setDefaultFormat( QSettings::IniFormat );
	QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settingsDir.path() );
	app.setOrganizationName( "VanillaPlusSmokeTest" ); app.setApplicationName( "VanillaPlusSmokeTest" );
	{
		QString gameRoot = settingsDir.path() + "/game";
		if ( !QDir().mkpath( gameRoot + "/Data/Textures" ) ) { std::cerr << "Could not create test resource tree\n"; return 1; }
		QSettings settings;
		QVariantMap paths, folders, statuses;
		paths.insert( "Skyrim SE", gameRoot );
		paths.insert( "Skyrim", gameRoot );
		paths.insert( "Fallout 4", gameRoot );
		folders.insert( "Skyrim", QStringList{} ); // An explicit empty list must stay empty.
		folders.insert( "Other Games", QStringList{ "custom-resource" } );
		statuses.insert( "Fallout 4", false );
		settings.setValue( "Game Paths", paths ); settings.setValue( "Game Folders", folders ); settings.setValue( "Game Status", statuses );
		int version = 2;
		Game::GameManager::update_settings( version );
		auto result = settings.value( "Game Folders" ).toMap();
		if ( version != 3 || result.value( "Skyrim SE" ).toStringList() != QStringList{ gameRoot + "/Data/Textures" }
			|| !result.contains( "Skyrim" ) || !result.value( "Skyrim" ).toStringList().isEmpty()
			|| result.contains( "Fallout 4" ) || result.value( "Other Games" ).toStringList() != QStringList{ "custom-resource" } ) {
			std::cerr << "Resource initialization migration failed or overwrote user settings\n"; return 1;
		}
		QByteArray realGame = qgetenv( "NIFSKOPE_TEST_GAME_ROOT" );
		if ( !realGame.isEmpty() ) {
			settings.clear();
			settings.setValue( "Game Paths", QVariantMap{ { "Skyrim SE", QString::fromLocal8Bit( realGame ) } } );
			settings.setValue( "Game Folders", QVariantMap{} );
			settings.setValue( "Game Status", QVariantMap{ { "Skyrim SE", true } } );
			version = 2;
			Game::GameManager::update_settings( version );
			auto found = settings.value( "Game Folders" ).toMap().value( "Skyrim SE" ).toStringList();
			if ( found.isEmpty() ) { std::cerr << "Real Skyrim resource scan found no entries\n"; return 1; }
			Game::GameManager::get()->load();
			QByteArray texture;
			if ( !Game::GameManager::get_file( texture, Game::SKYRIM_SE, "textures/landscape/mountains/mountainslab02.dds" )
				 || !texture.startsWith( "DDS " ) ) {
				std::cerr << "Real Skyrim texture could not be read from detected resources\n"; return 1;
			}
			std::cout << "Real Skyrim resource migration found " << found.size() << " paths and read MountainSlab02 DDS\n";
			Game::GameManager::close_resources();
		}
		settings.clear(); settings.setValue( "Game Manager Version", 3 );
		if ( !realGame.isEmpty() ) Game::GameManager::get()->load();
	}
	if ( !NifModel::loadXML() ) { std::cerr << "Could not load NIF schema\n"; return 1; }
	if ( !KfmModel::loadXML() ) { std::cerr << "Could not load KFM schema\n"; return 1; }
	{
		NifSkope window;
		auto action = window.findChild<QAction *>( "aVanillaPlusGeometry" );
		auto graphAction = window.findChild<QAction *>( "aMaterialGraph" );
		auto sculptAction = window.findChild<QAction *>( "aSculptGeometry" );
		auto toolbar = window.findChild<QToolBar *>( "tRender" );
		if ( !action || !graphAction || graphAction->icon().isNull() || !toolbar || !toolbar->actions().contains( graphAction ) || !toolbar->actions().contains( action ) || action->icon().isNull() ) {
			std::cerr << "Vanilla+ toolbar button missing\n"; return 1;
		}
		if (!sculptAction || sculptAction->icon().isNull() || !toolbar->actions().contains(sculptAction)) {
			std::cerr << "Sculpt toolbar button missing\n"; return 1;
		}
	}
	NifModel nif;
	if ( !nif.loadFromFile( QString::fromLocal8Bit( argv[1] ) ) ) { std::cerr << "Could not load test NIF\n"; return 1; }
	// Check the real NIF serializer on every finite binary16 bit pattern,
	// including signed zero and subnormals, in both coordinate slots.
	for (unsigned bits=0;bits<65536;++bits) {
		if ((bits & 0x7c00u) == 0x7c00u) continue;
		QByteArray encoded(4, '\0');
		encoded[0]=char(bits & 255); encoded[1]=char(bits >> 8);
		encoded[2]=char(bits & 255); encoded[3]=char(bits >> 8);
		QBuffer input(&encoded); input.open(QIODevice::ReadOnly);
		NifIStream reader(&nif,&input); NifValue value(NifValue::tHalfVector2);
		if (!reader.read(value)) { std::cerr << "Half UV read failed\n"; return 1; }
		QByteArray outputBytes; QBuffer output(&outputBytes); output.open(QIODevice::WriteOnly);
		NifOStream writer(&nif,&output);
		if (!writer.write(value) || outputBytes != encoded) {
			std::cerr << "Half UV bit round-trip failed: " << bits << '\n'; return 1;
		}
	}
	std::cout << "All 63,488 finite half-float UV bit patterns round-trip through NIF streams\n";
	auto & resources = nif.getGameResources();
	resources.close_archives();
	resources.dataPaths.prepend( QString::fromLocal8Bit( argv[2] ) );
	QModelIndex shape;
	for ( int i = 0; i < nif.getBlockCount(); ++i ) {
		auto block = nif.getBlockIndex( i );
		if ( nif.isNiBlock( block, "BSTriShape" ) ) { shape = block; break; }
	}
	if ( !shape.isValid() ) { std::cerr << "No BSTriShape\n"; return 1; }
	auto shaderForTest = nif.getBlockIndex( nif.getLink( shape, "Shader Property" ), "BSLightingShaderProperty" );
	auto textureSetForTest = nif.getBlockIndex( nif.getLink( shaderForTest, "Texture Set" ), "BSShaderTextureSet" );
	auto texturePathsForTest = nif.getIndex( textureSetForTest, "Textures" );
	auto diffuseForTest = nif.getIndex( texturePathsForTest, 0 );
	Vector2 initialScale = nif.get<Vector2>( shaderForTest, "UV Scale" );
	QString initialTexture = nif.get<QString>( diffuseForTest );
	std::cout << "Shader type/flags: " << nif.get<unsigned int>(shaderForTest,"Shader Type") << ' '
		<< std::hex << nif.get<unsigned int>(shaderForTest,"Shader Flags 1") << ' '
		<< nif.get<unsigned int>(shaderForTest,"Shader Flags 2") << std::dec << '\n';
	auto testDesc=nif.get<BSVertexDesc>(shape,"Vertex Desc");
	std::cout << "Rock tangent/color/particle: " << testDesc.HasFlag(VF_TANGENT) << '/'
		<< testDesc.HasFlag(VF_COLORS) << '/' << nif.get<quint32>(shape,"Particle Data Size") << '\n';
	QByteArray before, after;
	QBuffer first( &before ); first.open( QIODevice::WriteOnly );
	if ( !nif.save( first ) ) { std::cerr << "Could not snapshot NIF\n"; return 1; }
	first.close();
	bool passed = false;
	QTimer::singleShot( 0, [&]() {
		auto dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() );
		if ( !dialog ) { std::cerr << "No modal dialog\n"; return; }
		auto apply = dialog->findChild<QPushButton *>( "vanillaApply" );
		auto generate = dialog->findChild<QPushButton *>( "vanillaGenerateHeight" );
		auto stage = dialog->findChild<QPushButton *>( "vanillaStageCopy" );
		auto diffuseWeight = dialog->findChild<QDoubleSpinBox *>( "vanillaDiffuseWeight" );
		auto normalWeight = dialog->findChild<QDoubleSpinBox *>( "vanillaNormalWeight" );
		auto height = dialog->findChild<QLabel *>( "vanillaHeightPreview" );
		auto preview = dialog->findChild<QLabel *>( "vanillaMeshPreview" );
		auto status = dialog->findChild<QLabel *>( "vanillaPreviewStatus" );
		auto stats = dialog->findChild<QLabel *>( "vanillaPreviewStats" );
		auto mode = dialog->findChild<QComboBox *>( "vanillaPreviewMode" );
		auto tessellation = dialog->findChild<QComboBox *>( "vanillaTessellationMode" );
		auto spatial = dialog->findChild<QDoubleSpinBox *>( "vanillaBilateralSpatial" );
		auto protect = dialog->findChild<QCheckBox *>( "vanillaProtectPinching" );
		auto cleanup = dialog->findChild<QCheckBox *>( "vanillaQualityCleanup" );
		auto reduce = dialog->findChild<QCheckBox *>( "vanillaReduce" );
		auto smooth = dialog->findChild<QCheckBox *>( "vanillaSmoothShading" );
		auto fine = dialog->findChild<QDoubleSpinBox *>( "vanillaFineGain" );
		auto depth = dialog->findChild<QSpinBox *>( "vanillaSubdivisionDepth" );
		auto strength = dialog->findChild<QDoubleSpinBox *>( "vanillaDisplacementStrength" );
		if ( !apply || !generate || !stage || !diffuseWeight || !normalWeight || !height || !preview || !status || !stats || !mode || !depth || !strength || !tessellation || !spatial || !fine || !protect || !smooth || !cleanup || !reduce ) {
			std::cerr << "Missing preview controls: " << dialog->windowTitle().toStdString() << '\n';
			for ( auto label : dialog->findChildren<QLabel *>() ) std::cerr << label->text().toStdString() << '\n';
			dialog->reject(); return;
		}
		bool diffuseLoaded = false, normalLoaded = false;
		// Numeric typing must not run the full geometry pipeline per digit.
		{
			auto editor=strength->findChild<QLineEdit *>(); double previous=strength->value(),next=previous+.125;
			if(!editor || strength->keyboardTracking()) { std::cerr<<"Geometry numeric edits are not coalesced\n"; dialog->reject(); return; }
			strength->setFocus();
			editor->setText(strength->locale().toString(next,'f',strength->decimals()));
			if(strength->value()!=previous) { std::cerr<<"Typing committed prematurely\n"; dialog->reject(); return; }
			QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(strength,&enter);
			if(strength->value()!=next || !dialog->isVisible()) { std::cerr<<"Enter did not commit numeric edit safely: "<<strength->value()<<" vs "<<next<<", visible "<<dialog->isVisible()<<"\n"; dialog->reject(); return; }
			strength->setValue(previous);
		}
		for ( auto label : dialog->findChildren<QLabel *>() ) {
			diffuseLoaded |= label->text().contains( "Diffuse: Loaded:" );
			normalLoaded |= label->text().contains( "Normal: Loaded:" );
		}
		if ( !diffuseLoaded || !normalLoaded || height->pixmap().isNull() || apply->isEnabled() ) {
			std::cerr << "Texture preview or Apply state incorrect\n";
			for ( auto label : dialog->findChildren<QLabel *>() ) std::cerr << label->text().toStdString() << '\n';
			dialog->reject(); return;
		}
		generate->click();
		auto relief=QRegularExpression("Displacement RMS: requested ([0-9.eE+-]+); applied ([0-9.eE+-]+)").match(stats->text());
		if (!relief.hasMatch() || relief.captured(1).toDouble()<=0
			|| relief.captured(2).toDouble()<relief.captured(1).toDouble()*0.01) {
			std::cerr << "Rock gained triangles without meaningful relief: " << stats->text().toStdString() << '\n'; dialog->reject(); return;
		}
		stage->click();
		if (!status->text().contains("Staged copy passed")) {
			std::cerr << "Staged copy failed: " << status->text().toStdString() << '\n'; dialog->reject(); return;
		}
		{
			auto bias=dialog->findChild<QCheckBox *>("vanillaDarkFeatureBias");
			auto attraction=dialog->findChild<QCheckBox *>("vanillaDarkAttractionPreview");
			auto biasStrength=dialog->findChild<QDoubleSpinBox *>("vanillaDarkStrength");
			auto biasShift=dialog->findChild<QDoubleSpinBox *>("vanillaDarkMaximumShift");
			auto wire=dialog->findChild<QCheckBox *>("vanillaWireframe");
			if (!wire) for (auto box:dialog->findChildren<QCheckBox *>()) if (box->text()=="Show triangle edges") wire=box;
			if (!bias || !attraction || !biasStrength || !biasShift || !wire || bias->isChecked()) {
				std::cerr << "Dark feature experiment controls/defaults missing\n"; dialog->reject(); return;
			}
			QImage baseline=preview->pixmap().toImage(),baseHeight=height->pixmap().toImage();
			QString baselineStats=stats->text();
			preview->pixmap().save(QCoreApplication::applicationDirPath()+"/dark-rock-baseline.png");
			wire->setChecked(true); preview->pixmap().save(QCoreApplication::applicationDirPath()+"/dark-rock-baseline-wire.png"); wire->setChecked(false);
			attraction->setChecked(true);
			if (height->pixmap().toImage()==baseHeight || preview->pixmap().toImage()!=baseline) {
				std::cerr << "Attraction-only preview changed geometry or showed no field\n"; dialog->reject(); return;
			}
			height->pixmap().save(QCoreApplication::applicationDirPath()+"/dark-rock-attraction.png");
			bias->setChecked(true);
			auto countPattern=QRegularExpression("Working: ([0-9]+) vertices, ([0-9]+) triangles");
			auto baselineCount=countPattern.match(baselineStats),biasedCount=countPattern.match(stats->text());
			auto moved=QRegularExpression("([0-9]+) accepted; ([0-9]+) rejected").match(stats->text());
			if (!moved.hasMatch() || moved.captured(1).toInt()==0 || baselineCount.capturedTexts()!=biasedCount.capturedTexts()) {
				std::cerr << "Rock dark bias did not relocate samples at fixed density: " << stats->text().toStdString() << '\n'; dialog->reject(); return;
			}
			std::cout << "Dark rock baseline: " << baselineStats.toStdString() << '\n';
			std::cout << "Dark rock biased: " << stats->text().toStdString() << '\n';
			preview->pixmap().save(QCoreApplication::applicationDirPath()+"/dark-rock-biased.png");
			wire->setChecked(true); preview->pixmap().save(QCoreApplication::applicationDirPath()+"/dark-rock-biased-wire.png"); wire->setChecked(false);
			stage->click(); if (!status->text().contains("Staged copy passed")) {
				std::cerr << "Dark biased staged write-back failed: " << status->text().toStdString() << '\n'; dialog->reject(); return;
			}
			biasStrength->setValue(0);
			if (preview->pixmap().toImage()!=baseline) { std::cerr << "Zero-strength dark bias changed baseline\n"; dialog->reject(); return; }
			bias->setChecked(false); biasStrength->setValue(0.5); attraction->setChecked(false);
			if (preview->pixmap().toImage()!=baseline || height->pixmap().toImage()!=baseHeight) {
				std::cerr << "Disabling dark bias failed to restore exact baseline\n"; dialog->reject(); return;
			}
		}
		QImage comparison = preview->pixmap().toImage();
		{
			auto profile=dialog->findChild<QComboBox *>("vanillaSurfaceProfile");
			auto mask=dialog->findChild<QCheckBox *>("vanillaRockSuppressionPreview");
			if (!profile || !mask || profile->currentIndex()!=0) { std::cerr << "Surface profiles missing or Neutral not default\n"; dialog->reject(); return; }
			// Equal uniform density separates profile response from adaptive
			// changes in the number of triangles needed to sample the target.
			tessellation->setCurrentIndex(1);
			QString neutralStats=stats->text(); QImage neutralImage=preview->pixmap().toImage();
			preview->pixmap().save(QCoreApplication::applicationDirPath()+"/surface-rock-neutral.png");
			profile->setCurrentIndex(1);
			auto counts=QRegularExpression("Working: ([0-9]+) vertices, ([0-9]+) triangles");
			if (counts.match(neutralStats).capturedTexts()!=counts.match(stats->text()).capturedTexts() || !stats->text().contains("Rock target:") || preview->pixmap().toImage()==neutralImage) {
				std::cerr << "Rock profile did not change relief at matched density: " << stats->text().toStdString() << '\n'; dialog->reject(); return;
			}
			std::cout << "Surface Neutral: " << neutralStats.toStdString() << '\n';
			std::cout << "Surface Rock: " << stats->text().toStdString() << '\n';
			preview->pixmap().save(QCoreApplication::applicationDirPath()+"/surface-rock-profile.png");
			mask->setChecked(true); height->pixmap().save(QCoreApplication::applicationDirPath()+"/surface-rock-suppression.png");
			stage->click(); if (!status->text().contains("Staged copy passed")) { std::cerr << "Rock profile staging failed\n"; dialog->reject(); return; }
			mask->setChecked(false); profile->setCurrentIndex(0); tessellation->setCurrentIndex(0);
			if (preview->pixmap().toImage()!=comparison) { std::cerr << "Neutral failed to restore baseline exactly\n"; dialog->reject(); return; }
		}
		if ( !generate->isEnabled() || diffuseWeight->value() <= 0 || normalWeight->value() <= 0
			|| strength->value() <= 0 || depth->value() == 0 || mode->currentIndex() != 3 || tessellation->currentIndex() != 0
			|| comparison.isNull() || comparison.copy( 0, 85, 260, 185 ) == comparison.copy( 260, 85, 260, 185 ) ) {
			std::cerr << "Generate button did not create a visible combined preview: " << status->text().toStdString() << '\n';
			dialog->reject(); return;
		}
		auto match = QRegularExpression( "Working: [0-9]+ vertices, ([0-9]+) triangles\\. Uniform at this depth: ([0-9]+)" ).match( stats->text() );
		if ( !match.hasMatch() || match.captured(1).toULongLong() >= match.captured(2).toULongLong() ) {
			std::cerr << "Adaptive preview did not reduce triangle count: " << stats->text().toStdString() << '\n'; dialog->reject(); return;
		}
		std::cout << "Real rock adaptive triangles: " << match.captured(1).toStdString()
			<< "; uniform at same depth: " << match.captured(2).toStdString() << '\n';
		if (!protect->isChecked() || !smooth->isChecked()) { std::cerr << "Pinch protection or smooth shading not enabled\n"; dialog->reject(); return; }
		QImage protectedImage=preview->pixmap().toImage();
		protect->setChecked(false);
		QImage unprotectedImage=preview->pixmap().toImage();
		protect->setChecked(true);
		std::cout << stats->text().toStdString() << '\n';
		if (!cleanup->isChecked() || !stats->text().contains("Triangle cleanup:")) { std::cerr << "Triangle cleanup control missing\n"; dialog->reject(); return; }
		cleanup->setChecked(false);
		if (stats->text().contains("Triangle cleanup:")) { std::cerr << "Triangle cleanup toggle failed\n"; dialog->reject(); return; }
		cleanup->setChecked(true);
		if (protectedImage==unprotectedImage || !stats->text().contains("Pinch protection adjusted")) {
			std::cerr << "Pinch protection did not affect real rock preview\n"; dialog->reject(); return;
		}
		smooth->setChecked(false);
		if (protectedImage==preview->pixmap().toImage()) { std::cerr << "Shading mode did not affect preview\n"; dialog->reject(); return; }
		smooth->setChecked(true);
		dialog->grab().save( QCoreApplication::applicationDirPath() + "/vanillaplus-generated-smoke.png" );
		mode->setCurrentIndex(2); smooth->setChecked(false);
		dialog->grab().save( QCoreApplication::applicationDirPath() + "/vanillaplus-adaptive-flat-smoke.png" );
		tessellation->setCurrentIndex(1);
		dialog->grab().save( QCoreApplication::applicationDirPath() + "/vanillaplus-uniform-flat-smoke.png" );
		tessellation->setCurrentIndex(0); smooth->setChecked(true); mode->setCurrentIndex(3);
		reduce->setChecked(true);
		auto reducedMatch=QRegularExpression("Reduced: ([0-9]+) vertices, ([0-9]+) triangles; sampled error ([0-9.eE+-]+)").match(stats->text());
		bool biasPreserved=reducedMatch.hasMatch() && reducedMatch.captured(2).toULongLong()==match.captured(1).toULongLong()
			&& stats->text().contains("Reduction retained the dense surface to preserve section bias");
		if (!reducedMatch.hasMatch() || (reducedMatch.captured(2).toULongLong() >= match.captured(1).toULongLong() && !biasPreserved)) {
			std::cerr << "Real rock reduction failed: " << stats->text().toStdString() << ' ' << status->text().toStdString() << '\n'; dialog->reject(); return;
		}
		std::cout << "Real rock reduced triangles: " << reducedMatch.captured(2).toStdString()
			<< "; sampled error: " << reducedMatch.captured(3).toStdString() << '\n';
		stage->click();
		if (!status->text().contains("Staged copy passed")) { std::cerr << "Reduced staging failed: " << status->text().toStdString() << '\n'; dialog->reject(); return; }
		mode->setCurrentIndex(6);
		dialog->grab().save(QCoreApplication::applicationDirPath()+"/vanillaplus-reduction-smoke.png");
		reduce->setChecked(false); mode->setCurrentIndex(3);
		// The live model may change elsewhere while the dialog is open. Preview
		// must continue using its original material snapshot and decoded textures.
		nif.set<Vector2>( shaderForTest, "UV Scale", Vector2(7, 9) );
		nif.set<QString>( diffuseForTest, QString() );
		mode->setCurrentIndex( 0 ); mode->setCurrentIndex( 3 );
		bool snapshotStable = comparison == preview->pixmap().toImage();
		stage->click();
		bool staleRejected = status->text().contains("changed since the preview opened");
		nif.set<Vector2>( shaderForTest, "UV Scale", initialScale );
		nif.set<QString>( diffuseForTest, initialTexture );
		if (!snapshotStable) { std::cerr << "Preview used live material instead of snapshot\n"; dialog->reject(); return; }
		if (!staleRejected) { std::cerr << "Staging accepted a changed material\n"; dialog->reject(); return; }
		mode->setCurrentIndex( 4 );
		if (preview->pixmap().isNull()) { std::cerr << "UV alignment view is missing\n"; dialog->reject(); return; }
		dialog->grab().save( QCoreApplication::applicationDirPath() + "/vanillaplus-uv-smoke.png" );
		QImage filtered = height->pixmap().toImage();
		spatial->setValue( 0 ); fine->setValue( 1 );
		if ( filtered == height->pixmap().toImage() ) { std::cerr << "Smoothing controls did not change height preview\n"; dialog->reject(); return; }
		depth->setValue( 0 ); mode->setCurrentIndex( 2 ); strength->setValue( 0 );
		QImage flat = preview->pixmap().toImage();
		strength->setValue( 3 );
		QImage displaced = preview->pixmap().toImage();
		dialog->grab().save( QCoreApplication::applicationDirPath() + "/vanillaplus-ui-smoke.png" );
		passed = !flat.isNull() && !displaced.isNull() && flat != displaced && status->text().contains( "Preview only" );
		tessellation->setCurrentIndex( 1 ); depth->setValue( 5 );
		passed = passed && status->text().contains( "budget" ) && height->pixmap().isNull() && !apply->isEnabled();
		if ( !passed ) std::cerr << "Displacement control did not change the mesh preview: " << status->text().toStdString() << '\n';
		dialog->reject();
	} );
	showVanillaPlusGeometry( &nif, shape, nullptr );
	QBuffer second( &after ); second.open( QIODevice::WriteOnly );
	if ( !nif.save( second ) ) { std::cerr << "Could not snapshot after cancel\n"; return 1; }
	second.close();
	if ( !passed || before != after ) { std::cerr << "Preview changed the model or controls failed\n"; return 1; }
	NifModel reopened; QBuffer saved(&after); saved.open(QIODevice::ReadOnly);
	if (!reopened.load(saved)) { std::cerr << "Saved NIF did not reopen\n"; return 1; }
	auto reopenedShape=reopened.getBlockIndex(nif.getBlockNumber(shape));
	auto vertices=nif.getIndex(shape,"Vertex Data"), reopenedVertices=reopened.getIndex(reopenedShape,"Vertex Data");
	if (nif.rowCount(vertices)!=reopened.rowCount(reopenedVertices)) { std::cerr << "Reopened vertex count differs\n"; return 1; }
	for (int i=0;i<nif.rowCount(vertices);++i) {
		auto a=nif.get<Vector2>(nif.getIndex(vertices,i),"UV"), b=reopened.get<Vector2>(reopened.getIndex(reopenedVertices,i),"UV");
		for (int j=0;j<2;++j) if (std::bit_cast<quint32>(a[j])!=std::bit_cast<quint32>(b[j])) {
			std::cerr << "Reopened original UV differs at vertex " << i << '\n'; return 1;
		}
	}
	bool rejectedInvalid = false;
	QTimer::singleShot( 0, [&]() {
		if ( auto box = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() ) ) {
			rejectedInvalid = box->text().contains( "Select" );
			box->accept();
		}
	} );
	showVanillaPlusGeometry( &nif, QModelIndex(), nullptr );
	if ( !rejectedInvalid ) { std::cerr << "Invalid selection was not explained\n"; return 1; }
	auto expectWarning = [&](const QString &text) {
		bool shown=false;
		QTimer::singleShot(0,[&]() {
			if (auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
				shown=box->text().contains(text,Qt::CaseInsensitive); box->accept();
			} else if (auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
		});
		showVanillaPlusGeometry(&nif,shape,nullptr);
		return shown;
	};
	auto vertexData=nif.getIndex(shape,"Vertex Data"), vertex0=nif.getIndex(vertexData,0);
	Vector2 oldUV=nif.get<Vector2>(vertex0,"UV");
	if (!nif.set<HalfVector2>(vertex0,"UV",HalfVector2(0.1f,oldUV[1]))) { std::cerr << "Could not set test UV\n"; return 1; }
	bool rejectedUnrepresentable=expectWarning("half-precision");
	nif.set<HalfVector2>(vertex0,"UV",HalfVector2(oldUV));
	if (!rejectedUnrepresentable) { std::cerr << "Unrepresentable original UV was not rejected\n"; return 1; }
	auto originalFlags=nif.get<unsigned int>(shaderForTest,"Shader Flags 1");
	nif.set<unsigned int>(shaderForTest,"Shader Flags 1",originalFlags | (1u << 23));
	bool rejectedProjected=expectWarning("unsupported");
	nif.set<unsigned int>(shaderForTest,"Shader Flags 1",originalFlags);
	if (!rejectedProjected) { std::cerr << "Unsupported projected UV shader was not rejected\n"; return 1; }
	{
		NifModel classic; QBuffer input(&before); input.open(QIODevice::ReadOnly);
		if (!classic.load(input)) { std::cerr << "Could not create classic-shape test model\n"; return 1; }
		classic.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
		auto classicShape=classic.insertNiBlock("NiTriShape",-1);
		auto classicData=classic.insertNiBlock("NiTriShapeData",-1);
		if (!classic.setLink(classicShape,"Data",classic.getBlockNumber(classicData))
			|| !classic.setLink(classicShape,"Shader Property",nif.getBlockNumber(shaderForTest))
			|| !classic.set<quint16>(classicData,"Num Vertices",3)
			|| !classic.set<bool>(classicData,"Has Vertices",true)
			|| !classic.set<bool>(classicData,"Has Normals",true)
			|| !classic.set<quint16>(classicData,"BS Data Flags",1)
			|| !classic.set<quint16>(classicData,"Num Triangles",1)
			|| !classic.set<bool>(classicData,"Has Triangles",true)) {
			std::cerr << "Could not configure classic test shape\n"; return 1;
		}
		if (!classic.updateArraySize(classicData,"Vertices") || !classic.updateArraySize(classicData,"Normals")
			|| !classic.updateArraySize(classicData,"UV Sets") || !classic.updateArraySize(classicData,"Triangles")) {
			std::cerr << "Could not allocate classic test arrays\n"; return 1;
		}
		auto sets=classic.getIndex(classicData,"UV Sets");
		if (classic.rowCount(sets)!=1 || !classic.updateArraySize(classic.getIndex(sets,0))) {
			std::cerr << "Could not allocate classic UVs\n"; return 1;
		}
		classic.setArray<Vector3>(classicData,"Vertices",QVector<Vector3>{Vector3(0,0,0),Vector3(1,0,0),Vector3(0,1,0)});
		classic.setArray<Vector3>(classicData,"Normals",QVector<Vector3>{Vector3(0,0,1),Vector3(0,0,1),Vector3(0,0,1)});
		classic.setArray<Vector2>(classic.getIndex(sets,0),QVector<Vector2>{Vector2(0,0),Vector2(1,0),Vector2(0,1)});
		classic.setArray<::Triangle>(classicData,"Triangles",QVector<::Triangle>{::Triangle(0,1,2)});
		QUndoStack stack; classic.undoStack=&stack;
		QByteArray baseline; QBuffer baseBuffer(&baseline); baseBuffer.open(QIODevice::WriteOnly);
		if (!classic.save(baseBuffer)) { std::cerr << "Could not snapshot classic test\n"; return 1; }
		bool classicPreview=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) { if (auto message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { std::cerr << message->text().toStdString() << '\n'; message->accept(); } return; }
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
			auto apply=dialog->findChild<QPushButton *>("vanillaApply");
			auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			if (generate && apply && stage && status && generate->isEnabled()) {
				generate->click(); stage->click();
				classicPreview=!apply->isEnabled() && status->text().contains("preview only",Qt::CaseInsensitive);
			}
			dialog->reject();
		});
		showVanillaPlusGeometry(&classic,classicShape,nullptr);
		QByteArray afterClassic; QBuffer afterBuffer(&afterClassic); afterBuffer.open(QIODevice::WriteOnly); classic.save(afterBuffer);
		if (!classicPreview || baseline!=afterClassic || stack.count()!=0) { std::cerr << "Classic preview or read-only guarantee failed\n"; return 1; }
		auto stripShape=classic.insertNiBlock("NiTriStrips",-1);
		auto stripBlock=classic.insertNiBlock("NiTriStripsData",-1);
		if (!classic.setLink(stripShape,"Data",classic.getBlockNumber(stripBlock))
			|| !classic.setLink(stripShape,"Shader Property",nif.getBlockNumber(shaderForTest))
			|| !classic.set<quint16>(stripBlock,"Num Vertices",3)
			|| !classic.set<bool>(stripBlock,"Has Vertices",true)
			|| !classic.set<bool>(stripBlock,"Has Normals",true)
			|| !classic.set<quint16>(stripBlock,"BS Data Flags",1)
			|| !classic.set<quint16>(stripBlock,"Num Triangles",1)
			|| !classic.set<quint16>(stripBlock,"Num Strips",1)
			|| !classic.set<bool>(stripBlock,"Has Points",true)) {
			std::cerr << "Could not configure strip test shape\n"; return 1;
		}
		classic.updateArraySize(stripBlock,"Vertices"); classic.updateArraySize(stripBlock,"Normals"); classic.updateArraySize(stripBlock,"UV Sets");
		classic.updateArraySize(stripBlock,"Strip Lengths");
		classic.setArray<quint16>(stripBlock,"Strip Lengths",QVector<quint16>{3});
		classic.updateArraySize(stripBlock,"Points");
		auto stripUV=classic.getIndex(stripBlock,"UV Sets"), stripPoints=classic.getIndex(stripBlock,"Points");
		if (classic.rowCount(stripUV)!=1 || classic.rowCount(stripPoints)!=1
			|| !classic.updateArraySize(classic.getIndex(stripUV,0)) || !classic.updateArraySize(classic.getIndex(stripPoints,0))) {
			std::cerr << "Could not allocate strip test arrays\n"; return 1;
		}
		classic.setArray<Vector3>(stripBlock,"Vertices",QVector<Vector3>{Vector3(0,0,0),Vector3(1,0,0),Vector3(0,1,0)});
		classic.setArray<Vector3>(stripBlock,"Normals",QVector<Vector3>{Vector3(0,0,1),Vector3(0,0,1),Vector3(0,0,1)});
		classic.setArray<Vector2>(classic.getIndex(stripUV,0),QVector<Vector2>{Vector2(0,0),Vector2(1,0),Vector2(0,1)});
		classic.setArray<quint16>(classic.getIndex(stripPoints,0),QVector<quint16>{0,1,2});
		bool stripPreview=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) { if (auto message=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { std::cerr << message->text().toStdString() << '\n'; message->accept(); } return; }
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
			auto apply=dialog->findChild<QPushButton *>("vanillaApply");
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			if (generate && apply && status && generate->isEnabled()) {
				generate->click(); stripPreview=!apply->isEnabled() && status->text().contains("Preview only");
			}
			dialog->reject();
		});
		showVanillaPlusGeometry(&classic,stripShape,nullptr);
		if (!stripPreview || stack.count()!=0) { std::cerr << "Triangle-strip preview failed\n"; return 1; }
		classic.undoStack=nullptr;
		std::cout << "Classic NiTriShape and NiTriStrips previews passed; write-back disabled\n";
	}
	auto originalFlags2=nif.get<unsigned int>(shaderForTest,"Shader Flags 2");
	for (unsigned flag : {1u<<9,1u<<29}) {
		nif.set<unsigned int>(shaderForTest,"Shader Flags 2",originalFlags2 | flag);
		bool rejectedSpecial=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
			auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			if (generate && stage && status) {
				generate->click(); stage->click();
				rejectedSpecial=status->text().contains("cannot preserve safely");
			}
			dialog->reject();
		});
		showVanillaPlusGeometry(&nif,shape,nullptr);
		nif.set<unsigned int>(shaderForTest,"Shader Flags 2",originalFlags2);
		if (!rejectedSpecial) { std::cerr << "Special environmental shader was not rejected for staging\n"; return 1; }
	}
	// Exercise each single-source fallback using only temporary in-memory edits.
	auto shader = nif.getBlockIndex( nif.getLink( shape, "Shader Property" ), "BSLightingShaderProperty" );
	auto set = nif.getBlockIndex( nif.getLink( shader, "Texture Set" ), "BSShaderTextureSet" );
	auto textures = nif.getIndex( set, "Textures" );
	auto diffuseIndex = nif.getIndex( textures, 0 ), normalIndex = nif.getIndex( textures, 1 );
	QString diffusePath = nif.get<QString>( diffuseIndex ), normalPath = nif.get<QString>( normalIndex );
	for ( int missing = 0; missing < 2; ++missing ) {
		nif.set<QString>( missing == 0 ? diffuseIndex : normalIndex, QString() );
		bool fallbackPassed = false;
		QTimer::singleShot( 0, [&]() {
			if ( auto dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() ) ) {
				auto generate = dialog->findChild<QPushButton *>( "vanillaGenerateHeight" );
				auto dw = dialog->findChild<QDoubleSpinBox *>( "vanillaDiffuseWeight" );
				auto nw = dialog->findChild<QDoubleSpinBox *>( "vanillaNormalWeight" );
				auto height = dialog->findChild<QLabel *>( "vanillaHeightPreview" );
				if ( generate && dw && nw && height && generate->isEnabled() ) {
					generate->click();
					fallbackPassed = !height->pixmap().isNull() && ( missing == 0 ? ( dw->value() == 0 && nw->value() == 1 )
						: ( dw->value() == 1 && nw->value() == 0 ) );
				}
				dialog->reject();
			}
		} );
		showVanillaPlusGeometry( &nif, shape, nullptr );
		nif.set<QString>( diffuseIndex, diffusePath ); nif.set<QString>( normalIndex, normalPath );
		if ( !fallbackPassed ) { std::cerr << "Single-texture height fallback failed\n"; return 1; }
	}
	{
		Vector2 originalOffset=nif.get<Vector2>(shader,"UV Offset");
		if (!nif.set<Vector2>(shader,"UV Offset",Vector2(8,8))) return 1;
		bool fallbackShown=false,staged=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			auto stage=dialog->findChild<QPushButton *>("vanillaStageCopy");
			if (status && stage) {
				fallbackShown=status->text().contains("Using original UV handling") && status->text().contains("out-of-range");
				stage->click(); staged=status->text().contains("Staged copy passed");
			}
			dialog->reject();
		});
		showVanillaPlusGeometry(&nif,shape,nullptr);
		nif.set<Vector2>(shader,"UV Offset",originalOffset);
		if (!fallbackShown || !staged) { std::cerr << "Tiled UV fallback was not reported or could not stage\n"; return 1; }
	}
	{
		NifModel edited; QBuffer initial(&before); initial.open(QIODevice::ReadOnly);
		if (!edited.load(initial)) { std::cerr << "Could not create independent apply test\n"; return 1; }
		edited.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
		QUndoStack undo; edited.undoStack=&undo;
		int modelNotifications=0;
		QObject::connect(&edited,&NifModel::dataChanged,[&](const QModelIndex &,const QModelIndex &) { ++modelNotifications; });
		auto editedShape=edited.getBlockIndex(nif.getBlockNumber(shape));
		auto editedShader=edited.getBlockIndex(edited.getLink(editedShape,"Shader Property"));
		auto editedVertices=edited.getIndex(editedShape,"Vertex Data");
		QVector<ByteVector3> originalNormals;
		for (int i=0;i<edited.rowCount(editedVertices);++i)
			originalNormals.push_back(edited.get<ByteVector3>(edited.getIndex(editedVertices,i),"Normal"));
		for (int i=0;i<edited.rowCount(editedVertices);++i) {
			ByteColor4 color;
			color[0]=float((i*31)%256)/255; color[1]=float((i*47+11)%256)/255;
			color[2]=float((i*13+71)%256)/255; color[3]=float((i*53+17)%256)/255;
			if (!edited.set<ByteColor4>(edited.getIndex(editedVertices,i),"Vertex Colors",color)) {
				std::cerr << "Could not set test RGBA\n"; return 1;
			}
		}
		auto sourceTriangles=edited.getArray<::Triangle>(editedShape,"Triangles");
		auto firstTriangle=sourceTriangles[0];
		auto firstRow=edited.getIndex(editedVertices,firstTriangle[0]), secondRow=edited.getIndex(editedVertices,firstTriangle[1]);
		auto posA=edited.get<Vector3>(firstRow,"Vertex"), posB=edited.get<Vector3>(secondRow,"Vertex");
		auto uvA=edited.get<Vector2>(firstRow,"UV"), uvB=edited.get<Vector2>(secondRow,"UV");
		auto colorA=edited.get<ByteColor4>(firstRow,"Vertex Colors"), colorB=edited.get<ByteColor4>(secondRow,"Vertex Colors");
		QByteArray coloredBefore; QBuffer coloredOutput(&coloredBefore); coloredOutput.open(QIODevice::WriteOnly);
		if (!edited.save(coloredOutput)) { std::cerr << "Could not snapshot colored test mesh\n"; return 1; }
		auto uvScale=edited.get<Vector2>(editedShader,"UV Scale");
		bool applied=false, staleRefused=false, reductionUsed=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			auto generate=dialog->findChild<QPushButton *>("vanillaGenerateHeight");
			auto apply=dialog->findChild<QPushButton *>("vanillaApply");
			auto status=dialog->findChild<QLabel *>("vanillaPreviewStatus");
			auto tess=dialog->findChild<QComboBox *>("vanillaTessellationMode");
			auto depthControl=dialog->findChild<QSpinBox *>("vanillaSubdivisionDepth");
			auto strengthControl=dialog->findChild<QDoubleSpinBox *>("vanillaDisplacementStrength");
			auto reduce=dialog->findChild<QCheckBox *>("vanillaReduce");
			auto stats=dialog->findChild<QLabel *>("vanillaPreviewStats");
			if (!generate || !apply || !status || !tess || !depthControl || !strengthControl || !reduce || !stats) { dialog->reject(); return; }
			generate->click();
			tess->setCurrentIndex(1); depthControl->setValue(2); strengthControl->setValue(0); reduce->setChecked(true);
			if (auto bias=dialog->findChild<QCheckBox *>("vanillaDarkFeatureBias")) bias->setChecked(true);
			auto counts=QRegularExpression("Working: [0-9]+ vertices, ([0-9]+) triangles.*Reduced: [0-9]+ vertices, ([0-9]+) triangles").match(stats->text());
			reductionUsed=counts.hasMatch() && counts.captured(2).toULongLong()<counts.captured(1).toULongLong();
			if (!apply->isEnabled()) { std::cerr << "Apply not enabled for undo-capable model\n"; dialog->reject(); return; }
			edited.set<Vector2>(editedShader,"UV Scale",Vector2(7,9));
			apply->click();
			staleRefused=status->text().contains("changed since the preview opened") && undo.count()==0;
			edited.set<Vector2>(editedShader,"UV Scale",uvScale);
			apply->click();
			applied=dialog->result()==QDialog::Accepted;
			if (!applied) { std::cerr << "Apply failed: " << status->text().toStdString() << '\n'; dialog->reject(); }
		});
		showVanillaPlusGeometry(&edited,editedShape,nullptr);
		if (!applied || !staleRefused || !reductionUsed || undo.count()!=1 || undo.isClean() || modelNotifications==0 || edited.get<int>(editedShape,"Num Vertices")<=nif.get<int>(shape,"Num Vertices")) {
			std::cerr << "Apply or stale-source protection failed\n"; return 1;
		}
		auto saveEdited=[&]() { QByteArray data; QBuffer b(&data); b.open(QIODevice::WriteOnly); if (!edited.save(b)) data.clear(); return data; };
		QByteArray appliedBytes=saveEdited();
		NifModel appliedReopen; QBuffer appliedInput(&appliedBytes); appliedInput.open(QIODevice::ReadOnly);
		if (appliedBytes.isEmpty() || !appliedReopen.load(appliedInput)
			|| appliedReopen.get<int>(appliedReopen.getBlockIndex(nif.getBlockNumber(shape)),"Num Vertices")!=edited.get<int>(editedShape,"Num Vertices")) {
			std::cerr << "Applied model did not save/reopen in memory\n"; return 1;
		}
		auto appliedShape=appliedReopen.getBlockIndex(nif.getBlockNumber(shape));
		auto appliedVertices=appliedReopen.getIndex(appliedShape,"Vertex Data");
		for (int i=0;i<originalNormals.size();++i) {
			auto normal=appliedReopen.get<ByteVector3>(appliedReopen.getIndex(appliedVertices,i),"Normal");
			for (int j=0;j<3;++j) if (std::bit_cast<quint32>(normal[j])!=std::bit_cast<quint32>(originalNormals[i][j])) {
				std::cerr << "Packed original normal changed after undisplaced tessellation\n"; return 1;
			}
		}
		bool midpointFound=false;
		for (int i=nif.get<int>(shape,"Num Vertices");i<appliedReopen.rowCount(appliedVertices);++i) {
			auto row=appliedReopen.getIndex(appliedVertices,i); auto p=appliedReopen.get<Vector3>(row,"Vertex");
			auto uv=appliedReopen.get<Vector2>(row,"UV");
			bool match=true;
			for (int j=0;j<3;++j) match &= std::abs(p[j]-(posA[j]+posB[j])*0.5f)<0.0001f;
			for (int j=0;j<2;++j) match &= std::abs(uv[j]-(uvA[j]+uvB[j])*0.5f)<0.001f;
			if (!match) continue;
			auto color=appliedReopen.get<ByteColor4>(row,"Vertex Colors");
			bool colorMatch=true;
			for (int j=0;j<4;++j) colorMatch &= std::abs(color[j]-(colorA[j]+colorB[j])*0.5f)<=0.0041f;
			midpointFound |= colorMatch;
		}
		if (!midpointFound) { std::cerr << "Generated midpoint did not preserve interpolated RGBA\n"; return 1; }
		undo.undo();
		if (!undo.isClean() || saveEdited()!=coloredBefore) { std::cerr << "Undo did not restore original NIF bytes\n"; return 1; }
		undo.redo();
		if (undo.isClean() || saveEdited()!=appliedBytes) { std::cerr << "Redo did not restore applied NIF bytes\n"; return 1; }
		edited.undoStack=nullptr;
		std::cout << "Apply, stale-source rejection, in-memory reopen, byte-identical undo and redo passed\n";
	}
	{
		NifModel graph; QBuffer input(&before); input.open(QIODevice::ReadOnly);
		if (!graph.load(input)) return 1;
		graph.getGameResources().dataPaths.prepend(QString::fromLocal8Bit(argv[2]));
		QUndoStack stack; graph.undoStack=&stack;
		auto graphShape=graph.getBlockIndex(nif.getBlockNumber(shape));
		auto graphShader=graph.getBlockIndex(graph.getLink(graphShape,"Shader Property"));
		auto graphTextures=graph.getBlockIndex(graph.getLink(graphShader,"Texture Set"));
		auto initialOffset=graph.get<Vector2>(graphShader,"UV Offset");
		graph.set<Vector2>(graphShader,"UV Offset",Vector2(initialOffset[0],-0.0f));
		auto saveGraph=[&]() { QByteArray b; QBuffer out(&b); out.open(QIODevice::WriteOnly); if (!graph.save(out)) b.clear(); return b; };
		QByteArray initialBytes=saveGraph();
		auto findEdit=[](QDialog *dialog,const char *name) -> QLineEdit * {
			auto view=dialog->findChild<QGraphicsView *>("materialGraphView"); if (!view) return nullptr;
			for (auto item:view->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
				if (auto widget=proxy->widget()) if (auto edit=widget->findChild<QLineEdit *>(name)) return edit;
			return nullptr;
		};
		auto findTool=[](QDialog *dialog,const char *name) -> QToolButton * {
			auto view=dialog->findChild<QGraphicsView *>("materialGraphView"); if (!view) return nullptr;
			for (auto item:view->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
				if (auto widget=proxy->widget()) if (auto tool=widget->findChild<QToolButton *>(name)) return tool;
			return nullptr;
		};
		auto findWidget=[](QDialog *dialog,const char *name) -> QWidget * {
			auto view=dialog->findChild<QGraphicsView *>("materialGraphView"); if (!view) return nullptr;
			for (auto item:view->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
				if (auto widget=proxy->widget()) if (auto found=widget->findChild<QWidget *>(name)) return found;
			return nullptr;
		};
		bool canceled=false,slotOrder=false,clearWorked=false,browseCanceled=false,browseSelected=false,uvControl=false,previewChanges=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto alpha=findEdit(dialog,"materialAlpha"); auto diffuse=findEdit(dialog,"materialTexture0");
			auto choose=findTool(dialog,"materialTextureChoose0"), clear=findTool(dialog,"materialTextureClear0");
			auto view=dialog->findChild<QGraphicsView *>("materialGraphView");
			double ys[5]={}; bool all=true;
			for (int i=0;i<5;++i) {
				auto edit=findEdit(dialog,qPrintable(QString("materialTexture%1").arg(i)));
				auto proxy=edit ? edit->graphicsProxyWidget() : nullptr;
				if (!proxy && edit) proxy=edit->window()->graphicsProxyWidget();
				if (!proxy) all=false; else ys[i]=proxy->scenePos().y();
			}
			slotOrder=all && ys[0]<ys[1] && ys[1]<ys[2] && ys[2]<ys[3] && ys[3]<ys[4];
			if (alpha && diffuse && choose && clear && view) {
				auto offsetU=findEdit(dialog,"materialUVOffsetU"), offsetV=findEdit(dialog,"materialUVOffsetV");
				auto pad=findWidget(dialog,"materialUvOffsetPad");
				auto preview=qobject_cast<QLabel *>(findWidget(dialog,"materialUvPreview"));
				auto previewStatus=qobject_cast<QLabel *>(findWidget(dialog,"materialUvPreviewStatus"));
				if (offsetU && offsetV && pad && preview && previewStatus) {
					QImage previous=preview->pixmap().toImage();
					offsetU->setText("15000"); offsetV->setText("0");
					QMouseEvent press(QEvent::MouseButtonPress,QPointF(48,48),QPointF(48,48),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
					QMouseEvent move(QEvent::MouseMove,QPointF(67,56),QPointF(67,56),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);
					QMouseEvent release(QEvent::MouseButtonRelease,QPointF(67,56),QPointF(67,56),Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
					QApplication::sendEvent(pad,&press); QApplication::sendEvent(pad,&move); QApplication::sendEvent(pad,&release);
					uvControl=std::abs(offsetU->text().toDouble()-15000.25)<1e-6 && offsetV->text().toDouble()==0;
					offsetU->setText("15000"); offsetV->setText("0");
					QMouseEvent verticalPress(QEvent::MouseButtonPress,QPointF(48,48),QPointF(48,48),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
					QMouseEvent verticalMove(QEvent::MouseMove,QPointF(56,67),QPointF(56,67),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);
					QMouseEvent verticalRelease(QEvent::MouseButtonRelease,QPointF(56,67),QPointF(56,67),Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
					QApplication::sendEvent(pad,&verticalPress); QApplication::sendEvent(pad,&verticalMove); QApplication::sendEvent(pad,&verticalRelease);
					uvControl &= offsetU->text().toDouble()==15000 && std::abs(offsetV->text().toDouble()-0.25)<1e-6;
					offsetU->setText("0"); offsetV->setText("0");
					QMouseEvent finePress(QEvent::MouseButtonPress,QPointF(48,48),QPointF(48,48),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
					QMouseEvent fineMove(QEvent::MouseMove,QPointF(67,48),QPointF(67,48),Qt::NoButton,Qt::LeftButton,Qt::ControlModifier);
					QMouseEvent fineRelease(QEvent::MouseButtonRelease,QPointF(67,48),QPointF(67,48),Qt::LeftButton,Qt::NoButton,Qt::ControlModifier);
					QApplication::sendEvent(pad,&finePress); QApplication::sendEvent(pad,&fineMove); QApplication::sendEvent(pad,&fineRelease);
					uvControl &= std::abs(offsetU->text().toDouble()-0.025)<1e-6 && offsetV->text().toDouble()==0;
					offsetU->setText("0.2");
					previewChanges=!previous.isNull() && previous!=preview->pixmap().toImage()
						&& previewStatus->text().contains("base UV") && previewStatus->text().contains("no parallax or animation");
					QApplication::processEvents();
					dialog->grab().save(QCoreApplication::applicationDirPath()+"/material-uv-preview-smoke.png");
				}
				QString saved=diffuse->text();
				QTimer::singleShot(0,[&]() {
					if (auto browser=qobject_cast<FileBrowserWidget *>(QApplication::activeModalWidget())) browser->reject();
				});
				choose->click(); browseCanceled=(diffuse->text()==saved);
				QTimer::singleShot(0,[&]() {
					auto browser=qobject_cast<FileBrowserWidget *>(QApplication::activeModalWidget()); if (!browser) return;
					auto tree=browser->findChild<QTreeWidget *>();
					if (tree) for (QTreeWidgetItemIterator it(tree); *it; ++it)
						if ((*it)->text(0).endsWith("MountainSlab02_N.dds",Qt::CaseInsensitive)) {
							browser->checkItemActivated(*it,0); return;
						}
					browser->reject();
				});
				choose->click(); browseSelected=diffuse->text().endsWith("MountainSlab02_N.dds",Qt::CaseInsensitive) && diffuse->text().contains('\\');
				clear->click(); clearWorked=diffuse->text().isEmpty();
				alpha->setText("0.375"); diffuse->setText("textures\\test\\example.dds"); canceled=true;
			}
			QApplication::processEvents();
			dialog->grab().save(QCoreApplication::applicationDirPath()+"/material-graph-smoke.png");
			dialog->reject();
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		if (!canceled || !slotOrder || !clearWorked || !browseCanceled || !browseSelected || !uvControl || !previewChanges || saveGraph()!=initialBytes || stack.count()!=0) {
			std::cerr << "Material graph controls or cancel failed: " << slotOrder << ' ' << clearWorked << ' ' << browseCanceled << ' ' << browseSelected << ' ' << uvControl << ' ' << previewChanges << '\n'; return 1;
		}
		auto originalShaderFlags=graph.get<unsigned>(graphShader,"Shader Flags 1");
		auto originalShaderFlags2=graph.get<unsigned>(graphShader,"Shader Flags 2");
		auto originalShaderType=graph.get<unsigned>(graphShader,"Shader Type");
		graph.set<unsigned>(graphShader,"Shader Flags 1",originalShaderFlags | (1u<<7));
		bool correctEnvironment=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto label=qobject_cast<QLabel *>(findWidget(dialog,"materialTextureStatus4"));
			correctEnvironment=label && label->text().contains("Feature inactive");
			dialog->reject();
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		graph.set<unsigned>(graphShader,"Shader Flags 1",originalShaderFlags);
		if (!correctEnvironment || saveGraph()!=initialBytes) { std::cerr << "Environment feature state ignored shader type\n"; return 1; }
		bool rejectedBad=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto alpha=findEdit(dialog,"materialAlpha"), diffuse=findEdit(dialog,"materialTexture0");
			auto apply=dialog->findChild<QPushButton *>("materialGraphApply");
			auto status=dialog->findChild<QLabel *>("materialGraphStatus");
			if (alpha && diffuse && apply && status) {
				alpha->setText("2"); diffuse->setText("textures\\test\\example.dds"); apply->click();
				rejectedBad=status->text().contains("range") && stack.count()==0;
			}
			dialog->reject();
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		if (!rejectedBad || saveGraph()!=initialBytes) { std::cerr << "Material graph accepted invalid values or partially changed NIF\n"; return 1; }
		bool rejectedStale=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto alpha=findEdit(dialog,"materialAlpha"); auto apply=dialog->findChild<QPushButton *>("materialGraphApply");
			auto status=dialog->findChild<QLabel *>("materialGraphStatus");
			if (alpha && apply && status) {
				float old=graph.get<float>(graphShader,"Alpha"); alpha->setText("0.5");
				graph.set<float>(graphShader,"Alpha",0.75f); apply->click();
				rejectedStale=status->text().contains("changed while") && stack.count()==0;
				graph.set<float>(graphShader,"Alpha",old);
			}
			dialog->reject();
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		if (!rejectedStale || saveGraph()!=initialBytes) { std::cerr << "Material graph accepted stale material\n"; return 1; }
		bool appliedGraph=false;
		QTimer::singleShot(0,[&]() {
			auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
			auto alpha=findEdit(dialog,"materialAlpha"), diffuse=findEdit(dialog,"materialTexture0"), scale=findEdit(dialog,"materialUVScaleU");
			auto offsetU=findEdit(dialog,"materialUVOffsetU"),offsetV=findEdit(dialog,"materialUVOffsetV");
			auto apply=dialog->findChild<QPushButton *>("materialGraphApply");
			auto choose=findTool(dialog,"materialTextureChoose0"), clear=findTool(dialog,"materialTextureClear1");
			if (alpha && diffuse && scale && offsetU && offsetV && apply && choose && clear) {
				QTimer::singleShot(0,[&]() {
					auto browser=qobject_cast<FileBrowserWidget *>(QApplication::activeModalWidget()); if (!browser) return;
					auto tree=browser->findChild<QTreeWidget *>();
					if (tree) for (QTreeWidgetItemIterator it(tree); *it; ++it)
						if ((*it)->text(0).endsWith("MountainSlab02_N.dds",Qt::CaseInsensitive)) { browser->checkItemActivated(*it,0); return; }
					browser->reject();
				});
				choose->click(); clear->click(); alpha->setText("0.375"); scale->setText("-2"); offsetU->setText("25000");
				apply->click(); appliedGraph=dialog->result()==QDialog::Accepted;
			}
			if (!appliedGraph) { auto label=dialog->findChild<QLabel *>("materialGraphStatus"); if (label) std::cerr << label->text().toStdString() << '\n'; dialog->reject(); }
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		auto textures=graph.getArray<QString>(graphTextures,"Textures");
		auto scale=graph.get<Vector2>(graphShader,"UV Scale");
		auto offset=graph.get<Vector2>(graphShader,"UV Offset");
		if (!appliedGraph || stack.count()!=1 || graph.get<float>(graphShader,"Alpha")!=0.375f
			|| !textures.value(0).endsWith(QStringLiteral("MountainSlab02_N.dds"),Qt::CaseInsensitive)
			|| !textures.value(1).isEmpty() || scale[0]!=-2 || offset[0]!=25000 || !std::signbit(offset[1])
			|| graph.get<unsigned>(graphShader,"Shader Flags 1")!=originalShaderFlags
			|| graph.get<unsigned>(graphShader,"Shader Flags 2")!=originalShaderFlags2
			|| graph.get<unsigned>(graphShader,"Shader Type")!=originalShaderType) {
			std::cerr << "Material graph apply failed\n"; return 1;
		}
		QByteArray appliedBytes=saveGraph(); stack.undo();
		NifModel baselineGraph; QBuffer baseInput(&initialBytes); baseInput.open(QIODevice::ReadOnly);
		NifModel appliedGraphModel; QBuffer appliedInput(&appliedBytes); appliedInput.open(QIODevice::ReadOnly);
		if (!baselineGraph.load(baseInput) || !appliedGraphModel.load(appliedInput)) return 1;
		for (int i=0;i<baselineGraph.getBlockCount();++i) if (i!=graph.getBlockNumber(graphShader) && i!=graph.getBlockNumber(graphTextures)) {
			QByteArray a,b; QBuffer ba(&a),bb(&b); ba.open(QIODevice::WriteOnly); bb.open(QIODevice::WriteOnly);
			if (!baselineGraph.saveIndex(ba,baselineGraph.getBlockIndex(i)) || !appliedGraphModel.saveIndex(bb,appliedGraphModel.getBlockIndex(i)) || a!=b) {
				std::cerr << "Material graph changed an unrelated block\n"; return 1;
			}
		}
		if (saveGraph()!=initialBytes) { std::cerr << "Material graph undo differs\n"; return 1; }
		stack.redo(); if (saveGraph()!=appliedBytes) { std::cerr << "Material graph redo differs\n"; return 1; }
		NifModel reopenedGraph; QBuffer reopenInput(&appliedBytes); reopenInput.open(QIODevice::ReadOnly);
		if (!reopenedGraph.load(reopenInput)) { std::cerr << "Material graph save/reopen failed\n"; return 1; }
		// Exercise the actual NIF representation and renderer interpolation in an
		// isolated model. This is not yet an animation authoring implementation.
		{
			auto shader = reopenedGraph.getBlockIndex(graph.getBlockNumber(graphShader));
			int next = reopenedGraph.getLink(shader, "Controller");
			for (int axis = 0; axis < 2; ++axis) {
				auto c = reopenedGraph.insertNiBlock("BSLightingShaderPropertyFloatController", -1);
				auto i = reopenedGraph.insertNiBlock("NiFloatInterpolator", -1);
				auto d = reopenedGraph.insertNiBlock("NiFloatData", -1);
				if (!c.isValid() || !i.isValid() || !d.isValid()
					|| !reopenedGraph.setLink(c, "Target", reopenedGraph.getBlockNumber(shader))
					|| !reopenedGraph.setLink(c, "Next Controller", next)
					|| !reopenedGraph.setLink(c, "Interpolator", reopenedGraph.getBlockNumber(i))
					|| !reopenedGraph.setLink(i, "Data", reopenedGraph.getBlockNumber(d))
					|| !reopenedGraph.set<quint32>(c, "Controlled Variable", axis == 0 ? 20 : 22)
					|| !reopenedGraph.set<quint16>(c, "Flags", 8)
					|| !reopenedGraph.set<float>(c, "Frequency", 1.0f)
					|| !reopenedGraph.set<float>(c, "Start Time", 0.0f)
					|| !reopenedGraph.set<float>(c, "Stop Time", 2.0f)) return 1;
				auto group = reopenedGraph.getIndex(d, "Data");
				if (!reopenedGraph.set<quint32>(group, "Num Keys", 2)
					|| !reopenedGraph.set<quint32>(group, "Interpolation", 1)
					|| !reopenedGraph.updateArraySize(group, "Keys")) return 1;
				auto keys = reopenedGraph.getIndex(group, "Keys");
				for (int k = 0; k < 2; ++k) {
					auto key = reopenedGraph.getIndex(keys, k);
					if (!reopenedGraph.set<float>(key, "Time", float(k * 2))
						|| !reopenedGraph.set<float>(key, "Value", axis == 0 ? float(k) : float(-2 * k))) return 1;
				}
				next = reopenedGraph.getBlockNumber(c);
			}
			if (!reopenedGraph.setLink(shader, "Controller", next)) return 1;
			QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
			if (!reopenedGraph.save(output)) return 1;
			NifModel restored; QBuffer input(&bytes); input.open(QIODevice::ReadOnly);
			if (!restored.load(input)) return 1;
			auto restoredShader = restored.getBlockIndex(graph.getBlockNumber(graphShader));
			int link = restored.getLink(restoredShader, "Controller");
			for (int axis = 1; axis >= 0; --axis) {
				auto c = restored.getBlockIndex(link);
				auto i = restored.getBlockIndex(restored.getLink(c, "Interpolator"));
				auto d = restored.getBlockIndex(restored.getLink(i, "Data"));
				TestMaterialController controller(c);
				controller.update(&restored, c);
				float value = 0; int last = 0;
				if (restored.get<quint32>(c, "Controlled Variable") != (axis == 0 ? 20u : 22u)
					|| restored.getLink(c, "Target") != restored.getBlockNumber(restoredShader)
					|| !controller.active || std::abs(controller.ctrlTime(2.5f) - 0.5f) > 0.0001f
					|| !Controller::interpolate(value, d, "Data", controller.ctrlTime(0.5f), last)
					|| std::abs(value - (axis == 0 ? 0.25f : -0.5f)) > 0.0001f) {
					std::cerr << "Material pan controller round-trip or interpolation failed\n"; return 1;
				}
				link = restored.getLink(c, "Next Controller");
			}
		}
		auto controller=graph.insertNiBlock("BSLightingShaderPropertyFloatController",-1);
		auto interp=graph.insertNiBlock("NiFloatInterpolator",-1);
		auto keyData=graph.insertNiBlock("NiFloatData",-1);
		if (!graph.setLink(graphShader,"Controller",graph.getBlockNumber(controller))
			|| !graph.setLink(controller,"Interpolator",graph.getBlockNumber(interp))
			|| !graph.setLink(interp,"Data",graph.getBlockNumber(keyData))
			|| !graph.set<quint32>(controller,"Controlled Variable",12)
			|| !graph.set<float>(controller,"Start Time",0)
			|| !graph.set<float>(controller,"Stop Time",2)) {
			std::cerr << "Could not create animation test controller\n"; return 1;
		}
		auto group=graph.getIndex(keyData,"Data");
		if (!graph.set<quint32>(group,"Num Keys",2) || !graph.set<quint32>(group,"Interpolation",1)
			|| !graph.updateArraySize(group,"Keys")) { std::cerr << "Could not create animation test keys\n"; return 1; }
		auto keys=graph.getIndex(group,"Keys");
		for (int i=0;i<2;++i) {
			auto key=graph.getIndex(keys,i);
			graph.set<float>(key,"Time",float(i*2));
			graph.set<float>(key,"Value",float(i));
		}
		auto pan=graph.insertNiBlock("BSLightingShaderPropertyFloatController",-1);
		auto panInterp=graph.insertNiBlock("NiFloatInterpolator",-1);
		auto panData=graph.insertNiBlock("NiFloatData",-1);
		if (!graph.setLink(controller,"Next Controller",graph.getBlockNumber(pan))
			|| !graph.setLink(pan,"Interpolator",graph.getBlockNumber(panInterp))
			|| !graph.setLink(panInterp,"Data",graph.getBlockNumber(panData))
			|| !graph.set<quint32>(pan,"Controlled Variable",20)) return 1;
		auto panGroup=graph.getIndex(panData,"Data");
		if (!graph.set<quint32>(panGroup,"Num Keys",2) || !graph.set<quint32>(panGroup,"Interpolation",2)
			|| !graph.updateArraySize(panGroup,"Keys")) return 1;
		auto panKeys=graph.getIndex(panGroup,"Keys");
		for (int i=0;i<2;++i) {
			auto key=graph.getIndex(panKeys,i);
			graph.set<float>(key,"Time",float(i)); graph.set<float>(key,"Value",float(i));
			graph.set<float>(key,"Forward",0.25f); graph.set<float>(key,"Backward",0.5f);
		}
		auto tbcController=graph.insertNiBlock("BSLightingShaderPropertyFloatController",-1);
		auto tbcInterp=graph.insertNiBlock("NiFloatInterpolator",-1);
		auto tbcData=graph.insertNiBlock("NiFloatData",-1);
		if (!graph.setLink(pan,"Next Controller",graph.getBlockNumber(tbcController))
			|| !graph.setLink(tbcController,"Interpolator",graph.getBlockNumber(tbcInterp))
			|| !graph.setLink(tbcInterp,"Data",graph.getBlockNumber(tbcData))
			|| !graph.set<quint32>(tbcController,"Controlled Variable",11)) return 1;
		auto tbcGroup=graph.getIndex(tbcData,"Data");
		if (!graph.set<quint32>(tbcGroup,"Num Keys",2) || !graph.set<quint32>(tbcGroup,"Interpolation",3)
			|| !graph.updateArraySize(tbcGroup,"Keys")) return 1;
		auto tbcKeys=graph.getIndex(tbcGroup,"Keys");
		for (int i=0;i<2;++i) {
			auto key=graph.getIndex(tbcKeys,i);
			graph.set<float>(key,"Time",float(i)); graph.set<float>(key,"Value",float(i));
			if (!graph.set<Vector3>(key,"TBC",Vector3(0.2f,0.3f,0.4f))) return 1;
		}
		QByteArray animationBefore=saveGraph(); bool inspected=false;
		QTimer::singleShot(0,[&]() {
			auto graphDialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!graphDialog) return;
			auto view=graphDialog->findChild<QGraphicsView *>("materialGraphView"); QPushButton *inspect=nullptr;
			if (view) for (auto item:view->scene()->items()) if (auto proxy=qgraphicsitem_cast<QGraphicsProxyWidget *>(item))
				if (auto widget=proxy->widget()) if (auto button=widget->findChild<QPushButton *>("materialInspectAnimation")) inspect=button;
			if (!inspect) { graphDialog->reject(); return; }
			QTimer::singleShot(0,[&]() {
				auto animation=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!animation) return;
				auto tree=animation->findChild<QTreeWidget *>("materialAnimationTree"); bool alpha=false,linear=false,time=false,pan=false,quadratic=false,tangent=false,fade=false,tbc=false;
				if (tree) for (QTreeWidgetItemIterator it(tree); *it; ++it) {
					QString line=(*it)->text(0)+QStringLiteral(" ")+(*it)->text(1);
					alpha |= line.contains("Alpha",Qt::CaseInsensitive);
					linear |= line.contains("Linear"); time |= line.contains("Time 2");
					pan |= line.contains("U Offset",Qt::CaseInsensitive) || line.contains("U_Offset",Qt::CaseInsensitive);
					quadratic |= line.contains("Quadratic"); tangent |= line.contains("forward 0.25");
					fade |= line.contains("fade in");
					tbc |= line.contains("tension 0.2") && line.contains("bias 0.3") && line.contains("continuity 0.4");
				}
				inspected=alpha && linear && time && pan && quadratic && tangent && fade && tbc;
				if (!inspected) std::cerr << "Animation fields: alpha=" << alpha << " linear=" << linear << " time=" << time
					<< " pan=" << pan << " quadratic=" << quadratic << " tangent=" << tangent << " fade=" << fade << '\n';
				QApplication::processEvents();
				animation->grab().save(QCoreApplication::applicationDirPath()+"/material-animation-smoke.png");
				animation->reject();
			});
			inspect->click(); graphDialog->reject();
		});
		showMaterialGraph(&graph,graphShape,nullptr);
		if (!inspected || saveGraph()!=animationBefore) { std::cerr << "Animation inspection failed or changed NIF\n"; return 1; }
		graph.undoStack=nullptr;
		std::cout << "Material graph edit/undo/redo and read-only animation inspection passed\n";
	}
	resources.close_archives(); resources.dataPaths.clear();
	bool missingHandled = false;
	QTimer::singleShot( 0, [&]() {
		if ( auto dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() ) ) {
			auto label = dialog->findChild<QLabel *>( "vanillaHeightPreview" );
			auto apply = dialog->findChild<QPushButton *>( "vanillaApply" );
			auto generate = dialog->findChild<QPushButton *>( "vanillaGenerateHeight" );
			missingHandled = label && label->text().contains( "No active height" ) && apply && !apply->isEnabled()
				&& generate && !generate->isEnabled();
			dialog->reject();
		}
	} );
	showVanillaPlusGeometry( &nif, shape, nullptr );
	if ( !missingHandled ) { std::cerr << "Missing texture was not handled safely\n"; return 1; }
	std::cout << "Toolbar, real NIF/DDS preview, missing textures, limits, invalid selection and unchanged Cancel passed\n";
	return 0;
}
