// test_remote_map_capabilities_parser.cpp - pure-logic GetCapabilities XML
// parsing for the #66 real NetworkProbe.
//
// QgsWmsCapabilities / QgsWmtsCapabilities are NOT vendored in this tree, so the
// probe must hand-parse the GetCapabilities XML for the registration-essential
// RemoteMapStructure fields: layerNames, crsList, imageFormat, extent. This is
// the bulk of the probe's logic and the part most likely to harbor bugs, so it
// is a PURE function (no QGIS, no network) tested against fixture XML strings.
//
// WMTS TileMatrixSet parsing (pixelSizeX/Y from ScaleDenominator,
// service-discovered z-range) is DEFERRED to a follow-up: WMTS still registers
// Ready with reachability + the fields parsed here, and z-range comes from
// caller options.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

#include "app/display/remote_map_capabilities_parser.h"

using Catch::Approx;
using sicnu::data::RemoteMapService;
using sicnu::data::RemoteMapStructure;
using sicnu::display::parseWmsCapabilities;
using sicnu::display::parseWmtsCapabilities;

namespace
{

/// A minimal WMS 1.3.0 GetCapabilities document exercising the fields the
/// parser extracts: a root layer with two named children, a CRS list, an
/// EX_GeographicBoundingBox, and the advertised image/png format.
const QByteArray kWms130 = QByteArrayLiteral(
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<WMS_Capabilities version=\"1.3.0\""
  " xmlns=\"http://www.opengis.net/wms\""
  " xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
  "  <Service><Name>WMS</Name><Title>Demo WMS</Title></Service>"
  "  <Capability>"
  "    <Request>"
  "      <GetMap>"
  "        <Format>image/png</Format>"
  "        <Format>image/jpeg</Format>"
  "      </GetMap>"
  "    </Request>"
  "    <Layer>"
  "      <Title>Root</Title>"
  "      <CRS>EPSG:4326</CRS>"
  "      <CRS>EPSG:3857</CRS>"
  "      <EX_GeographicBoundingBox>"
  "        -180.0 -90.0 180.0 90.0"
  "      </EX_GeographicBoundingBox>"
  "      <Layer queryable=\"1\">"
  "        <Name>imagery</Name>"
  "        <Title>Imagery</Title>"
  "      </Layer>"
  "      <Layer>"
  "        <Name>labels</Name>"
  "        <Title>Labels</Title>"
  "      </Layer>"
  "    </Layer>"
  "  </Capability>"
  "</WMS_Capabilities>" );

/// A WMS 1.1.1 document using SRS + LatLonBoundingBox (the older encoding the
/// first slice still handles, since real services ship both).
const QByteArray kWms111 = QByteArrayLiteral(
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<WMT_MS_Capabilities version=\"1.1.1\">"
  "  <Capability>"
  "    <Request>"
  "      <GetMap>"
  "        <Format>image/png</Format>"
  "      </GetMap>"
  "    </Request>"
  "    <Layer>"
  "      <Title>Root</Title>"
  "      <SRS>EPSG:4326</SRS>"
  "      <LatLonBoundingBox minx=\"-10\" miny=\"-5\" maxx=\"10\" maxy=\"5\"/>"
  "      <Layer>"
  "        <Name>basemap</Name>"
  "      </Layer>"
  "    </Layer>"
  "  </Capability>"
  "</WMT_MS_Capabilities>" );

/// A minimal WMTS GetCapabilities document. The first slice extracts
/// reachability + layer name + format only; TileMatrixSet parsing is a
/// documented follow-up.
const QByteArray kWmts = QByteArrayLiteral(
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<Capabilities xmlns=\"http://www.opengis.net/wmts/1.0\""
  " xmlns:ows=\"http://www.opengis.net/ows/1.1\">"
  "  <ows:OperationsMetadata>"
  "    <ows:Operation name=\"GetTile\">"
  "      <ows:DCP>"
  "        <ows:HTTP>"
  "          <ows:Get>"
  "            <ows:AllowedFormats><ows:Format>image/png</ows:Format></ows:AllowedFormats>"
  "          </ows:Get>"
  "        </ows:HTTP>"
  "      </ows:DCP>"
  "    </ows:Operation>"
  "  </ows:OperationsMetadata>"
  "  <Contents>"
  "    <Layer>"
  "      <ows:Title>Imagery</ows:Title>"
  "      <ows:Identifier>imagery</ows:Identifier>"
  "      <Format>image/png</Format>"
  "    </Layer>"
  "  </Contents>"
  "</Capabilities>" );

} // namespace

TEST_CASE( "parseWmsCapabilities extracts the registration-essential fields",
           "[remote_map][capabilities_parser]" )
{
  const RemoteMapStructure structure = parseWmsCapabilities( kWms130 );

  REQUIRE( structure.valid );
  // The named layers are collected (root Layers without a <Name> are skipped).
  REQUIRE( structure.layerNames.size() == 2 );
  CHECK( structure.layerNames.contains( QStringLiteral( "imagery" ) ) );
  CHECK( structure.layerNames.contains( QStringLiteral( "labels" ) ) );
  // CRS list from the root <CRS> children.
  REQUIRE( structure.crsList.size() == 2 );
  CHECK( structure.crsList.contains( QStringLiteral( "EPSG:4326" ) ) );
  CHECK( structure.crsList.contains( QStringLiteral( "EPSG:3857" ) ) );
  // First advertised GetMap format.
  CHECK( structure.imageFormat == QStringLiteral( "image/png" ) );
  // EX_GeographicBoundingBox: west, south, east, north.
  CHECK( structure.extent.valid );
  CHECK( structure.extent.minimumX == Catch::Approx( -180.0 ) );
  CHECK( structure.extent.minimumY == Catch::Approx( -90.0 ) );
  CHECK( structure.extent.maximumX == Catch::Approx( 180.0 ) );
  CHECK( structure.extent.maximumY == Catch::Approx( 90.0 ) );
  // The parser does not set the service family (the probe stamps it from the
  // requesting provider); assert the parser leaves the default.
  CHECK( structure.service == RemoteMapService::Wms );
}

TEST_CASE( "parseWmsCapabilities handles WMS 1.1.1 SRS + LatLonBoundingBox",
           "[remote_map][capabilities_parser]" )
{
  const RemoteMapStructure structure = parseWmsCapabilities( kWms111 );

  REQUIRE( structure.valid );
  REQUIRE( structure.layerNames.size() == 1 );
  CHECK( structure.layerNames.first() == QStringLiteral( "basemap" ) );
  REQUIRE( structure.crsList.size() == 1 );
  CHECK( structure.crsList.first() == QStringLiteral( "EPSG:4326" ) );
  // LatLonBoundingBox attributes, not a text node.
  CHECK( structure.extent.valid );
  CHECK( structure.extent.minimumX == Catch::Approx( -10.0 ) );
  CHECK( structure.extent.minimumY == Catch::Approx( -5.0 ) );
  CHECK( structure.extent.maximumX == Catch::Approx( 10.0 ) );
  CHECK( structure.extent.maximumY == Catch::Approx( 5.0 ) );
}

TEST_CASE( "parseWmsCapabilities marks malformed/empty XML invalid",
           "[remote_map][capabilities_parser]" )
{
  // Empty body: not a capabilities document.
  CHECK_FALSE( parseWmsCapabilities( QByteArrayLiteral( "" ) ).valid );
  // Garbage that is not XML at all.
  CHECK_FALSE( parseWmsCapabilities( QByteArrayLiteral( "not xml" ) ).valid );
  // Valid XML but not a WMS Capabilities root.
  CHECK_FALSE( parseWmsCapabilities(
                 QByteArrayLiteral( "<foo><bar/></foo>" ) ).valid );
}

TEST_CASE( "parseWmtsCapabilities extracts reachability + layer + format "
           "(TileMatrixSet deferred)",
           "[remote_map][capabilities_parser]" )
{
  const RemoteMapStructure structure = parseWmtsCapabilities( kWmts );

  REQUIRE( structure.valid );
  // The WMTS <Layer>/<ows:Identifier> names the layer (single in this fixture).
  REQUIRE( structure.layerNames.size() == 1 );
  CHECK( structure.layerNames.first() == QStringLiteral( "imagery" ) );
  // The advertised tile format.
  CHECK( structure.imageFormat == QStringLiteral( "image/png" ) );
  // TileMatrixSet parsing (pixelSizeX/Y, service z-range) is a documented
  // follow-up to this slice; the parser does not populate them yet.
  CHECK_FALSE( structure.pixelSizeX.has_value() );
  CHECK_FALSE( structure.pixelSizeY.has_value() );
  CHECK( structure.service == RemoteMapService::Wmts );
}

TEST_CASE( "parseWmtsCapabilities marks malformed/empty XML invalid",
           "[remote_map][capabilities_parser]" )
{
  CHECK_FALSE( parseWmtsCapabilities( QByteArrayLiteral( "" ) ).valid );
  CHECK_FALSE( parseWmtsCapabilities(
                 QByteArrayLiteral( "<not-wmts/>" ) ).valid );
}
