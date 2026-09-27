// tests/test_plugin_host_allowlist.cpp — review P1-5 regression gate for the
// legacy in-process PluginHost channel. Built in the DEFAULT configuration
// (links sicnu_core only); the broader test_plugin_host needs
// SICNU_EMBED_PYTHON=ON.
#include <catch2/catch_test_macros.hpp>

#include "core/plugin_host.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QLibraryInfo>
#include <QTemporaryDir>

// Review P1-5: the legacy in-process channel used to dlopen + instantiate
// EVERY library in the plugin directory. A directory scan now loads only
// allowlisted first-party modules, and even those must declare the SICNU
// plugin IID before instance() runs any library code.
namespace
{
QString findForeignQtPlugin()
{
    // Any real Qt plugin that is NOT a SicnuPluginInterface (SQL drivers,
    // image formats, platforms) stands in for a dropped-in library.
    const QString root = QLibraryInfo::path( QLibraryInfo::PluginsPath );
    for ( const QString &sub : { QStringLiteral( "sqldrivers" ), QStringLiteral( "imageformats" ),
                                 QStringLiteral( "platforms" ) } )
    {
        const QDir dir( QDir( root ).filePath( sub ) );
        for ( const QString &name : dir.entryList( QDir::Files ) )
        {
            const QString path = dir.absoluteFilePath( name );
            if ( QLibrary::isLibrary( path ) )
                return path;
        }
    }
    return QString();
}
} // namespace

TEST_CASE( "PluginHost module names strip lib prefix and suffixes", "[core][plugin_host][security]" )
{
    CHECK( PluginHost::nativeModuleName( QStringLiteral( "/x/libprocessing_plugin.so" ) )
           == QStringLiteral( "processing_plugin" ) );
    CHECK( PluginHost::nativeModuleName( QStringLiteral( "/x/libfoo.so.1.2" ) ) == QStringLiteral( "foo" ) );
    CHECK( PluginHost::nativeModuleName( QStringLiteral( "C:/x/layer_tree_plugin.dll" ) )
           == QStringLiteral( "layer_tree_plugin" ) );
    CHECK( PluginHost::nativeModuleName( QStringLiteral( "/x/libbar.dylib" ) ) == QStringLiteral( "bar" ) );
}

TEST_CASE( "PluginHost directory scan never loads non-allowlisted native libraries", "[core][plugin_host][security]" )
{
    const QString foreign = findForeignQtPlugin();
    if ( foreign.isEmpty() )
        SKIP( "no Qt plugin available to stand in for a dropped-in library" );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString suffix = QFileInfo( foreign ).fileName().section( QLatin1Char( '.' ), 1 );
    const QString dropped = dir.filePath( QStringLiteral( "libevil_dropin." ) + suffix );
    REQUIRE( QFile::copy( foreign, dropped ) );

    PluginHost host;
    host.setPythonPluginsEnabled( false );
    QStringList errors;
    QObject::connect( &host, &PluginHost::pluginError,
                      [&errors]( const QString &, const QString &error ) { errors << error; } );

    SECTION( "default allowlist is empty: nothing is dlopen'ed" )
    {
        host.loadPlugins( dir.path() );
        CHECK( host.loadedPlugins().isEmpty() );
        CHECK( errors.isEmpty() ); // skipped silently, never instantiated
    }

    SECTION( "an allowlisted name without the SICNU IID is refused before instance()" )
    {
        host.setTrustedNativePlugins( { QStringLiteral( "evil_dropin" ) } );
        host.loadPlugins( dir.path() );
        CHECK( host.loadedPlugins().isEmpty() );
        REQUIRE( errors.size() == 1 );
        CHECK( errors.first().contains( QStringLiteral( "SicnuPluginInterface" ) ) );
    }

    SECTION( "an allowlisted symlink escaping the directory is refused" )
    {
        QTemporaryDir linkDir;
        REQUIRE( linkDir.isValid() );
        const QString link = linkDir.filePath( QStringLiteral( "libevil_dropin." ) + suffix );
        if ( !QFile::link( dropped, link ) )
            SKIP( "symlinks unavailable on this filesystem" );
        host.setTrustedNativePlugins( { QStringLiteral( "evil_dropin" ) } );
        host.loadPlugins( linkDir.path() );
        CHECK( host.loadedPlugins().isEmpty() );
        REQUIRE( errors.size() == 1 );
        CHECK( errors.first().contains( QStringLiteral( "escapes" ) ) );
    }
}
