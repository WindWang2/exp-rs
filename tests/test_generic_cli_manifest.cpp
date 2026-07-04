// test_generic_cli_manifest.cpp — Processing Toolbox Phase 1 Task 7
#include <catch2/catch_test_macros.hpp>

#include <QDir>

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/providers/generic_cli/provider.h>

#include "app/app_paths.h"

static void ensureCustomToolsProvider()
{
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
        "custom_tools:otb_median_filter",
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