// test_remote_map_uri.cpp - per-service QGIS URI builder (#64)
//
// buildRemoteMapUri is a pure function (no QGIS, no auth) so it can be unit
// tested against the exact URI string each service family must produce. This
// covers the WMS/WMTS/TMS arms that the display-integration test (offscreen,
// network-bound) cannot reach, plus the XYZ/TMS z-range encoding.
#include <catch2/catch_test_macros.hpp>

#include <QMap>
#include <QString>

#include "app/display/remote_map_uri.h"

using sicnu::display::buildRemoteMapUri;

TEST_CASE( "XYZ URI is the tile template with the declared z-range appended",
           "[remote_map][uri]" )
{
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "zMin" ), QStringLiteral( "0" ) );
  options.insert( QStringLiteral( "zMax" ), QStringLiteral( "12" ) );
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "xyz" ),
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ), options );
  CHECK( uri == QStringLiteral(
           "https://tiles.example.com/{z}/{x}/{y}.png?zmin=0&zmax=12" ) );
}

TEST_CASE( "TMS URI encodes the y-flipped template with z-range",
           "[remote_map][uri]" )
{
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "zMin" ), QStringLiteral( "1" ) );
  options.insert( QStringLiteral( "zMax" ), QStringLiteral( "14" ) );
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "tms" ),
    QStringLiteral( "https://tms.example.com/{z}/{x}/{y}.jpg" ), options );
  CHECK( uri == QStringLiteral(
           "https://tms.example.com/{z}/{x}/{y}.jpg?zmin=1&zmax=14" ) );
}

TEST_CASE( "An XYZ template without a z-range passes through unchanged",
           "[remote_map][uri]" )
{
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "xyz" ),
    QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ), {} );
  CHECK( uri == QStringLiteral( "https://tiles.example.com/{z}/{x}/{y}.png" ) );
}

TEST_CASE( "WMS URI is a layers/crs/format/url query string",
           "[remote_map][uri]" )
{
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "layers" ), QStringLiteral( "imagery,labels" ) );
  options.insert( QStringLiteral( "crs" ), QStringLiteral( "EPSG:4326" ) );
  options.insert( QStringLiteral( "format" ), QStringLiteral( "image/png" ) );
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "wms" ), QStringLiteral( "https://wms.example.com" ), options );
  CHECK( uri == QStringLiteral(
           "layers=imagery,labels&crs=EPSG:4326&format=image/png&url=https://wms.example.com" ) );
}

TEST_CASE( "WMTS URI carries the tile matrix set",
           "[remote_map][uri]" )
{
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "layer" ), QStringLiteral( "Imagery" ) );
  options.insert( QStringLiteral( "tileMatrixSet" ), QStringLiteral( "EPSG:3857" ) );
  options.insert( QStringLiteral( "format" ), QStringLiteral( "image/png" ) );
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "wmts" ), QStringLiteral( "https://wmts.example.com" ), options );
  CHECK( uri == QStringLiteral(
           "layers=Imagery&format=image/png&tileMatrixSet=EPSG:3857&url=https://wmts.example.com" ) );
}

TEST_CASE( "A WMS descriptor with no layers emits no layers= parameter",
           "[remote_map][uri]" )
{
  QMap<QString, QString> options;
  options.insert( QStringLiteral( "crs" ), QStringLiteral( "EPSG:3857" ) );
  const QString uri = buildRemoteMapUri(
    QStringLiteral( "wms" ), QStringLiteral( "https://wms.example.com" ), options );
  CHECK( uri == QStringLiteral( "crs=EPSG:3857&url=https://wms.example.com" ) );
}
