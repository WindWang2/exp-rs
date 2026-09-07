// tests/test_cartography_library.cpp
// Design System 4.0 Milestone B — component schema v2 (variants, defaults,
// data bindings, compatibility), resolution precedence, and the catalog
// drift test: every shipped component descriptor must instantiate and
// compile into a layout.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/design_tokens.h"
#include "agent/cartography/registry.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgsapplication.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QString>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value specWithFrame( const std::string &layoutName )
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
  return spec;
}

/// Completes a component-driven item into something the compiler accepts:
/// the drift test supplies only the live data a component cannot know
/// (sample chart data, geometry).
void completeDriftItem( Json::Value &item, const std::string &collection )
{
  if ( item.isMember( "rect_mm" ) )
    return; // descriptors with explicit rects are complete
  Json::Value rect( Json::arrayValue );
  rect.append( 214 );
  rect.append( 40 );
  rect.append( 70 );
  rect.append( 50 );
  const bool needsRect = collection != "grids" && collection != "constraints";
  if ( needsRect )
    item["rect_mm"] = rect;
  // Text furniture needs live content the component cannot know.
  if ( ( collection == "titles" || collection == "labels" ||
         collection == "source_notes" || collection == "annotations" ) &&
       !item.isMember( "text" ) )
    item["text"] = "Drift check text";
  if ( collection == "legends" && !item.isMember( "map_ref" ) )
    item["map_ref"] = "map-1";
  if ( collection == "scale_bars" && !item.isMember( "map_ref" ) )
    item["map_ref"] = "map-1";
  if ( collection == "north_arrows" && !item.isMember( "map_ref" ) )
    item["map_ref"] = "map-1";
  if ( collection == "grids" )
    item["map_ref"] = "map-1";
  if ( collection == "charts" && item.isMember( "chart" ) )
  {
    Json::Value &chart = item["chart"];
    if ( !chart.isMember( "binding" ) || !chart["binding"].isObject() )
      chart["binding"] = Json::Value( Json::objectValue );
    if ( !chart["binding"].isMember( "mode" ) )
      chart["binding"]["mode"] = "inline";
    const std::string kind = chart.get( "kind", "bar" ).asString();
    if ( chart["binding"]["mode"].asString() == "inline" && kind == "matrix" &&
         !chart["binding"].isMember( "matrix" ) )
    {
      chart["binding"]["matrix"]["labels"] = Json::Value( Json::arrayValue );
      chart["binding"]["matrix"]["labels"].append( "water" );
      chart["binding"]["matrix"]["labels"].append( "forest" );
      Json::Value row( Json::arrayValue );
      row.append( 42 );
      row.append( 1 );
      chart["binding"]["matrix"]["rows"] = Json::Value( Json::arrayValue );
      chart["binding"]["matrix"]["rows"].append( row );
      Json::Value row2( Json::arrayValue );
      row2.append( 3 );
      row2.append( 88 );
      chart["binding"]["matrix"]["rows"].append( row2 );
    }
    else if ( chart["binding"]["mode"].asString() == "inline" &&
              ( !chart["binding"].isMember( "data" ) || chart["binding"]["data"].empty() ) )
    {
      Json::Value data( Json::arrayValue );
      Json::Value point( Json::objectValue );
      point["label"] = "a";
      point["value"] = 12.0;
      data.append( point );
      Json::Value point2( Json::objectValue );
      point2["label"] = "b";
      point2["value"] = 30.0;
      data.append( point2 );
      chart["binding"]["data"] = data;
    }
  }
}

} // namespace

TEST_CASE( "Component schema v2 validation", "[cartography][components][v2]" )
{
  // Good v2 descriptor with variants, defaults, bindings.
  Json::Value good( Json::objectValue );
  good["id"] = "test/variant-component";
  good["version"] = 1;
  good["category"] = "legend";
  good["parameters"]["title"] = "图例";
  good["defaults"]["columns"] = 1;
  Json::Value variant( Json::objectValue );
  variant["id"] = "two-col";
  variant["parameters"]["columns"] = 2;
  good["variants"].append( variant );
  Json::Value binding( Json::objectValue );
  binding["name"] = "renderer";
  binding["source"] = "map_ref.renderer";
  good["data_bindings"].append( binding );
  good["compatibility"]["requires_map_frame"] = true;
  QString error;
  CHECK( ComponentRegistry::instance().registerComponent( good, &error ) );

  // Duplicate variant ids rejected.
  Json::Value dup = good;
  dup["id"] = "test/dup-variants";
  dup["variants"].append( variant );
  CHECK_FALSE( ComponentRegistry::instance().registerComponent( dup, &error ) );
  CHECK( error.contains( QLatin1String( "duplicate" ) ) );

  // Malformed data_bindings rejected.
  Json::Value badBindings = good;
  badBindings["id"] = "test/bad-bindings";
  badBindings["data_bindings"][0] = "not-an-object";
  CHECK_FALSE( ComponentRegistry::instance().registerComponent( badBindings, &error ) );

  // Unknown category still rejected (v1 contract preserved).
  Json::Value badCategory = good;
  badCategory["id"] = "test/bad-category";
  badCategory["category"] = "rocket";
  CHECK_FALSE( ComponentRegistry::instance().registerComponent( badCategory, &error ) );
}

TEST_CASE( "resolveComponent applies variant parameters and defaults",
           "[cartography][components][variants]" )
{
  const Json::Value plain = resolveComponent( QLatin1String( "scale-bar/single" ) );
  REQUIRE( !plain.isNull() );
  CHECK( plain["parameters"]["style"].asString() == "Single Box" );

  const Json::Value ticks = resolveComponent( QLatin1String( "scale-bar/single" ),
                                              QLatin1String( "line-ticks-down" ) );
  REQUIRE( !ticks.isNull() );
  CHECK( ticks["parameters"]["style"].asString() == "Line Ticks Down" );

  CHECK( resolveComponent( QLatin1String( "scale-bar/single" ),
                           QLatin1String( "no-such-variant" ) )
           .isNull() );
  CHECK( resolveComponent( QLatin1String( "no-such-component" ) ).isNull() );

  // title/main compact variant lowers the parameterized font size.
  const Json::Value compact = resolveComponent( QLatin1String( "title/main" ),
                                                QLatin1String( "compact" ) );
  REQUIRE( !compact.isNull() );
  CHECK( compact["parameters"]["font"]["size_pt"].asDouble() == Catch::Approx( 16.0 ) );
}

TEST_CASE( "applyComponentDefaults follows the documented precedence chain",
           "[cartography][components][precedence]" )
{
  // item > variant parameters > component defaults.
  Json::Value item( Json::objectValue );
  Json::Value reference( Json::objectValue );
  reference["id"] = "scale-bar/single";
  reference["variant"] = "line-ticks-down";
  item["source_component"] = reference;
  item["units"] = "m"; // explicit field must win over variant parameters

  QString error;
  REQUIRE( applyComponentDefaults( item, &error ) );
  CHECK( item["style"].asString() == "Line Ticks Down" ); // from the variant
  CHECK( item["units"].asString() == "m" );               // explicit wins
  CHECK( item["units_per_segment"].asDouble() == Catch::Approx( 2.0 ) ); // base default

  // Unknown references fail with a reason and leave the item untouched.
  Json::Value unknown( Json::objectValue );
  unknown["source_component"] = "no/such";
  CHECK_FALSE( applyComponentDefaults( unknown, &error ) );
  CHECK( error.contains( QLatin1String( "unknown component" ) ) );

  // Plain id strings and grid intervals resolve into item-shaped fields.
  Json::Value grid( Json::objectValue );
  grid["source_component"] = "grid/graticule";
  REQUIRE( applyComponentDefaults( grid, &error ) );
  CHECK( grid["interval"].asDouble() == Catch::Approx( 1.0 ) );
}

TEST_CASE( "Template slot content outranks component defaults",
           "[cartography][components][precedence]" )
{
  // Instantiation merges slot content into the item BEFORE component
  // resolution, so a slot draft beats generic component styling (ADR 0130).
  // Synthetic template with a controlled collision: content says columns 3,
  // the component's single-column defaults say columns 1.
  Json::Value tmpl( Json::objectValue );
  tmpl["id"] = "precedence-probe";
  Json::Value slot( Json::objectValue );
  slot["role"] = "legend.primary";
  slot["accepts"] = "legends";
  Json::Value rect( Json::arrayValue );
  rect.append( 210 );
  rect.append( 30 );
  rect.append( 70 );
  rect.append( 80 );
  slot["rect_mm"] = rect;
  Json::Value content( Json::objectValue );
  content["columns"] = 3;
  slot["content"] = content;
  slot["component"] = "legend/categorical";
  tmpl["slots"].append( slot );
  REQUIRE( TemplateRegistry::instance().registerTemplate( tmpl ) );

  QString error;
  Json::Value draft = TemplateRegistry::instance().instantiateTemplate(
    QLatin1String( "precedence-probe" ), Json::Value(), &error );
  REQUIRE( !draft.isNull() );
  REQUIRE( draft["legends"].size() == 1 );
  CHECK( draft["legends"][0]["columns"].asInt() == 3 ); // content wins
  CHECK( draft["legends"][0]["title"].asString() == "图例" ); // component default fills the rest
}

TEST_CASE( "Legacy frame_style constraint items stay valid",
           "[cartography][components][compat]" )
{
  // v1 templates attach frame decoration as constraints items with
  // kind "frame_style"; only solver-kind names are enum-validated.
  Json::Value spec = makeMapSpec( "frame-style-compat", Json::Value() );
  Json::Value constraint( Json::objectValue );
  constraint["id"] = "constraint-1";
  constraint["kind"] = "frame_style";
  Json::Value style( Json::objectValue );
  style["frame_width_mm"] = 0.5;
  constraint["style"] = style;
  spec["constraints"].append( constraint );
  CHECK( validateMapSpec( spec ).empty() );

  Json::Value typo( Json::objectValue );
  typo["id"] = "constraint-2";
  typo["kind"] = "algin";
  Json::Value ids( Json::arrayValue );
  ids.append( "a" );
  ids.append( "b" );
  typo["items"] = ids;
  spec["constraints"].append( typo );
  const auto problems = validateMapSpec( spec );
  REQUIRE_FALSE( problems.empty() );
  CHECK( problems.front().find( "unknown constraint kind" ) != std::string::npos );
}

TEST_CASE( "collectionForCategory covers every declared category",
           "[cartography][components]" )
{
  CHECK( collectionForCategory( "north-arrow" ) == "north_arrows" );
  CHECK( collectionForCategory( "map-frame" ) == "map_frames" );
  CHECK( collectionForCategory( "inset-map" ) == "inset_maps" );
  CHECK( collectionForCategory( "publication" ) == "labels" );
  CHECK( collectionForCategory( "frame" ) == "constraints" );
  CHECK( collectionForCategory( "no-such" ).empty() );
}

TEST_CASE( "Catalog drift: every shipped component validates and compiles",
           "[cartography][components][drift]")
{
  ComponentRegistry &registry = ComponentRegistry::instance();
  const Json::Value components = registry.components();
  REQUIRE( components.isArray() );
  CHECK( components.size() >= 40 ); // library expanded from the 10 seeds

  int compiled = 0;
  std::vector<std::string> failures;
  for ( const auto &descriptor : components )
  {
    const std::string id = descriptor["id"].asString();
    // 1. every descriptor validates against schema v2
    const auto problems = validateComponentDescriptor( descriptor );
    if ( !problems.empty() )
    {
      failures.push_back( id + ": " + problems.front() );
      continue;
    }
    // 2. every declared variant resolves
    if ( descriptor.isMember( "variants" ) )
      for ( const auto &variant : descriptor["variants"] )
        if ( resolveComponent( QString::fromStdString( id ),
                               QString::fromStdString( variant["id"].asString() ) )
               .isNull() )
          failures.push_back( id + ": variant '" + variant["id"].asString() +
                              "' does not resolve" );
    // 3. the component drives a compilable item
    const std::string category = descriptor["category"].asString();
    const std::string collection = collectionForCategory( category );
    if ( collection.empty() || collection == "constraints" )
      continue; // frame styles are consumed by the composition solver (M5)
    Json::Value spec = specWithFrame( "component-drift" );
    Json::Value item( Json::objectValue );
    item["source_component"] = id;
    if ( category != "map-frame" && collection != "inset_maps" )
      applyComponentDefaults( item, nullptr );
    completeDriftItem( item, collection );
    appendMapSpecItem( spec, collection, item );
    QString error;
    QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
    if ( !layout )
      failures.push_back( id + ": compile failed: " + error.toStdString() );
    else
      ++compiled;
    QgsProject::instance()->layoutManager()->clear();
  }
  for ( const auto &failure : failures )
    WARN( failure );
  CHECK( failures.empty() );
  CHECK( compiled >= 35 );
}
