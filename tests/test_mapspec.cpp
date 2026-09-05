// tests/test_mapspec.cpp
// Phase H/I/J/L/M — MapSpec document model, compiler roundtrip, registries,
// charts, preflight/repair loop.
#include <catch2/catch_session.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/chart_registry.h"
#include "agent/cartography/registry.h"
#include "agent/contracts/spatial_contracts.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgsapplication.h>
#include <qgslayoutitemmap.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QDir>
#include <QFileInfo>

#include "agent/layout_tools/layout_service.h"

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

int main( int argc, char *argv[] )
{
  qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

using namespace sicnu::agent::mapspec;
using sicnu::agent::cartography::ChartRegistry;

namespace {

Json::Value minimalSpec( const std::string &layoutName = "bench-map" )
{
  Json::Value spec = makeMapSpec( layoutName, Json::Value() );
  appendMapSpecItem( spec, "map_frames", Json::Value() );
  Json::Value &frame = spec["map_frames"][0];
  Json::Value rect( Json::arrayValue );
  rect.append( 12 );
  rect.append( 24 );
  rect.append( 190 );
  rect.append( 160 );
  frame["rect_mm"] = rect;
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  frame["extent"] = extent;
  return spec;
}

} // namespace

TEST_CASE( "MapSpec document model: make/append/validate", "[mapspec]" )
{
  Json::Value spec = makeMapSpec( "demo", Json::Value() );
  CHECK( spec["kind"].asString() == "map_spec" );
  CHECK( spec["spec_version"].asInt() == kMapSpecCurrentVersion );
  CHECK( spec["page"]["width_mm"].asDouble() == Catch::Approx( 297.0 ) );

  // An empty spec with no map frame still validates structurally (maps are a
  // cartography-preflight concern, not a structural one).
  CHECK( validateMapSpec( spec ).empty() );

  // Unknown collection rejected.
  CHECK( appendMapSpecItem( spec, "not_a_collection", Json::Value() ).empty() );
  CHECK( isCollection( "map_frames" ) );
  CHECK_FALSE( isCollection( "frames" ) );
  CHECK( idPrefixFor( "titles" ) == "title" );

  // Id assignment is stable and unique.
  const std::string t1 = appendMapSpecItem( spec, "titles", Json::Value() );
  const std::string t2 = appendMapSpecItem( spec, "titles", Json::Value() );
  CHECK( t1 == "title-1" );
  CHECK( t2 == "title-2" );
  Json::Value title = spec["titles"][0];
  title["text"] = "Hello";
  spec["titles"][0] = title;
  Json::Value title2 = spec["titles"][1];
  title2["text"] = "Second";
  spec["titles"][1] = title2;
  CHECK( validateMapSpec( spec ).empty() );

  // find/remove by id.
  Json::Value location = findMapSpecItem( spec, "title-2" );
  REQUIRE( location["collection"].asString() == "titles" );
  CHECK( removeMapSpecItem( spec, "title-2" ) );
  CHECK( findMapSpecItem( spec, "title-2" ).isNull() );
  CHECK( validateMapSpec( spec ).empty() );
}

TEST_CASE( "MapSpec validation catches geometry, ids, and references", "[mapspec][validation]" )
{
  Json::Value spec = minimalSpec();

  // Duplicate id across collections.
  Json::Value dup( Json::objectValue );
  dup["id"] = "map-1";
  Json::Value dupRect( Json::arrayValue );
  dupRect.append( 1 );
  dupRect.append( 1 );
  dupRect.append( 10 );
  dupRect.append( 10 );
  dup["rect_mm"] = dupRect;
  spec["titles"].append( dup );
  const auto duplicateProblems = validateMapSpec( spec );
  REQUIRE_FALSE( duplicateProblems.empty() );
  CHECK( duplicateProblems.front().find( "duplicate item id" ) != std::string::npos );

  // Off-page rects are structurally fine but are flagged (repairably) by the
  // cartography preflight.
  Json::Value offPage = minimalSpec( "off-page" );
  Json::Value far( Json::objectValue );
  far["id"] = "label-far";
  Json::Value farRect( Json::arrayValue );
  farRect.append( 500 );
  farRect.append( 500 );
  farRect.append( 20 );
  farRect.append( 8 );
  far["rect_mm"] = farRect;
  far["text"] = "far";
  offPage["labels"].append( far );
  CHECK( validateMapSpec( offPage ).empty() );
  const Json::Value offPageReport = sicnu::agent::cartography::preflightMapSpec( offPage );
  bool hasRepairableOffPage = false;
  for ( const auto &issue : offPageReport["issues"] )
  {
    hasRepairableOffPage = hasRepairableOffPage ||
                           ( issue["code"].asString() == "MAP_OFF_PAGE" &&
                             issue["repairable"].asBool() &&
                             issue["item_id"].asString() == "label-far" );
  }
  CHECK( hasRepairableOffPage );

  // Negative extents are rejected.
  Json::Value tiny = minimalSpec( "tiny" );
  Json::Value &frameRect = tiny["map_frames"][0]["rect_mm"];
  frameRect[2] = -5;
  const auto tinyProblems = validateMapSpec( tiny );
  REQUIRE_FALSE( tinyProblems.empty() );
  CHECK( tinyProblems.front().find( "positive" ) != std::string::npos );
}

TEST_CASE( "MapSpec patch ops", "[mapspec][patch]" )
{
  Json::Value spec = minimalSpec();

  Json::Value addPatch( Json::objectValue );
  addPatch["op"] = "add";
  addPatch["collection"] = "titles";
  Json::Value titleValue( Json::objectValue );
  titleValue["text"] = "Patched title";
  addPatch["value"] = titleValue;
  std::string error;
  REQUIRE( applyMapSpecPatch( spec, addPatch, &error ) );
  CHECK( spec["titles"][0]["id"].asString() == "title-1" );
  CHECK( spec["titles"][0]["text"].asString() == "Patched title" );

  Json::Value updatePatch( Json::objectValue );
  updatePatch["op"] = "update";
  updatePatch["id"] = "title-1";
  Json::Value updateValue( Json::objectValue );
  updateValue["text"] = "Renamed";
  updatePatch["value"] = updateValue;
  REQUIRE( applyMapSpecPatch( spec, updatePatch, &error ) );
  CHECK( spec["titles"][0]["text"].asString() == "Renamed" );

  Json::Value removePatch( Json::objectValue );
  removePatch["op"] = "remove";
  removePatch["id"] = "title-1";
  REQUIRE( applyMapSpecPatch( spec, removePatch, &error ) );
  CHECK( spec["titles"].empty() );

  // Invalid ops report errors.
  Json::Value bad( Json::objectValue );
  bad["op"] = "explode";
  CHECK_FALSE( applyMapSpecPatch( spec, bad, &error ) );
  CHECK_FALSE( error.empty() );

  Json::Value brokenRef( Json::objectValue );
  brokenRef["id"] = "map-1";
  spec["legends"].append( brokenRef );
  brokenRef = Json::Value( Json::objectValue );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  legend["rect_mm"] = Json::Value( Json::arrayValue );
  legend["rect_mm"].append( 210 );
  legend["rect_mm"].append( 30 );
  legend["rect_mm"].append( 60 );
  legend["rect_mm"].append( 70 );
  legend["map_ref"] = "map-does-not-exist";
  spec["legends"][0] = legend;
  const auto refProblems = validateMapSpec( spec );
  REQUIRE_FALSE( refProblems.empty() );
  CHECK( refProblems.front().find( "map_ref" ) != std::string::npos );
}

TEST_CASE( "MapSpec version upgrade v0 → v1", "[mapspec][migrate]" )
{
  Json::Value v0( Json::objectValue );
  v0["layout_name"] = "legacy";
  Json::Value page( Json::objectValue );
  page["width_mm"] = 210.0;
  page["height_mm"] = 297.0;
  v0["page"] = page;
  Json::Value items( Json::arrayValue );
  Json::Value title( Json::objectValue );
  title["kind"] = "title";
  title["text"] = "Legacy map";
  items.append( title );
  Json::Value mapItem( Json::objectValue );
  mapItem["kind"] = "map";
  items.append( mapItem );
  v0["items"] = items;

  const Json::Value upgraded = upgradeMapSpec( v0 );
  CHECK( upgraded["kind"].asString() == "map_spec" );
  CHECK( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  REQUIRE( upgraded["titles"].size() == 1 );
  CHECK( upgraded["titles"][0]["text"].asString() == "Legacy map" );
  CHECK( upgraded["map_frames"].size() == 1 );
  CHECK( validateMapSpec( upgraded ).empty() );
}

TEST_CASE( "MapSpec compiler creates a layout and extractor roundtrips", "[mapspec][compiler]" )
{
  Json::Value spec = minimalSpec( "roundtrip-map" );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 210 );
  rect.append( 30 );
  rect.append( 60 );
  rect.append( 70 );
  legend["rect_mm"] = rect;
  legend["map_ref"] = "map-1";
  appendMapSpecItem( spec, "legends", legend );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  CHECK( error.isEmpty() );

  // Layout contains the map frame + legend items (ids preserved).
  sicnu::agent::layout_tools::LayoutService &service =
    sicnu::agent::layout_tools::LayoutService::instance();
  CHECK( service.findLayout( QStringLiteral( "roundtrip-map" ) ) != nullptr );
  CHECK( service.findItem( layout, QStringLiteral( "map-1" ) ) != nullptr );
  CHECK( service.findItem( layout, QStringLiteral( "legend-1" ) ) != nullptr );
  auto *mapItem = qobject_cast<QgsLayoutItemMap *>( service.findItem( layout, "map-1" ) );
  REQUIRE( mapItem != nullptr );
  CHECK( mapItem->extent().width() == Catch::Approx( 1.0 ).margin( 0.2 ) );

  // Extractor produces a valid MapSpec (structural roundtrip, not 1:1).
  const Json::Value extracted = MapSpecCompiler::extract( layout );
  CHECK( extracted["kind"].asString() == "map_spec" );
  CHECK( validateMapSpec( extracted ).empty() );
  CHECK( sicnu::agent::cartography::preflightMapSpec( extracted )["kind"].asString() ==
         "map_quality_report" );

  // Replacing the layout on recompile must not fail.
  QgsPrintLayout *recompiled = MapSpecCompiler::compile( spec, &error );
  REQUIRE( recompiled != nullptr );
}

TEST_CASE( "Component registry loads shipped data with embedded fallback",
           "[cartography][components]" )
{
  auto &registry = sicnu::agent::cartography::ComponentRegistry::instance();
  // No directory set: falls back to <cwd>/data/cartography or embedded.
  const Json::Value all = registry.components();
  REQUIRE( all.isArray() );
  CHECK( all.size() >= 3 );
  bool hasArrow = false;
  for ( const auto &component : all )
    hasArrow = hasArrow || component["id"].asString() == "north-arrow/minimal";
  CHECK( hasArrow );

  const Json::Value byCategory = registry.byCategory( "north-arrow" );
  CHECK( byCategory.size() >= 1 );

  const Json::Value found = registry.find( "north-arrow/minimal" );
  CHECK( found["category"].asString() == "north-arrow" );

  QString error;
  Json::Value bad( Json::objectValue );
  bad["id"] = "x/invalid";
  bad["category"] = "rocket";
  CHECK_FALSE( registry.registerComponent( bad, &error ) );

  bad["category"] = "legend";
  CHECK( registry.registerComponent( bad, &error ) );
  CHECK( !registry.find( "x/invalid" ).isNull() );
}

TEST_CASE( "Template registry instantiates valid MapSpec drafts",
           "[cartography][templates]" )
{
  auto &registry = sicnu::agent::cartography::TemplateRegistry::instance();
  CHECK( registry.templates().size() >= 1 );

  QString error;
  Json::Value params( Json::objectValue );
  params["layout_name"] = "draft-from-template";
  params["title"] = "Bench title";
  const Json::Value draft = registry.instantiateTemplate(
    QLatin1String( "remote-sensing-result" ), params, &error );
  REQUIRE( !draft.isNull() );
  CHECK( draft["layout_name"].asString() == "draft-from-template" );
  CHECK( draft["template"].asString() == "remote-sensing-result" );
  REQUIRE( draft["map_frames"].size() == 1 );
  REQUIRE( draft["titles"].size() == 1 );
  CHECK( draft["titles"][0]["semantic_role"].asString() == "title.main" );
  CHECK( validateMapSpec( draft ).empty() );

  Json::Value missing = registry.instantiateTemplate( QLatin1String( "no-such-template" ),
                                                      Json::Value(), &error );
  CHECK( missing.isNull() );
  CHECK_FALSE( error.isEmpty() );
}

TEST_CASE( "Chart registry validates and renders inline charts", "[cartography][charts]" )
{
  Json::Value chart( Json::objectValue );
  chart["kind"] = "bar";
  chart["title"] = "Class composition";
  chart["binding"]["mode"] = "inline";
  Json::Value data( Json::arrayValue );
  for ( const char *label : { "water", "forest", "urban", "farm" } )
  {
    Json::Value entry( Json::objectValue );
    entry["label"] = label;
    entry["value"] = 25.0;
    data.append( entry );
  }
  chart["binding"]["data"] = data;

  QString error;
  const QString id = ChartRegistry::instance().createChart( chart, &error );
  REQUIRE( !id.isEmpty() );
  CHECK( id == QLatin1String( "chart-1" ) );
  CHECK( !ChartRegistry::instance().find( id ).isNull() );

  const QString path = QDir::temp().filePath( QStringLiteral( "bench-chart.png" ) );
  REQUIRE( sicnu::agent::cartography::renderChartToFile(
    ChartRegistry::instance().find( id ), path, &error ) );
  CHECK( QFileInfo::exists( path ) );
  CHECK( QFileInfo( path ).size() > 1000 );

  // Invalid specs are rejected.
  Json::Value bad( Json::objectValue );
  bad["kind"] = "hologram";
  bad["binding"]["mode"] = "inline";
  CHECK( ChartRegistry::instance().createChart( bad, &error ).isEmpty() );

  // Update + delete lifecycle.
  Json::Value patch( Json::objectValue );
  patch["title"] = "Renamed chart";
  CHECK( ChartRegistry::instance().updateChart( id, patch, &error ) );
  CHECK( ChartRegistry::instance().find( id )["title"].asString() == "Renamed chart" );
  CHECK( ChartRegistry::instance().removeChart( id ) );
  CHECK( ChartRegistry::instance().find( id ).isNull() );
}

TEST_CASE( "Cartography preflight scores known-broken maps", "[cartography][preflight]" )
{
  // Complete spec passes with a high score.
  Json::Value good = minimalSpec( "quality-good" );
  Json::Value title( Json::objectValue );
  title["text"] = "Complete map";
  appendMapSpecItem( good, "titles", title );
  Json::Value legend( Json::objectValue );
  legend["map_ref"] = "map-1";
  appendMapSpecItem( good, "legends", legend );
  Json::Value scaleBar( Json::objectValue );
  scaleBar["map_ref"] = "map-1";
  appendMapSpecItem( good, "scale_bars", scaleBar );
  Json::Value arrow( Json::objectValue );
  arrow["map_ref"] = "map-1";
  appendMapSpecItem( good, "north_arrows", arrow );
  Json::Value note( Json::objectValue );
  note["text"] = "Source: bench";
  appendMapSpecItem( good, "source_notes", note );
  const Json::Value goodReport = sicnu::agent::cartography::preflightMapSpec( good );
  CHECK( goodReport["passed"].asBool() );
  CHECK( goodReport["quality_score"].asInt() >= 60 ); // missing furniture warnings tolerated

  // No map frame at all → error + score 0.
  Json::Value emptySpec = makeMapSpec( "quality-empty", Json::Value() );
  const Json::Value emptyReport = sicnu::agent::cartography::preflightMapSpec( emptySpec );
  CHECK_FALSE( emptyReport["passed"].asBool() );
  CHECK( emptyReport["error_count"].asInt() == 1 );  // MAP_MISSING_MAP
  CHECK( emptyReport["warning_count"].asInt() == 5 ); // furniture warnings
  CHECK( emptyReport["quality_score"].asInt() == 40 );
  bool hasMissingMap = false;
  for ( const auto &issue : emptyReport["issues"] )
    hasMissingMap = hasMissingMap || issue["code"].asString() == "MAP_MISSING_MAP";
  CHECK( hasMissingMap );

  // Missing title is repairable and carries a suggested action.
  removeMapSpecItem( good, "title-1" );
  const Json::Value noTitle = sicnu::agent::cartography::preflightMapSpec( good );
  bool repairableTitle = false;
  for ( const auto &issue : noTitle["issues"] )
  {
    repairableTitle = repairableTitle ||
                      ( issue["code"].asString() == "MAP_MISSING_TITLE" &&
                        issue["repairable"].asBool() &&
                        issue["suggested_action"]["action"].asString() == "add_title" );
  }
  CHECK( repairableTitle );
}

TEST_CASE( "Cartography repair loop converges on broken drafts", "[cartography][repair]" )
{
  Json::Value spec = makeMapSpec( "repair-loop", Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 12 );
  rect.append( 24 );
  rect.append( 190 );
  rect.append( 160 );
  frame["rect_mm"] = rect;
  spec["map_frames"].append( frame );
  // Off-page annotation
  Json::Value note( Json::objectValue );
  note["id"] = "note-far";
  Json::Value farRect( Json::arrayValue );
  farRect.append( 400 );
  farRect.append( 300 );
  farRect.append( 40 );
  farRect.append( 10 );
  note["rect_mm"] = farRect;
  note["text"] = "far away";
  spec["annotations"].append( note );

  Json::Value report = sicnu::agent::cartography::preflightMapSpec( spec );
  int total = sicnu::agent::cartography::repairMapSpec( spec, report );
  CHECK( total >= 5 ); // title, legend, scale bar, north arrow, source note, off-page
  // Repairs converge: after the first pass the map is error-free (residual
  // repairable overlaps from the clamp are polished by a second pass).
  report = sicnu::agent::cartography::preflightMapSpec( spec );
  CHECK( report["error_count"].asInt() == 0 );
  const int scoreAfter = report["quality_score"].asInt();
  sicnu::agent::cartography::repairMapSpec( spec, report );
  report = sicnu::agent::cartography::preflightMapSpec( spec );
  CHECK( report["passed"].asBool() );
  CHECK( report["quality_score"].asInt() >= scoreAfter );
}
