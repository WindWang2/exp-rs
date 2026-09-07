// tests/test_cartography_quality.cpp
// Design System 4.0 Milestone E — the preflight rule catalog and the
// deterministic repair loop: every new rule fires on a broken spec, stays
// quiet on a clean one, and its repair converges without deleting content.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/cartography_tools.h"
#include "agent/cartography/design_tokens.h"
#include "agent/mapspec/mapspec.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <QDir>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value fullSpec( const std::string &layoutName )
{
  Json::Value spec = makeMapSpec( layoutName, Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
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
  spec["map_frames"].append( frame );

  Json::Value title( Json::objectValue );
  title["id"] = "title-1";
  title["text"] = "A reasonably short title";
  Json::Value titleRect( Json::arrayValue );
  titleRect.append( 12 );
  titleRect.append( 6 );
  titleRect.append( 200 );
  titleRect.append( 12 );
  title["rect_mm"] = titleRect;
  spec["titles"].append( title );

  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value legendRect( Json::arrayValue );
  legendRect.append( 210 );
  legendRect.append( 30 );
  legendRect.append( 70 );
  legendRect.append( 80 );
  legend["rect_mm"] = legendRect;
  legend["map_ref"] = "map-1";
  spec["legends"].append( legend );

  Json::Value scaleBar( Json::objectValue );
  scaleBar["id"] = "scalebar-1";
  Json::Value barRect( Json::arrayValue );
  barRect.append( 14 );
  barRect.append( 188 );
  barRect.append( 48 );
  barRect.append( 10 );
  scaleBar["rect_mm"] = barRect;
  scaleBar["map_ref"] = "map-1";
  spec["scale_bars"].append( scaleBar );

  Json::Value arrow( Json::objectValue );
  arrow["id"] = "arrow-1";
  arrow["semantic_role"] = "north_arrow.primary";
  Json::Value arrowRect( Json::arrayValue );
  arrowRect.append( 68 );
  arrowRect.append( 188 );
  arrowRect.append( 12 );
  arrowRect.append( 12 );
  arrow["rect_mm"] = arrowRect;
  arrow["map_ref"] = "map-1";
  spec["north_arrows"].append( arrow );

  Json::Value note( Json::objectValue );
  note["semantic_role"] = "source.primary";
  note["id"] = "note-1";
  note["text"] = "Source: bench";
  Json::Value noteRect( Json::arrayValue );
  noteRect.append( 120 );
  noteRect.append( 190 );
  noteRect.append( 130 );
  noteRect.append( 8 );
  note["rect_mm"] = noteRect;
  spec["source_notes"].append( note );
  return spec;
}

bool hasCode( const Json::Value &report, const std::string &code, const std::string &itemId = "" )
{
  for ( const auto &issue : report["issues"] )
  {
    if ( !issue.isObject() || issue["code"].asString() != code )
      continue;
    if ( itemId.empty() || issue["item_id"].asString() == itemId )
      return true;
  }
  return false;
}

} // namespace

TEST_CASE( "Text width estimator is deterministic and CJK-aware",
           "[cartography][quality][overflow]" )
{
  const double latin = estimateTextWidthMm( "Water 2024", 10.0 );
  CHECK( latin > 0 );
  // Equal glyph counts: full-width CJK ems must out-measure 0.55em Latin.
  CHECK( estimateTextWidthMm( "地表覆盖地地", 10.0 ) >
         estimateTextWidthMm( "aaaaaaaa", 10.0 ) ); // 8 CJK vs 8 narrow glyphs
  CHECK( estimateTextWidthMm( "Water 2024", 10.0 ) == Catch::Approx( latin ).epsilon( 0.0001 ) );
  // multi-line: width is the widest line, not the sum
  const double twoLines = estimateTextWidthMm( "short\nlonger line", 10.0 );
  CHECK( twoLines == Catch::Approx( estimateTextWidthMm( "longer line", 10.0 ) ) );
}

TEST_CASE( "Preflight rule catalog is complete and machine-readable",
           "[cartography][quality][catalog]" )
{
  const Json::Value catalog = preflightRuleCatalog();
  REQUIRE( catalog.isArray() );
  CHECK( catalog.size() >= 20 );
  for ( const auto &rule : catalog )
  {
    REQUIRE( rule.isObject() );
    CHECK_FALSE( rule["code"].asString().empty() );
    CHECK( ( rule["severity"].asString() == "error" || rule["severity"].asString() == "warning" ) );
    CHECK( rule["repairable"].isBool() );
  }
}

TEST_CASE( "Title overflow fires on oversized text and repair widens or shrinks",
           "[cartography][quality][overflow]" )
{
  Json::Value spec = fullSpec( "overflow-map" );
  Json::Value &title = spec["titles"][0];
  title["text"] = "An extraordinarily long map title that certainly cannot fit into a narrow band";
  title["rect_mm"][2] = 60; // narrow band

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_TITLE_OVERFLOW", "title-1" ) );

  const int repairs = repairMapSpec( spec, report );
  CHECK( repairs >= 1 );
  const Json::Value after = preflightMapSpec( spec );
  CHECK_FALSE( hasCode( after, "MAP_TITLE_OVERFLOW", "title-1" ) );
}

TEST_CASE( "Source note clipping is reported and repaired", "[cartography][quality][overflow]" )
{
  Json::Value spec = fullSpec( "clipping-map" );
  Json::Value &note = spec["source_notes"][0];
  note["text"] = "数据来源: 一个非常非常非常长的数据来源说明，用于测试裁剪检测行为";
  note["rect_mm"][2] = 40;
  note["rect_mm"][0] = 200;

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_SOURCE_NOTE_CLIPPING", "note-1" ) );
  repairMapSpec( spec, report );
  CHECK_FALSE( hasCode( preflightMapSpec( spec ), "MAP_SOURCE_NOTE_CLIPPING", "note-1" ) );
}

TEST_CASE( "Declared page margins are enforced and repairable",
           "[cartography][quality][margins]" )
{
  Json::Value spec = fullSpec( "margin-map" );
  spec["page"]["margin_mm"] = 20;
  // title sits at y=6 < margin 20
  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_MARGIN_VIOLATION", "title-1" ) );

  repairMapSpec( spec, report );
  const Json::Value after = preflightMapSpec( spec );
  CHECK_FALSE( hasCode( after, "MAP_MARGIN_VIOLATION", "title-1" ) );
  CHECK( spec["titles"][0]["rect_mm"][1].asDouble() >= 19.5 );

  // Without a declared margin the rule stays silent (no invented contract).
  Json::Value noMargin = fullSpec( "no-margin" );
  CHECK_FALSE( hasCode( preflightMapSpec( noMargin ), "MAP_MARGIN_VIOLATION" ) );
}

TEST_CASE( "Legend density grows the legend or adds columns",
           "[cartography][quality][density]" )
{
  Json::Value spec = fullSpec( "density-map" );
  Json::Value &legend = spec["legends"][0];
  legend["max_entries"] = 24; // needs ~8 + 24*4.5 = 116mm; rect has 80mm

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_LEGEND_DENSITY", "legend-1" ) );
  repairMapSpec( spec, report );
  // The legend grows downward inside the page margin.
  CHECK( spec["legends"][0]["rect_mm"][3].asDouble() > 100 );
  CHECK_FALSE( hasCode( preflightMapSpec( spec ), "MAP_LEGEND_DENSITY", "legend-1" ) );
}

TEST_CASE( "Duplicate furniture: identical duplicates removed, distinct kept",
           "[cartography][quality][duplicates]" )
{
  // Identical duplicate (modulo id) is removed by repair.
  Json::Value spec = fullSpec( "dup-map" );
  Json::Value arrow2 = spec["north_arrows"][0];
  arrow2["id"] = "arrow-2";
  spec["north_arrows"].append( arrow2 );
  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_DUPLICATE_FURNITURE", "arrow-2" ) );
  repairMapSpec( spec, report );
  CHECK( spec["north_arrows"].size() == 1 ); // duplicate removed
  CHECK( spec["north_arrows"][0]["id"].asString() == "arrow-1" );

  // Distinct content with the same role stays (never silently deleted).
  Json::Value two = fullSpec( "dup-distinct" );
  Json::Value note2 = two["source_notes"][0];
  note2["id"] = "note-2";
  note2["text"] = "Additional disclaimer note";
  two["source_notes"].append( note2 );
  const Json::Value report2 = preflightMapSpec( two );
  CHECK( hasCode( report2, "MAP_DUPLICATE_FURNITURE", "note-2" ) );
  repairMapSpec( two, report2 );
  CHECK( two["source_notes"].size() == 2 ); // content preserved
}

TEST_CASE( "Unknown component references are reported and stripped",
           "[cartography][quality][components]" )
{
  Json::Value spec = fullSpec( "unknown-component" );
  Json::Value &legend = spec["legends"][0];
  legend["source_component"] = "legend/does-not-exist";
  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_UNKNOWN_COMPONENT", "legend-1" ) );
  repairMapSpec( spec, report );
  CHECK( spec["legends"][0]["source_component"].isNull() );
  CHECK( spec["legends"][0]["map_ref"].asString() == "map-1" ); // content intact
}

TEST_CASE( "Unbalanced multi-map frames are reported and equalized",
           "[cartography][quality][frames]" )
{
  Json::Value spec = fullSpec( "unbalanced-map" );
  Json::Value frame2( Json::objectValue );
  frame2["id"] = "map-2";
  Json::Value rect2( Json::arrayValue );
  rect2.append( 12 );
  rect2.append( 60 );
  rect2.append( 80 );
  rect2.append( 60 ); // first frame is 160 tall — >15% deviation
  frame2["rect_mm"] = rect2;
  frame2["extent"] = spec["map_frames"][0]["extent"];
  spec["map_frames"].append( frame2 );

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_UNBALANCED_FRAMES", "map-2" ) );
  repairMapSpec( spec, report );
  CHECK( spec["map_frames"][1]["rect_mm"][3].asDouble() ==
         Catch::Approx( spec["map_frames"][0]["rect_mm"][3].asDouble() ) );
}

TEST_CASE( "Insets must sit inside a map frame; repair pins them to the corner",
           "[cartography][quality][insets]" )
{
  Json::Value spec = fullSpec( "inset-map" );
  Json::Value inset( Json::objectValue );
  inset["id"] = "inset-1";
  Json::Value insetRect( Json::arrayValue );
  insetRect.append( 240 );
  insetRect.append( 60 );
  insetRect.append( 50 );
  insetRect.append( 40 ); // outside the map frame (12..202 x 24..184)
  inset["rect_mm"] = insetRect;
  spec["inset_maps"].append( inset );

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_INSET_PLACEMENT", "inset-1" ) );
  repairMapSpec( spec, report );
  const Json::Value after = preflightMapSpec( spec );
  CHECK_FALSE( hasCode( after, "MAP_INSET_PLACEMENT", "inset-1" ) );
  // bottom-right corner of the first frame with a 4mm gap
  CHECK( spec["inset_maps"][0]["rect_mm"][0].asDouble() == Catch::Approx( 202 - 50 - 4 ) );
  CHECK( spec["inset_maps"][0]["rect_mm"][1].asDouble() == Catch::Approx( 184 - 40 - 4 ) );
}

TEST_CASE( "Invalid chart bindings are reported (non-repairable)",
           "[cartography][quality][bindings]" )
{
  Json::Value spec = fullSpec( "binding-map" );
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-1";
  Json::Value chartRect( Json::arrayValue );
  chartRect.append( 210 );
  chartRect.append( 130 );
  chartRect.append( 70 );
  chartRect.append( 50 );
  chart["rect_mm"] = chartRect;
  chart["chart"]["kind"] = "bar";
  chart["chart"]["binding"]["mode"] = "vector_expression"; // no layer bound
  spec["charts"].append( chart );
  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_INVALID_BINDING", "chart-1" ) );
  // Other rules still run on the same spec (the scale bar is present).
  CHECK( !hasCode( report, "MAP_MISSING_SCALE_BAR" ) );
}

TEST_CASE( "Unsatisfiable constraints surface as non-repairable findings",
           "[cartography][quality][constraints]" )
{
  Json::Value spec = fullSpec( "constraint-map" );
  Json::Value bad( Json::objectValue );
  bad["id"] = "constraint-1";
  bad["kind"] = "align";
  Json::Value ids( Json::arrayValue );
  ids.append( "title-1" );
  // Validation shape-checks align edge strings; only the solver
  // enum-enforces the value, so it surfaces as MAP_CONSTRAINT_UNSATISFIABLE.
  ids.append( "note-1" );
  bad["items"] = ids;
  bad["edge"] = "middle";
  spec["constraints"].append( bad );

  const Json::Value report = preflightMapSpec( spec );
  CHECK( hasCode( report, "MAP_CONSTRAINT_UNSATISFIABLE" ) );
}

TEST_CASE( "Overlapping furniture is reported exactly once per pair",
           "[cartography][quality][overlap]" )
{
  Json::Value spec = fullSpec( "overlap-once" );
  Json::Value stray( Json::objectValue );
  stray["id"] = "label-stray";
  stray["text"] = "stray";
  Json::Value strayRect( Json::arrayValue );
  strayRect.append( 210 );
  strayRect.append( 30 );
  strayRect.append( 30 );
  strayRect.append( 20 ); // inside legend-1's rect
  stray["rect_mm"] = strayRect;
  spec["labels"].append( stray );
  const Json::Value report = preflightMapSpec( spec );
  int count = 0;
  for ( const auto &issue : report["issues"] )
    if ( issue.isObject() && issue["code"].asString() == "MAP_OVERLAP" )
      ++count;
  CHECK( count == 1 ); // one pair, reported exactly once (stable order)
}

TEST_CASE( "Repair loop is deterministic and bounded", "[cartography][quality][determinism]" )
{
  Json::Value broken = fullSpec( "determinism-a" );
  Json::Value stray( Json::objectValue );
  stray["id"] = "label-stray";
  stray["text"] = "stray annotation";
  Json::Value strayRect( Json::arrayValue );
  strayRect.append( 400 );
  strayRect.append( 400 );
  strayRect.append( 40 );
  strayRect.append( 10 );
  stray["rect_mm"] = strayRect;
  broken["annotations"].append( stray );
  Json::Value &title = broken["titles"][0];
  title["text"] = "Way too long title text for the rect it is given here";
  title["rect_mm"][2] = 80;

  Json::Value a = broken;
  Json::Value b = broken;
  Json::Value qualityA = preflightMapSpec( a );
  Json::Value qualityB = preflightMapSpec( b );
  int iterations = 0;
  while ( iterations < 10 && !qualityA["passed"].asBool() )
  {
    if ( repairMapSpec( a, qualityA ) == 0 )
      break;
    ++iterations;
    qualityA = preflightMapSpec( a );
    repairMapSpec( b, qualityB );
    qualityB = preflightMapSpec( b );
  }
  CHECK( qualityA["passed"].asBool() );
  CHECK( a.toStyledString() == b.toStyledString() ); // byte-identical convergence
}

TEST_CASE( "repair tool clamps max_iterations (1..10)", "[cartography][quality][tool]" )
{
  registerCartographyTools();
  const auto tool = sicnu::agent::spatial_tools::SpatialToolRegistry::instance().find(
    "cartography:repair" );
  REQUIRE( tool.has_value() );

  Json::Value spec = fullSpec( "tool-clamp" );
  Json::Value stray( Json::objectValue );
  stray["id"] = "label-stray";
  stray["text"] = "stray annotation";
  Json::Value strayRect( Json::arrayValue );
  strayRect.append( 400 );
  strayRect.append( 400 );
  strayRect.append( 40 );
  strayRect.append( 10 );
  stray["rect_mm"] = strayRect;
  spec["annotations"].append( stray );

  Json::Value input( Json::objectValue );
  input["mapspec"] = spec;
  input["max_iterations"] = 99;
  const auto result = ( *tool )->execute( input );
  REQUIRE( result.success );
  const Json::Value &out = result.output;
  CHECK( out.isObject() );
  CHECK( out["iterations"].asInt() <= 10 );
  CHECK( out["quality"].isObject() );
}
