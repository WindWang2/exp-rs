// test_generic_cli_manifest.cpp — Processing Toolbox Phase 1 Task 7
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/providers/generic_cli/provider.h>

#include "app/app_paths.h"

static void ensureCustomToolsProvider()
{
    if ( !QCoreApplication::instance() ) {
        static int argc = 1;
        static char name[] = "generic-cli-contracts";
        static char *argv[] = { name, nullptr };
        static auto *application = new QgsApplication( argc, argv, false );
        Q_UNUSED( application );
    }
    auto *registry = QgsApplication::processingRegistry();
    if ( !registry->providerById( "custom_tools" ) )
        registry->addProvider( new GenericCliProvider() );
}

TEST_CASE( "Generic CLI shipped tools load", "[processing][generic_cli]" )
{
    ensureCustomToolsProvider();

    auto *registry = QgsApplication::processingRegistry();
    REQUIRE( registry->providerById( "custom_tools" ) != nullptr );

    const QStringList expectedIds = {
        "custom_tools:gdal2tiles",
        "custom_tools:otb_bilateral_filter",
        "custom_tools:otb_mean_filter",
        "custom_tools:otb_block_matching",
        "custom_tools:otb_disparity_to_dem",
    };

    for ( const QString &id : expectedIds )
    {
        INFO( "Missing generic CLI tool: " << id.toStdString() );
        const QgsProcessingAlgorithm *algo = registry->algorithmById( id );
        REQUIRE( algo != nullptr );
        CHECK_FALSE( algo->displayName().isEmpty() );
    }
}

TEST_CASE( "Generic CLI shipped tools directory exists", "[processing][generic_cli]" )
{
    const QString shipped = AppPaths::resolveDataPath( "data/tools/custom" );
    INFO( "Shipped tools path: " << shipped.toStdString() );
    CHECK( QDir( shipped ).exists() );
    CHECK( QDir( shipped ).entryList( QStringList{ "*.json" }, QDir::Files ).size() >= 5 );
}
TEST_CASE( "Generic CLI duplicates preserve the first definition across refreshes", "[processing][generic_cli][duplicates]" )
{
    ensureCustomToolsProvider();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto writeTool = [&]( const QString &fileName, const QString &title ) {
        QJsonObject tool;
        // This id also exists in the shipped directory scanned afterwards.
        tool["id"] = "gdal2tiles";
        tool["name"] = title;
        tool["command"] = "unused_probe_command";
        tool["args"] = QJsonArray{ "--help" };
        QFile file( directory.filePath( fileName ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( QJsonDocument( tool ).toJson() ) > 0 );
    };
    writeTool( QStringLiteral( "a.json" ), QStringLiteral( "First definition" ) );
    writeTool( QStringLiteral( "b.json" ), QStringLiteral( "Later duplicate" ) );
    GenericCliProvider provider( directory.path() );
    REQUIRE( provider.load() );
    const auto count = provider.algorithms().size();
    for ( int refresh = 0; refresh < 3; ++refresh ) {
        CAPTURE( refresh );
        const auto *algorithm = provider.algorithm( QStringLiteral( "gdal2tiles" ) );
        REQUIRE( algorithm != nullptr );
        CHECK( algorithm->displayName() == QStringLiteral( "First definition" ) );
        int matching = 0;
        for ( const auto *entry : provider.algorithms() )
            if ( entry->name() == QStringLiteral( "gdal2tiles" ) ) ++matching;
        CHECK( matching == 1 );
        CHECK( provider.algorithms().size() == count );
        provider.refreshAlgorithms();
    }
}
