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

#include "app/app_paths.h"

static void ensureProvidersRegistered()
{
    auto *registry = QgsApplication::processingRegistry();
    if ( !registry->providerById( "gdal_tools" ) )
        registry->addProvider( new GdalToolsProvider() );
    if ( !registry->providerById( "otb_tools" ) )
        registry->addProvider( new OtbToolsProvider() );
    if ( !registry->providerById( "qgis_algorithms" ) )
        registry->addProvider( new QgisAlgorithmsProvider() );
    if ( !registry->providerById( "custom_tools" ) )
        registry->addProvider( new GenericCliProvider() );
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
        const int targetPct = cat.value( "target_pct" ).toInt( 80 );
        const QJsonArray required = cat.value( "required" ).toArray();

        int found = 0;
        for ( const QJsonValue &v : required )
        {
            if ( registry->algorithmById( v.toString() ) )
                ++found;
        }

        const int pct = required.isEmpty() ? 100 : ( found * 100 / required.size() );
        INFO( catName.toStdString() << " coverage: " << found << "/" << required.size() );
        CHECK( pct >= targetPct );
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