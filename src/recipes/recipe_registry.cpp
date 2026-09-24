// src/recipes/recipe_registry.cpp
#include "recipes/recipe_registry.h"

#include "recipes/recipe_validator.h"
#include "recipes/scientific_recipe.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "platform/portable.h"

namespace sicnu::recipes {

namespace fs = std::filesystem;

void ScientificRecipeRegistry::setDirectory( const std::string &directory )
{
  mDirectory = directory;
  mLoaded = false; // next query re-scans the new directory
}

std::string ScientificRecipeRegistry::directory() const
{
  return mDirectory.empty() ? defaultDirectory() : mDirectory;
}

std::string ScientificRecipeRegistry::defaultDirectory() const
{
  // Path-valued env: UTF-8 boundary + UTF-8 path decode (the narrow
  // fs::is_directory(env) would decode ACP bytes on Windows).
  const std::string env = sicnu::portable::envUtf8( "SICNU_SCIENTIFIC_RECIPES_DIR" );
  if ( !env.empty() && fs::is_directory( sicnu::portable::pathFromUtf8( env ) ) )
    return env;

  const fs::path cwd = fs::current_path() / "data" / "agent" / "scientific_recipes";
  if ( fs::is_directory( cwd ) )
    return cwd.string();

#ifdef SICNU_SOURCE_DIR
  const fs::path source =
    fs::path( SICNU_SOURCE_DIR ) / "data" / "agent" / "scientific_recipes";
  if ( fs::is_directory( source ) )
    return source.string();
#endif
  return {};
}

int ScientificRecipeRegistry::reload()
{
  mLoaded = false;
  mRecipes.clear();
  mLoadProblems.clear();

  const std::string dir = directory();
  std::error_code ec;
  if ( dir.empty() || !fs::is_directory( dir, ec ) )
  {
    mLoadProblems.push_back( "scientific recipe directory not found: " +
                             ( dir.empty() ? std::string( "<none>" ) : dir ) );
    mLoaded = true;
    return 0;
  }

  std::vector<fs::path> files;
  for ( const auto &entry : fs::directory_iterator( dir, ec ) )
  {
    if ( ec )
      break;
    if ( entry.is_regular_file() && entry.path().extension() == ".json" )
      files.push_back( entry.path() );
  }
  std::sort( files.begin(), files.end() );

  int loaded = 0;
  for ( const auto &path : files )
  {
    const std::string filename = path.filename().string();
    std::ifstream in( path, std::ios::binary );
    if ( !in )
    {
      mLoadProblems.push_back( filename + ": unreadable" );
      continue;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();

    Json::Value doc;
    std::string parseError;
    if ( !parseRecipeJson( buffer.str(), doc, &parseError ) || !doc.isObject() )
    {
      mLoadProblems.push_back( filename + ": invalid JSON" +
                               ( parseError.empty() ? "" : " (" + parseError + ")" ) );
      continue;
    }
    if ( !isScientificRecipe( doc ) )
    {
      mLoadProblems.push_back( filename + ": not a scientific recipe (schema id)" );
      continue;
    }
    RecipeDiagnostics diags = validateRecipe( doc );
    if ( hasErrors( diags ) )
    {
      std::string summary;
      for ( const auto &s : diagnosticStrings( diags ) )
        summary += " " + s;
      mLoadProblems.push_back( filename + ":" + summary );
      continue;
    }
    const std::string id = doc["recipe_id"].asString();
    if ( mRecipes.count( id ) )
    {
      mLoadProblems.push_back( filename + ": duplicate recipe_id " + id );
      continue;
    }
    mRecipes.emplace( id, std::move( doc ) );
    ++loaded;
  }
  mLoaded = true;
  return loaded;
}

std::string ScientificRecipeRegistry::status() const
{
  if ( !mLoaded || mRecipes.empty() )
    return "unavailable";
  return mLoadProblems.empty() ? "ok" : "degraded";
}

std::vector<std::string> ScientificRecipeRegistry::recipeIds() const
{
  std::vector<std::string> ids;
  ids.reserve( mRecipes.size() );
  for ( const auto &[id, _] : mRecipes )
    ids.push_back( id );
  return ids; // map iteration is already sorted
}

Json::Value ScientificRecipeRegistry::recipe( const std::string &recipeId ) const
{
  const auto it = mRecipes.find( recipeId );
  return it == mRecipes.end() ? Json::Value() : it->second;
}

Json::Value ScientificRecipeRegistry::listRecipes( int page, int pageSize ) const
{
  if ( pageSize <= 0 || pageSize > kMaxPageSize )
    pageSize = kMaxPageSize;
  if ( page < 0 )
    page = 0;

  Json::Value out( Json::objectValue );
  Json::Value items( Json::arrayValue );
  const int total = static_cast<int>( mRecipes.size() );
  int skipped = 0, emitted = 0;
  for ( const auto &[id, doc] : mRecipes )
  {
    if ( skipped++ < page * pageSize )
      continue;
    if ( emitted++ >= pageSize )
      break;
    Json::Value summary( Json::objectValue );
    summary["recipe_id"] = id;
    summary["title"] = doc.get( "title", "" );
    if ( doc.isMember( "title_zh" ) )
      summary["title_zh"] = doc["title_zh"];
    const Json::Value &goal = doc["goal_pattern"];
    if ( goal.isObject() )
    {
      summary["intent"] = goal.get( "intent", "" );
      summary["modality"] = goal.get( "modality", "" );
    }
    summary["stage_count"] = doc["stages"].isArray()
                               ? static_cast<int>( doc["stages"].size() ) : 0;
    if ( doc["compilation"].isObject() && doc["compilation"].isMember( "coverage" ) )
      summary["coverage"] = doc["compilation"]["coverage"];
    items.append( std::move( summary ) );
  }
  out["recipes"] = std::move( items );
  out["total"] = total;
  out["page"] = page;
  out["page_size"] = pageSize;
  return out;
}

} // namespace sicnu::recipes
