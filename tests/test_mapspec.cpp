// tests/test_mapspec.cpp
// Phase H/I/J/L/M — MapSpec document model, compiler roundtrip, registries,
// charts, preflight/repair loop.
#include <catch2/catch_session.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/cartography_operators.h"
#include "agent/cartography/chart_registry.h"
#include "agent/cartography/composition.h"
#include "agent/cartography/export.h"
#include "agent/cartography/registry.h"
#include "agent/contracts/spatial_contracts.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"
#include "agent/mapspec/mapspec_conditions.h"

#include <cmath>
#include <limits>

#include <qgsapplication.h>
#include <qgis.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayertree.h>
#include <qgsprintlayout.h>
#include <qgslayoutmanager.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

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
  // Workbench 10.0: the cartography operator family rides the same harness
  // (idempotent; Catch2 randomizes case order, so registration is up front).
  sicnu::agent::cartography::initCartographyOperators();
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

TEST_CASE( "MapSpec validation rejects path-shaped layout names and ids (#1381)", "[mapspec][validation]" )
{
  // layout_name with traversal content never reaches path construction.
  Json::Value escape = minimalSpec( "x/../../../../home/.config/evil" );
  REQUIRE_FALSE( validateMapSpec( escape ).empty() );

  escape = minimalSpec( "..\\..\\windows-evil" );
  REQUIRE_FALSE( validateMapSpec( escape ).empty() );

  // Item ids are flat identifiers too — a hostile chart id must not be
  // spliceable into the session chart file path.
  Json::Value spec = minimalSpec();
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart/../../evil";
  Json::Value chartRect( Json::arrayValue );
  chartRect.append( 1 );
  chartRect.append( 1 );
  chartRect.append( 10 );
  chartRect.append( 10 );
  chart["rect_mm"] = chartRect;
  spec["charts"].append( chart );
  const auto problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  bool flagged = false;
  for ( const auto &problem : problems )
    flagged = flagged || problem.find( "flat identifier" ) != std::string::npos;
  CHECK( flagged );

  // Legal names (CJK, spaces, inner dots, dashes) stay valid.
  CHECK( validateMapSpec( minimalSpec( "海陆分布 基准图 v1.2" ) ).empty() );

  // The compiler-side guard itself: every path-capable byte collapses, so
  // a sanitized stem can never traverse regardless of what reaches compile.
  const QString hostile = QStringLiteral( "x/../../\\..\\..:evil" );
  const QString stem = sicnu::agent::cartography::sanitizeFileStem( hostile, QStringLiteral( "layout" ) );
  CHECK_FALSE( stem.isEmpty() );
  CHECK( !stem.contains( QLatin1Char( '/' ) ) );
  CHECK( !stem.contains( QLatin1Char( '\\' ) ) );
  CHECK( !stem.contains( QLatin1Char( ':' ) ) );
  // Path-shape specials all collapsed to inert characters.
  for ( const QChar ch : stem )
  {
    const ushort u = ch.unicode();
    const bool keep = ( u >= 'a' && u <= 'z' ) || ( u >= 'A' && u <= 'Z' ) ||
                      ( u >= '0' && u <= '9' ) || u == '_' || u == '-' || u == '.' || u == ' ';
    CHECK( keep );
  }
  CHECK( sicnu::agent::cartography::sanitizeFileStem( QStringLiteral( "..." ), QStringLiteral( "layout" ) )
         == QStringLiteral( "layout" ) );
  CHECK( sicnu::agent::cartography::sanitizeFileStem( QStringLiteral( "  " ), QStringLiteral( "layout" ) )
         == QStringLiteral( "layout" ) );
  CHECK( sicnu::agent::cartography::sanitizeFileStem( QStringLiteral( "海陆分布 v1" ), QStringLiteral( "layout" ) )
         == QStringLiteral( "____ v1" ) );
}

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
    QLatin1String( "remote-sensing-result-a4l" ), params, &error );
  REQUIRE( !draft.isNull() );
  CHECK( draft["layout_name"].asString() == "draft-from-template" );
  CHECK( draft["template"].asString() == "remote-sensing-result-a4l" );
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

TEST_CASE( "MapSpec validation and condition parser robustness (R13-G WP-01/WP-02)", "[mapspec][robustness]" )
{
  SECTION( "Non-finite numbers and type mismatches in rect_mm and page are rejected" )
  {
    Json::Value spec = minimalSpec( "robustness-test" );
    // rect_mm with NaN
    spec["map_frames"][0]["rect_mm"][0] = std::numeric_limits<double>::quiet_NaN();
    auto problems = validateMapSpec( spec );
    REQUIRE_FALSE( problems.empty() );

    // rect_mm with Infinity
    spec["map_frames"][0]["rect_mm"][0] = std::numeric_limits<double>::infinity();
    problems = validateMapSpec( spec );
    REQUIRE_FALSE( problems.empty() );

    // page width_mm as string or NaN
    Json::Value specPage = minimalSpec( "robustness-page" );
    specPage["page"]["width_mm"] = "invalid_string";
    problems = validateMapSpec( specPage );
    REQUIRE_FALSE( problems.empty() );

    specPage["page"]["width_mm"] = std::numeric_limits<double>::quiet_NaN();
    problems = validateMapSpec( specPage );
    REQUIRE_FALSE( problems.empty() );
  }

  SECTION( "Deeply chained conditions exceeding nesting budget are rejected" )
  {
    std::string chainedExpr = "a == 1";
    for ( int i = 0; i < 10; ++i )
      chainedExpr += " or b == " + std::to_string( i );
    std::vector<std::string> problems;
    bool ok = validateConditionSyntax( chainedExpr, &problems );
    CHECK_FALSE( ok );
    REQUIRE_FALSE( problems.empty() );
  }

  SECTION( "Multi-dot numbers are tokenizer problems, not silent truncation (R16-D T08 WP-9)" )
  {
    // "1.2.3" used to tokenize as the number 1.2 (std::stod parses the
    // prefix) and the ".3" tail vanished — a typo compared equal to nothing.
    std::vector<std::string> problems;
    CHECK_FALSE( validateConditionSyntax( "a > 1.2.3", &problems ) );
    REQUIRE_FALSE( problems.empty() );
    bool mentionsToken = false;
    for ( const auto &problem : problems )
      mentionsToken = mentionsToken || problem.find( "1.2.3" ) != std::string::npos;
    CHECK( mentionsToken );

    bool value = true;
    std::string error;
    CHECK_FALSE( evaluateCondition( "v == 1.2.3", Json::Value( Json::objectValue ), &value,
                                    &error ) );
    CHECK_FALSE( value );
    CHECK_FALSE( error.empty() );

    // Well-formed decimals (and negatives) still tokenize (regression).
    std::vector<std::string> clean;
    CHECK( validateConditionSyntax( "a >= 1.25 and b < -0.5", &clean ) );
    CHECK( clean.empty() );
    Json::Value context( Json::objectValue );
    context["a"] = 1.25;
    context["b"] = -1.0;
    bool okValue = false;
    REQUIRE( evaluateCondition( "a >= 1.25 and b < -0.5", context, &okValue, &error ) );
    CHECK( okValue );
  }

  SECTION( "Compiler rejects non-string expressions (WP-03)" )
  {
    Json::Value spec = minimalSpec( "expr-type-test" );
    Json::Value title( Json::objectValue );
    title["id"] = "title-bad-expr";
    Json::Value rect( Json::arrayValue );
    rect.append( 12 ); rect.append( 12 ); rect.append( 100 ); rect.append( 10 );
    title["rect_mm"] = rect;
    title["text"] = "Sample Title";
    title["expression"] = 12345; // integer instead of string
    spec["titles"].append( title );

    QString error;
    QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
    INFO( "error was: " << error.toStdString() );
    CHECK( layout == nullptr );
    CHECK( error.contains( "expression must be a" ) );

    // Also test unparseable QgsExpression
    spec["titles"][0]["expression"] = "1 + * 2";
    error.clear();
    layout = MapSpecCompiler::compile( spec, &error );
    CHECK( layout == nullptr );
    CHECK( error.contains( "invalid expression" ) );
  }

  SECTION( "Composition solver safely handles non-finite rects and bounds (WP-04)" )
  {
    Json::Value spec = minimalSpec( "composition-nan-test" );
    Json::Value label( Json::objectValue );
    label["id"] = "label-nan";
    Json::Value rect( Json::arrayValue );
    rect.append( std::numeric_limits<double>::quiet_NaN() );
    rect.append( 10 );
    rect.append( 50 );
    rect.append( 10 );
    label["rect_mm"] = rect;
    spec["labels"].append( label );

    sicnu::agent::cartography::CompositionResult result;
    REQUIRE_NOTHROW( result = sicnu::agent::cartography::resolveComposition( spec, 12.0 ) );

    // Test anchor with NaN and negative margins in composition solver
    Json::Value anchorItem( Json::objectValue );
    anchorItem["id"] = "anchor-nan";
    Json::Value anchor( Json::objectValue );
    anchor["edge"] = "top-left";
    anchor["margin_mm"] = std::numeric_limits<double>::quiet_NaN();
    anchorItem["anchor"] = anchor;
    Json::Value rect2( Json::arrayValue );
    rect2.append( 0 ); rect2.append( 0 ); rect2.append( 20 ); rect2.append( 20 );
    anchorItem["rect_mm"] = rect2;
    spec["labels"].append( anchorItem );

    REQUIRE_NOTHROW( result = sicnu::agent::cartography::resolveComposition( spec, 12.0 ) );
    // anchorMargin safely defaulted to marginDefaultMm (12.0) instead of poisoning rect with NaN
    CHECK( spec["labels"][1]["rect_mm"][0].asDouble() == 12.0 );
    CHECK( spec["labels"][1]["rect_mm"][1].asDouble() == 12.0 );
  }
}

// ---------------------------------------------------------------------------
// R14-D WP-C+D — validation honesty + cartography rendering correctness.
// ---------------------------------------------------------------------------

namespace {

bool anyProblemMentions( const std::vector<std::string> &problems, const std::string &needle )
{
  for ( const auto &problem : problems )
    if ( problem.find( needle ) != std::string::npos )
      return true;
  return false;
}

Json::Value rectOf( double x, double y, double w, double h )
{
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  return rect;
}

} // namespace

TEST_CASE( "MapSpec validation rejects non-string map_ref on map-referencing collections (CARTO-2)",
           "[mapspec][validation][r14d]" )
{
  const char *const collections[] = { "legends", "north_arrows", "scale_bars", "charts",
                                      "colorbars", "grids" };
  for ( const char *collection : collections )
  {
    Json::Value spec = minimalSpec( std::string( "mapref-type-" ) + collection );
    Json::Value item( Json::objectValue );
    item["id"] = std::string( collection ) + "-bad";
    item["map_ref"] = 42; // numeric, not a string
    if ( std::string( collection ) == "grids" )
      item["interval"] = 0.5; // keep the other grid rules quiet
    spec[collection].append( item );
    const auto problems = validateMapSpec( spec );
    INFO( "collection: " << collection );
    CHECK( anyProblemMentions( problems, "map_ref must be a string" ) );
  }

  // The compiler must never see the raw value: compile runs validation and
  // refuses with a located, typed error instead of throwing out of
  // asString() (jsoncpp is compiled with exceptions).
  Json::Value spec = minimalSpec( "mapref-throw" );
  Json::Value grid( Json::objectValue );
  grid["id"] = "grid-1";
  grid["map_ref"] = 42;
  grid["interval"] = 0.5;
  spec["grids"].append( grid );
  QString error;
  QgsPrintLayout *layout = nullptr;
  REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &error ) );
  CHECK( layout == nullptr );
  CHECK( error.contains( QStringLiteral( "invalid MapSpec" ) ) );
  CHECK( error.contains( QStringLiteral( "map_ref must be a string" ) ) );
}

TEST_CASE( "upgradeMapSpec tolerates wrong-typed spec_version; validation reports it (CARTO-4)",
           "[mapspec][validation][migrate][r14d]" )
{
  Json::Value spec = minimalSpec( "version-typed" );

  // String version: no throw, no coercion — the doc passes through so the
  // subsequent validateMapSpec produces the typed VALIDATION_FAILED.
  spec["spec_version"] = "5";
  Json::Value upgraded;
  REQUIRE_NOTHROW( upgraded = upgradeMapSpec( spec ) );
  CHECK( upgraded["spec_version"].asString() == "5" );
  const auto stringProblems = validateMapSpec( upgraded );
  REQUIRE_FALSE( stringProblems.empty() );
  CHECK( anyProblemMentions( stringProblems, "spec_version" ) );

  // Float version: same contract.
  spec["spec_version"] = 5.5;
  REQUIRE_NOTHROW( upgraded = upgradeMapSpec( spec ) );
  CHECK( upgraded["spec_version"].asDouble() == Catch::Approx( 5.5 ) );
  CHECK_FALSE( validateMapSpec( upgraded ).empty() );

  // Integer versions still upgrade (regression).
  spec["spec_version"] = 3;
  REQUIRE_NOTHROW( upgraded = upgradeMapSpec( spec ) );
  CHECK( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  CHECK( validateMapSpec( upgraded ).empty() );
}

TEST_CASE( "MapSpec validation rejects malformed map frame extents (CARTO-6)",
           "[mapspec][validation][r14d]" )
{
  const auto extentProblems = []( const Json::Value &extent ) {
    Json::Value spec = minimalSpec( "extent-check" );
    spec["map_frames"][0]["extent"] = extent;
    return validateMapSpec( spec );
  };

  Json::Value shortExtent( Json::arrayValue );
  shortExtent.append( 116 );
  shortExtent.append( 39 );
  shortExtent.append( 117 );
  auto problems = extentProblems( shortExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "extent must be [xmin, ymin, xmax, ymax]" ) );

  Json::Value stringExtent( Json::arrayValue );
  stringExtent.append( "a" );
  stringExtent.append( "b" );
  stringExtent.append( "c" );
  stringExtent.append( "d" );
  problems = extentProblems( stringExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "extent entries must be numbers" ) );

  Json::Value nanExtent( Json::arrayValue );
  nanExtent.append( std::numeric_limits<double>::quiet_NaN() );
  nanExtent.append( 39 );
  nanExtent.append( 117 );
  nanExtent.append( 40 );
  problems = extentProblems( nanExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "finite" ) );

  Json::Value crossedExtent( Json::arrayValue );
  crossedExtent.append( 117 );
  crossedExtent.append( 40 );
  crossedExtent.append( 116 );
  crossedExtent.append( 39 );
  problems = extentProblems( crossedExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "xmin < xmax" ) );

  Json::Value degenerate( Json::arrayValue );
  degenerate.append( 5 );
  degenerate.append( 5 );
  degenerate.append( 5 );
  degenerate.append( 5 );
  problems = extentProblems( degenerate );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "xmin < xmax" ) );

  // --- R16-D T08 WP-3: inset extents share the map-frame contract ----------
  // The check used to compare the collection name against "insets" while the
  // declared collection is "inset_maps" — dead code: a crossed/NaN/string
  // inset extent passed validation silently.
  const auto insetExtentProblems = []( const Json::Value &extent ) {
    Json::Value spec = minimalSpec( "inset-extent-check" );
    Json::Value inset( Json::objectValue );
    inset["id"] = "inset-1";
    inset["rect_mm"] = rectOf( 210, 24, 60, 40 );
    inset["extent"] = extent;
    spec["inset_maps"].append( inset );
    return validateMapSpec( spec );
  };
  problems = insetExtentProblems( crossedExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "xmin < xmax" ) );

  problems = insetExtentProblems( nanExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "finite" ) );

  problems = insetExtentProblems( stringExtent );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "extent entries must be numbers" ) );

  Json::Value cleanInsetExtent( Json::arrayValue );
  cleanInsetExtent.append( 116.0 );
  cleanInsetExtent.append( 39.0 );
  cleanInsetExtent.append( 116.5 );
  cleanInsetExtent.append( 39.5 );
  CHECK( insetExtentProblems( cleanInsetExtent ).empty() );

  // The valid minimalSpec extent stays clean (regression).
  CHECK( validateMapSpec( minimalSpec( "extent-valid" ) ).empty() );
}

TEST_CASE( "Chart/colorbar string-typed fields are validated, not thrown (R16-D T08 WP-1)",
           "[mapspec][validation][r16d-t08]" )
{
  SECTION( "colorbar min/max labels must be strings (numeric min/max is the natural typo)" )
  {
    Json::Value spec = minimalSpec( "colorbar-typed" );
    Json::Value colorbar( Json::objectValue );
    colorbar["id"] = "colorbar-1";
    colorbar["rect_mm"] = rectOf( 12, 190, 80, 8 );
    colorbar["min"] = 0;
    colorbar["max"] = 100;
    spec["colorbars"].append( colorbar );

    const auto problems = validateMapSpec( spec );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "must be a string" ) );

    // The compiler refuses the document before any render — jsoncpp's
    // LogicError must never escape compile through renderColorbarToFile's
    // unguarded asString().
    QString compileError;
    QgsPrintLayout *layout = nullptr;
    REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &compileError ) );
    CHECK( layout == nullptr );
    CHECK( compileError.contains( QStringLiteral( "must be a string" ) ) );
  }

  SECTION( "chart binding.data[].label must be a string" )
  {
    Json::Value spec = minimalSpec( "chart-label-typed" );
    Json::Value chartItem( Json::objectValue );
    chartItem["id"] = "chart-1";
    chartItem["rect_mm"] = rectOf( 210, 24, 70, 60 );
    chartItem["chart"] = Json::Value( Json::objectValue );
    chartItem["chart"]["kind"] = "bar";
    chartItem["chart"]["binding"] = Json::Value( Json::objectValue );
    chartItem["chart"]["binding"]["mode"] = "inline";
    Json::Value point( Json::objectValue );
    point["label"] = 5; // numeric label — inlineData() used to throw here
    point["value"] = 3;
    chartItem["chart"]["binding"]["data"].append( point );
    spec["charts"].append( chartItem );
    CHECK( validateMapSpec( spec ).empty() ); // item-level shape is valid

    // The chart-level validator owns the typed report...
    const auto chartProblems =
      sicnu::agent::cartography::validateChartSpec( chartItem["chart"] );
    REQUIRE_FALSE( chartProblems.empty() );
    CHECK( anyProblemMentions( chartProblems, "label" ) );

    // ...and the render path degrades to a typed error instead of throwing.
    QString compileError;
    QgsPrintLayout *layout = nullptr;
    REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &compileError ) );
    CHECK( layout != nullptr );
  }

  SECTION( "review R16-D R1: numeric title / native-path strings degrade, never throw" )
  {
    Json::Value spec = minimalSpec( "chart-title-typed" );
    Json::Value chartItem( Json::objectValue );
    chartItem["id"] = "chart-3";
    chartItem["rect_mm"] = rectOf( 210, 180, 70, 60 );
    chartItem["chart"] = Json::Value( Json::objectValue );
    chartItem["chart"]["kind"] = "bar";
    chartItem["chart"]["title"] = 42; // numeric title — renderChartToFile used to throw
    chartItem["chart"]["binding"] = Json::Value( Json::objectValue );
    chartItem["chart"]["binding"]["mode"] = "inline";
    Json::Value point( Json::objectValue );
    point["label"] = "a";
    point["value"] = 1;
    chartItem["chart"]["binding"]["data"].append( point );
    spec["charts"].append( chartItem );
    QString compileError;
    QgsPrintLayout *layout = nullptr;
    REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &compileError ) );
    CHECK( layout != nullptr );
    QgsProject::instance()->layoutManager()->removeLayout( layout );
  }

  SECTION( "chart style.palette entries must be color strings" )
  {
    Json::Value spec = minimalSpec( "chart-palette-typed" );
    Json::Value chartItem( Json::objectValue );
    chartItem["id"] = "chart-2";
    chartItem["rect_mm"] = rectOf( 210, 100, 70, 60 );
    chartItem["chart"] = Json::Value( Json::objectValue );
    chartItem["chart"]["kind"] = "bar";
    chartItem["chart"]["binding"] = Json::Value( Json::objectValue );
    chartItem["chart"]["binding"]["mode"] = "inline";
    Json::Value point( Json::objectValue );
    point["label"] = "a";
    point["value"] = 1;
    chartItem["chart"]["binding"]["data"].append( point );
    Json::Value palette( Json::arrayValue );
    palette.append( 1 );
    palette.append( 2 );
    chartItem["chart"]["style"]["palette"] = palette;
    spec["charts"].append( chartItem );

    const auto chartProblems =
      sicnu::agent::cartography::validateChartSpec( chartItem["chart"] );
    REQUIRE_FALSE( chartProblems.empty() );
    CHECK( anyProblemMentions( chartProblems, "palette" ) );

    QString compileError;
    QgsPrintLayout *layout = nullptr;
    REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &compileError ) );
    CHECK( layout != nullptr );

    // The matrix ramp reads palette[0]/palette[last] raw: same contract.
    Json::Value matrixChart = chartItem["chart"];
    matrixChart["kind"] = "matrix";
    matrixChart.removeMember( "binding" );
    matrixChart["binding"] = Json::Value( Json::objectValue );
    matrixChart["binding"]["mode"] = "inline";
    matrixChart["binding"]["matrix"] = Json::Value( Json::objectValue );
    matrixChart["binding"]["matrix"]["labels"].append( "A" );
    matrixChart["binding"]["matrix"]["labels"].append( "B" );
    Json::Value row( Json::arrayValue );
    row.append( 1 );
    row.append( 2 );
    matrixChart["binding"]["matrix"]["rows"].append( row );
    Json::Value row2( Json::arrayValue );
    row2.append( 2 );
    row2.append( 1 );
    matrixChart["binding"]["matrix"]["rows"].append( row2 );
    matrixChart["style"]["palette"] = palette;
    const auto matrixProblems =
      sicnu::agent::cartography::validateChartSpec( matrixChart );
    REQUIRE_FALSE( matrixProblems.empty() );
    CHECK( anyProblemMentions( matrixProblems, "palette" ) );
    bool rendered = false;
    QString renderError;
    REQUIRE_NOTHROW(
      rendered = sicnu::agent::cartography::renderChartToFile(
        matrixChart, QStringLiteral( "/tmp/t08-matrix-palette.png" ), &renderError ) );
    CHECK_FALSE( rendered );
    CHECK( renderError.contains( QStringLiteral( "palette" ) ) );
  }
}

TEST_CASE( "Text furniture string-typed text is validated, not silently replaced (R18-D)",
           "[mapspec][validation][r18d]" )
{
  SECTION( "numeric title text is refused typed, not rendered as the placeholder" )
  {
    Json::Value spec = minimalSpec( "numeric-title-text" );
    Json::Value title( Json::objectValue );
    title["id"] = "title-1";
    title["rect_mm"] = rectOf( 12, 12, 180, 12 );
    title["text"] = 2024; // numeric — the natural way to write a year
    spec["titles"].append( title );

    const auto problems = validateMapSpec( spec );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "text" ) );

    // The compiler refuses the document before any render: the label setter
    // for a numeric text throws Json::LogicError inside LayoutService, which
    // used to be swallowed as an ignored property — leaving the placeholder
    // "Map title" on the sheet and skipping every property declared after
    // text.
    QString compileError;
    QgsPrintLayout *layout = nullptr;
    REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &compileError ) );
    CHECK( layout == nullptr );
    CHECK( compileError.contains( QStringLiteral( "text" ) ) );
  }

  SECTION( "labels / source_notes / annotations text: same contract" )
  {
    for ( const char *collection : { "labels", "source_notes", "annotations" } )
    {
      Json::Value spec = minimalSpec( std::string( "typed-text-" ) + collection );
      Json::Value item( Json::objectValue );
      item["id"] = "item-1";
      item["rect_mm"] = rectOf( 12, 40, 100, 10 );
      item["text"] = 7;
      spec[collection].append( item );
      const auto problems = validateMapSpec( spec );
      CHECK_FALSE( problems.empty() );
    }
  }
}

TEST_CASE( "Numeric legend title is refused typed instead of silently mislinking the legend (R18-D)",
           "[mapspec][validation][r18d]" )
{
  // Two map frames; the legend declares map_ref to the second and a numeric
  // title. The title property applies before linked_map in LayoutService,
  // so a throwing title setter used to skip linked_map — the legend silently
  // bound to the reference map (frame 1) and mirrored the wrong layer set.
  Json::Value spec = minimalSpec( "legend-title-typed" );
  const std::string secondFrameId = appendMapSpecItem( spec, "map_frames", Json::Value() );
  REQUIRE( secondFrameId != spec["map_frames"][0].get( "id", "" ).asString() );
  Json::Value &frame2 = spec["map_frames"][1];
  frame2["rect_mm"] = rectOf( 110, 60, 60, 60 );
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 116.5 );
  extent.append( 39.5 );
  frame2["extent"] = extent;

  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  legend["rect_mm"] = rectOf( 210, 30, 60, 70 );
  legend["map_ref"] = secondFrameId;
  legend["title"] = 42; // numeric title — the natural typo for a year/code
  spec["legends"].append( legend );

  const auto problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "title" ) );

  // The compiler refuses the document instead of delivering a legend that
  // mirrors the wrong map.
  QString error;
  QgsPrintLayout *layout = nullptr;
  REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &error ) );
  CHECK( layout == nullptr );
  CHECK( error.contains( QStringLiteral( "title" ) ) );
}

TEST_CASE( "MapSpec validation rejects non-finite font sizes and bad grid declarations (CARTO-7/CARTO-11)",
           "[mapspec][validation][r14d]" )
{
  SECTION( "font.size_pt needs explicit finiteness (NaN passes <= 0 guards)" )
  {
    const auto fontProblems = []( const Json::Value &sizePt ) {
      Json::Value spec = minimalSpec( "font-size-check" );
      Json::Value label( Json::objectValue );
      label["id"] = "label-font";
      label["rect_mm"] = rectOf( 12, 180, 80, 8 );
      label["font"] = Json::Value( Json::objectValue );
      label["font"]["size_pt"] = sizePt;
      spec["labels"].append( label );
      return validateMapSpec( spec );
    };
    auto problems = fontProblems( Json::Value( std::numeric_limits<double>::quiet_NaN() ) );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "size_pt" ) );
    problems = fontProblems( Json::Value( -5.0 ) );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "size_pt" ) );
    problems = fontProblems( Json::Value( "big" ) );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "size_pt" ) );
    CHECK( fontProblems( Json::Value( 18.0 ) ).empty() );
  }

  SECTION( "grids need a map_ref and a positive finite interval" )
  {
    const auto gridProblems = []( const Json::Value &grid ) {
      Json::Value spec = minimalSpec( "grid-check" );
      spec["grids"].append( grid );
      return validateMapSpec( spec );
    };
    Json::Value noRef( Json::objectValue );
    noRef["id"] = "grid-1";
    noRef["interval"] = 0.5;
    auto problems = gridProblems( noRef );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "missing map_ref" ) );

    Json::Value negative( Json::objectValue );
    negative["id"] = "grid-2";
    negative["map_ref"] = "map-1";
    negative["interval"] = -5.0;
    problems = gridProblems( negative );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "interval" ) );

    Json::Value nanInterval( Json::objectValue );
    nanInterval["id"] = "grid-3";
    nanInterval["map_ref"] = "map-1";
    nanInterval["interval"] = std::numeric_limits<double>::quiet_NaN();
    problems = gridProblems( nanInterval );
    REQUIRE_FALSE( problems.empty() );
    CHECK( anyProblemMentions( problems, "interval" ) );

    Json::Value valid( Json::objectValue );
    valid["id"] = "grid-4";
    valid["map_ref"] = "map-1";
    valid["interval"] = 0.5;
    CHECK( gridProblems( valid ).empty() );
  }
}

TEST_CASE( "Scale bar units compile to the declared QGIS distance unit (CARTO-3)",
           "[mapspec][compiler][r14d]" )
{
  const std::vector<std::pair<std::string, Qgis::DistanceUnit>> cases = {
    { "m", Qgis::DistanceUnit::Meters },
    { "km", Qgis::DistanceUnit::Kilometers },
    { "mi", Qgis::DistanceUnit::Miles },
    { "ft", Qgis::DistanceUnit::Feet },
    { "nm", Qgis::DistanceUnit::NauticalMiles },
    { "deg", Qgis::DistanceUnit::Degrees },
  };
  for ( const auto &entry : cases )
  {
    Json::Value spec = minimalSpec( "units-map-" + entry.first );
    Json::Value scaleBar( Json::objectValue );
    scaleBar["id"] = "scale_bar-1";
    scaleBar["rect_mm"] = rectOf( 12, 190, 60, 8 );
    scaleBar["map_ref"] = "map-1";
    scaleBar["units"] = entry.first;
    spec["scale_bars"].append( scaleBar );
    QString error;
    QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
    INFO( "unit: " << entry.first << " error: " << error.toStdString() );
    REQUIRE( layout != nullptr );
    auto *bar = qobject_cast<QgsLayoutItemScaleBar *>(
      sicnu::agent::layout_tools::LayoutService::instance().findItem( layout,
                                                                      QStringLiteral( "scale_bar-1" ) ) );
    REQUIRE( bar != nullptr );
    CHECK( bar->units() == entry.second );
    CHECK( bar->unitLabel().toStdString() == entry.first );
  }

  // Unknown unit vocabulary: typed validation refusal, never a silent guess.
  Json::Value spec = minimalSpec( "units-map-unknown" );
  Json::Value scaleBar( Json::objectValue );
  scaleBar["id"] = "scale_bar-1";
  scaleBar["rect_mm"] = rectOf( 12, 190, 60, 8 );
  scaleBar["map_ref"] = "map-1";
  scaleBar["units"] = "furlongs";
  spec["scale_bars"].append( scaleBar );
  const auto problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "not a supported distance unit" ) );
  QString error;
  QgsPrintLayout *layout = nullptr;
  REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &error ) );
  CHECK( layout == nullptr );
  CHECK( error.contains( QStringLiteral( "invalid MapSpec" ) ) );
}

TEST_CASE( "Declared legend classes populate manual multi-column legends (CARTO-10)",
           "[mapspec][compiler][r14d]" )
{
  Json::Value spec = minimalSpec( "legend-classes" );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  legend["rect_mm"] = rectOf( 210, 30, 60, 70 );
  legend["map_ref"] = "map-1";
  legend["columns"] = 2;
  Json::Value classes( Json::arrayValue );
  classes.append( "water" );
  classes.append( "forest" );
  legend["classes"] = classes;
  spec["legends"].append( legend );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  INFO( "error: " << error.toStdString() );
  REQUIRE( layout != nullptr );
  auto *legendItem = qobject_cast<QgsLayoutItemLegend *>(
    sicnu::agent::layout_tools::LayoutService::instance().findItem( layout,
                                                                    QStringLiteral( "legend-1" ) ) );
  REQUIRE( legendItem != nullptr );
  CHECK( legendItem->columnCount() == 2 );
  // Declared classes drive the content: the manual model carries one entry
  // per class (rendered through a session-owned categorized layer).
  legendItem->refresh();
  REQUIRE( legendItem->model() != nullptr );
  REQUIRE( legendItem->model()->rootGroup() != nullptr );
  const auto layers = legendItem->model()->rootGroup()->findLayers();
  CHECK( layers.size() == 1 );
  if ( !layers.isEmpty() )
    CHECK( layers.first()->layer() != nullptr );
}

TEST_CASE( "MapSpec compiler refuses unresolvable frame layer references (CARTO-5)",
           "[mapspec][compiler][r14d]" )
{
  QgsVectorLayer *resolvable = new QgsVectorLayer(
    QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "carto5-layer" ),
    QStringLiteral( "memory" ) );
  const QString resolvableId = QgsProject::instance()->addMapLayer( resolvable )->id();
  struct LayerCleanup
  {
    QString id;
    ~LayerCleanup() { QgsProject::instance()->removeMapLayer( id ); }
  } cleanup{ resolvableId };

  Json::Value spec = minimalSpec( "layers-partial" );
  spec["map_frames"][0]["layers"].append( "carto5-layer" );
  spec["map_frames"][0]["layers"].append( "definitely-not-a-project-layer" );
  QString error;
  QgsPrintLayout *layout = nullptr;
  REQUIRE_NOTHROW( layout = MapSpecCompiler::compile( spec, &error ) );
  CHECK( layout == nullptr );
  CHECK( error.contains( QStringLiteral( "unresolvable reference" ) ) );
  CHECK( error.contains( QStringLiteral( "definitely-not-a-project-layer" ) ) );
  // The failed compile leaves no half-built layout registered.
  CHECK( sicnu::agent::layout_tools::LayoutService::instance().findLayout(
           QStringLiteral( "layers-partial" ) ) == nullptr );

  // A fully-resolvable list still compiles (regression), and a declarative
  // layers[] id is a legal ref even when no project layer backs it (the
  // MAP_LAYER_UNREFERENCED repair attaches such ids to frames).
  Json::Value good = minimalSpec( "layers-full" );
  good["map_frames"][0]["layers"].append( "carto5-layer" );
  Json::Value declared( Json::objectValue );
  declared["id"] = "layer-declarative";
  good["layers"].append( declared );
  good["map_frames"][0]["layers"].append( "layer-declarative" );
  error.clear();
  QgsPrintLayout *compiled = nullptr;
  REQUIRE_NOTHROW( compiled = MapSpecCompiler::compile( good, &error ) );
  CHECK( compiled != nullptr );

  // R14-D review P2-4: the declaration-only ref is LEGAL, but the compile
  // must record it so produce can warn that the frame renders without it —
  // a fully-backed list carries no such marker.
  const QVariant unbacked =
    compiled->customProperty( QStringLiteral( "sicnu/declaredUnbackedLayerRefs" ) );
  REQUIRE( unbacked.isValid() );
  CHECK( unbacked.toStringList().contains( QStringLiteral( "layer-declarative" ) ) );
  CHECK_FALSE( unbacked.toStringList().contains( QStringLiteral( "carto5-layer" ) ) );
}

TEST_CASE( "MapSpec validation rejects unknown token sets and malformed style overrides (CARTO-16)",
           "[mapspec][validation][r14d]" )
{
  Json::Value spec = minimalSpec( "tokens-check" );
  spec["style"]["token_set"] = "no-such-token-set";
  auto problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "not a registered token set" ) );

  // A registered id stays valid (the shipped default set).
  spec["style"]["token_set"] = "scientific-light";
  CHECK( validateMapSpec( spec ).empty() );
  spec["style"].removeMember( "token_set" );

  spec["style"]["overrides"]["spacing"]["margin_mm"] = -5;
  problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "style.overrides.spacing.margin_mm" ) );

  spec["style"]["overrides"]["spacing"]["margin_mm"] =
    std::numeric_limits<double>::quiet_NaN();
  problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "style.overrides.spacing.margin_mm" ) );

  spec["style"].removeMember( "overrides" );
  spec["style"]["overrides"]["typography"]["styles"]["body"]["size_pt"] = -1;
  problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( anyProblemMentions( problems, "style.overrides.typography.styles.body.size_pt" ) );

  // A well-formed override set stays clean.
  spec["style"].removeMember( "overrides" );
  spec["style"]["overrides"]["spacing"]["margin_mm"] = 8;
  CHECK( validateMapSpec( spec ).empty() );
}
