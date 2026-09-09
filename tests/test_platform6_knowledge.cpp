// tests/test_platform6_knowledge.cpp
//
// Knowledge Platform 6.0 — composable components (C), template taxonomy (D),
// style applicability (E), recipe knowledge (F), solution explainability (G),
// preflight 6.0 semantic rules (I).
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/quality.h"
#include "agent/cartography/registry.h"
#include "agent/cartography/solution_registry.h"
#include "agent/cartography/style_spec.h"
#include "agent/harness/recipe_catalog.h"
#include "agent/mapspec/mapspec.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <set>

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;
using sicnu::agent::harness::HarnessError;
using sicnu::agent::harness::RecipeCatalog;

namespace {

bool writeTextFile( const QString &path, const std::string &contents )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  file.write( contents.data(), static_cast<qint64>( contents.size() ) );
  return true;
}

Json::Value componentWithChildren()
{
  Json::Value descriptor( Json::objectValue );
  descriptor["id"] = "legend/composite-test";
  descriptor["category"] = "legend";
  descriptor["version"] = 1;
  Json::Value child( Json::objectValue );
  child["role"] = "title";
  child["required"] = false;
  Json::Value content( Json::objectValue );
  content["title"] = "图例";
  child["content"] = content;
  descriptor["children"].append( child );
  Json::Value classes( Json::objectValue );
  classes["role"] = "classes";
  classes["required"] = true;
  descriptor["children"].append( classes );
  return descriptor;
}

} // namespace

// ---------------------------------------------------------------------------
// Milestone C — bounded composable component grammar
// ---------------------------------------------------------------------------

TEST_CASE( "Composite component children grammar is validated and materialized (C)",
           "[platform6][components]" )
{
  // Valid descriptor.
  CHECK( validateComponentDescriptor( componentWithChildren() ).empty() );

  // Duplicate roles rejected.
  Json::Value duplicate = componentWithChildren();
  Json::Value second( Json::objectValue );
  second["role"] = "title";
  duplicate["children"].append( second );
  CHECK_FALSE( validateComponentDescriptor( duplicate ).empty() );

  // Nesting rejected (depth bounded to 1).
  Json::Value nested = componentWithChildren();
  nested["children"][0]["children"] = Json::Value( Json::arrayValue );
  CHECK_FALSE( validateComponentDescriptor( nested ).empty() );

  // Budget enforced.
  Json::Value crowded = componentWithChildren();
  for ( int i = 0; i < 20; ++i )
  {
    Json::Value filler( Json::objectValue );
    filler["role"] = "filler-" + std::to_string( i );
    crowded["children"].append( filler );
  }
  CHECK_FALSE( validateComponentDescriptor( crowded ).empty() );

  // Materialization: applyComponentDefaults carries children onto the item;
  // explicit per-role item children win; missing roles are inherited.
  ComponentRegistry &registry = ComponentRegistry::instance();
  REQUIRE( registry.registerComponent( componentWithChildren() ) );
  Json::Value item( Json::objectValue );
  item["source_component"] = "legend/composite-test";
  Json::Value explicitTitle( Json::objectValue );
  explicitTitle["role"] = "title";
  explicitTitle["overrides"] = Json::Value( Json::objectValue );
  item["children"].append( explicitTitle );
  REQUIRE( applyComponentDefaults( item, nullptr ) );
  REQUIRE( item["children"].isArray() );
  CHECK( item["children"].size() == 2 );
  // Explicit child first (kept verbatim), inherited classes child appended.
  CHECK( item["children"][0]["role"].asString() == "title" );
  CHECK( item["children"][1]["role"].asString() == "classes" );
}

TEST_CASE( "MapSpec items validate the children grammar (C)", "[platform6][mapspec]" )
{
  Json::Value spec = makeMapSpec( "children-check", Json::Value() );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 200 );
  rect.append( 30 );
  rect.append( 60 );
  rect.append( 80 );
  legend["rect_mm"] = rect;
  Json::Value child( Json::objectValue );
  child["role"] = "nodata";
  legend["children"].append( child );
  spec["legends"].append( legend );
  CHECK( validateMapSpec( spec ).empty() );

  Json::Value bad = makeMapSpec( "children-check-bad", Json::Value() );
  Json::Value legend2( Json::objectValue );
  legend2["id"] = "legend-1";
  legend2["rect_mm"] = rect;
  Json::Value badChild( Json::objectValue );
  badChild["required"] = "yes"; // must be boolean
  badChild["role"] = "classes";
  badChild["children"] = Json::Value( Json::arrayValue ); // nesting forbidden
  legend2["children"].append( badChild );
  bad["legends"].append( legend2 );
  CHECK_FALSE( validateMapSpec( bad ).empty() );
}

// ---------------------------------------------------------------------------
// Milestone D — semantic template taxonomy
// ---------------------------------------------------------------------------

TEST_CASE( "Template facet vocabularies are closed and validated (D)",
           "[platform6][templates]" )
{
  CHECK( isTemplateTask( "flood" ) == false ); // tasks vocabulary has no "flood"
  CHECK( isTemplateTask( "classification" ) );
  CHECK( isTemplateMedium( "a4" ) );
  CHECK( isTemplateMedium( "atlas" ) );
  CHECK( isTemplatePurpose( "operational" ) );

  Json::Value descriptor( Json::objectValue );
  descriptor["id"] = "facet-test";
  descriptor["facets"]["tasks"].append( "classification" );
  descriptor["facets"]["medium"] = "a4";
  descriptor["facets"]["purpose"] = "analysis";
  CHECK( validateTemplateFacets( descriptor ).empty() );

  Json::Value typo = descriptor;
  typo["facets"]["medium"] = "A4L"; // legacy id fragments are not facets
  CHECK_FALSE( validateTemplateFacets( typo ).empty() );
  typo = descriptor;
  typo["facets"]["tasks"][0] = "flood-mapping";
  CHECK_FALSE( validateTemplateFacets( typo ).empty() );
}

TEST_CASE( "Faceted template search explains matches and ranks facet-complete first (D)",
           "[platform6][templates]" )
{
  Json::Value withFacets( Json::objectValue );
  withFacets["id"] = "tpl-faceted";
  withFacets["description"] = "Land cover classification page.";
  withFacets["facets"]["tasks"].append( "classification" );
  withFacets["facets"]["tasks"].append( "vegetation" );
  withFacets["facets"]["medium"] = "a4";
  withFacets["facets"]["purpose"] = "analysis";
  Json::Value legacy( Json::objectValue );
  legacy["id"] = "tpl-legacy";
  legacy["description"] = "Land cover classification page.";

  Json::Value catalog( Json::arrayValue );
  catalog.append( withFacets );
  catalog.append( legacy );

  TemplateQuery query;
  query.task = "classification";
  query.medium = "a4";
  const Json::Value result = searchTemplates( catalog, query );
  CHECK( result["total"].asInt() == 1 ); // the legacy document has no medium facet
  REQUIRE( result["items"].size() == 1 );
  CHECK( result["items"][0]["id"].asString() == "tpl-faceted" );
  CHECK( result["items"][0]["match"]["score"].asInt() == 2 );
  CHECK( result["items"][0]["match"]["reasons"].size() == 2 );

  // Keyword-only queries still reach legacy documents but rank them last.
  TemplateQuery keywordQuery;
  keywordQuery.keyword = "classification";
  const Json::Value keywordResult = searchTemplates( catalog, keywordQuery );
  CHECK( keywordResult["total"].asInt() == 2 );
  REQUIRE( keywordResult["items"].size() == 2 );
  CHECK( keywordResult["items"][0]["id"].asString() == "tpl-faceted" );
  CHECK( keywordResult["items"][0]["match"]["score"].asInt() >
         keywordResult["items"][1]["match"]["score"].asInt() );

  // The shipped catalog carries valid facets everywhere (explicit dir —
  // never ambient cwd/singleton state, review P1).
  TemplateRegistry &registry = TemplateRegistry::instance();
  registry.setDirectory(
    QString::fromStdString( std::string( SICNU_CARTOGRAPHY_DATA_DIR ) ) );
  registry.reload();
  for ( const auto &tmpl : registry.templates() )
  {
    const auto problems = validateTemplateFacets( tmpl );
    INFO( "template " << tmpl["id"].asString() << ": "
                      << ( problems.empty() ? std::string( "ok" ) : problems.front() ) );
    CHECK( problems.empty() );
  }
}

// ---------------------------------------------------------------------------
// Milestone E — style semantic applicability
// ---------------------------------------------------------------------------

TEST_CASE( "Style applicability refuses semantically wrong targets (E)",
           "[platform6][styles]" )
{
  Json::Value ndviStyle( Json::objectValue );
  ndviStyle["id"] = "style.test.ndvi";
  ndviStyle["applies_to"] = "raster";
  ndviStyle["applicability"]["value_domain"]["min"] = -1;
  ndviStyle["applicability"]["value_domain"]["max"] = 1;
  ndviStyle["applicability"]["modalities"].append( "optical" );

  CHECK( validateStyleApplicability( ndviStyle ).empty() );

  // NDVI style on SAR magnitude: value ranges do not overlap.
  Json::Value sarDataset( Json::objectValue );
  sarDataset["kind"] = "raster";
  sarDataset["modality"] = "sar";
  sarDataset["value_min"] = 20.0;
  sarDataset["value_max"] = 100.0;
  const auto sarProblems = checkStyleApplicability( ndviStyle, sarDataset );
  CHECK( sarProblems.size() == 2 ); // domain + modality

  // NDVI style on NDVI data: applicable.
  Json::Value opticalDataset( Json::objectValue );
  opticalDataset["kind"] = "raster";
  opticalDataset["modality"] = "optical";
  opticalDataset["value_min"] = -0.8;
  opticalDataset["value_max"] = 0.9;
  CHECK( checkStyleApplicability( ndviStyle, opticalDataset ).empty() );

  // Malformed applicability blocks are rejected at validation time.
  Json::Value bad = ndviStyle;
  bad["applicability"]["value_domain"]["max"] = -2; // min >= max
  CHECK_FALSE( validateStyleApplicability( bad ).empty() );
}

// ---------------------------------------------------------------------------
// Milestone F — recipe decisionable knowledge
// ---------------------------------------------------------------------------

TEST_CASE( "Recipe metadata validation bounds decisionable knowledge (F)",
           "[platform6][harness]" )
{
  Json::Value recipe( Json::objectValue );
  recipe["recipe_id"] = "test.meta";
  recipe["capabilities"].append( "water-detection" );
  recipe["limitations"].append( "cloud cover degrades optical branch" );
  recipe["applicability"]["modalities"].append( "optical" );
  recipe["applicability"]["resolution_range"]["min_m"] = 3;
  recipe["applicability"]["resolution_range"]["max_m"] = 30;
  recipe["presets"]["sentinel-2"]["index"] = "NDWI";
  recipe["expected_artifacts"][0]["name"] = "water_extent";
  recipe["expected_artifacts"][0]["kind"] = "raster";
  recipe["quality_gates"][0]["id"] = "uncertainty-note";
  CHECK( RecipeCatalog::validateRecipeMetadata( recipe ).empty() );

  Json::Value bad = recipe;
  bad["presets"]["sentinel-2"] = "NDWI"; // preset must be an object
  CHECK_FALSE( RecipeCatalog::validateRecipeMetadata( bad ).empty() );
  bad = recipe;
  bad["applicability"]["resolution_range"]["max_m"] = 1; // min > max
  CHECK_FALSE( RecipeCatalog::validateRecipeMetadata( bad ).empty() );
}

TEST_CASE( "The flood-mapping exemplar models branches without cross-branch contamination (F)",
           "[platform6][harness]" )
{
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( ( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" ) );
  catalog.reload();
  CHECK( catalog.loadProblems().empty() );
  const Json::Value recipe = catalog.recipe( "harness.flood_mapping" );
  REQUIRE_FALSE( recipe.isNull() );
  CHECK( recipe["capabilities"].size() >= 1 );

  QTemporaryDir dir;
  REQUIRE( writeTextFile( dir.filePath( QStringLiteral( "optical.tif" ) ), "x" ) );

  // Optical only: SAR branch and fusion branch drop; optical branch runs.
  Json::Value bindings( Json::objectValue );
  bindings["slots"]["primary"] = dir.filePath( QStringLiteral( "optical.tif" ) ).toStdString();
  bindings["output_dir"] = dir.filePath( QStringLiteral( "out" ) ).toStdString();
  HarnessError error;
  const Json::Value plan = catalog.instantiateRecipe( "harness.flood_mapping", bindings, error );
  REQUIRE( plan.isObject() );
  std::set<std::string> stepIds;
  for ( const auto &step : plan["steps"] )
    stepIds.insert( step["id"].asString() );
  CHECK( stepIds.count( "opt_index" ) == 1 );
  CHECK( stepIds.count( "opt_water" ) == 1 );
  CHECK( stepIds.count( "sar_calibrate" ) == 0 );
  CHECK( stepIds.count( "sar_water" ) == 0 );
  CHECK( stepIds.count( "fused_disagreement" ) == 0 );
  // Declared outputs of dropped branches are filtered (plan stays valid).
  for ( const auto &output : plan["outputs"] )
  {
    const std::string fromStep = output.get( "from_step", "" ).asString();
    INFO( "output from_step: " << fromStep );
    CHECK( ( fromStep.empty() || stepIds.count( fromStep ) == 1 ) );
  }

  // Both modalities: the fusion branch opens.
  REQUIRE( writeTextFile( dir.filePath( QStringLiteral( "sar.tif" ) ), "x" ) );
  Json::Value both( Json::objectValue );
  both["slots"]["primary"] = dir.filePath( QStringLiteral( "optical.tif" ) ).toStdString();
  both["slots"]["sar"] = dir.filePath( QStringLiteral( "sar.tif" ) ).toStdString();
  both["output_dir"] = dir.filePath( QStringLiteral( "out2" ) ).toStdString();
  const Json::Value plan2 = catalog.instantiateRecipe( "harness.flood_mapping", both, error );
  REQUIRE( plan2.isObject() );
  std::set<std::string> stepIds2;
  for ( const auto &step : plan2["steps"] )
    stepIds2.insert( step["id"].asString() );
  CHECK( stepIds2.count( "opt_water" ) == 1 );
  CHECK( stepIds2.count( "sar_water" ) == 1 );
  CHECK( stepIds2.count( "fused_disagreement" ) == 1 );
  CHECK( plan2["outputs"].size() == 3 );

  // SAR only: the optical branch drops, the when_slots fusion gate stays
  // CLOSED (only one of its two slots is bound), and the map_output whose
  // from_step was gate-dropped is filtered from the plan.
  Json::Value sarOnly( Json::objectValue );
  sarOnly["slots"]["sar"] = dir.filePath( QStringLiteral( "sar.tif" ) ).toStdString();
  sarOnly["output_dir"] = dir.filePath( QStringLiteral( "out3" ) ).toStdString();
  const Json::Value plan3 = catalog.instantiateRecipe( "harness.flood_mapping", sarOnly, error );
  REQUIRE( plan3.isObject() );
  std::set<std::string> stepIds3;
  for ( const auto &step : plan3["steps"] )
    stepIds3.insert( step["id"].asString() );
  CHECK( stepIds3.count( "sar_calibrate" ) == 1 );
  CHECK( stepIds3.count( "sar_water" ) == 1 );
  CHECK( stepIds3.count( "opt_index" ) == 0 );
  CHECK( stepIds3.count( "fused_disagreement" ) == 0 );
  CHECK( plan3["outputs"].size() == 1 );
  CHECK( plan3["outputs"][0]["name"].asString() == "sar_water_extent" );
  CHECK_FALSE( plan3.isMember( "map_output" ) );
}

// ---------------------------------------------------------------------------
// Milestone G — solution explainability
// ---------------------------------------------------------------------------

TEST_CASE( "Solution search explains hits and rejections (G)", "[platform6][solutions]" )
{
  Json::Value floodOptical( Json::objectValue );
  floodOptical["id"] = "solution.flood.optical";
  floodOptical["title"] = "Optical flood extent";
  floodOptical["tasks"].append( "flood" );
  floodOptical["modalities"].append( "optical" );
  floodOptical["sensors"].append( "sentinel-2" );
  Json::Value floodSar( Json::objectValue );
  floodSar["id"] = "solution.flood.sar";
  floodSar["title"] = "SAR flood extent";
  floodSar["tasks"].append( "flood" );
  floodSar["modalities"].append( "sar" );
  floodSar["sensors"].append( "sentinel-1" );
  Json::Value catalog( Json::arrayValue );
  catalog.append( floodOptical );
  catalog.append( floodSar );

  SolutionQuery query;
  query.task = "flood";
  query.modality = "optical";
  const Json::Value result = searchSolutions( catalog, query );
  REQUIRE( result["items"].size() == 1 );
  CHECK( result["items"][0]["id"].asString() == "solution.flood.optical" );
  const Json::Value reasons = result["items"][0]["match"]["reasons"];
  bool sawTask = false;
  bool sawModality = false;
  for ( const auto &reason : reasons )
  {
    sawTask = sawTask || reason.asString().rfind( "task:", 0 ) == 0;
    sawModality = sawModality || reason.asString().rfind( "modality:", 0 ) == 0;
  }
  CHECK( sawTask );
  CHECK( sawModality );

  REQUIRE( result["rejected"].size() == 1 );
  CHECK( result["rejected"][0]["id"].asString() == "solution.flood.sar" );
  CHECK( result["rejected"][0]["reasons"][0].asString().find( "modality" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// Milestone I — preflight 6.0 semantic rules
// ---------------------------------------------------------------------------

TEST_CASE( "Preflight reports unknown style refs and missing uncertainty notes (I)",
           "[platform6][quality]" )
{
  Json::Value spec = makeMapSpec( "preflight6", Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 12 );
  rect.append( 30 );
  rect.append( 190 );
  rect.append( 150 );
  frame["rect_mm"] = rect;
  frame["extent"] = Json::Value( Json::arrayValue );
  frame["extent"].append( 116.0 );
  frame["extent"].append( 39.0 );
  frame["extent"].append( 117.0 );
  frame["extent"].append( 40.0 );
  spec["map_frames"].append( frame );
  Json::Value title( Json::objectValue );
  title["id"] = "title-1";
  Json::Value titleRect( Json::arrayValue );
  titleRect.append( 12 );
  titleRect.append( 10 );
  titleRect.append( 120 );
  titleRect.append( 12 );
  title["rect_mm"] = titleRect;
  title["text"] = "flood probability";
  spec["titles"].append( title );
  Json::Value legend( Json::objectValue );
  legend["id"] = "legend-1";
  Json::Value legendRect( Json::arrayValue );
  legendRect.append( 215 );
  legendRect.append( 30 );
  legendRect.append( 70 );
  legendRect.append( 80 );
  legend["rect_mm"] = legendRect;
  legend["style_ref"] = "style.does-not-exist";
  spec["legends"].append( legend );

  const Json::Value report = preflightMapSpec( spec );
  bool sawUnknownStyle = false;
  for ( const auto &finding : report["issues"] )
    if ( finding.get( "code", "" ).asString() == "MAP_STYLE_REF_UNKNOWN" )
      sawUnknownStyle = true;
  CHECK( sawUnknownStyle );
}
