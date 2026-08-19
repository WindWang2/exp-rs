#include <catch2/catch_test_macros.hpp>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/providers/gdal_tools/provider.h>
#include <processing/providers/otb_tools/provider.h>
#include <processing/providers/qgis_algorithms/provider.h>
#include <processing/providers/generic_cli/provider.h>
#include <processing/tools/cli_tool_discovery.h>

#include "app/app_paths.h"
#include "processing/framework/algorithm_engine.h"

static void ensureProvidersRegistered()
{
    sicnu::AlgorithmEngine::instance().initialize();
}

static QJsonObject loadManifest()
{
    const QString path = AppPaths::resolveDataPath( "data/processing/toolbox_manifest.json" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( file.readAll() ).object();
}

TEST_CASE( "Toolbox manifest category coverage", "[processing][coverage]" )
{
    ensureProvidersRegistered();
    const QJsonObject manifest = loadManifest();
    const QJsonObject categories = manifest.value( "categories" ).toObject();
    auto *registry = QgsApplication::processingRegistry();

    for ( auto it = categories.begin(); it != categories.end(); ++it )
    {
        const QString catName = it.key();
        const QJsonObject cat = it.value().toObject();
        const int targetPct = cat.value( "target_pct" ).toInt( 100 );
        const QJsonArray required = cat.value( "required" ).toArray();

        int found = 0;
        for ( const QJsonValue &v : required )
        {
            if ( registry->algorithmById( v.toString() ) )
                ++found;
        }

        const int pct = required.isEmpty() ? 100 : ( found * 100 / required.size() );
        INFO( catName.toStdString() << " coverage: " << found << "/" << required.size() );
        // S3: tighten to 100% — any single missing tool fails the gate.
        CHECK( pct >= targetPct );
        CHECK( found == required.size() );
        CHECK( pct == 100 );
    }
}

TEST_CASE( "Toolbox manifest entries are all registered (bidirectional)", "[processing][coverage][manifest]" )
{
    ensureProvidersRegistered();
    const QJsonObject manifest = loadManifest();
    auto *registry = QgsApplication::processingRegistry();

    // Collect every id the manifest claims must exist.
    QSet<QString> manifestIds;
    const QJsonObject categories = manifest.value( "categories" ).toObject();
    for ( auto it = categories.begin(); it != categories.end(); ++it )
    {
        const QJsonArray required = it.value().toObject().value( "required" ).toArray();
        for ( const QJsonValue &v : required )
            manifestIds.insert( v.toString() );
    }
    const QJsonArray handcrafted = manifest.value( "handcrafted_required" ).toArray();
    for ( const QJsonValue &v : handcrafted )
        manifestIds.insert( v.toString() );

    for ( const QString &id : manifestIds )
    {
        INFO( "Manifest id not registered: " << id.toStdString() );
        CHECK( registry->algorithmById( id ) != nullptr );
    }
}

TEST_CASE( "Discovered GDAL and OTB CLI tools registered", "[processing][coverage][discovery]" )
{
    ensureProvidersRegistered();
    auto *registry = QgsApplication::processingRegistry();

    const QStringList otbApps = CliToolDiscovery::discoverOtbApplicationNames();
    INFO( "Discovered OTB apps: " << otbApps.size() );
    if ( otbApps.isEmpty() )
    {
        WARN( "No OTB CLI applications found in system path or OTB_BIN_DIR" );
    }
    else
    {
        for ( const QString &appName : otbApps )
        {
            const QString algoId = QStringLiteral( "otb_tools:" ) + CliToolDiscovery::otbAlgorithmId( appName );
            INFO( "Missing discovered OTB: " << algoId.toStdString() );
            CHECK( registry->algorithmById( algoId ) != nullptr );
        }
    }

    const QStringList gdalTools = CliToolDiscovery::discoverGdalToolNames();
    INFO( "Discovered GDAL tools: " << gdalTools.size() );
    if ( gdalTools.isEmpty() )
    {
        WARN( "No GDAL CLI tools found in system path" );
    }
    else
    {
        for ( const QString &toolName : gdalTools )
        {
            const QString algoId = QStringLiteral( "gdal_tools:" ) + CliToolDiscovery::gdalAlgorithmId( toolName );
            INFO( "Missing discovered GDAL: " << algoId.toStdString() );
            CHECK( registry->algorithmById( algoId ) != nullptr );
        }
    }
}

TEST_CASE( "Toolbox handcrafted algorithms registered", "[processing][coverage]" )
{
    ensureProvidersRegistered();
    const QJsonObject manifest = loadManifest();
    const QJsonArray handcrafted = manifest.value( "handcrafted_required" ).toArray();
    auto *registry = QgsApplication::processingRegistry();

    for ( const QJsonValue &v : handcrafted )
    {
        const QString id = v.toString();
        INFO( "Missing handcrafted: " << id.toStdString() );
        CHECK( registry->algorithmById( id ) != nullptr );
    }
}