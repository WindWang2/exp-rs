// tests/test_workspace_snapshot.cpp
#include <catch2/catch_test_macros.hpp>
#include "agent/workspace_snapshot.h"
#include <QJsonArray>
#include <QJsonObject>

TEST_CASE( "WorkspaceSnapshot - Pure C++ Serialization & Prompt Formatting", "[agent][workspace_snapshot]" )
{
  SECTION( "Empty WorkspaceSnapshot produces valid empty JSON and prompt string" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot;
    QJsonObject json = snapshot.toJson();

    REQUIRE( json.contains( QStringLiteral( "assets" ) ) );
    REQUIRE( json[QStringLiteral( "assets" )].toArray().isEmpty() );
    REQUIRE_FALSE( json.contains( QStringLiteral( "mapView" ) ) );

    QString prompt = snapshot.toSystemPromptHeader();
    REQUIRE( prompt.contains( QStringLiteral( "[WORKSPACE CONTEXT]" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "Loaded Data Assets: (None)" ) ) );
  }

  SECTION( "Populated WorkspaceSnapshot serializes raster & map view info" )
  {
    sicnu::agent::WorkspaceSnapshot snapshot;

    sicnu::agent::DataAssetInfo asset;
    asset.id = QStringLiteral( "asset-101" );
    asset.displayName = QStringLiteral( "landsat_sample.tif" );
    asset.path = QStringLiteral( "/tmp/landsat_sample.tif" );
    asset.kind = QStringLiteral( "Raster" );
    asset.width = 1024;
    asset.height = 768;
    asset.bandCount = 4;
    asset.crsWkt = QStringLiteral( "EPSG:32648" );

    snapshot.assets.append( asset );

    snapshot.mapView.crsAuthId = QStringLiteral( "EPSG:32648" );
    snapshot.mapView.extentStr = QStringLiteral( "100.0,20.0,105.0,25.0" );
    snapshot.mapView.scale = 50000.0;
    snapshot.mapView.activeLayerName = QStringLiteral( "landsat_sample" );

    QJsonObject json = snapshot.toJson();
    REQUIRE( json[QStringLiteral( "assets" )].toArray().size() == 1 );

    QJsonObject assetObj = json[QStringLiteral( "assets" )].toArray().at( 0 ).toObject();
    REQUIRE( assetObj[QStringLiteral( "id" )].toString() == QStringLiteral( "asset-101" ) );
    REQUIRE( assetObj[QStringLiteral( "width" )].toInt() == 1024 );
    REQUIRE( assetObj[QStringLiteral( "bands" )].toInt() == 4 );

    QJsonObject mapObj = json[QStringLiteral( "mapView" )].toObject();
    REQUIRE( mapObj[QStringLiteral( "crs" )].toString() == QStringLiteral( "EPSG:32648" ) );
    REQUIRE( mapObj[QStringLiteral( "selectedLayer" )].toString() == QStringLiteral( "landsat_sample" ) );

    QString prompt = snapshot.toSystemPromptHeader();
    REQUIRE( prompt.contains( QStringLiteral( "landsat_sample.tif" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "1024x768, 4 bands" ) ) );
    REQUIRE( prompt.contains( QStringLiteral( "Selected Layer: landsat_sample" ) ) );
  }
}
