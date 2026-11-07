#include <QCoreApplication>
#include <QSettings>
#include <QTextStream>

int main( int argc, char ** argv )
{
	QCoreApplication app( argc, argv );
	app.setOrganizationName( "NifTools" );
	app.setApplicationName( "NifSkope 2.0" );
	QSettings settings;
	QTextStream out( stdout );
	const auto paths = settings.value( "Game Paths" ).toMap();
	const auto folders = settings.value( "Game Folders" ).toMap();
	const auto statuses = settings.value( "Game Status" ).toMap();
	const QString key = "Skyrim SE";
	out << "Settings location: " << settings.fileName() << '\n';
	out << "Manager version: " << settings.value( "Game Manager Version" ).toInt() << '\n';
	out << "Skyrim SE path: " << paths.value( key ).toString() << '\n';
	out << "Skyrim SE enabled: " << statuses.value( key, true ).toBool() << '\n';
	out << "Skyrim SE resource key exists: " << folders.contains( key ) << '\n';
	const auto values = folders.value( key ).toStringList();
	out << "Skyrim SE resource count: " << values.size() << '\n';
	for ( const auto & path : values ) out << path << '\n';
	return settings.status() == QSettings::NoError ? 0 : 1;
}
