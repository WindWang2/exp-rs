// test_recipe_validator.cpp — recipe linter: closed vocabularies, typed errors
#include <catch2/catch_test_macros.hpp>

#include "recipes/recipe_validator.h"
#include "recipes/scientific_recipe.h"

#include <set>

using namespace sicnu::recipes;

namespace {

Json::Value validRecipe()
{
  Json::Value r( Json::objectValue );
  r["schema"] = kRecipeSchemaId;
  r["recipe_id"] = "lab.lab02_spectral_analysis";
  r["title"] = "Spectral Analysis";
  Json::Value goal( Json::objectValue );
  goal["intent"] = "spectral_analysis";
  goal["keywords_en"] = Json::Value( Json::arrayValue );
  goal["keywords_zh"] = Json::Value( Json::arrayValue );
  goal["modality"] = "optical";
  r["goal_pattern"] = goal;
  Json::Value origin( Json::objectValue );
  origin["kind"] = "labspec_d2";
  origin["lab_id"] = "lab02_spectral_analysis";
  origin["compiled_by"] = kCompilerId;
  r["teaching_origin"] = origin;
  r["required_assets"] = Json::Value( Json::arrayValue );
  Json::Value s1( Json::objectValue );
  s1["id"] = "s01_load";
  s1["index"] = 0;
  s1["kind"] = "human_only";
  s1["human_only"] = true;
  s1["boundary"] = "ui_action";
  Json::Value s2( Json::objectValue );
  s2["id"] = "s02_ndvi";
  s2["index"] = 1;
  s2["kind"] = "operator";
  s2["operator_id"] = "rs:spectral_index";
  s2["depends_on"] = Json::Value( Json::arrayValue );
  s2["depends_on"].append( "s01_load" );
  Json::Value hook( Json::objectValue );
  hook["kind"] = "artifact_exists";
  hook["target"] = "outputs/ndvi.tif";
  s2["verifier_hooks"] = Json::Value( Json::arrayValue );
  s2["verifier_hooks"].append( hook );
  r["stages"] = Json::Value( Json::arrayValue );
  r["stages"].append( s1 );
  r["stages"].append( s2 );
  Json::Value pf( Json::objectValue );
  pf["required_operators"] = Json::Value( Json::arrayValue );
  pf["required_assets"] = Json::Value( Json::arrayValue );
  r["preflight"] = pf;
  return r;
}

std::set<std::string> codes( const RecipeDiagnostics &diags )
{
  std::set<std::string> out;
  for ( const auto &d : diags )
    if ( d.severity == DiagnosticSeverity::Error )
      out.insert( d.code );
  return out;
}

} // namespace

TEST_CASE( "A well-formed recipe validates clean", "[validator]" )
{
  const auto diags = validateRecipe( validRecipe() );
  for ( const auto &d : diags )
    INFO( d.message );
  REQUIRE( !hasErrors( diags ) );
}

TEST_CASE( "Schema + recipe_id discipline", "[validator]" )
{
  Json::Value r = validRecipe();
  r["schema"] = "harness_recipe";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kSchemaMismatch ) == 1 );

  r = validRecipe();
  r["recipe_id"] = "harness.optical_ndvi";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadRecipeId ) == 1 );

  r = validRecipe();
  r["recipe_id"] = "lab.BAD ID";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadRecipeId ) == 1 );
}

TEST_CASE( "Stage vocabulary is closed", "[validator]" )
{
  Json::Value r = validRecipe();
  r["stages"][1]["kind"] = "teleport";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadStageKind ) == 1 );

  r = validRecipe();
  r["stages"][1]["verifier_hooks"][0]["kind"] = "run_shell";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadHookKind ) == 1 );
}

TEST_CASE( "depends_on must reference earlier stages", "[validator]" )
{
  Json::Value r = validRecipe();
  r["stages"][0]["depends_on"] = Json::Value( Json::arrayValue );
  r["stages"][0]["depends_on"].append( "s02_ndvi" ); // forward ref
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadDependsOn ) == 1 );

  r = validRecipe();
  r["stages"][1]["depends_on"][0] = "s02_ndvi"; // self ref
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadDependsOn ) == 1 );

  r = validRecipe();
  r["stages"][1]["depends_on"][0] = "nonexistent";
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kBadDependsOn ) == 1 );
}

TEST_CASE( "Operator stage requires operator_id; reflection requires prompt", "[validator]" )
{
  Json::Value r = validRecipe();
  r["stages"][1].removeMember( "operator_id" );
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kMissingField ) >= 1 );

  r = validRecipe();
  Json::Value refl( Json::objectValue );
  refl["id"] = "reflection_1";
  refl["index"] = 2;
  refl["kind"] = "reflection";
  r["stages"].append( refl );
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kMissingField ) >= 1 );
}

TEST_CASE( "Unknown keys are warnings, not errors (lint discipline)", "[validator]" )
{
  Json::Value r = validRecipe();
  r["surprise"] = 1;
  r["stages"][0]["surprise_stage_key"] = true;
  const auto diags = validateRecipe( r );
  int warnings = 0;
  for ( const auto &d : diags )
    if ( d.code == diag_codes::kUnknownKey &&
         d.severity == DiagnosticSeverity::Warning )
      ++warnings;
  REQUIRE( warnings == 2 );
  REQUIRE( !hasErrors( diags ) );
}

TEST_CASE( "Compiled-with-no_steps stub fails validation", "[validator]" )
{
  Json::Value r = validRecipe();
  Json::Value comp( Json::objectValue );
  comp["diagnostics"] = Json::Value( Json::arrayValue );
  comp["diagnostics"].append( "no_steps" );
  r["compilation"] = comp;
  REQUIRE( codes( validateRecipe( r ) ).count( diag_codes::kNoSteps ) == 1 );
}

TEST_CASE( "Missing teaching_origin is a warning (authored recipes allowed)", "[validator]" )
{
  Json::Value r = validRecipe();
  r.removeMember( "teaching_origin" );
  const auto diags = validateRecipe( r );
  bool warned = false;
  for ( const auto &d : diags )
    if ( d.code == diag_codes::kMissingTeachingOrigin &&
         d.severity == DiagnosticSeverity::Warning )
      warned = true;
  REQUIRE( warned );
  REQUIRE( !hasErrors( diags ) );
}
