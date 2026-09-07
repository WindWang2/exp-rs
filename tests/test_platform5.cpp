// tests/test_platform5.cpp
//
// Knowledge Platform 5.0 — solution/style registries, bounded conditions,
// MapSpec v3 surfaces (relative constraints, conditional pruning, locator
// descriptors, atlas v2 keys), the full-catalog lint, and the recipe/solution
// catalog growth gates.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/cartography/design_tokens.h"
#include "agent/cartography/quality.h"
#include "agent/cartography/registry.h"
#include "agent/cartography/solution_registry.h"
#include "agent/cartography/style_compiler.h"
#include "agent/cartography/style_spec.h"
#include "agent/harness/agent_plan.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"

#include <qgsrasterrenderer.h>

#include <QDir>
#include <QFile>
#include <QStringList>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>

#include <qgslayout.h>
#include <qgslayoutmanager.h>
#include <qgslayoutpagecollection.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include "agent/mapspec/mapspec_compiler.h"

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;
using sicnu::agent::harness::RecipeCatalog;
using sicnu::agent::harness::isKnownIntent;

namespace {

Json::Value readJsonFile( const std::string &path )
{
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    return Json::Value();
  const QByteArray bytes = file.readAll();
  Json::Value out;
  Json::Reader reader;
  reader.parse( std::string( bytes.constData(), bytes.size() ), out );
  return out;
}

std::filesystem::path sourceDir() { return std::filesystem::path( CMAKE_SOURCE_DIR ); }

Json::Value makeMinimalSolution( const std::string &id = "solution.test.minimal" )
{
  Json::Value doc( Json::objectValue );
  doc["schema_version"] = "1.0";
  doc["kind"] = "solution_template";
  doc["id"] = id;
  doc["version"] = 1;
  doc["title"] = "Test solution";
  Json::Value tasks( Json::arrayValue );
  tasks.append( "water" );
  doc["tasks"] = tasks;
  Json::Value modalities( Json::arrayValue );
  modalities.append( "optical" );
  doc["modalities"] = modalities;
  Json::Value contracts( Json::arrayValue );
  Json::Value primary( Json::objectValue );
  primary["name"] = "primary";
  primary["kind"] = "raster";
  primary["required"] = true;
  contracts.append( primary );
  doc["input_contracts"] = contracts;
  doc["analysis_recipe"] = "harness.optical_ndvi";
  doc["map_template"] = "water-flood-a4l";
  return doc;
}

Json::Value makeSpec( double pageW = 297.0, double pageH = 210.0 )
{
  Json::Value spec( Json::objectValue );
  spec["schema_version"] = "1.0";
  spec["kind"] = "map_spec";
  spec["spec_version"] = kMapSpecCurrentVersion;
  spec["layout_name"] = "platform5-test";
  spec["page"]["width_mm"] = pageW;
  spec["page"]["height_mm"] = pageH;
  for ( int i = 0; i < kCollectionCount; ++i )
    spec[kCollections[i]] = Json::Value( Json::arrayValue );
  return spec;
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

} // namespace

// ---------------------------------------------------------------------------
// StyleSpec module (Milestone A/E)
// ---------------------------------------------------------------------------

TEST_CASE( "StyleSpec validation accepts the shipped semantic styles", "[platform5][style]" )
{
  StyleRegistry &registry = StyleRegistry::instance();
  registry.setDirectory( QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  registry.reload();
  const Json::Value styles = registry.styles();
  REQUIRE( styles.isArray() );
  CHECK( styles.size() >= 15 );
  if ( !registry.loadProblems().isEmpty() )
    FAIL( registry.loadProblems().join( QLatin1String( "; " ) ).toStdString() );
  for ( const auto &style : styles )
  {
    const auto problems = validateStyleSpec( style );
    if ( !problems.empty() )
      FAIL( style["id"].asString() + ": " + problems.front() );
  }
}

TEST_CASE( "StyleSpec validation rejects malformed documents", "[platform5][style]" )
{
  Json::Value bad( Json::objectValue );
  bad["id"] = "style.bad";
  bad["version"] = 1;
  bad["kind"] = "style_spec";
  bad["applies_to"] = "texture"; // not raster|vector|any
  bad["raster"]["renderertype"] = "magic_renderer";
  auto problems = validateStyleSpec( bad );
  REQUIRE_FALSE( problems.empty() );

  bad["applies_to"] = "raster";
  bad["raster"]["renderertype"] = "singleband_pseudocolor";
  bad["raster"]["band"] = 0; // bands are 1-based
  problems = validateStyleSpec( bad );
  CHECK_FALSE( problems.empty() );

  bad["raster"].removeMember( "band" );
  bad["raster"]["classification"]["classes"] = Json::Value( Json::arrayValue );
  for ( int i = 0; i < 100; ++i )
  {
    Json::Value entry( Json::objectValue );
    entry["min"] = i;
    entry["max"] = i + 1;
    entry["color"] = "#000000";
    bad["raster"]["classification"]["classes"].append( entry );
  }
  problems = validateStyleSpec( bad );
  CHECK_FALSE( problems.empty() ); // class budget exceeded
}

TEST_CASE( "Token references resolve against the referenced token set", "[platform5][style]" )
{
  StyleRegistry &registry = StyleRegistry::instance();
  registry.setDirectory( QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  const Json::Value style = registry.find( "style.flood-extent" );
  REQUIRE_FALSE( style.isNull() );

  std::vector<std::string> problems;
  const Json::Value resolved = resolveStyleTokens( style, resolveTokenSet( style ), &problems );
  CHECK( problems.empty() );
  // No token reference survives resolution: colors are concrete hex values.
  std::function<void( const Json::Value & )> assertNoTokens = [&]( const Json::Value &node ) {
    if ( node.isString() )
      CHECK( node.asString().rfind( "token:", 0 ) != 0 );
    else if ( node.isArray() )
      for ( const auto &entry : node )
        assertNoTokens( entry );
    else if ( node.isObject() )
      for ( const auto &key : node.getMemberNames() )
        assertNoTokens( node[key] );
  };
  assertNoTokens( resolved );

  // Unknown token paths are reported, never silently kept or guessed.
  Json::Value broken = style;
  broken["raster"]["opacity"] = "token:colors.does_not_exist";
  const Json::Value resolvedBroken = resolveStyleTokens( broken, resolveTokenSet( style ), &problems );
  REQUIRE_FALSE( problems.empty() );
  CHECK( problems.front().find( "token:colors.does_not_exist" ) != std::string::npos );
  CHECK( resolvedBroken["raster"]["opacity"].asString() == "token:colors.does_not_exist" );
}

// ---------------------------------------------------------------------------
// Solution registry (Milestones A/B/D)
// ---------------------------------------------------------------------------

TEST_CASE( "Solution registry loads the shipped catalog with resolvable references",
           "[platform5][solution]" )
{
  SolutionRegistry &registry = SolutionRegistry::instance();
  registry.setDirectory( QString::fromStdString( ( sourceDir() / "data/agent/solutions" ).string() ) );
  registry.reload();
  const Json::Value solutions = registry.solutions();
  REQUIRE( solutions.isArray() );
  CHECK( solutions.size() >= 40 );
  CHECK( registry.loadProblems().isEmpty() );

  // Every shipped solution must resolve its references against the live
  // catalogs (recipes, templates, styles).
  RecipeCatalog::instance().setDirectory( ( sourceDir() / "data/agent/recipes" ).string() );
  RecipeCatalog::instance().reload();
  TemplateRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  RefResolvers resolvers;
  resolvers.recipe = []( const std::string &id ) {
    return !RecipeCatalog::instance().recipe( id ).isNull();
  };
  resolvers.mapTemplate = resolvers.reportTemplate = []( const std::string &id ) {
    return !TemplateRegistry::instance().find( QString::fromStdString( id ) ).isNull();
  };
  resolvers.style = []( const std::string &id ) {
    return !StyleRegistry::instance().find( QString::fromStdString( id ) ).isNull();
  };
  for ( const auto &solution : solutions )
  {
    const auto problems = validateSolutionTemplate( solution, &resolvers );
    if ( !problems.empty() )
      FAIL( solution["id"].asString() + ": " + problems.front() );
  }
}

TEST_CASE( "Solution faceted search is deterministic, bounded and filtered",
           "[platform5][solution]" )
{
  SolutionRegistry &registry = SolutionRegistry::instance();
  registry.setDirectory( QString::fromStdString( ( sourceDir() / "data/agent/solutions" ).string() ) );
  registry.reload();
  const Json::Value solutions = registry.solutions();
  REQUIRE( solutions.size() >= 40 );

  SolutionQuery flood;
  flood.task = "flood";
  const Json::Value floodResult = searchSolutions( solutions, flood );
  CHECK( floodResult["total"].asInt() >= 4 );
  // Deterministic id ordering across calls.
  Json::Value again = searchSolutions( solutions, flood );
  CHECK( again.toStyledString() == floodResult.toStyledString() );

  SolutionQuery sar;
  sar.modality = "sar";
  for ( const auto &item : searchSolutions( solutions, sar )["items"] )
    for ( const auto &m : item["modalities"] )
      CHECK( m.asString() == "sar" );

  SolutionQuery page;
  page.pageSize = 5;
  const Json::Value first = searchSolutions( solutions, page );
  REQUIRE( first["items"].size() == 5 );
  CHECK( first["next_page"].asInt() == 1 );

  SolutionQuery nonsense;
  nonsense.keyword = "zzz-no-such-thing";
  CHECK( searchSolutions( solutions, nonsense )["total"].asInt() == 0 );
}

TEST_CASE( "Solution aliases resolve to the canonical document", "[platform5][solution]" )
{
  SolutionRegistry &registry = SolutionRegistry::instance();
  registry.setDirectory( QString::fromStdString( ( sourceDir() / "data/agent/solutions" ).string() ) );
  const Json::Value canonical = registry.find( "solution.flood.optical-ndwi" );
  REQUIRE_FALSE( canonical.isNull() );
  const Json::Value viaAlias = registry.find( "flood.optical" );
  REQUIRE_FALSE( viaAlias.isNull() );
  CHECK( viaAlias["id"].asString() == canonical["id"].asString() );
}

TEST_CASE( "Solution inheritance merges parents and rejects cycles", "[platform5][solution]" )
{
  QMap<QString, Json::Value> catalog;
  Json::Value parent = makeMinimalSolution( "solution.test.parent" );
  parent["title"] = "Parent title";
  Json::Value parentKeywords( Json::arrayValue );
  parentKeywords.append( "parent" );
  parent["keywords"] = parentKeywords;
  catalog.insert( "solution.test.parent", parent );

  Json::Value child = makeMinimalSolution( "solution.test.child" );
  child["extends"] = "solution.test.parent";
  child.removeMember( "title" ); // inherit the title
  QStringList problems;
  const Json::Value resolved = resolveSolutionInheritance( child, catalog, &problems );
  REQUIRE_FALSE( resolved.isNull() );
  CHECK( resolved["title"].asString() == "Parent title" );
  CHECK( resolved["id"].asString() == "solution.test.child" ); // child fields win
  CHECK( resolved["keywords"][0].asString() == "parent" );

  Json::Value cycle = makeMinimalSolution( "solution.test.cycle" );
  cycle["extends"] = "solution.test.cycle";
  problems = QStringList();
  CHECK( resolveSolutionInheritance( cycle, catalog, &problems ).isNull() );
  CHECK_FALSE( problems.isEmpty() );
}

TEST_CASE( "Solution validation with resolvers reports unknown references", "[platform5][solution]" )
{
  RefResolvers resolvers;
  resolvers.recipe = []( const std::string &id ) { return id == "harness.optical_ndvi"; };
  resolvers.mapTemplate = []( const std::string &id ) { return id == "water-flood-a4l"; };
  Json::Value doc = makeMinimalSolution();
  doc["map_template"] = "no-such-template";
  const auto problems = validateSolutionTemplate( doc, &resolvers );
  bool sawTemplate = false;
  for ( const auto &problem : problems )
    sawTemplate = sawTemplate || problem.find( "unknown map_template" ) != std::string::npos;
  CHECK( sawTemplate );
}

// ---------------------------------------------------------------------------
// Bounded conditions (Milestone F)
// ---------------------------------------------------------------------------

TEST_CASE( "Condition syntax accepts bounded expressions and rejects junk",
           "[platform5][conditions]" )
{
  CHECK( validateConditionSyntax( "has(dem)", nullptr ) );
  CHECK( validateConditionSyntax( "param.threshold >= 0.5 and has(qa)", nullptr ) );
  CHECK( validateConditionSyntax( "(modality == \"sar\" or modality == \"optical\") and true", nullptr ) );

  std::vector<std::string> problems;
  CHECK_FALSE( validateConditionSyntax( "has(", &problems ) );
  CHECK_FALSE( validateConditionSyntax( "x ==" , &problems ) );
  CHECK_FALSE( validateConditionSyntax( "1 + 1 == 2", &problems ) ); // arithmetic is not in the grammar
  CHECK_FALSE( validateConditionSyntax( std::string( 300, 'a' ), &problems ) );
  CHECK_FALSE( validateConditionSyntax( "import os", &problems ) );
}

TEST_CASE( "Condition evaluation resolves against a materialized context",
           "[platform5][conditions]" )
{
  Json::Value ctx( Json::objectValue );
  ctx["modality"] = "sar";
  ctx["param"]["threshold"] = 0.5;
  ctx["has_dem"] = true;

  bool value = false;
  std::string error;
  REQUIRE( evaluateCondition( "modality == \"sar\"", ctx, &value, &error ) );
  CHECK( value );
  REQUIRE( evaluateCondition( "param.threshold >= 0.5", ctx, &value, &error ) );
  CHECK( value );
  REQUIRE( evaluateCondition( "param.threshold > 0.75", ctx, &value, &error ) );
  CHECK_FALSE( value );
  REQUIRE( evaluateCondition( "has_dem and has(modality)", ctx, &value, &error ) );
  CHECK( value );
  REQUIRE( evaluateCondition( "modality == \"optical\" or has_dem", ctx, &value, &error ) );
  CHECK( value );

  // has() on a missing path is a legal FALSE (presence probe), but a bare
  // unknown path is an evaluation ERROR, never silent false.
  REQUIRE( evaluateCondition( "has(nope)", ctx, &value, &error ) );
  CHECK_FALSE( value );
  CHECK_FALSE( evaluateCondition( "nope", ctx, &value, &error ) );
  CHECK( error.find( "nope" ) != std::string::npos );
  REQUIRE( evaluateCondition( "param.threshold", ctx, &value, &error ) == false );
}

TEST_CASE( "Condition paths are collectable for context validation", "[platform5][conditions]" )
{
  const auto paths = conditionPaths( "has(dem) and param.threshold > 0.1 or flags.cloudy" );
  REQUIRE( paths.size() == 3 );
  CHECK( paths.count( "dem" ) == 1 );
  CHECK( paths.count( "param.threshold" ) == 1 );
  CHECK( paths.count( "flags.cloudy" ) == 1 );
}

// ---------------------------------------------------------------------------
// MapSpec v3 surfaces (Milestones F/G/H)
// ---------------------------------------------------------------------------

TEST_CASE( "v3 documents validate with conditions, locators and the full atlas surface",
           "[platform5][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value frame = rectItem( "map-1", 12, 30, 200, 140 );
  spec["map_frames"].append( frame );
  Json::Value inset = rectItem( "inset_map-1", 220, 30, 60, 45 );
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-1";
  locator["style"] = "outline";
  locator["label"] = "Region overview";
  inset["locator"] = locator;
  spec["inset_maps"].append( inset );

  Json::Value title = rectItem( "title-1", 12, 12, 200, 12 );
  title["text"] = "Flood overview";
  title["visible_if"] = "has(primary)";
  spec["titles"].append( title );

  Json::Value colorbar = rectItem( "colorbar-1", 220, 90, 60, 10 );
  colorbar["ramp"] = "uncertainty";
  colorbar["content_if"] = "has(probability_band)";
  spec["colorbars"].append( colorbar );

  spec["page"]["atlas"]["enabled"] = true;
  spec["page"]["atlas"]["coverage_layer"] = "regions";
  spec["page"]["atlas"]["filter"] = "\"region_type\" = 'district'";
  spec["page"]["atlas"]["sort_by"] = "name";
  spec["page"]["atlas"]["sort_order"] = "desc";
  spec["page"]["atlas"]["margin_fraction"] = 0.1;
  spec["page"]["atlas"]["feature_variables"] = Json::Value( Json::arrayValue );
  spec["page"]["atlas"]["feature_variables"].append( "name" );

  spec["pages"] = Json::Value( Json::arrayValue );
  Json::Value reportPage( Json::objectValue );
  reportPage["width_mm"] = 297;
  reportPage["height_mm"] = 210;
  reportPage["role"] = "report";
  reportPage["page_if"] = "has(statistics)";
  spec["pages"].append( reportPage );

  const auto problems = validateMapSpec( spec );
  if ( !problems.empty() )
    FAIL( problems.front() );
}

TEST_CASE( "v3 validation rejects broken locator targets and bad conditions",
           "[platform5][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value inset = rectItem( "inset_map-1", 220, 30, 60, 45 );
  Json::Value locator( Json::objectValue );
  locator["target"] = "map-does-not-exist";
  inset["locator"] = locator;
  spec["inset_maps"].append( inset );
  Json::Value title = rectItem( "title-1", 12, 12, 100, 10 );
  title["text"] = "x";
  title["visible_if"] = "has(";
  spec["titles"].append( title );

  const auto problems = validateMapSpec( spec );
  bool sawLocator = false;
  bool sawCondition = false;
  for ( const auto &problem : problems )
  {
    sawLocator = sawLocator || problem.find( "locator.target" ) != std::string::npos;
    sawCondition = sawCondition || problem.find( "visible_if" ) != std::string::npos;
  }
  CHECK( sawLocator );
  CHECK( sawCondition );
}

TEST_CASE( "Relative constraint kinds are solver-known and validated", "[platform5][mapspec]" )
{
  CHECK( isConstraintKind( "below" ) );
  CHECK( isConstraintKind( "fit_content" ) );
  CHECK( isRelativeConstraintKind( "avoid_overlap" ) );
  CHECK_FALSE( isRelativeConstraintKind( "align" ) );

  Json::Value spec = makeSpec();
  Json::Value title = rectItem( "title-1", 12, 12, 200, 12 );
  title["text"] = "t";
  spec["titles"].append( title );
  Json::Value subtitle = rectItem( "title-2", 12, 30, 200, 8 );
  subtitle["text"] = "s";
  spec["titles"].append( subtitle );

  Json::Value constraint( Json::objectValue );
  constraint["id"] = "c1";
  constraint["kind"] = "below";
  Json::Value items( Json::arrayValue );
  items.append( "title-1" );
  items.append( "title-2" );
  constraint["items"] = items;
  constraint["gap_mm"] = 2;
  spec["constraints"].append( constraint );

  Json::Value badConstraint = constraint;
  badConstraint["id"] = "c2";
  badConstraint["items"] = Json::Value( Json::arrayValue );
  badConstraint["items"].append( "title-1" );
  spec["constraints"].append( badConstraint );

  Json::Value fit = constraint;
  fit["id"] = "c3";
  fit["kind"] = "fit_content";
  fit["items"] = Json::Value( Json::arrayValue );
  fit["items"].append( "title-2" );
  spec["constraints"].append( fit ); // missing content_mm -> validation problem

  const auto problems = validateMapSpec( spec );
  bool sawPair = false;
  bool sawContent = false;
  for ( const auto &problem : problems )
  {
    sawPair = sawPair || problem.find( "c2" ) != std::string::npos;
    sawContent = sawContent || problem.find( "content_mm" ) != std::string::npos;
  }
  CHECK( sawPair );
  CHECK( sawContent );
}

TEST_CASE( "The composition solver resolves v3 relative constraints deterministically",
           "[platform5][composition]" )
{
  Json::Value spec = makeSpec();
  spec["titles"].append( rectItem( "a", 20, 20, 100, 10 ) );
  spec["titles"].append( rectItem( "b", 200, 200, 40, 8 ) );
  Json::Value constraint( Json::objectValue );
  constraint["id"] = "c-below";
  constraint["kind"] = "below";
  Json::Value items( Json::arrayValue );
  items.append( "a" );
  items.append( "b" );
  constraint["items"] = items;
  constraint["gap_mm"] = 3;
  spec["constraints"].append( constraint );

  const CompositionResult first = resolveComposition( spec, 10.0 );
  CHECK( first.constraintsSolved == 1 );
  const Json::Value b = spec["titles"][1]["rect_mm"];
  CHECK( b[0].asDouble() == Catch::Approx( 20.0 ) );
  CHECK( b[1].asDouble() == Catch::Approx( 33.0 ) );
  // Determinism: identical input, byte-identical outcome.
  Json::Value specCopy = spec;
  specCopy["titles"][1] = rectItem( "b", 200, 200, 40, 8 );
  resolveComposition( specCopy, 10.0 );
  CHECK( specCopy["titles"][1]["rect_mm"].toStyledString() ==
         spec["titles"][1]["rect_mm"].toStyledString() );

  // avoid_overlap only moves when needed.
  Json::Value overlap = makeSpec();
  overlap["titles"].append( rectItem( "k", 20, 20, 100, 10 ) );
  overlap["titles"].append( rectItem( "m", 50, 25, 20, 5 ) );
  Json::Value avoid( Json::objectValue );
  avoid["id"] = "c-overlap";
  avoid["kind"] = "avoid_overlap";
  Json::Value pair( Json::arrayValue );
  pair.append( "k" );
  pair.append( "m" );
  avoid["items"] = pair;
  overlap["constraints"].append( avoid );
  const CompositionResult solved = resolveComposition( overlap, 5.0 );
  CHECK( solved.constraintsSolved == 1 );
  CHECK( overlap["titles"][1]["rect_mm"][1].asDouble() == Catch::Approx( 35.0 ) );
}

TEST_CASE( "Conditional pruning hides items, strips content and remaps pages",
           "[platform5][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value frame = rectItem( "map-1", 12, 30, 200, 140 );
  spec["map_frames"].append( frame );
  Json::Value title = rectItem( "title-1", 12, 12, 100, 10 );
  title["text"] = "hidden";
  title["visible_if"] = "false";
  spec["titles"].append( title );
  // With intrinsic colors: content stripped, item kept.
  Json::Value colorbar = rectItem( "colorbar-1", 220, 30, 50, 10 );
  colorbar["content_if"] = "false";
  Json::Value stops( Json::arrayValue );
  stops.append( "#000000" );
  colorbar["colors"] = stops;
  spec["colorbars"].append( colorbar );
  // Pure slot furniture without any intrinsic content: dropped entirely.
  Json::Value colorbar2 = rectItem( "colorbar-2", 220, 50, 50, 10 );
  colorbar2["content_if"] = "false";
  spec["colorbars"].append( colorbar2 );

  spec["pages"] = Json::Value( Json::arrayValue );
  Json::Value reportPage( Json::objectValue );
  reportPage["width_mm"] = 297;
  reportPage["height_mm"] = 210;
  reportPage["page_if"] = "has(statistics)";
  spec["pages"].append( reportPage );
  Json::Value tableItem = rectItem( "chart-1", 12, 180, 80, 20 );
  tableItem["page"] = 1;
  spec["charts"].append( tableItem );

  Json::Value ctx( Json::objectValue ); // no statistics -> page pruned
  spec["condition_context"] = ctx;      // the resolver runs on stamped docs
  std::vector<std::string> errors;
  const Json::Value ledger = resolveMapSpecConditions( spec, ctx, &errors );

  CHECK( findMapSpecItem( spec, "title-1" ).isNull() );
  // Furniture with intrinsic colors stays (content member only stripped).
  const Json::Value keptColorbar = findMapSpecItem( spec, "colorbar-1" );
  REQUIRE_FALSE( keptColorbar.isNull() );
  CHECK( spec[keptColorbar["collection"].asString()][keptColorbar["index"].asInt()]
           .isMember( "colors" ) );
  // Content-less furniture is dropped.
  CHECK( findMapSpecItem( spec, "colorbar-2" ).isNull() );
  CHECK( spec["pages"].size() == 0 );
  CHECK( findMapSpecItem( spec, "chart-1" ).isNull() ); // item lived on the pruned page
  CHECK_FALSE( spec.isMember( "condition_context" ) );
  CHECK( ledger.isArray() );
}

TEST_CASE( "Unevaluable conditions keep their content and are reported", "[platform5][mapspec]" )
{
  Json::Value spec = makeSpec();
  Json::Value title = rectItem( "title-1", 12, 12, 100, 10 );
  title["text"] = "kept";
  title["visible_if"] = "missing_key"; // bare unknown path -> evaluation error
  spec["titles"].append( title );
  spec["condition_context"] = Json::Value( Json::objectValue );

  std::vector<std::string> errors;
  resolveMapSpecConditions( spec, Json::Value( Json::objectValue ), &errors );
  CHECK_FALSE( findMapSpecItem( spec, "title-1" ).isNull() );
  REQUIRE_FALSE( errors.empty() );
  CHECK( errors.front().find( "missing_key" ) != std::string::npos );
}

TEST_CASE( "upgradeMapSpec migrates v2 documents to v3 without losing fields",
           "[platform5][mapspec]" )
{
  Json::Value doc = makeSpec();
  doc["spec_version"] = 2;
  doc["titles"].append( rectItem( "title-1", 12, 12, 100, 10 ) );
  Json::Value upgraded = upgradeMapSpec( doc );
  CHECK( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  CHECK( upgraded["titles"].size() == 1 );
  // Idempotent.
  CHECK( upgradeMapSpec( upgraded )["spec_version"].asInt() == kMapSpecCurrentVersion );
}

// ---------------------------------------------------------------------------
// Catalog integration (Milestones B/K) + recipe growth (Milestone C)
// ---------------------------------------------------------------------------

TEST_CASE( "The catalog index covers styles and solutions", "[platform5][catalog]" )
{
  StyleRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  SolutionRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/agent/solutions" ).string() ) );
  TemplateRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  const Json::Value index = buildCatalogIndex();
  CHECK( index.isMember( "styles" ) );
  CHECK( index["styles"].size() >= 15 );
  CHECK( index.isMember( "solutions" ) );
  CHECK( index["solutions"].size() >= 40 );
  // Compact summaries stay inside the token budget (256 chars of description).
  for ( const auto &entry : index["solutions"] )
    CHECK( entry["title"].asString().size() < 200 );
}

TEST_CASE( "The shipped recipe catalog carries the Platform 5.0 facets", "[platform5][recipes]" )
{
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( ( sourceDir() / "data/agent/recipes" ).string() );
  catalog.reload();
  const Json::Value recipes = catalog.listRecipes();
  CHECK( recipes.size() >= 70 );

  std::set<std::string> families;
  int facetBearing = 0;
  for ( const auto &summary : recipes )
  {
    const Json::Value full = catalog.recipe( summary["recipe_id"].asString() );
    if ( full.isMember( "family" ) && full.isMember( "modality" ) && full.isMember( "sensors" ) )
      ++facetBearing;
    families.insert( full.get( "family", "" ).asString() );
  }
  CHECK( facetBearing == static_cast<int>( recipes.size() ) );
  CHECK( families.size() >= 8 );
}

TEST_CASE( "The extended intent vocabulary is closed and preflight-routed",
           "[platform5][intents]" )
{
  for ( const char *intent : { "ndvi", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi",
                               "evi", "savi", "ndre", "water", "flood", "sar_water", "sar_flood",
                               "sar", "ship", "change", "sar_change", "temporal", "terrain",
                               "classify", "accuracy", "qa", "preprocess", "inference", "phenology" } )
    CHECK( isKnownIntent( intent ) );
  CHECK( isKnownIntent( "" ) );
  CHECK_FALSE( isKnownIntent( "warp_speed" ) );
}

TEST_CASE( "Recipe instantiation stays deterministic and fails typed on unresolvable slots",
           "[platform5][recipes]" )
{
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( ( sourceDir() / "data/agent/recipes" ).string() );
  catalog.reload();
  const Json::Value recipe = catalog.recipe( "harness.optical_ndvi" );
  REQUIRE_FALSE( recipe.isNull() );

  sicnu::agent::harness::HarnessError error;
  Json::Value bindings( Json::objectValue );
  bindings["slots"]["primary"] = "no-such-dataset";
  const Json::Value plan = catalog.instantiateRecipe( "harness.optical_ndvi", bindings, error );
  CHECK( plan.isNull() );
  CHECK_FALSE( error.code.empty() );
}

TEST_CASE( "Style application reports per-entry problems and applies what it can",
           "[platform5][style-compiler]" )
{
  // Application needs a live QGIS layer; the compile-side contract (resolved
  // tokens in, problems reported, nothing mutated on failure) is asserted via
  // the buildRasterRenderer pure path.
  Json::Value raster( Json::objectValue );
  raster["renderertype"] = "singleband_pseudocolor";
  raster["band"] = 1;
  raster["classification"]["mode"] = "discrete";
  Json::Value classes( Json::arrayValue );
  Json::Value entry( Json::objectValue );
  entry["min"] = 0;
  entry["max"] = 1;
  entry["color"] = "#00ff00";
  entry["label"] = "ok";
  classes.append( entry );
  raster["classification"]["classes"] = classes;
  QgsRasterRenderer *renderer = buildRasterRenderer( raster, 1 );
  REQUIRE( renderer != nullptr );
  CHECK( renderer->type() == QLatin1String( "singlebandpseudocolor" ) );
  delete renderer;

  Json::Value bad = raster;
  bad["band"] = 9;
  CHECK( buildRasterRenderer( bad, 1 ) == nullptr );
}

// ---------------------------------------------------------------------------
// Milestone SCALE: bounded-catalog performance gates + Milestone F compile
// coverage for multi-page reports.
// ---------------------------------------------------------------------------

TEST_CASE( "Scale gates: catalog load, facet search and the solution pipeline stay bounded",
           "[platform5][scale]" )
{
  using tclock = std::chrono::steady_clock;
  auto msOf = []( tclock::time_point a, tclock::time_point b ) {
    return std::chrono::duration<double, std::milli>( b - a ).count();
  };

  TemplateRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  TemplateRegistry::instance().reload();
  StyleRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  StyleRegistry::instance().reload();
  SolutionRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/agent/solutions" ).string() ) );
  SolutionRegistry::instance().reload();
  RecipeCatalog::instance().setDirectory( ( sourceDir() / "data/agent/recipes" ).string() );
  RecipeCatalog::instance().reload();

  const Json::Value solutions = SolutionRegistry::instance().solutions();
  const Json::Value templates = TemplateRegistry::instance().templates();
  const Json::Value recipes = RecipeCatalog::instance().listRecipes();
  REQUIRE( solutions.size() >= 40 );
  REQUIRE( templates.size() >= 55 );
  REQUIRE( recipes.size() >= 70 );

  // Facet search over the full catalog.
  const auto searchStart = tclock::now();
  int totalHits = 0;
  for ( int i = 0; i < 50; ++i )
  {
    SolutionQuery query;
    query.task = "flood";
    query.modality = i % 2 ? "sar" : "";
    query.page = i % 5;
    totalHits += searchSolutions( solutions, query )["total"].asInt();
  }
  const double searchMs = msOf( searchStart, tclock::now() ) / 50.0;
  CHECK( totalHits > 0 );
  CHECK( searchMs < 20.0 );

  // Compact search responses stay inside the token budget. Measured:
  // a full compact summary serializes to ~440 chars, so gate the per-hit
  // cost (500) and the family page (all 5 flood solutions) at 2500.
  SolutionQuery flood;
  flood.task = "flood";
  const Json::Value page = searchSolutions( solutions, flood );
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  const std::string serialized = Json::writeString( builder, page );
  const int hits = static_cast<int>( page["items"].size() );
  REQUIRE( hits > 0 );
  CHECK( serialized.size() / hits <= 500u );
  CHECK( serialized.size() <= 2500 );

  // Instantiation pipeline: contract check + recipe compile + template draft.
  const auto instStart = tclock::now();
  sicnu::agent::harness::HarnessError error;
  Json::Value bindings( Json::objectValue );
  Json::Value slotRefs( Json::objectValue ); // NB: not `slots` — Qt moc macro
  slotRefs["primary"] = "unbound-ref";
  bindings["slots"] = slotRefs;
  const Json::Value plan =
    RecipeCatalog::instance().instantiateRecipe( "harness.optical_ndvi", bindings, error );
  const double instMs = msOf( instStart, tclock::now() );
  CHECK( instMs < 50.0 );

  // Whole-catalog preflight of a representative A4 draft.
  Json::Value draft = TemplateRegistry::instance().instantiateTemplate(
    "water-flood-a4l", Json::Value( Json::objectValue ), nullptr );
  REQUIRE_FALSE( draft.isNull() );
  const double margin = tokenNumber( resolveTokenSet( draft ), "spacing.margin_mm", 12.0 );
  const auto preflightStart = tclock::now();
  resolveComposition( draft, margin );
  const Json::Value quality = preflightMapSpec( draft );
  const double preflightMs = msOf( preflightStart, tclock::now() );
  CHECK( preflightMs < 50.0 );
  CHECK( quality["issues"].isArray() );
}

TEST_CASE( "Multi-page report templates compile with page roles and conditional pages",
           "[platform5][mapspec][multipage]" )
{
  TemplateRegistry::instance().setDirectory(
    QString::fromStdString( ( sourceDir() / "data/cartography" ).string() ) );
  Json::Value draft = TemplateRegistry::instance().instantiateTemplate(
    "report-multipage-a4l", Json::Value( Json::objectValue ), nullptr );
  REQUIRE_FALSE( draft.isNull() );
  REQUIRE( draft["pages"].isArray() );
  REQUIRE( draft["pages"].size() == 2 );
  CHECK( draft["pages"][0]["role"].asString() == "map" );
  CHECK( draft["pages"][1]["role"].asString() == "report" );
  CHECK( draft["pages"][1]["page_if"].asString() == "has(statistics)" );

  // Items carry their page placement.
  int onPage = 0;
  for ( const auto &item : draft["titles"] )
    if ( item.isMember( "page" ) && item["page"].isIntegral() && item["page"].asInt() > 0 )
      ++onPage;
  CHECK( onPage >= 1 );

  // The compile path accepts the multi-page draft and builds all pages.
  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( draft, &error );
  REQUIRE( layout != nullptr );
  CHECK( layout->pageCollection()->pageCount() == 3 );
  QgsProject::instance()->layoutManager()->clear();
}
