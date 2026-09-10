// tests/test_platform8.cpp
//
// Cartography Platform 8.0 — closes the verified 7.0 gaps:
//   * raster NoData wired into the QGIS renderer (style:apply) — the
//     declared surface was validated-only in 7.0;
//   * locator connector graphics (inset ↔ referenced frame);
//   * page-aware relative-constraint evidence;
//   * typography break policies (hanging punctuation);
//   * NoData legend QA + repair;
//   * MapSpec v5 output declarations + deeper binding validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/chart_registry.h"
#include "agent/cartography/composition.h"
#include "agent/cartography/quality.h"
#include "agent/cartography/style_compiler.h"
#include "agent/cartography/style_spec.h"
#include "agent/cartography/typography.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <QDir>
#include <QTemporaryDir>

#include <qgscoordinatereferencesystem.h>
#include <qgslayoutitempolyline.h>
#include <qgslayoutitemshape.h>
#include <qgslayoutitemmap.h>
#include <qgsprintlayout.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsrectangle.h>

#include "agent/layout_tools/layout_service.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value makeStyleSpec( const std::string &id )
{
  Json::Value style( Json::objectValue );
  style["schema_version"] = "1.0";
  style["kind"] = "style_spec";
  style["id"] = id;
  style["version"] = 1;
  style["applies_to"] = "raster";
  return style;
}

void requireNoProblems( const std::vector<std::string> &problems )
{
  for ( const auto &problem : problems )
    FAIL( problem );
}

/// Writes a tiny 8x8 Float32 GeoTIFF (values 0..62, the two last cells
/// -9999) through QgsRasterFileWriter and returns the path. Bounded
/// (64 cells) and removed with the temp dir.
QString writeTinyRaster( const QString &dir )
{
  const QString path = dir + "/p8-nodata-fixture.tif";
  QgsRasterFileWriter writer( path );
  QgsRasterDataProvider *provider = writer.createOneBandRaster(
    Qgis::DataType::Float32, 8, 8, QgsRectangle( 0, 0, 8, 8 ),
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
  if ( !provider )
    return QString();
  QgsRasterBlock block( Qgis::DataType::Float32, 8, 8 );
  for ( int row = 0; row < 8; ++row )
    for ( int col = 0; col < 8; ++col )
      block.setValue( row, col, row * 8.0 + col );
  block.setValue( 7, 6, -9999.0 );
  block.setValue( 7, 7, -9999.0 );
  const bool written = provider->writeBlock( &block, 1, 0, 0 );
  delete provider;
  return written ? path : QString();
}

} // namespace

TEST_CASE( "P8 style: nodata value reaches the provider as a user nodata range",
           "[platform8][style][nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = writeTinyRaster( dir.path() );
  REQUIRE_FALSE( path.isEmpty() );

  QgsRasterLayer raster( path, QStringLiteral( "p8-nodata" ) );
  REQUIRE( raster.isValid() );

  Json::Value style = makeStyleSpec( "p8-nodata-transparent" );
  style["raster"]["renderertype"] = "singleband_gray";
  style["raster"]["nodata"]["value"] = -9999.0;
  style["raster"]["nodata"]["transparent"] = true;
  style["raster"]["nodata"]["label"] = "NoData";
  requireNoProblems( validateStyleSpec( style ) );

  QString error;
  REQUIRE( applyStyleSpecToLayer( &raster, style, &error ) );
  REQUIRE( error.isEmpty() );

  // The declared value is now a provider-level user nodata range — the
  // exact mechanism QGIS uses to render those pixels transparent.
  const QgsRasterRangeList ranges = raster.dataProvider()->userNoDataValues( 1 );
  REQUIRE( ranges.size() == 1 );
  REQUIRE( ranges.first().contains( -9999.0 ) );

  // Transparent declaration keeps the renderer's default (invalid =
  // transparent) nodata shading.
  const QgsRasterRenderer *renderer = raster.renderer();
  REQUIRE( renderer );
  REQUIRE_FALSE( renderer->nodataColor().isValid() );

  // Re-apply stays idempotent (one range, not accumulated).
  REQUIRE( applyStyleSpecToLayer( &raster, style, &error ) );
  REQUIRE( raster.dataProvider()->userNoDataValues( 1 ).size() == 1 );
}

TEST_CASE( "P8 style: non-transparent nodata shades pixels through the renderer",
           "[platform8][style][nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = writeTinyRaster( dir.path() );
  REQUIRE_FALSE( path.isEmpty() );

  QgsRasterLayer raster( path, QStringLiteral( "p8-nodata-shaded" ) );
  REQUIRE( raster.isValid() );

  Json::Value style = makeStyleSpec( "p8-nodata-shaded" );
  style["raster"]["renderertype"] = "singleband_pseudocolor";
  style["raster"]["classification"]["mode"] = "discrete";
  Json::Value classes( Json::arrayValue );
  Json::Value entry( Json::objectValue );
  entry["min"] = 0.0;
  entry["max"] = 100.0;
  entry["color"] = "#2c7fb8";
  entry["label"] = "value";
  classes.append( entry );
  style["raster"]["classification"]["classes"] = classes;
  style["raster"]["nodata"]["value"] = -9999.0;
  style["raster"]["nodata"]["transparent"] = false;
  style["raster"]["nodata"]["color"] = "#FFFFFF";
  style["raster"]["nodata"]["label"] = "NoData";
  requireNoProblems( validateStyleSpec( style ) );

  QString error;
  REQUIRE( applyStyleSpecToLayer( &raster, style, &error ) );

  const QgsRasterRenderer *renderer = raster.renderer();
  REQUIRE( renderer );
  REQUIRE( renderer->nodataColor().isValid() );
  REQUIRE( renderer->nodataColor() == QColor( Qt::white ) );
  REQUIRE( raster.dataProvider()->userNoDataValues( 1 ).size() == 1 );
}

TEST_CASE( "P8 style: nodata default shading is black and validation rejects bad shapes",
           "[platform8][style][nodata]" )
{
  // buildRasterRenderer (knowledge path) carries the shading too.
  Json::Value block( Json::objectValue );
  block["renderertype"] = "singleband_gray";
  Json::Value nodata( Json::objectValue );
  nodata["transparent"] = false;
  block["nodata"] = nodata;
  QgsRasterRenderer *renderer = buildRasterRenderer( block, 1, nullptr );
  REQUIRE( renderer );
  REQUIRE( renderer->nodataColor().isValid() );
  REQUIRE( renderer->nodataColor() == QColor( Qt::black ) );
  delete renderer;

  // Validation: nodata.color must be a string.
  Json::Value style = makeStyleSpec( "p8-nodata-bad-color" );
  style["raster"]["renderertype"] = "singleband_gray";
  style["raster"]["nodata"]["color"] = 7;
  const std::vector<std::string> problems = validateStyleSpec( style );
  bool saw = false;
  for ( const auto &problem : problems )
    saw = saw || problem.find( "nodata.color must be a string" ) != std::string::npos;
  REQUIRE( saw );

  // A colorless transparent declaration stays valid (7.0 documents).
  Json::Value good = makeStyleSpec( "p8-nodata-minimal" );
  good["raster"]["renderertype"] = "singleband_gray";
  good["raster"]["nodata"]["value"] = 255.0;
  requireNoProblems( validateStyleSpec( good ) );
}

namespace {

/// A minimal valid spec with one map frame carrying extent [0,0,1,1] on
/// rect [10,10,120,120] (A4 landscape page).
Json::Value makeConnectorSpec( const std::string &layoutName )
{
  Json::Value spec = makeMapSpec( layoutName, Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 10.0 );
  rect.append( 10.0 );
  rect.append( 120.0 );
  rect.append( 120.0 );
  frame["rect_mm"] = rect;
  Json::Value extent( Json::arrayValue );
  extent.append( 0.0 );
  extent.append( 0.0 );
  extent.append( 1.0 );
  extent.append( 1.0 );
  frame["extent"] = extent;
  appendMapSpecItem( spec, "map_frames", frame );
  return spec;
}

} // namespace

TEST_CASE( "P8 mapspec: locator connector compiles to a QGIS polyline",
           "[platform8][mapspec][locator]" )
{
  Json::Value spec = makeConnectorSpec( "p8-locator-connector" );
  Json::Value inset( Json::objectValue );
  inset["id"] = "inset-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 180.0 );
  rect.append( 20.0 );
  rect.append( 60.0 );
  rect.append( 50.0 );
  inset["rect_mm"] = rect;
  // Inset shows the inner quarter of the frame extent; its center (0.5, 0.5)
  // projects to the target frame center (70, 70) page mm.
  Json::Value extent( Json::arrayValue );
  extent.append( 0.25 );
  extent.append( 0.25 );
  extent.append( 0.75 );
  extent.append( 0.75 );
  inset["extent"] = extent;
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-1";
  Json::Value connector( Json::objectValue );
  connector["style"] = "dash";
  connector["stroke_mm"] = 0.6;
  locator["connector"] = connector;
  inset["locator"] = locator;
  const std::string insetId = appendMapSpecItem( spec, "inset_maps", inset );
  REQUIRE( validateMapSpec( spec ).empty() );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto &service = sicnu::agent::layout_tools::LayoutService::instance();
  auto *line = qobject_cast<QgsLayoutItemPolyline *>(
    service.findItem( layout, QString::fromStdString( insetId + "-locator-connector" ) ) );
  REQUIRE( line != nullptr );
  REQUIRE( line->nodes().size() == 2 );
  // The item covers the connector's bounding box: start is where the
  // center→anchor ray leaves the inset rect at its left edge
  // (scale = 30/140): scene (210 - 30, 45 + 25*30/140); end is the inset
  // extent center projected into the target frame: scene (70, 70).
  const QgsLayoutPoint pos = line->pagePositionWithUnits();
  CHECK( pos.x() == Catch::Approx( 70.0 ).margin( 0.01 ) );
  CHECK( pos.y() == Catch::Approx( 45.0 + 25.0 * 30.0 / 140.0 ).margin( 0.01 ) );
  // Nodes are item-local: start at the box top-right, end at the box
  // bottom-left (the connector runs up-left to the projected anchor).
  const QPointF start = line->nodes().front();
  CHECK( start.x() == Catch::Approx( 110.0 ).margin( 0.01 ) );
  CHECK( start.y() == Catch::Approx( 0.0 ).margin( 0.01 ) );
  const QPointF end = line->nodes().back();
  CHECK( end.x() == Catch::Approx( 0.0 ).margin( 0.01 ) );
  CHECK( end.y() == Catch::Approx( 70.0 - ( 45.0 + 25.0 * 30.0 / 140.0 ) ).margin( 0.01 ) );
}

TEST_CASE( "P8 mapspec: connector without resolvable extents anchors at the frame center",
           "[platform8][mapspec][locator]" )
{
  Json::Value spec = makeConnectorSpec( "p8-connector-center" );
  Json::Value inset( Json::objectValue );
  inset["id"] = "inset-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 180.0 );
  rect.append( 20.0 );
  rect.append( 60.0 );
  rect.append( 50.0 );
  inset["rect_mm"] = rect;
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-1";
  locator["connector"] = Json::Value( Json::objectValue );
  inset["locator"] = locator;
  const std::string insetId = appendMapSpecItem( spec, "inset_maps", inset );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto *line = qobject_cast<QgsLayoutItemPolyline *>(
    sicnu::agent::layout_tools::LayoutService::instance().findItem(
      layout, QString::fromStdString( insetId + "-locator-connector" ) ) );
  REQUIRE( line != nullptr );
  // No extents anywhere: the anchor is the frame center (70, 70) — the same
  // connector geometry as the explicit-extent case above.
  const QgsLayoutPoint pos = line->pagePositionWithUnits();
  CHECK( pos.x() == Catch::Approx( 70.0 ).margin( 0.01 ) );
  CHECK( pos.y() == Catch::Approx( 45.0 + 25.0 * 30.0 / 140.0 ).margin( 0.01 ) );
}

TEST_CASE( "P8 mapspec: connector projects north-up extents without vertical mirroring",
           "[platform8][mapspec][locator]" )
{
  // Asymmetric case (adversarial review P1-1): the inset shows the NORTHERN
  // quarter of the frame extent. QGIS renders ymin at the frame BOTTOM, so
  // the anchor must land in the upper part of the target frame — page-y
  // = rectY + (ymax - mapY)/H * rectH = 10 + (1 - 0.875) * 120 = 25.
  Json::Value spec = makeConnectorSpec( "p8-connector-north" );
  Json::Value inset( Json::objectValue );
  inset["id"] = "inset-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 180.0 );
  rect.append( 20.0 );
  rect.append( 60.0 );
  rect.append( 50.0 );
  inset["rect_mm"] = rect;
  // Extent center at map y = 0.75+0.25*... : center (0.5, 0.875).
  Json::Value extent( Json::arrayValue );
  extent.append( 0.25 );
  extent.append( 0.75 );
  extent.append( 0.75 );
  extent.append( 1.0 );
  inset["extent"] = extent;
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-1";
  locator["connector"] = Json::Value( Json::objectValue );
  inset["locator"] = locator;
  appendMapSpecItem( spec, "inset_maps", inset );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto *line = qobject_cast<QgsLayoutItemPolyline *>(
    sicnu::agent::layout_tools::LayoutService::instance().findItem(
      layout, QStringLiteral( "inset_map-1-locator-connector" ) ) );
  REQUIRE( line != nullptr );
  const double anchorX = 70.0;
  const double anchorY = 10.0 + ( 1.0 - 0.875 ) * 120.0; // 25 — upper frame
  const double dx = anchorX - 210.0;
  const double dy = anchorY - 45.0;
  const double scale = std::min( 30.0 / 140.0, 25.0 / 5.0 );
  const double startY = 45.0 + dy * scale;
  const QgsLayoutPoint pos = line->pagePositionWithUnits();
  // Item origin = the connector bounding box top-left: x at the anchor's
  // clamped column, y at the smaller of start/end page y.
  CHECK( pos.x() == Catch::Approx( 70.0 ).margin( 0.01 ) );
  CHECK( pos.y() == Catch::Approx( std::min( startY, anchorY ) ).margin( 0.01 ) );
}

TEST_CASE( "P8 preflight: malformed legend nodata keeps firing and validates",
           "[platform8][preflight][nodata][validation]" )
{
  StyleRegistry &registry = StyleRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();

  // Structural validation: legends[].nodata must be {label?, color?}.
  Json::Value spec = makeConnectorSpec( "p8-nodata-malformed" );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 200.0 );
  rect.append( 40.0 );
  rect.append( 60.0 );
  rect.append( 60.0 );
  legend["rect_mm"] = rect;
  legend["nodata"] = "NoData";
  appendMapSpecItem( spec, "legends", legend );
  bool sawShape = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    sawShape = sawShape || problem.find( "nodata must be an object" ) != std::string::npos;
  REQUIRE( sawShape );

  // QA: a well-formed declaration suppresses the rule; a malformed one does
  // not (the compiler renders nothing for it).
  REQUIRE_FALSE( validateMapSpec( spec ).empty() );
  legend["nodata"] = Json::Value( Json::objectValue );
  legend["nodata"]["label"] = "NoData";
  spec["legends"].clear();
  appendMapSpecItem( spec, "legends", legend );
  REQUIRE( validateMapSpec( spec ).empty() );
}

TEST_CASE( "P8 mapspec: locator connector surface is validated",
           "[platform8][mapspec][validation]" )
{
  Json::Value spec = makeConnectorSpec( "p8-connector-invalid" );
  Json::Value inset( Json::objectValue );
  inset["id"] = "inset-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 180.0 );
  rect.append( 20.0 );
  rect.append( 60.0 );
  rect.append( 50.0 );
  inset["rect_mm"] = rect;
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-1";
  Json::Value connector( Json::objectValue );
  connector["style"] = "zigzag";
  connector["stroke_mm"] = 12.0;
  connector["color"] = 3;
  locator["connector"] = connector;
  inset["locator"] = locator;
  appendMapSpecItem( spec, "inset_maps", inset );

  const std::vector<std::string> problems = validateMapSpec( spec );
  int saw = 0;
  for ( const auto &problem : problems )
  {
    saw += problem.find( "connector.style must be solid|dash" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "connector.stroke_mm must be positive" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "connector.color must be a string" ) != std::string::npos ? 1 : 0;
  }
  REQUIRE( saw == 3 );
}

TEST_CASE( "P8 mapspec: v5 output declarations validate and upgrade stamps v5",
           "[platform8][mapspec][output][migrate]" )
{
  // Valid declaration.
  Json::Value spec = makeConnectorSpec( "p8-output-valid" );
  spec["output"]["formats"].append( "png" );
  spec["output"]["formats"].append( "pdf" );
  spec["output"]["dpi"] = 300;
  spec["output"]["dir"] = "exports";
  REQUIRE( validateMapSpec( spec ).empty() );

  // Malformed declarations each produce one targeted problem.
  Json::Value bad = makeConnectorSpec( "p8-output-bad" );
  bad["output"]["formats"].append( "svg" );
  bad["output"]["dpi"] = 30;
  bad["output"]["dir"] = "";
  const std::vector<std::string> problems = validateMapSpec( bad );
  int saw = 0;
  for ( const auto &problem : problems )
  {
    saw += problem.find( "output.formats entries must be" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "output.dpi must be a number in [72, 1200]" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "output.dir must be a non-empty string" ) != std::string::npos ? 1 : 0;
  }
  REQUIRE( saw == 3 );

  Json::Value emptyFormats = makeConnectorSpec( "p8-output-empty" );
  emptyFormats["output"]["formats"] = Json::Value( Json::arrayValue );
  bool sawEmpty = false;
  for ( const auto &problem : validateMapSpec( emptyFormats ) )
    sawEmpty = sawEmpty || problem.find( "output.formats must be a non-empty array" ) !=
                               std::string::npos;
  REQUIRE( sawEmpty );

  // v4 documents upgrade cleanly to v5 (additive surface, nothing normalized).
  Json::Value legacy = makeConnectorSpec( "p8-output-legacy" );
  legacy["spec_version"] = 4;
  const Json::Value upgraded = upgradeMapSpec( legacy );
  CHECK( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  CHECK( validateMapSpec( upgraded ).empty() );
}

TEST_CASE( "P8 mapspec: binding surface is shape-validated", "[platform8][mapspec][binding]" )
{
  auto bindingSpec = []( const std::string &layoutName ) {
    Json::Value spec = makeConnectorSpec( layoutName );
    Json::Value title( Json::objectValue );
    title["id"] = "title-1";
    title["text"] = "t";
    Json::Value rect( Json::arrayValue );
    rect.append( 10.0 );
    rect.append( 10.0 );
    rect.append( 60.0 );
    rect.append( 10.0 );
    title["rect_mm"] = rect;
    appendMapSpecItem( spec, "titles", title );
    return spec;
  };

  Json::Value good = bindingSpec( "p8-binding-good" );
  good["titles"][0]["binding"]["mode"] = "inline";
  good["titles"][0]["binding"]["layer"] = "lc-2015";
  Json::Value labels( Json::arrayValue );
  labels.append( "a" );
  labels.append( "b" );
  Json::Value row0( Json::arrayValue );
  row0.append( 1 );
  row0.append( 2 );
  Json::Value row1( Json::arrayValue );
  row1.append( 3 );
  row1.append( 4 );
  Json::Value rows( Json::arrayValue );
  rows.append( row0 );
  rows.append( row1 );
  good["titles"][0]["binding"]["matrix"]["labels"] = labels;
  good["titles"][0]["binding"]["matrix"]["rows"] = rows;
  REQUIRE( validateMapSpec( good ).empty() );

  Json::Value bad = bindingSpec( "p8-binding-bad" );
  bad["titles"][0]["binding"]["mode"] = 7;
  bad["titles"][0]["binding"]["layer"] = 42;
  bad["titles"][0]["binding"]["params"] = "nope";
  bad["titles"][0]["binding"]["data"] = "nope";
  bad["titles"][0]["binding"]["matrix"]["labels"] = labels;
  Json::Value shortRow( Json::arrayValue );
  shortRow.append( 1 );
  Json::Value shortRows( Json::arrayValue );
  shortRows.append( shortRow );
  bad["titles"][0]["binding"]["matrix"]["rows"] = shortRows;
  int saw = 0;
  for ( const auto &problem : validateMapSpec( bad ) )
  {
    saw += problem.find( "binding.mode must be a non-empty string" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "binding.layer must be a string" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "binding.params must be an object" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "binding.data must be an array" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "binding.matrix must be square (rows == labels)" ) != std::string::npos
             ? 1
             : 0;
  }
  REQUIRE( saw == 5 );

  // Oversized inline data hits the bounded budget.
  Json::Value big = bindingSpec( "p8-binding-big" );
  for ( int i = 0; i < 300; ++i )
    big["titles"][0]["binding"]["data"].append( i );
  bool sawBudget = false;
  for ( const auto &problem : validateMapSpec( big ) )
    sawBudget = sawBudget ||
                problem.find( "binding.data exceeds the 256 entry budget" ) != std::string::npos;
  REQUIRE( sawBudget );
}

TEST_CASE( "P8 solver: page-aware pins are refused with page_overflow evidence",
           "[platform8][solver][page]" )
{
  auto buildSpec = []( bool tallSecondPage ) {
    Json::Value spec = makeMapSpec( "p8-page-pin", Json::Value() );
    Json::Value title( Json::objectValue );
    title["id"] = "title-1";
    title["text"] = "t";
    Json::Value titleRect( Json::arrayValue );
    titleRect.append( 10.0 );
    titleRect.append( 198.0 );
    titleRect.append( 60.0 );
    titleRect.append( 6.0 );
    title["rect_mm"] = titleRect;
    appendMapSpecItem( spec, "titles", title );
    Json::Value note( Json::objectValue );
    note["id"] = "note-1";
    note["text"] = "n";
    note["page"] = 1;
    Json::Value noteRect( Json::arrayValue );
    noteRect.append( 10.0 );
    noteRect.append( 92.0 );
    noteRect.append( 60.0 );
    noteRect.append( 6.0 );
    note["rect_mm"] = noteRect;
    const std::string noteId = appendMapSpecItem( spec, "source_notes", note );
    Json::Value keep( Json::objectValue );
    keep["id"] = "c-keep";
    keep["kind"] = "keep_with";
    Json::Value items( Json::arrayValue );
    items.append( "title-1" );
    items.append( noteId );
    keep["items"] = items;
    keep["gap_mm"] = 4.0;
    spec["constraints"].append( keep );
    if ( tallSecondPage )
    {
      Json::Value page( Json::objectValue );
      page["width_mm"] = 297.0;
      page["height_mm"] = 300.0;
      spec["pages"].append( page );
    }
    return spec;
  };

  // note-1 is declared on page 1, which inherits the base page height
  // (210 mm): the keep_with pin ends at y=208+6=214 > 210 and is refused
  // with page_overflow evidence instead of silently writing off-page
  // geometry.
  Json::Value overflow = buildSpec( false );
  const CompositionResult overflowResult = resolveComposition( overflow, 12.0 );
  for ( const auto &entry : overflowResult.unsatisfied )
    INFO( "unsatisfied: " << entry );
  for ( const auto &violation : overflowResult.violated )
    INFO( "violated: " << violation.cid << " " << violation.reason );
  for ( const auto &decision : overflowResult.decisions )
    INFO( "decision: " << decision.cid << " " << decision.outcome << " " << decision.reason );
  bool sawOverflow = false;
  for ( const auto &entry : overflowResult.unsatisfied )
    sawOverflow = sawOverflow || entry.find( "page_overflow" ) != std::string::npos;
  REQUIRE( sawOverflow );
  bool sawViolation = false;
  for ( const auto &violation : overflowResult.violated )
    sawViolation = sawViolation || violation.reason.find( "page_overflow" ) != std::string::npos;
  REQUIRE( sawViolation );
  // The refused pin never wrote off-page geometry.
  CHECK( overflow["source_notes"][0]["rect_mm"][1].asDouble() ==
         Catch::Approx( 92.0 ).margin( 0.001 ) );

  // With an explicitly taller second page the very same pin applies.
  Json::Value fits = buildSpec( true );
  const CompositionResult fitsResult = resolveComposition( fits, 12.0 );
  bool sawApplied = false;
  for ( const auto &decision : fitsResult.decisions )
    sawApplied = sawApplied || ( decision.cid == "c-keep" && decision.outcome == "applied" );
  REQUIRE( sawApplied );
  CHECK( fits["source_notes"][0]["rect_mm"][1].asDouble() == Catch::Approx( 208.0 ).margin( 0.001 ) );
}

TEST_CASE( "P8 solver: avoid_overlap refuses a page-crossing push-down",
           "[platform8][solver][page]" )
{
  Json::Value spec = makeMapSpec( "p8-overlap-pin", Json::Value() );
  Json::Value keeper( Json::objectValue );
  keeper["id"] = "title-1";
  keeper["text"] = "k";
  Json::Value keeperRect( Json::arrayValue );
  keeperRect.append( 10.0 );
  keeperRect.append( 200.0 );
  keeperRect.append( 60.0 );
  keeperRect.append( 6.0 );
  keeper["rect_mm"] = keeperRect;
  appendMapSpecItem( spec, "titles", keeper );
  Json::Value mover( Json::objectValue );
  mover["id"] = "label-1";
  mover["text"] = "m";
  Json::Value moverRect( Json::arrayValue );
  moverRect.append( 20.0 );
  moverRect.append( 204.0 );
  moverRect.append( 20.0 );
  moverRect.append( 4.0 );
  mover["rect_mm"] = moverRect;
  const std::string moverId = appendMapSpecItem( spec, "labels", mover );
  Json::Value avoid( Json::objectValue );
  avoid["id"] = "c-avoid";
  avoid["kind"] = "avoid_overlap";
  Json::Value items( Json::arrayValue );
  items.append( "title-1" );
  items.append( moverId );
  avoid["items"] = items;
  avoid["gap_mm"] = 2.0;
  spec["constraints"].append( avoid );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  // Pushing the mover to y=208 (h=4) ends at 212 > 210: refused, geometry
  // untouched, evidence recorded.
  bool sawOverflow = false;
  for ( const auto &entry : result.unsatisfied )
    sawOverflow = sawOverflow || entry.find( "page_overflow" ) != std::string::npos;
  REQUIRE( sawOverflow );
  CHECK( spec["labels"][0]["rect_mm"][1].asDouble() == Catch::Approx( 204.0 ).margin( 0.001 ) );
}

TEST_CASE( "P8 typography: halfwidth break policy compresses line-final punctuation",
           "[platform8][typography]" )
{
  REQUIRE( isTextBreakPolicy( "none" ) );
  REQUIRE( isTextBreakPolicy( "halfwidth" ) );
  REQUIRE_FALSE( isTextBreakPolicy( "squeeze" ) );

  // One em = 3.528 mm at 10 pt: six fullwidth glyphs measure 21.168 mm.
  const std::string text = "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94\xE3\x80\x82"; // 一二三四五。
  const double em = 10.0 * kMmPerPoint;
  // A box that a full-advance line misses but the halfwidth-compressed
  // line fits (21.168 > 20.0 >= 19.404).
  const double boxMm = 20.0;
  const std::vector<std::string> plain = wrapTextMm( text, boxMm, 10.0 );
  REQUIRE( plain.size() == 2 );
  const std::vector<std::string> compressed =
    wrapTextMmBudgeted( text, boxMm, 10.0, nullptr, "halfwidth" );
  REQUIRE( compressed.size() == 1 );

  // Unknown policies resolve to "none" — identical output to the default.
  const std::vector<std::string> unknown =
    wrapTextMmBudgeted( text, boxMm, 10.0, nullptr, "squeeze" );
  REQUIRE( unknown.size() == plain.size() );

  // The fit report carries the resolved policy and the compressed width
  // (5 fullwidth glyphs + the line-final comma at half advance = 5.5 em).
  TextFitRequest request;
  request.text = text;
  request.boxWidthMm = boxMm;
  request.boxHeightMm = 100.0;
  request.fontPt = 10.0;
  request.policy = "overflow_report";
  request.breakPolicy = "halfwidth";
  const TextFitReport report = fitTextIntoBox( request );
  CHECK( report.breakPolicyApplied == "halfwidth" );
  CHECK( report.fits );
  CHECK( report.lines.size() == 1 );
  CHECK( report.usedWidthMm == Catch::Approx( 5.5 * em ).margin( 0.001 ) );

  // The same box under the default policy wraps to two lines (the line-end
  // full advance leaves the pair 一二三四五。 one glyph too wide for the box).
  request.breakPolicy = "none";
  const TextFitReport plainReport = fitTextIntoBox( request );
  CHECK( plainReport.breakPolicyApplied == "none" );
  CHECK( plainReport.lines.size() == 2 );
}

TEST_CASE( "P8 mapspec: declared font typography surface validates",
           "[platform8][mapspec][typography][validation]" )
{
  Json::Value spec = makeConnectorSpec( "p8-font-invalid" );
  Json::Value title( Json::objectValue );
  title["id"] = "title-1";
  title["text"] = "t";
  Json::Value rect( Json::arrayValue );
  rect.append( 10.0 );
  rect.append( 10.0 );
  rect.append( 60.0 );
  rect.append( 10.0 );
  title["rect_mm"] = rect;
  title["font"]["break_policy"] = "squeeze";
  title["font"]["line_height"] = 0.0;
  appendMapSpecItem( spec, "titles", title );

  int saw = 0;
  for ( const auto &problem : validateMapSpec( spec ) )
  {
    saw += problem.find( "font.break_policy must be none|halfwidth" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "font.line_height must be a number in (0, 3]" ) != std::string::npos ? 1
                                                                                              : 0;
  }
  REQUIRE( saw == 2 );

  Json::Value good = makeConnectorSpec( "p8-font-good" );
  Json::Value okTitle = spec["titles"][0];
  okTitle["font"]["break_policy"] = "halfwidth";
  okTitle["font"]["line_height"] = 1.4;
  good["titles"].clear();
  appendMapSpecItem( good, "titles", okTitle );
  REQUIRE( validateMapSpec( good ).empty() );
}

TEST_CASE( "P8 preflight: nodata legend rule fires and repair converges",
           "[platform8][preflight][nodata][repair]" )
{
  StyleRegistry &registry = StyleRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );

  Json::Value style = makeStyleSpec( "p8-nd-style" );
  style["raster"]["renderertype"] = "singleband_pseudocolor";
  style["raster"]["classification"]["mode"] = "discrete";
  Json::Value classes( Json::arrayValue );
  Json::Value entry( Json::objectValue );
  entry["min"] = 0.0;
  entry["max"] = 1.0;
  entry["color"] = "#2c7fb8";
  entry["label"] = "cover";
  classes.append( entry );
  style["raster"]["classification"]["classes"] = classes;
  style["raster"]["nodata"]["value"] = -9999.0;
  style["raster"]["nodata"]["transparent"] = true;
  style["raster"]["nodata"]["label"] = "No data (masked)";
  QString styleError;
  REQUIRE( registry.registerStyle( style, &styleError ) );

  Json::Value spec = makeConnectorSpec( "p8-nodata-legend" );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 200.0 );
  rect.append( 40.0 );
  rect.append( 60.0 );
  rect.append( 60.0 );
  legend["rect_mm"] = rect;
  legend["style_ref"] = "p8-nd-style";
  Json::Value legendClasses( Json::arrayValue );
  legendClasses.append( "cover" );
  legend["classes"] = legendClasses;
  appendMapSpecItem( spec, "legends", legend );

  const Json::Value report = preflightMapSpec( spec );
  bool sawRule = false;
  for ( const auto &issueItem : report["issues"] )
    sawRule = sawRule || issueItem.get( "code", "" ).asString() == "MAP_NODATA_LEGEND";
  REQUIRE( sawRule );

  const int applied = repairMapSpec( spec, report );
  REQUIRE( applied >= 1 );
  CHECK( spec["legends"][0]["nodata"]["label"].asString() == "No data (masked)" );

  // Convergence: the re-preflight clears the rule.
  const Json::Value rechecked = preflightMapSpec( spec );
  bool stillFires = false;
  for ( const auto &issueItem : rechecked["issues"] )
    stillFires = stillFires || issueItem.get( "code", "" ).asString() == "MAP_NODATA_LEGEND";
  REQUIRE_FALSE( stillFires );
}

TEST_CASE( "P8 compiler: legend nodata renders as a swatch composite",
           "[platform8][mapspec][nodata][compiler]" )
{
  Json::Value spec = makeConnectorSpec( "p8-nodata-swatch" );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 200.0 );
  rect.append( 40.0 );
  rect.append( 60.0 );
  rect.append( 60.0 );
  legend["rect_mm"] = rect;
  Json::Value nodata( Json::objectValue );
  nodata["label"] = "NoData";
  legend["nodata"] = nodata;
  appendMapSpecItem( spec, "legends", legend );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto &service = sicnu::agent::layout_tools::LayoutService::instance();
  auto *swatch = qobject_cast<QgsLayoutItemShape *>(
    service.findItem( layout, QStringLiteral( "legend-1-nodata-swatch" ) ) );
  REQUIRE( swatch != nullptr );
  CHECK( service.findItem( layout, QStringLiteral( "legend-1-nodata-label" ) ) != nullptr );
}

TEST_CASE( "P8 compose: digest, provenance and declared output are surfaced",
           "[platform8][harness][compose]" )
{
  Json::Value spec = makeConnectorSpec( "p8-compose-identity" );
  spec["template"] = "inset-locator-a4l";
  spec["output"]["formats"].append( "png" );
  spec["output"]["dpi"] = 200;
  Json::Value title( Json::objectValue );
  title["id"] = "title-1";
  title["text"] = "composed";
  Json::Value rect( Json::arrayValue );
  rect.append( 10.0 );
  rect.append( 10.0 );
  rect.append( 60.0 );
  rect.append( 10.0 );
  title["rect_mm"] = rect;
  title["source_component"] = "title/main";
  appendMapSpecItem( spec, "titles", title );

  auto compose = sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find( "cartography:compose" );
  if ( !compose )
  {
    sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerBuiltinTools();
    compose = sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find( "cartography:compose" );
  }
  REQUIRE( compose );
  Json::Value input;
  input["mapspec"] = spec;
  const sicnu::agent::spatial_tools::SpatialToolResult result = ( *compose )->execute( input );
  REQUIRE( result.success );
  REQUIRE( result.output.isObject() );

  // Digest: identical to the standalone structural digest of the resolved
  // spec (what was composed, rendering-free).
  CHECK( result.output["structural_digest"].asString() ==
         structuralDigest( result.output["mapspec"] ) );
  CHECK( result.output["structural_digest"].asString().size() == 64 );

  // Provenance: declared template and component references.
  CHECK( result.output["provenance"]["template"].asString() == "inset-locator-a4l" );
  bool sawComponent = false;
  for ( const auto &component : result.output["provenance"]["components"] )
    sawComponent = sawComponent || component["id"].asString() == "title/main";
  REQUIRE( sawComponent );

  // Declared output block is echoed for the delivery contract.
  CHECK( result.output["declared_output"]["dpi"].asDouble() == 200.0 );
}
