// src/recipes/scientific_recipe.cpp
#include "recipes/scientific_recipe.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>

namespace sicnu::recipes {

bool isScientificRecipe( const Json::Value &doc )
{
  // isString guard: registry scans feed hostile files through here, and
  // asString() on an object/array raises Json::LogicError.
  return doc.isObject() && doc[ "schema" ].isString() &&
         doc[ "schema" ].asString() == kRecipeSchemaId;
}

std::string serializeRecipe( const Json::Value &recipe )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  builder["commentStyle"] = "None";
  builder["enableYAMLCompatibility"] = false;
  builder["dropNullPlaceholders"] = false;
  builder["useSpecialFloats"] = false;
  builder["emitUTF8"] = true; // keep 中文 readable in committed artifacts
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  std::ostringstream out;
  writer->write( recipe, &out );
  return out.str();
}

bool parseRecipeJson( const std::string &text, Json::Value &out, std::string *error )
{
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  // Bound nesting: lab/recipe files are authored, but the parser is also the
  // entry point for registry scans — a depth cap costs nothing.
  builder["stackLimit"] = 256;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errors;
  if ( !reader->parse( text.data(), text.data() + text.size(), &out, &errors ) )
  {
    if ( error )
      *error = errors;
    return false;
  }
  return true;
}

namespace {

/// ASCII-fold a title into a stage-id slug: lowercase, [a-z0-9] runs joined
/// by '_', non-ASCII (zh titles) dropped — falls back to "step" when empty.
std::string slugify( const std::string &text )
{
  std::string slug;
  bool pendingDash = false;
  for ( const unsigned char c : text )
  {
    if ( std::isalnum( c ) )
    {
      if ( pendingDash && !slug.empty() )
        slug.push_back( '_' );
      pendingDash = false;
      slug.push_back( static_cast<char>( std::tolower( c ) ) );
    }
    else if ( c < 0x80 )
      pendingDash = true; // ASCII punctuation → separator
    // UTF-8 continuation bytes (>=0x80) are skipped entirely
  }
  while ( !slug.empty() && slug.back() == '_' )
    slug.pop_back();
  return slug.empty() ? "step" : slug.substr( 0, 40 );
}

} // namespace

std::string makeStageId( int index, const std::string &title, const std::string &preferredId )
{
  std::ostringstream id;
  id << 's';
  if ( index + 1 < 10 )
    id << '0';
  id << ( index + 1 ) << '_';
  if ( !preferredId.empty() )
    id << slugify( preferredId );
  else
    id << slugify( title );
  return id.str();
}

std::string recipeIdForLab( const std::string &labId )
{
  return std::string( kRecipeIdPrefix ) + labId;
}

// ---------------------------------------------------------------------------
// Semantic equivalence
// ---------------------------------------------------------------------------
namespace {

void addDiff( std::vector<std::string> *diff, const std::string &line )
{
  if ( diff )
    diff->push_back( line );
}

std::vector<std::string> assetPaths( const Json::Value &recipe )
{
  std::vector<std::string> out;
  for ( const auto &asset : recipe["required_assets"] )
    if ( asset["predicate"]["path"].isString() )
      out.push_back( asset["predicate"]["path"].asString() );
  std::sort( out.begin(), out.end() );
  return out;
}

/// hook → "kind|target|ref" tokens, sorted (multiset compare).
std::vector<std::string> hookKeys( const Json::Value &stage )
{
  std::vector<std::string> out;
  for ( const auto &h : stage["verifier_hooks"] )
    out.push_back( h.get( "kind", "" ).asString() + "|" +
                   h.get( "target", "" ).asString() + "|" +
                   h.get( "ref", "" ).asString() );
  std::sort( out.begin(), out.end() );
  return out;
}

} // namespace

bool recipesEquivalent( const Json::Value &a, const Json::Value &b,
                        std::vector<std::string> *diff )
{
  bool same = true;
  auto check = [&]( bool cond, const std::string &line )
  {
    if ( !cond )
    {
      same = false;
      addDiff( diff, line );
    }
  };

  check( a["recipe_id"] == b["recipe_id"],
         "recipe_id: " + a["recipe_id"].asString() + " != " + b["recipe_id"].asString() );
  check( a["goal_pattern"]["intent"] == b["goal_pattern"]["intent"], "goal_pattern.intent differs" );
  check( a["goal_pattern"]["modality"] == b["goal_pattern"]["modality"], "goal_pattern.modality differs" );
  check( assetPaths( a ) == assetPaths( b ), "required asset paths differ" );

  const Json::Value &sa = a["stages"];
  const Json::Value &sb = b["stages"];
  check( sa.size() == sb.size(),
         "stage count: " + std::to_string( sa.size() ) + " != " + std::to_string( sb.size() ) );
  const Json::ArrayIndex n = std::min( sa.size(), sb.size() );
  for ( Json::ArrayIndex i = 0; i < n; ++i )
  {
    const Json::Value &x = sa[i];
    const Json::Value &y = sb[i];
    const std::string where = "stage " + std::to_string( i );
    check( x["kind"] == y["kind"],
           where + " kind: " + x["kind"].asString() + " != " + y["kind"].asString() );
    if ( x["kind"].asString() == stage_kinds::kOperator )
    {
      check( x["operator_id"] == y["operator_id"], where + " operator_id differs" );
      check( x["params"] == y["params"], where + " params differ" );
    }
    if ( x["kind"].asString() == stage_kinds::kHumanOnly )
      check( x["boundary"] == y["boundary"], where + " boundary differs" );
    if ( x["kind"].asString() == stage_kinds::kReflection )
      check( x["prompt"] == y["prompt"], where + " reflection prompt differs" );
    check( hookKeys( x ) == hookKeys( y ), where + " verifier hooks differ" );
  }
  check( hookKeys( a ) == hookKeys( b ), "lab-scoped verifier hooks differ" );

  // Evidence: grading refs + artifact paths + claims (order-insensitive).
  const Json::Value &ea = a["evidence"];
  const Json::Value &eb = b["evidence"];
  check( ea["grading_rules"] == eb["grading_rules"], "evidence.grading_rules differs" );
  check( ea["grading_pipeline"] == eb["grading_pipeline"], "evidence.grading_pipeline differs" );
  std::vector<std::string> apaths, bpaths;
  for ( const auto &v : ea["artifacts"] ) apaths.push_back( v.get( "path", "" ).asString() );
  for ( const auto &v : eb["artifacts"] ) bpaths.push_back( v.get( "path", "" ).asString() );
  std::sort( apaths.begin(), apaths.end() );
  std::sort( bpaths.begin(), bpaths.end() );
  check( apaths == bpaths, "evidence.artifacts differ" );
  std::vector<std::string> ac, bc;
  for ( const auto &v : ea["claims"] ) ac.push_back( serializeRecipe( v ) );
  for ( const auto &v : eb["claims"] ) bc.push_back( serializeRecipe( v ) );
  std::sort( ac.begin(), ac.end() );
  std::sort( bc.begin(), bc.end() );
  check( ac == bc, "evidence.claims differ" );

  return same;
}

} // namespace sicnu::recipes
