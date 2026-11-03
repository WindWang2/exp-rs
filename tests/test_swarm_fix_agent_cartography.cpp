// tests/test_swarm_fix_agent_cartography.cpp
// Regression anchors for the 2026-11-04 whole-repo swarm review, agent
// cartography / mapspec / layout_tools findings AG-M-1 .. AG-M-10.
//
// Every case pins the POST-FIX behaviour of exactly one finding so a
// regression fails here instead of silently in a downstream surface:
//   AG-M-1  layout:preflight judges each item against its OWN page rect
//   AG-M-2  continuous ramp stops are interpolation nodes (no single colour)
//   AG-M-3  negative chart values render on the negative half of the axis
//   AG-M-4  a false body page_if prunes the page-0 items (and the ledger)
//   AG-M-5  an unparseable atlas sort_expression fails the compile typed
//   AG-M-6  an unknown colorbar ramp refuses instead of defaulting silently
//   AG-M-7  distribute spaces the GAPS, so unequal widths cannot overlap
//   AG-M-8  explain's unsat_cores filter maps constraint ids → item ids
//   AG-M-9  preflight/repair resolve conditions like the operator surface
//   AG-M-10 every preflight rule shares the documented 500-issue bound
//
// Runs with its own harness main(): QgsApplication is required because the
// layout/style/compiler cases touch QGIS renderers and print layouts.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/chart_registry.h"
#include "agent/cartography/composition.h"
#include "agent/cartography/quality.h"
#include "agent/cartography/style_compiler.h"
#include "agent/layout_tools/layout_service.h"
#include "agent/layout_tools/layout_tools.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgscolorrampshader.h>
#include <qgslayoutitempage.h>
#include <qgslayoutmanager.h>
#include <qgslayoutpagecollection.h>
#include <qgslayoutsize.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsrastershader.h>
#include <qgssinglebandpseudocolorrenderer.h>

using namespace sicnu::agent::mapspec;
using sicnu::agent::spatial_tools::SpatialToolRegistry;

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

namespace {

void cleanupProject()
{
  QgsProject::instance()->removeAllMapLayers();
  QgsProject::instance()->layoutManager()->clear();
}

SpatialToolRegistry &registry()
{
  auto &reg = SpatialToolRegistry::instance();
  reg.registerBuiltinTools(); // idempotent; registers layout:* / cartography:*
  return reg;
}

sicnu::agent::spatial_tools::SpatialToolResult runTool( const std::string &name,
                                                        const Json::Value &input )
{
  const auto tool = registry().find( name );
  REQUIRE( tool.has_value() );
  return ( *tool )->execute( input );
}

Json::Value rectItem( const std::string &id, double x, double y, double w, double h )
{
  Json::Value item( Json::objectValue );
  item["id"] = id;
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  item["rect_mm"] = rect;
  return item;
}

/// A minimal valid MapSpec: envelope + one map frame with an extent.
Json::Value minimalSpec( const std::string &layoutName )
{
  Json::Value spec = makeMapSpec( layoutName, Json::Value() );
  spec["map_frames"].append( rectItem( "map-1", 12, 24, 190, 160 ) );
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  spec["map_frames"][0]["extent"] = extent;
  return spec;
}

bool hasLayoutIssue( const sicnu::agent::spatial_tools::SpatialToolResult &r, const std::string &check,
                     const std::string &item = "" )
{
  for ( const auto &issue : r.output["issues"] )
  {
    if ( !issue.isObject() || issue["check"].asString() != check )
      continue;
    if ( item.empty() || issue.get( "item", "" ).asString() == item )
      return true;
  }
  return false;
}

bool hasQualityCode( const Json::Value &report, const std::string &code,
                     const std::string &itemId = "" )
{
  for ( const auto &issue : report["issues"] )
  {
    if ( !issue.isObject() || issue["code"].asString() != code )
      continue;
    if ( itemId.empty() || issue.get( "item_id", "" ).asString() == itemId )
      return true;
  }
  return false;
}

/// Writes a 4x4 Float32 GeoTIFF so a live QgsRasterLayer exists for the
/// style-compiler cases (bounded: 16 cells, removed with the temp dir).
QString writeTinyRaster( const QString &dir )
{
  const QString path = QDir( dir ).filePath( QStringLiteral( "swarm-fixture.tif" ) );
  QgsRasterFileWriter writer( path );
  QgsRasterDataProvider *provider = writer.createOneBandRaster(
    Qgis::DataType::Float32, 4, 4, QgsRectangle( 0, 0, 4, 4 ),
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
  if ( !provider )
    return QString();
  QgsRasterBlock block( Qgis::DataType::Float32, 4, 4 );
  for ( int row = 0; row < 4; ++row )
    for ( int col = 0; col < 4; ++col )
      block.setValue( row, col, static_cast<double>( row * 4 + col ) );
  const bool written = provider->writeBlock( &block, 1, 0, 0 );
  delete provider;
  return written ? path : QString();
}

} // namespace

// ---------------------------------------------------------------------------
// AG-M-1: layout:preflight off-page detection must use the item's own page.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-1: layout:preflight judges each item against its own page",
           "[swarm][agm1][layout][preflight]" )
{
  cleanupProject();
  registry();

  Json::Value createInput( Json::objectValue );
  createInput["name"] = "SwarmAGM1";
  REQUIRE( runTool( "layout:create", createInput ).success );

  QgsPrintLayout *layout = sicnu::agent::layout_tools::LayoutService::instance().findLayout(
    QStringLiteral( "SwarmAGM1" ) );
  REQUIRE( layout != nullptr );

  // Second physical page (A4 portrait, directly below page 0).
  auto *secondPage = new QgsLayoutItemPage( layout );
  secondPage->setPageSize( QgsLayoutSize( 210.0, 297.0 ) );
  layout->pageCollection()->addPage( secondPage );
  REQUIRE( layout->pageCollection()->pageCount() == 2 );

  Json::Value labelInput( Json::objectValue );
  labelInput["layout"] = "SwarmAGM1";
  labelInput["type"] = "label";
  Json::Value props( Json::objectValue );
  props["text"] = "page 2 caption";
  props["x"] = 20.0;
  props["y"] = 320.0; // inside page 2's band [297, 594], far outside page 0
  props["width"] = 60.0;
  props["height"] = 10.0;
  labelInput["properties"] = props;
  REQUIRE( runTool( "layout:add_item", labelInput ).success );

  Json::Value preflightInput( Json::objectValue );
  preflightInput["layout"] = "SwarmAGM1";
  const auto report = runTool( "layout:preflight", preflightInput );
  REQUIRE( report.success );

  // The page-2 item is on its own sheet: no off-page finding for it. (The
  // old page-0-only comparison reported it as entirely off-page.)
  CHECK_FALSE( hasLayoutIssue( report, "off_page_item" ) );

  // Control: an item pushed genuinely outside every page still reports.
  Json::Value outsideInput = labelInput;
  outsideInput["properties"]["y"] = 900.0;
  REQUIRE( runTool( "layout:add_item", outsideInput ).success );
  const auto after = runTool( "layout:preflight", preflightInput );
  CHECK( hasLayoutIssue( after, "off_page_item" ) );

  sicnu::agent::layout_tools::LayoutService::instance().deleteLayout(
    QStringLiteral( "SwarmAGM1" ) );
  cleanupProject();
}

// ---------------------------------------------------------------------------
// AG-M-2: continuous ramp stops are interpolation nodes, not one shared value.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-2: continuous classification keeps distinct ramp stops",
           "[swarm][agm2][style][ramp]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString rasterPath = writeTinyRaster( dir.path() );
  REQUIRE_FALSE( rasterPath.isEmpty() );
  QgsRasterLayer raster( rasterPath, QStringLiteral( "swarm-agm2" ) );
  REQUIRE( raster.isValid() );

  Json::Value style( Json::objectValue );
  style["kind"] = "style_spec";
  style["id"] = "swarm-agm2-style";
  style["version"] = 1;
  style["applies_to"] = "raster";
  style["raster"]["renderertype"] = "singleband_pseudocolor";
  style["raster"]["band"] = 1;
  style["raster"]["stretch"]["min"] = 0.0;
  style["raster"]["stretch"]["max"] = 1.0;
  style["raster"]["classification"]["mode"] = "continuous";
  Json::Value ramp( Json::arrayValue );
  for ( const char *hex : { "#440154", "#3b528b", "#21918c", "#5ec962", "#fde725" } )
    ramp.append( hex );
  style["raster"]["classification"]["ramp"] = ramp;

  QString error;
  QStringList problems;
  REQUIRE( sicnu::agent::cartography::applyStyleSpecToLayer( &raster, style, &error, &problems ) );

  // QgsRasterRenderer / QgsRasterShaderFunction are not QObjects: QGIS
  // itself downcasts with dynamic_cast (qgssinglebandpseudocolorrenderer.cpp:73).
  auto *renderer = dynamic_cast<QgsSingleBandPseudoColorRenderer *>( raster.renderer() );
  REQUIRE( renderer != nullptr );
  auto *shader =
    dynamic_cast<QgsColorRampShader *>( renderer->shader()->rasterShaderFunction() );
  REQUIRE( shader != nullptr );
  const QList<QgsColorRampShader::ColorRampItem> items = shader->colorRampItemList();
  REQUIRE( items.size() == 5 );

  // Every stop carries its OWN node value across [min, max] — the old code
  // gave them all the global max, which collapsed the band to one colour.
  for ( int i = 0; i < items.size(); ++i )
    CHECK( items[i].value == Catch::Approx( 0.25 * i ) );
  for ( int i = 1; i < items.size(); ++i )
    CHECK( items[i].value > items[i - 1].value );

  // shade() therefore separates the ramp instead of returning stop 0 for
  // everything below the maximum.
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 0;
  REQUIRE( shader->shade( 0.0, &r, &g, &b, &a ) );
  const QColor low( r, g, b );
  REQUIRE( shader->shade( 1.0, &r, &g, &b, &a ) );
  const QColor high( r, g, b );
  REQUIRE( shader->shade( 0.5, &r, &g, &b, &a ) );
  const QColor mid( r, g, b );
  CHECK( low != high );
  CHECK( mid != low );
  CHECK( mid != high );
}

// ---------------------------------------------------------------------------
// AG-M-3: negative values must render on the negative half of the value axis.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-3: negative chart values render below the zero baseline",
           "[swarm][agm3][charts][negative]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  Json::Value chart( Json::objectValue );
  chart["kind"] = "bar";
  chart["binding"]["mode"] = "inline";
  chart["style"]["palette"] = Json::Value( Json::arrayValue );
  chart["style"]["palette"].append( "#ff0000" );
  chart["style"]["palette"].append( "#0000ff" );
  Json::Value data( Json::arrayValue );
  Json::Value positive( Json::objectValue );
  positive["label"] = "gain";
  positive["value"] = 5.0;
  data.append( positive );
  Json::Value negative( Json::objectValue );
  negative["label"] = "loss";
  negative["value"] = -5.0;
  data.append( negative );
  chart["binding"]["data"] = data;

  const QString path = QDir( dir.path() ).filePath( QStringLiteral( "swarm-agm3-bar.png" ) );
  QString error;
  REQUIRE( sicnu::agent::cartography::renderChartToFile( chart, path, &error ) );
  QImage image( path );
  REQUIRE( !image.isNull() );

  // Bar 0 (positive, red) and bar 1 (negative, blue) must occupy disjoint
  // halves of the plot: red entirely above blue. (fabs used to draw both
  // bars upward from the baseline with identical extents.)
  int redMinY = image.height();
  int redMaxY = -1;
  int blueMinY = image.height();
  int blueMaxY = -1;
  for ( int y = 0; y < image.height(); ++y )
  {
    for ( int x = 50; x < 250; ++x )
    {
      const QRgb px = image.pixel( x, y );
      if ( qRed( px ) == 255 && qGreen( px ) == 0 && qBlue( px ) == 0 )
      {
        redMinY = std::min( redMinY, y );
        redMaxY = std::max( redMaxY, y );
      }
    }
    for ( int x = 263; x < 463; ++x )
    {
      const QRgb px = image.pixel( x, y );
      if ( qRed( px ) == 0 && qGreen( px ) == 0 && qBlue( px ) == 255 )
      {
        blueMinY = std::min( blueMinY, y );
        blueMaxY = std::max( blueMaxY, y );
      }
    }
  }
  REQUIRE( redMaxY >= 0 );  // the positive bar rendered
  REQUIRE( blueMaxY >= 0 ); // the negative bar rendered
  CHECK( redMaxY < blueMinY );
  // ±5 spans the whole signed axis: the positive bar reaches the plot top,
  // the negative one the plot bottom (image is 480x320).
  CHECK( redMinY <= 35 );
  CHECK( blueMaxY >= 285 );

  // A line crossing zero must appear on both sides of the mid-plot line;
  // fabs used to keep every point on the positive side.
  Json::Value lineChart( Json::objectValue );
  lineChart["kind"] = "line";
  lineChart["binding"]["mode"] = "inline";
  lineChart["style"]["palette"] = Json::Value( Json::arrayValue );
  lineChart["style"]["palette"].append( "#ff0000" );
  Json::Value lineData( Json::arrayValue );
  Json::Value up( Json::objectValue );
  up["label"] = "up";
  up["value"] = 4.0;
  lineData.append( up );
  Json::Value down( Json::objectValue );
  down["label"] = "down";
  down["value"] = -4.0;
  lineData.append( down );
  lineChart["binding"]["data"] = lineData;

  const QString linePath = QDir( dir.path() ).filePath( QStringLiteral( "swarm-agm3-line.png" ) );
  REQUIRE( sicnu::agent::cartography::renderChartToFile( lineChart, linePath, &error ) );
  QImage lineImage( linePath );
  REQUIRE( !lineImage.isNull() );
  bool above = false;
  bool below = false;
  for ( int y = 0; y < lineImage.height(); ++y )
  {
    for ( int x = 44; x < lineImage.width() - 10; ++x )
    {
      const QRgb px = lineImage.pixel( x, y );
      if ( qRed( px ) > 150 && qGreen( px ) < 120 && qBlue( px ) < 120 )
      {
        if ( y < 160 )
          above = true;
        if ( y > 165 )
          below = true;
      }
    }
  }
  CHECK( above );
  CHECK( below );
}

// ---------------------------------------------------------------------------
// AG-M-4: a false page_if on the body page prunes the page-0 items.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-4: false body page_if prunes page-0 items and logs them",
           "[swarm][agm4][mapspec][conditions]" )
{
  Json::Value spec = minimalSpec( "swarm-agm4" );
  spec["titles"].append( rectItem( "title-1", 12, 6, 200, 12 ) );
  spec["labels"].append( rectItem( "label-1", 12, 30, 60, 10 ) ); // body page (no `page`)
  Json::Value pageItem = rectItem( "chart-1", 12, 180, 80, 20 );
  pageItem["page"] = 1; // lives on pages[0]
  spec["charts"].append( pageItem );
  Json::Value pages( Json::arrayValue );
  Json::Value sheet( Json::objectValue );
  sheet["width_mm"] = 210.0;
  sheet["height_mm"] = 297.0;
  pages.append( sheet );
  spec["pages"] = pages;
  spec["page"]["page_if"] = "has(statistics)";

  Json::Value ctx( Json::objectValue ); // no statistics -> the page is off
  std::vector<std::string> errors;
  const Json::Value ledger = resolveMapSpecConditions( spec, ctx, &errors );
  CHECK( errors.empty() );

  // Page-0 entries are gone; the later-page entry survives.
  CHECK( findMapSpecItem( spec, "title-1" ).isNull() );
  CHECK( findMapSpecItem( spec, "label-1" ).isNull() );
  CHECK_FALSE( findMapSpecItem( spec, "chart-1" ).isNull() );

  // The ledger documents the page decision AND each dropped item.
  bool pageLogged = false;
  bool titleLogged = false;
  bool labelLogged = false;
  for ( const auto &entry : ledger )
  {
    if ( !entry.isObject() )
      continue;
    const std::string id = entry["id"].asString();
    if ( id == "page" && entry["field"].asString() == "page_if" &&
         entry["outcome"].asString() == "hidden" )
      pageLogged = true;
    if ( id == "title-1" && entry["outcome"].asString() == "hidden" )
      titleLogged = true;
    if ( id == "label-1" && entry["outcome"].asString() == "hidden" )
      labelLogged = true;
  }
  CHECK( pageLogged );
  CHECK( titleLogged );
  CHECK( labelLogged );

  // The unconsumed dead flag is no longer written.
  CHECK_FALSE( spec["page"].isMember( "visible" ) );

  // Control: a satisfied condition keeps everything.
  Json::Value kept = minimalSpec( "swarm-agm4-kept" );
  kept["titles"].append( rectItem( "title-1", 12, 6, 200, 12 ) );
  kept["page"]["page_if"] = "has(statistics)";
  Json::Value withStats( Json::objectValue );
  withStats["statistics"] = true;
  resolveMapSpecConditions( kept, withStats, &errors );
  CHECK_FALSE( findMapSpecItem( kept, "title-1" ).isNull() );
}

// ---------------------------------------------------------------------------
// AG-M-5: an unparseable atlas sort_expression fails the compile typed.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-5: unparseable atlas sort_expression refuses the compile",
           "[swarm][agm5][mapspec][atlas]" )
{
  cleanupProject();

  Json::Value spec = minimalSpec( "swarm-agm5" );
  spec["page"]["atlas"] = Json::Value( Json::objectValue );
  spec["page"]["atlas"]["enabled"] = true;
  spec["page"]["atlas"]["sort_expression"] = "((("; // unbalanced parens

  QString error;
  QgsPrintLayout *layout = nullptr;
  REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &error ) );
  CHECK( layout == nullptr );
  CHECK( error.contains( QStringLiteral( "invalid atlas sort_expression" ) ) );

  // A well-formed expression still compiles.
  Json::Value good = minimalSpec( "swarm-agm5-good" );
  good["page"]["atlas"] = Json::Value( Json::objectValue );
  good["page"]["atlas"]["enabled"] = true;
  good["page"]["atlas"]["sort_expression"] = "\"name\"";
  QString goodError;
  QgsPrintLayout *goodLayout = nullptr;
  REQUIRE_NOTHROW( goodLayout = MapSpecCompiler::compile( good, &goodError ) );
  REQUIRE( goodLayout != nullptr );
  if ( goodLayout )
    QgsProject::instance()->layoutManager()->removeLayout( goodLayout );
  cleanupProject();
}

// ---------------------------------------------------------------------------
// AG-M-6: an unknown colorbar ramp refuses instead of defaulting silently.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-6: unknown colorbar ramp refuses; explicit stops still win",
           "[swarm][agm6][charts][colorbar]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  Json::Value unknownRamp( Json::objectValue );
  unknownRamp["ramp"] = "not-a-known-ramp";
  unknownRamp["min"] = "0";
  unknownRamp["max"] = "1";
  QString error;
  const QString badPath = QDir( dir.path() ).filePath( QStringLiteral( "swarm-agm6-bad.png" ) );
  CHECK_FALSE( sicnu::agent::cartography::renderColorbarToFile( unknownRamp, badPath, &error ) );
  CHECK( error.contains( QStringLiteral( "unknown colorbar ramp" ) ) );
  CHECK( error.contains( QStringLiteral( "viridis" ) ) ); // known names are listed

  // Known ramp names keep rendering.
  Json::Value knownRamp( Json::objectValue );
  knownRamp["ramp"] = "viridis";
  knownRamp["min"] = "0";
  knownRamp["max"] = "1";
  const QString okPath = QDir( dir.path() ).filePath( QStringLiteral( "swarm-agm6-ok.png" ) );
  REQUIRE( sicnu::agent::cartography::renderColorbarToFile( knownRamp, okPath, &error ) );
  CHECK( QFileInfo::exists( okPath ) );
  CHECK( QFileInfo( okPath ).size() > 100 );

  // Explicit colors take precedence over the (unknown) ramp name.
  Json::Value explicitStops = unknownRamp;
  Json::Value colors( Json::arrayValue );
  colors.append( "#000000" );
  colors.append( "#ffffff" );
  explicitStops["colors"] = colors;
  const QString stopPath = QDir( dir.path() ).filePath( QStringLiteral( "swarm-agm6-stops.png" ) );
  REQUIRE( sicnu::agent::cartography::renderColorbarToFile( explicitStops, stopPath, &error ) );
  CHECK( QFileInfo::exists( stopPath ) );

  // A failed write is a typed failure (the compiler lands the placeholder).
  const QString unwritable = QDir( dir.path() ).filePath( QStringLiteral( "no/such/dir/x.png" ) );
  CHECK_FALSE( sicnu::agent::cartography::renderColorbarToFile( knownRamp, unwritable, &error ) );
  CHECK( error.contains( QStringLiteral( "cannot write colorbar image" ) ) );
}

// ---------------------------------------------------------------------------
// AG-M-7: distribute spaces the gaps, so unequal widths cannot overlap.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-7: distribute equalizes gaps for unequal item widths",
           "[swarm][agm7][composition][distribute]" )
{
  Json::Value spec = minimalSpec( "swarm-agm7" );
  spec["labels"].append( rectItem( "a", 10, 40, 20, 10 ) );
  spec["labels"].append( rectItem( "b", 100, 40, 60, 10 ) ); // wide middle item
  spec["labels"].append( rectItem( "c", 250, 40, 30, 10 ) );
  Json::Value constraint( Json::objectValue );
  constraint["kind"] = "distribute";
  constraint["direction"] = "horizontal";
  constraint["items"] = Json::Value( Json::arrayValue );
  constraint["items"].append( "a" );
  constraint["items"].append( "b" );
  constraint["items"].append( "c" );
  spec["constraints"].append( constraint );

  sicnu::agent::cartography::resolveComposition( spec, 12.0 );

  const double ax = spec["labels"][0]["rect_mm"][0].asDouble();
  const double aw = spec["labels"][0]["rect_mm"][2].asDouble();
  const double bx = spec["labels"][1]["rect_mm"][0].asDouble();
  const double bw = spec["labels"][1]["rect_mm"][2].asDouble();
  const double cx = spec["labels"][2]["rect_mm"][0].asDouble();
  const double gap1 = bx - ( ax + aw );
  const double gap2 = cx - ( bx + bw );
  CHECK( gap1 == Catch::Approx( gap2 ).epsilon( 0.01 ) );
  CHECK( gap1 > 0.0 );
  // Endpoints stay put (only the middle item moves).
  CHECK( ax == Catch::Approx( 10.0 ) );
  CHECK( cx == Catch::Approx( 250.0 ) );
  // The widths are untouched.
  CHECK( bw == Catch::Approx( 60.0 ) );

  // Vertical direction follows the same gap arithmetic.
  Json::Value vertical = minimalSpec( "swarm-agm7-v" );
  vertical["labels"].append( rectItem( "a", 40, 10, 20, 15 ) );
  vertical["labels"].append( rectItem( "b", 40, 60, 20, 50 ) );
  vertical["labels"].append( rectItem( "c", 40, 170, 20, 20 ) );
  Json::Value verticalConstraint( Json::objectValue );
  verticalConstraint["kind"] = "distribute";
  verticalConstraint["direction"] = "vertical";
  verticalConstraint["items"] = Json::Value( Json::arrayValue );
  verticalConstraint["items"].append( "a" );
  verticalConstraint["items"].append( "b" );
  verticalConstraint["items"].append( "c" );
  vertical["constraints"].append( verticalConstraint );
  sicnu::agent::cartography::resolveComposition( vertical, 12.0 );

  const double ay = vertical["labels"][0]["rect_mm"][1].asDouble();
  const double ah = vertical["labels"][0]["rect_mm"][3].asDouble();
  const double by = vertical["labels"][1]["rect_mm"][1].asDouble();
  const double bh = vertical["labels"][1]["rect_mm"][3].asDouble();
  const double cy = vertical["labels"][2]["rect_mm"][1].asDouble();
  const double vGap1 = by - ( ay + ah );
  const double vGap2 = cy - ( by + bh );
  CHECK( vGap1 == Catch::Approx( vGap2 ).epsilon( 0.01 ) );
  CHECK( vGap1 > 0.0 );
}

// ---------------------------------------------------------------------------
// AG-M-8: explain's unsat_cores must filter by constraint → item mapping.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-8: explain reports unsat cores that touch the item",
           "[swarm][agm8][tools][explain]" )
{
  cleanupProject();
  registry();

  Json::Value spec = minimalSpec( "swarm-agm8" );
  spec["map_frames"].append( rectItem( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( rectItem( "c2", 150, 10, 80, 60 ) );
  spec["map_frames"].append( rectItem( "c3", 10, 120, 80, 60 ) ); // untouched by constraints
  spec["titles"].append( rectItem( "x", 70, 35, 20, 20 ) );
  Json::Value pullLeft( Json::objectValue );
  pullLeft["id"] = "pull-left";
  pullLeft["kind"] = "inside";
  pullLeft["items"] = Json::Value( Json::arrayValue );
  pullLeft["items"].append( "c1" );
  pullLeft["items"].append( "x" );
  spec["constraints"].append( pullLeft );
  Json::Value pullRight( Json::objectValue );
  pullRight["id"] = "pull-right";
  pullRight["kind"] = "inside";
  pullRight["items"] = Json::Value( Json::arrayValue );
  pullRight["items"].append( "c2" );
  pullRight["items"].append( "x" );
  spec["constraints"].append( pullRight );

  Json::Value input( Json::objectValue );
  input["mapspec"] = spec;
  input["item_id"] = "x";
  const auto result = runTool( "cartography:explain", input );
  REQUIRE( result.success );
  REQUIRE( result.output["solver"].isMember( "unsat_cores" ) );

  // The two fighting constraints leave one unsatisfied hard constraint; the
  // core entry names a CONSTRAINT id, which must map back to item "x".
  const Json::Value &cores = result.output["solver"]["unsat_cores"];
  CHECK( cores.size() >= 1 );
  bool namesFightingConstraint = false;
  for ( const auto &core : cores )
  {
    if ( !core.isObject() || !core.isMember( "constraint" ) )
      continue;
    const std::string cid = core["constraint"].asString();
    namesFightingConstraint = namesFightingConstraint || cid == "pull-left" ||
                              cid == "pull-right";
  }
  CHECK( namesFightingConstraint );

  // An item no constraint touches still gets an empty core list.
  Json::Value otherInput( Json::objectValue );
  otherInput["mapspec"] = spec;
  otherInput["item_id"] = "c3";
  const auto other = runTool( "cartography:explain", otherInput );
  REQUIRE( other.success );
  CHECK( other.output["solver"]["unsat_cores"].size() == 0 );
  cleanupProject();
}

// ---------------------------------------------------------------------------
// AG-M-9: the preflight tool resolves conditions like the operator surface.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-9: preflight tool resolves condition fields before the solve",
           "[swarm][agm9][tools][preflight][conditions]" )
{
  cleanupProject();
  registry();

  Json::Value spec = minimalSpec( "swarm-agm9" );
  // Off-page label that its own visible_if hides: after condition
  // resolution it must not reach any rule.
  Json::Value hidden = rectItem( "hidden-label", 400, 400, 40, 10 );
  hidden["visible_if"] = "has(mode)";
  spec["labels"].append( hidden );
  spec["condition_context"] = Json::Value( Json::objectValue ); // no `mode`

  // The spec-level preflight helper does NOT resolve conditions (the raw
  // document still carries the item).
  const Json::Value raw = sicnu::agent::cartography::preflightMapSpec( spec );
  CHECK( hasQualityCode( raw, "MAP_OFF_PAGE", "hidden-label" ) );

  // The tool resolves them first (same pass as cartography:compose and the
  // cartography operator family), so the hidden item is gone from both the
  // report and the echoed document.
  Json::Value input( Json::objectValue );
  input["mapspec"] = spec;
  const auto result = runTool( "cartography:preflight", input );
  REQUIRE( result.success );
  CHECK_FALSE( hasQualityCode( result.output, "MAP_OFF_PAGE", "hidden-label" ) );
  CHECK( findMapSpecItem( result.output["mapspec"], "hidden-label" ).isNull() );

  // The repair tool shares the same condition-aware pass.
  Json::Value repairInput( Json::objectValue );
  repairInput["mapspec"] = spec;
  repairInput["max_iterations"] = 1;
  const auto repaired = runTool( "cartography:repair", repairInput );
  REQUIRE( repaired.success );
  CHECK( findMapSpecItem( repaired.output["mapspec"], "hidden-label" ).isNull() );
  cleanupProject();
}

// ---------------------------------------------------------------------------
// AG-M-10: the documented 500-issue bound covers every rule, not two.
// ---------------------------------------------------------------------------
TEST_CASE( "AG-M-10: preflight issue lists are capped for every rule",
           "[swarm][agm10][quality][bounds]" )
{
  Json::Value spec = minimalSpec( "swarm-agm10" );
  spec["page"]["margin_mm"] = 20.0;
  // 600 margin-violating labels: a rule that used to append unbounded.
  Json::Value labels( Json::arrayValue );
  for ( int i = 0; i < 600; ++i )
  {
    Json::Value label = rectItem( "label-" + std::to_string( i ), i * 0.45, 6, 0.4, 8 );
    labels.append( label );
  }
  spec["labels"] = labels;

  const Json::Value report = sicnu::agent::cartography::preflightMapSpec( spec );
  const Json::Value &issues = report["issues"];
  REQUIRE( issues.isArray() );
  // 500 findings + the truncation notice — never the raw 600+ list.
  CHECK( issues.size() == 501 );
  if ( issues.size() == 501 )
  {
    CHECK( issues[500]["code"].asString() == "MAPSPEC_ISSUES_TRUNCATED" );
    int marginViolations = 0;
    for ( const auto &issue : issues )
      if ( issue["code"].asString() == "MAP_MARGIN_VIOLATION" )
        ++marginViolations;
    CHECK( marginViolations <= 494 ); // the rest were dropped by the cap
  }
}
