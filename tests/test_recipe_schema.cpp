// test_recipe_schema.cpp — ScientificRecipe value-object & serialization tests
#include <catch2/catch_test_macros.hpp>

#include "recipes/scientific_recipe.h"

#include <json/json.h>

using namespace sicnu::recipes;

namespace {

Json::Value minimalRecipe()
{
  Json::Value doc( Json::objectValue );
  doc["schema"] = kRecipeSchemaId;
  doc["recipe_id"] = "lab.lab02_spectral_analysis";
  doc["title"] = "Spectral Analysis";
  Json::Value goal( Json::objectValue );
  goal["intent"] = "spectral_analysis";
  goal["keywords_en"] = Json::Value( Json::arrayValue );
  goal["keywords_zh"] = Json::Value( Json::arrayValue );
  goal["modality"] = "optical";
  doc["goal_pattern"] = goal;
  Json::Value stage( Json::objectValue );
  stage["id"] = "s01_x";
  stage["index"] = 0;
  stage["kind"] = "operator";
  stage["operator_id"] = "rs:spectral_index";
  doc["stages"] = Json::Value( Json::arrayValue );
  doc["stages"].append( stage );
  return doc;
}

} // namespace

TEST_CASE( "isScientificRecipe sniffs the schema id", "[recipe][schema]" )
{
  REQUIRE( isScientificRecipe( minimalRecipe() ) );

  Json::Value wrong = minimalRecipe();
  wrong["schema"] = "harness_recipe";
  REQUIRE( !isScientificRecipe( wrong ) );

  REQUIRE( !isScientificRecipe( Json::Value( Json::arrayValue ) ) );
  REQUIRE( !isScientificRecipe( Json::Value() ) );
}

TEST_CASE( "serialize/parse round-trips identically", "[recipe][schema]" )
{
  const Json::Value doc = minimalRecipe();
  const std::string text = serializeRecipe( doc );

  Json::Value reparsed;
  std::string error;
  REQUIRE( parseRecipeJson( text, reparsed, &error ) );
  REQUIRE( reparsed == doc );
}

TEST_CASE( "serialization is deterministic (byte-identical on re-serialize)", "[recipe][schema]" )
{
  const Json::Value doc = minimalRecipe();
  REQUIRE( serializeRecipe( doc ) == serializeRecipe( doc ) );

  // Key insertion order does not affect output (jsoncpp sorts object keys).
  Json::Value shuffled( Json::objectValue );
  shuffled["stages"] = doc["stages"];
  shuffled["title"] = doc["title"];
  shuffled["goal_pattern"] = doc["goal_pattern"];
  shuffled["recipe_id"] = doc["recipe_id"];
  shuffled["schema"] = doc["schema"];
  REQUIRE( serializeRecipe( shuffled ) == serializeRecipe( doc ) );
}

TEST_CASE( "parseRecipeJson rejects malformed input", "[recipe][schema]" )
{
  Json::Value out;
  std::string error;
  REQUIRE( !parseRecipeJson( "{ not json", out, &error ) );
  REQUIRE( !error.empty() );
}

TEST_CASE( "makeStageId is deterministic and slugged", "[recipe][schema]" )
{
  REQUIRE( makeStageId( 0, "Load Sample Data", "" ) == "s01_load_sample_data" );
  REQUIRE( makeStageId( 2, "Calculate NDVI", "" ) == "s03_calculate_ndvi" );
  // D3 preferred ids win over titles.
  REQUIRE( makeStageId( 0, "辐射定标", "s1" ) == "s01_s1" );
  // zh-only titles fall back to a non-empty slug.
  REQUIRE( makeStageId( 4, "观察光谱曲线", "" ) == "s05_step" );
}

TEST_CASE( "recipeIdForLab namespaces ids under lab.", "[recipe][schema]" )
{
  REQUIRE( recipeIdForLab( "lab02_spectral_analysis" ) ==
           "lab.lab02_spectral_analysis" );
}
