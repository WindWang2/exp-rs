// test_recipe_lookup.cpp — semantic lookup adapter over the recipe registry
#include <catch2/catch_test_macros.hpp>

#include "recipes/recipe_lookup.h"
#include "recipes/recipe_registry.h"
#include "recipes/scientific_recipe.h"

#include <filesystem>
#include <fstream>

using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace {

Json::Value recipeWith( const std::string &id, const std::string &intent,
                        const std::string &modality,
                        std::vector<std::string> keywordsEn,
                        std::vector<std::string> keywordsZh,
                        std::vector<std::string> ops )
{
  Json::Value r( Json::objectValue );
  r["schema"] = kRecipeSchemaId;
  r["recipe_id"] = id;
  r["title"] = "t " + id;
  Json::Value goal( Json::objectValue );
  goal["intent"] = intent;
  for ( const auto &k : keywordsEn )
    goal["keywords_en"].append( k );
  for ( const auto &k : keywordsZh )
    goal["keywords_zh"].append( k );
  goal["modality"] = modality;
  r["goal_pattern"] = goal;
  Json::Value origin( Json::objectValue );
  origin["kind"] = "authored";
  origin["lab_id"] = id.substr( 4 );
  origin["compiled_by"] = "test";
  r["teaching_origin"] = origin;
  r["required_assets"] = Json::Value( Json::arrayValue );
  r["stages"] = Json::Value( Json::arrayValue );
  int i = 0;
  for ( const auto &op : ops )
  {
    Json::Value s( Json::objectValue );
    s["id"] = "s" + std::to_string( ++i );
    s["index"] = i - 1;
    s["kind"] = "operator";
    s["operator_id"] = op;
    r["stages"].append( s );
  }
  Json::Value pf( Json::objectValue );
  for ( const auto &op : ops )
    pf["required_operators"].append( op );
  pf["required_assets"] = Json::Value( Json::arrayValue );
  r["preflight"] = pf;
  return r;
}

void writeJson( const fs::path &path, const Json::Value &doc )
{
  fs::create_directories( path.parent_path() );
  std::ofstream out( path, std::ios::binary );
  out << serializeRecipe( doc );
}

/// Registry fixture: ndvi (optical), sar_change (sar), slope (terrain).
ScientificRecipeRegistry makeRegistry( const fs::path &dir )
{
  fs::remove_all( dir );
  writeJson( dir / "a.json",
             recipeWith( "lab.lab02_spectral", "spectral_analysis", "optical",
                         { "spectral", "ndvi", "index" }, { "光谱指数" },
                         { "rs:spectral_index", "rs:band_math" } ) );
  writeJson( dir / "b.json",
             recipeWith( "lab.lab12_sar", "sar_processing", "sar",
                         { "sar", "speckle", "calibrate" }, { "相干斑" },
                         { "rs:sar_calibrate" } ) );
  writeJson( dir / "c.json",
             recipeWith( "lab.lab05_terrain", "terrain_analysis", "terrain",
                         { "terrain", "slope", "dem" }, { "地形" },
                         { "rs:slope", "rs:hillshade" } ) );

  ScientificRecipeRegistry registry;
  registry.setDirectory( dir.string() );
  REQUIRE( registry.reload() == 3 );
  return registry;
}

} // namespace

TEST_CASE( "Intent match outranks keyword-only matches", "[lookup]" )
{
  const auto registry = makeRegistry( fs::temp_directory_path() / "lookup_a" );
  RecipeQuery q;
  q.intent = "spectral_analysis";
  const auto hits = searchRecipes( registry, q );
  REQUIRE( hits.size() == 1 );
  REQUIRE( hits[0].recipeId == "lab.lab02_spectral" );
  REQUIRE( hits[0].score == 4.0 );
}

TEST_CASE( "Free text hits en + zh keywords", "[lookup]" )
{
  const auto registry = makeRegistry( fs::temp_directory_path() / "lookup_b" );
  RecipeQuery q;
  q.text = "ndvi index"; // two keyword hits → score 2
  auto hits = searchRecipes( registry, q );
  REQUIRE( hits.size() == 1 );
  REQUIRE( hits[0].score == 2.0 );

  q.text = "光谱指数"; // zh substring against keywords_zh
  hits = searchRecipes( registry, q );
  REQUIRE( hits.size() == 1 );
  REQUIRE( hits[0].recipeId == "lab.lab02_spectral" );
}

TEST_CASE( "Modality + operator facets combine and rank deterministically", "[lookup]" )
{
  const auto registry = makeRegistry( fs::temp_directory_path() / "lookup_c" );
  RecipeQuery q;
  q.operators = { "rs:sar_calibrate" };
  q.modality = "sar";
  const auto hits = searchRecipes( registry, q );
  REQUIRE( hits.size() == 1 );
  REQUIRE( hits[0].recipeId == "lab.lab12_sar" );
  REQUIRE( hits[0].score == 3.0 ); // modality(2) + op(1)
}

TEST_CASE( "No match → empty (typed absence, not error)", "[lookup]" )
{
  const auto registry = makeRegistry( fs::temp_directory_path() / "lookup_d" );
  RecipeQuery q;
  q.text = "quantum chromodynamics";
  q.intent = "nonexistent_intent";
  q.modality = "lidar";
  REQUIRE( searchRecipes( registry, q ).empty() );
}

TEST_CASE( "Deterministic ordering: equal scores sort by recipe_id", "[lookup]" )
{
  const fs::path dir = fs::temp_directory_path() / "lookup_tie";
  fs::remove_all( dir );
  writeJson( dir / "z.json",
             recipeWith( "lab.zzz", "same", "optical", {}, {}, { "rs:a" } ) );
  writeJson( dir / "a.json",
             recipeWith( "lab.aaa", "same", "optical", {}, {}, { "rs:a" } ) );
  ScientificRecipeRegistry registry;
  registry.setDirectory( dir.string() );
  REQUIRE( registry.reload() == 2 );

  RecipeQuery q;
  q.intent = "same";
  const auto hits = searchRecipes( registry, q );
  REQUIRE( hits.size() == 2 );
  REQUIRE( hits[0].recipeId == "lab.aaa" );
  REQUIRE( hits[1].recipeId == "lab.zzz" );
}

TEST_CASE( "JSON wire surface echoes the query and carries summaries", "[lookup]" )
{
  const auto registry = makeRegistry( fs::temp_directory_path() / "lookup_e" );
  RecipeQuery q;
  q.intent = "terrain_analysis";
  const Json::Value out = searchRecipesJson( registry, q );
  REQUIRE( out["total"].asInt() == 1 );
  REQUIRE( out["hits"][0]["recipe_id"].asString() == "lab.lab05_terrain" );
  REQUIRE( out["hits"][0]["operator_stages"].asInt() == 2 );
  REQUIRE( out["query"]["intent"].asString() == "terrain_analysis" );
}
