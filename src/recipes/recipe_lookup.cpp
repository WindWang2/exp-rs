// src/recipes/recipe_lookup.cpp
#include "recipes/recipe_lookup.h"

#include "recipes/recipe_registry.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace sicnu::recipes {

namespace {

/// Lowercase alnum tokens (len>=3, ASCII) from free text.
std::set<std::string> tokenize( const std::string &text )
{
  std::set<std::string> tokens;
  std::string cur;
  for ( const unsigned char c : text )
  {
    if ( std::isalnum( c ) && c < 0x80 )
      cur.push_back( static_cast<char>( std::tolower( c ) ) );
    else if ( !cur.empty() )
    {
      if ( cur.size() >= 3 )
        tokens.insert( cur );
      cur.clear();
    }
  }
  if ( cur.size() >= 3 )
    tokens.insert( cur );
  return tokens;
}

/// Non-ASCII substrings (zh runs) from free text — matched verbatim.
std::vector<std::string> zhRuns( const std::string &text )
{
  std::vector<std::string> runs;
  std::string cur;
  for ( const unsigned char c : text )
  {
    if ( c >= 0x80 )
      cur.push_back( static_cast<char>( c ) );
    else if ( !cur.empty() )
    {
      runs.push_back( cur );
      cur.clear();
    }
  }
  if ( !cur.empty() )
    runs.push_back( cur );
  return runs;
}

Json::Value summarize( const std::string &id, const Json::Value &doc,
                       const std::vector<std::string> &matched )
{
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
  int operatorStages = 0;
  if ( doc["stages"].isArray() )
    for ( const auto &s : doc["stages"] )
      if ( s.get( "kind", "" ).asString() == "operator" )
        ++operatorStages;
  summary["operator_stages"] = operatorStages;
  if ( doc["compilation"].isObject() && doc["compilation"].isMember( "coverage" ) )
    summary["coverage"] = doc["compilation"]["coverage"];
  Json::Value matchedArr( Json::arrayValue );
  for ( const auto &m : matched )
    matchedArr.append( m );
  summary["matched"] = std::move( matchedArr );
  return summary;
}

} // namespace

std::vector<RecipeHit> searchRecipes( const ScientificRecipeRegistry &registry,
                                      const RecipeQuery &query )
{
  std::vector<RecipeHit> hits;
  const std::set<std::string> textTokens = tokenize( query.text );
  const std::vector<std::string> zhTokens = zhRuns( query.text );
  const std::string intentLower = [&]
  {
    std::string s = query.intent;
    std::transform( s.begin(), s.end(), s.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return s;
  }();

  for ( const std::string &id : registry.recipeIds() )
  {
    const Json::Value doc = registry.recipe( id );
    const Json::Value &goal = doc["goal_pattern"];
    double score = 0.0;
    std::vector<std::string> matched;

    if ( !intentLower.empty() )
    {
      // isString guards: asString() on a non-string raises Json::LogicError,
      // and lookup runs over everything the registry admitted — the
      // validator pins intent's type, but stay defensive for both fields.
      const std::string intent = goal[ "intent" ].isString() ? goal[ "intent" ].asString()
                                                             : std::string{};
      if ( intent == intentLower )
      {
        score += 4.0;
        matched.push_back( "intent:" + intent );
      }
      else if ( !intent.empty() &&
                ( intent.find( intentLower ) != std::string::npos ||
                  intentLower.find( intent ) != std::string::npos ) )
      {
        score += 1.0;
        matched.push_back( "intent~:" + intent );
      }
    }

    const std::string docModality = goal[ "modality" ].isString() ? goal[ "modality" ].asString()
                                                                  : std::string{};
    if ( !query.modality.empty() && docModality == query.modality )
    {
      score += 2.0;
      matched.push_back( "modality:" + query.modality );
    }

    // en keywords: token overlap.
    std::set<std::string> docKeywords;
    if ( goal.isObject() )
      for ( const auto &k : goal["keywords_en"] )
        if ( k.isString() )
          docKeywords.insert( k.asString() );
    for ( const auto &tok : textTokens )
      if ( docKeywords.count( tok ) )
      {
        score += 1.0;
        matched.push_back( "kw:" + tok );
      }

    // zh keywords: verbatim substring against the doc's zh keyword strings.
    if ( goal.isObject() )
      for ( const auto &zh : zhTokens )
        for ( const auto &k : goal["keywords_zh"] )
          if ( k.isString() && k.asString().find( zh ) != std::string::npos )
          {
            score += 1.0;
            matched.push_back( "kw_zh:" + zh );
            break;
          }

    // operator coverage: +1 per requested operator the recipe exercises.
    if ( !query.operators.empty() && doc["stages"].isArray() )
    {
      std::set<std::string> stageOps;
      for ( const auto &s : doc["stages"] )
        if ( s["operator_id"].isString() )
          stageOps.insert( s["operator_id"].asString() );
      for ( const auto &op : query.operators )
        if ( stageOps.count( op ) )
        {
          score += 1.0;
          matched.push_back( "op:" + op );
        }
    }

    if ( score > 0.0 )
      hits.push_back( RecipeHit{ id, score, summarize( id, doc, matched ) } );
  }

  std::sort( hits.begin(), hits.end(), []( const RecipeHit &a, const RecipeHit &b )
  {
    if ( a.score != b.score )
      return a.score > b.score;
    return a.recipeId < b.recipeId;
  } );

  int limit = query.limit;
  if ( limit <= 0 || limit > ScientificRecipeRegistry::kMaxPageSize )
    limit = ScientificRecipeRegistry::kMaxPageSize;
  if ( static_cast<int>( hits.size() ) > limit )
    hits.resize( static_cast<std::size_t>( limit ) );
  return hits;
}

Json::Value searchRecipesJson( const ScientificRecipeRegistry &registry,
                               const RecipeQuery &query )
{
  const auto hits = searchRecipes( registry, query );
  Json::Value out( Json::objectValue );
  Json::Value arr( Json::arrayValue );
  for ( const auto &h : hits )
  {
    Json::Value item = h.summary;
    item["score"] = h.score;
    arr.append( std::move( item ) );
  }
  out["hits"] = std::move( arr );
  out["total"] = static_cast<int>( hits.size() );
  Json::Value echo( Json::objectValue );
  if ( !query.intent.empty() )
    echo["intent"] = query.intent;
  if ( !query.text.empty() )
    echo["text"] = query.text;
  if ( !query.modality.empty() )
    echo["modality"] = query.modality;
  out["query"] = std::move( echo );
  return out;
}

} // namespace sicnu::recipes
