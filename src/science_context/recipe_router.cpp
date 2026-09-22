// src/science_context/recipe_router.cpp
#include "science_context/recipe_router.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace sicnu::science_context {

namespace {

std::string lower( std::string s )
{
    for ( char &c : s )
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    return s;
}

std::uint64_t fnv1a64( const std::string &s )
{
    std::uint64_t h = 14695981039346656037ull;
    for ( unsigned char c : s )
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex16( std::uint64_t v )
{
    static const char *kHex = "0123456789abcdef";
    std::string out( 16, '0' );
    for ( int i = 15; i >= 0; --i )
    {
        out[static_cast<std::size_t>( i )] = kHex[v & 0xf];
        v >>= 4;
    }
    return out;
}

} // namespace

void RecipeRouter::setRecipes( std::vector<RecipeDocument> recipes )
{
    mRecipes = std::move( recipes );
    std::sort( mRecipes.begin(), mRecipes.end(),
               []( const RecipeDocument &a, const RecipeDocument &b ) {
                   return a.recipeId < b.recipeId;
               } );
}

void RecipeRouter::clear()
{
    mRecipes.clear();
}

void RecipeRouter::setRegistryRevision( std::uint64_t revision )
{
    mRevision = revision;
}

std::string RecipeRouter::packDigest() const
{
    std::ostringstream oss;
    for ( const auto &r : mRecipes )
        oss << r.recipeId << '|' << r.intent << '|' << r.modality << ';';
    return hex16( fnv1a64( oss.str() ) );
}

RecipeDocument RecipeRouter::fromRecipeJson( const Json::Value &doc )
{
    RecipeDocument d;
    d.recipeId = doc.get( "recipe_id", "" ).asString();
    d.title = doc.get( "title", "" ).asString();
    const Json::Value &goal = doc["goal_pattern"];
    if ( goal.isObject() )
    {
        d.intent = goal.get( "intent", "" ).asString();
        d.modality = goal.get( "modality", "" ).asString();
        for ( const auto &k : goal["keywords_en"] )
        {
            if ( k.isString() )
                d.keywords.push_back( k.asString() );
        }
        for ( const auto &k : goal["keywords_zh"] )
        {
            if ( k.isString() )
                d.keywords.push_back( k.asString() );
        }
    }
    const Json::Value &stages = doc["stages"];
    if ( stages.isArray() )
    {
        d.stageCount = static_cast<int>( stages.size() );
        for ( const auto &st : stages )
        {
            const std::string kind = st.get( "kind", "" ).asString();
            if ( kind == "human_only" || kind == "reflection" )
                d.hasHumanOnly = true;
            if ( st.isMember( "verifier_hooks" ) && st["verifier_hooks"].isArray() &&
                 !st["verifier_hooks"].empty() )
                d.hasVerifierHooks = true;
        }
    }
    if ( doc.isMember( "verifier_hooks" ) && doc["verifier_hooks"].isArray() &&
         !doc["verifier_hooks"].empty() )
        d.hasVerifierHooks = true;
    d.requiredAssetHints = doc.get( "required_assets", Json::arrayValue );
    return d;
}

RecipeRouterResult RecipeRouter::search( const RecipeQuery &query ) const
{
    RecipeRouterResult result;
    result.registryRevision = mRevision;

    const std::string modality = lower( query.modality );
    const std::string observedModality =
        lower( query.observedState.get( "modality", "" ).asString() );
    const std::string text = lower( query.text );
    const std::string intent = query.intent;

    struct Scored
    {
        RecipeEntry entry;
        double score = 0.0;
    };
    std::vector<Scored> scored;

    for ( const auto &doc : mRecipes )
    {
        // Modality filter: SAR asset must not match optical-only recipe.
        if ( !doc.modality.empty() )
        {
            const std::string docMod = lower( doc.modality );
            if ( !modality.empty() && modality != docMod )
                continue;
            if ( modality.empty() && !observedModality.empty() &&
                 observedModality != "unknown" && observedModality != docMod )
            {
                // optical-only recipe vs sar observed → skip
                if ( ( observedModality == "sar" &&
                       ( docMod == "optical" || docMod == "hyperspectral" ) ) ||
                     ( ( observedModality == "optical" || observedModality == "hyperspectral" ) &&
                       docMod == "sar" ) )
                    continue;
            }
        }

        double score = 0.0;
        std::vector<std::string> matched;
        if ( !intent.empty() && doc.intent == intent )
        {
            score += 4.0;
            matched.push_back( "intent" );
        }
        if ( !modality.empty() && lower( doc.modality ) == modality )
        {
            score += 2.0;
            matched.push_back( "modality" );
        }
        if ( !text.empty() )
        {
            for ( const auto &kw : doc.keywords )
            {
                if ( text.find( lower( kw ) ) != std::string::npos )
                {
                    score += 1.0;
                    matched.push_back( "kw:" + kw );
                }
            }
            if ( text.find( lower( doc.title ) ) != std::string::npos )
            {
                score += 0.5;
                matched.push_back( "title" );
            }
        }

        if ( score <= 0.0 && !intent.empty() )
            continue; // require some match when intent provided
        if ( score <= 0.0 && intent.empty() && text.empty() )
            score = 0.1; // browse-all fallback, still deterministic

        RecipeEntry e;
        e.recipeId = doc.recipeId;
        e.title = doc.title;
        e.intent = doc.intent;
        e.modality = doc.modality;
        e.score = score;
        e.stageCount = doc.stageCount;
        e.hasHumanOnly = doc.hasHumanOnly;
        e.hasVerifierHooks = doc.hasVerifierHooks;
        e.matched = matched;
        scored.push_back( { std::move( e ), score } );
    }

    std::sort( scored.begin(), scored.end(), []( const Scored &a, const Scored &b ) {
        if ( a.score != b.score )
            return a.score > b.score;
        return a.entry.recipeId < b.entry.recipeId;
    } );

    int limit = query.limit;
    if ( limit <= 0 )
        limit = 5;
    if ( limit > 32 )
        limit = 32;
    for ( int i = 0; i < static_cast<int>( scored.size() ) && i < limit; ++i )
        result.hits.push_back( scored[static_cast<std::size_t>( i )].entry );
    return result;
}

} // namespace sicnu::science_context
