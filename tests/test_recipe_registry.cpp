// test_recipe_registry.cpp — directory-backed ScientificRecipeRegistry
#include <catch2/catch_test_macros.hpp>

#include "recipes/recipe_registry.h"
#include "recipes/scientific_recipe.h"

#include <filesystem>
#include <fstream>

using namespace sicnu::recipes;
namespace fs = std::filesystem;

namespace {

Json::Value recipeWithId( const std::string &id, const std::string &intent = "ndvi" )
{
  Json::Value r( Json::objectValue );
  r["schema"] = kRecipeSchemaId;
  r["recipe_id"] = id;
  r["title"] = "t " + id;
  Json::Value goal( Json::objectValue );
  goal["intent"] = intent;
  goal["keywords_en"] = Json::Value( Json::arrayValue );
  goal["keywords_zh"] = Json::Value( Json::arrayValue );
  goal["modality"] = "optical";
  r["goal_pattern"] = goal;
  Json::Value origin( Json::objectValue );
  origin["kind"] = "authored";
  origin["lab_id"] = id.substr( 4 );
  origin["compiled_by"] = "test";
  r["teaching_origin"] = origin;
  r["required_assets"] = Json::Value( Json::arrayValue );
  Json::Value s( Json::objectValue );
  s["id"] = "s01_x";
  s["index"] = 0;
  s["kind"] = "operator";
  s["operator_id"] = "rs:x";
  r["stages"] = Json::Value( Json::arrayValue );
  r["stages"].append( s );
  Json::Value pf( Json::objectValue );
  pf["required_operators"] = Json::Value( Json::arrayValue );
  pf["required_operators"].append( "rs:x" );
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

} // namespace

TEST_CASE( "Registry loads valid recipes, reports problems, fails closed", "[registry]" )
{
  const fs::path dir = fs::temp_directory_path() / "recipes_registry";
  fs::remove_all( dir );

  writeJson( dir / "a.json", recipeWithId( "lab.aaa", "ndvi" ) );
  writeJson( dir / "b.json", recipeWithId( "lab.bbb", "sar_change" ) );
  // invalid JSON
  { std::ofstream out( dir / "broken.json" ); out << "{oops"; }
  // wrong schema id
  {
    Json::Value wrong = recipeWithId( "lab.ccc" );
    wrong["schema"] = "harness_recipe";
    writeJson( dir / "wrong.json", wrong );
  }
  // duplicate id
  writeJson( dir / "dup.json", recipeWithId( "lab.aaa", "other" ) );
  // validator-failing doc (no stages)
  {
    Json::Value bad = recipeWithId( "lab.empty" );
    bad["stages"] = Json::Value( Json::arrayValue );
    writeJson( dir / "empty.json", bad );
  }

  ScientificRecipeRegistry registry;
  registry.setDirectory( dir.string() );
  const int n = registry.reload();
  REQUIRE( n == 2 );
  REQUIRE( registry.loaded() );
  REQUIRE( registry.status() == "degraded" ); // problems exist but load succeeded
  REQUIRE( registry.loadProblems().size() == 4 ); // broken + wrong-schema + duplicate + invalid

  REQUIRE( registry.recipeIds() ==
           std::vector<std::string>{ "lab.aaa", "lab.bbb" } );
  REQUIRE( registry.recipe( "lab.bbb" )["title"].asString() == "t lab.bbb" );
  REQUIRE( registry.recipe( "lab.nope" ).isNull() );
}

TEST_CASE( "Missing directory → unavailable status, typed problem", "[registry]" )
{
  ScientificRecipeRegistry registry;
  registry.setDirectory( "/nonexistent/recipes" );
  REQUIRE( registry.reload() == 0 );
  REQUIRE( registry.status() == "unavailable" );
  REQUIRE( registry.loadProblems().size() == 1 );
}

TEST_CASE( "Registry scans never throw on hostile JSON shapes", "[registry][negative]" )
{
  // The registry is a directory scanner: every file it reads is untrusted
  // input, and a non-string schema/recipe_id must degrade to a load problem,
  // not escape as Json::LogicError through asString().
  const fs::path dir = fs::temp_directory_path() / "recipes_hostile";
  fs::remove_all( dir );
  fs::create_directories( dir );

  // schema as an object
  { std::ofstream out( dir / "obj_schema.json" ); out << R"({"schema": {"a": 1}})"; }
  // recipe_id as an array
  {
    Json::Value bad = recipeWithId( "lab.arr" );
    bad["recipe_id"] = Json::Value( Json::arrayValue );
    writeJson( dir / "arr_id.json", bad );
  }
  // stage kind as an object
  {
    Json::Value bad = recipeWithId( "lab.kind" );
    bad["recipe_id"] = "lab.kind";
    bad["stages"][0]["kind"] = Json::Value( Json::objectValue );
    writeJson( dir / "obj_kind.json", bad );
  }
  // hook kind as an object
  {
    Json::Value bad = recipeWithId( "lab.hook" );
    Json::Value hook( Json::objectValue );
    hook["kind"] = Json::Value( Json::objectValue );
    bad["verifier_hooks"] = Json::Value( Json::arrayValue );
    bad["verifier_hooks"].append( hook );
    writeJson( dir / "obj_hook.json", bad );
  }

  ScientificRecipeRegistry registry;
  registry.setDirectory( dir.string() );
  REQUIRE( registry.reload() == 0 ); // nothing loads; nothing crashes
  // With zero accepted recipes the registry reports unavailable (status()
  // contract) even though the scan itself succeeded and collected problems.
  REQUIRE( registry.status() == "unavailable" );
  for ( const auto &problem : registry.loadProblems() )
    WARN( "problem: " << problem );
  REQUIRE( registry.loadProblems().size() == 4 );
  fs::remove_all( dir );
}

TEST_CASE( "listRecipes pages are bounded and deterministic", "[registry]" )
{
  const fs::path dir = fs::temp_directory_path() / "recipes_paged";
  fs::remove_all( dir );
  for ( int i = 0; i < 5; ++i )
  {
    char id[64];
    std::snprintf( id, sizeof( id ), "lab.r%02d", i );
    writeJson( dir / ( std::string( id ) + ".json" ), recipeWithId( id ) );
  }
  ScientificRecipeRegistry registry;
  registry.setDirectory( dir.string() );
  REQUIRE( registry.reload() == 5 );

  const Json::Value page0 = registry.listRecipes( 0, 2 );
  REQUIRE( page0["total"].asInt() == 5 );
  REQUIRE( page0["recipes"].size() == 2 );
  REQUIRE( page0["recipes"][0]["recipe_id"].asString() == "lab.r00" );

  const Json::Value page1 = registry.listRecipes( 1, 2 );
  REQUIRE( page1["recipes"].size() == 2 );
  REQUIRE( page1["recipes"][0]["recipe_id"].asString() == "lab.r02" );

  // page size is hard-capped
  const Json::Value huge = registry.listRecipes( 0, 100000 );
  REQUIRE( huge["page_size"].asInt() == ScientificRecipeRegistry::kMaxPageSize );
}
