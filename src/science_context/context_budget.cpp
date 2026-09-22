// src/science_context/context_budget.cpp
#include "science_context/context_budget.h"

#include <algorithm>

namespace sicnu::science_context {

void applyContextBudget( ScientificContextBundle &bundle, const BudgetPolicy &policy )
{
    TruncationMeta meta;
    const std::string before = serializeBundle( bundle );
    meta.originalBytes = static_cast<int>( before.size() );

    auto trimCaps = [&]( int keep ) {
        if ( static_cast<int>( bundle.capabilities.size() ) <= keep )
            return;
        std::sort( bundle.capabilities.begin(), bundle.capabilities.end(),
                   []( const CapabilityEntry &a, const CapabilityEntry &b ) {
                       if ( a.score != b.score )
                           return a.score > b.score;
                       return a.capabilityId < b.capabilityId;
                   } );
        const int dropped =
            static_cast<int>( bundle.capabilities.size() ) - keep;
        bundle.capabilities.resize( static_cast<std::size_t>( keep ) );
        meta.droppedCapabilities += dropped;
        meta.truncated = true;
        meta.sections.push_back( "capabilities" );
    };

    auto trimRecipes = [&]( int keep ) {
        if ( static_cast<int>( bundle.recipes.size() ) <= keep )
            return;
        std::sort( bundle.recipes.begin(), bundle.recipes.end(),
                   []( const RecipeEntry &a, const RecipeEntry &b ) {
                       if ( a.score != b.score )
                           return a.score > b.score;
                       return a.recipeId < b.recipeId;
                   } );
        const int dropped = static_cast<int>( bundle.recipes.size() ) - keep;
        bundle.recipes.resize( static_cast<std::size_t>( keep ) );
        meta.droppedRecipes += dropped;
        meta.truncated = true;
        meta.sections.push_back( "recipes" );
    };

    auto trimQuestions = [&]( int keep ) {
        if ( static_cast<int>( bundle.openQuestions.size() ) <= keep )
            return;
        const int dropped =
            static_cast<int>( bundle.openQuestions.size() ) - keep;
        bundle.openQuestions.resize( static_cast<std::size_t>( keep ) );
        meta.droppedQuestions += dropped;
        meta.truncated = true;
        meta.sections.push_back( "open_questions" );
    };

    auto trimAssets = [&]( int keep ) {
        if ( static_cast<int>( bundle.assets.size() ) <= keep )
            return;
        bundle.assets.resize( static_cast<std::size_t>( keep ) );
        meta.truncated = true;
        meta.sections.push_back( "assets" );
    };

    trimAssets( policy.maxAssets );
    trimCaps( policy.maxCapabilities );
    trimRecipes( policy.maxRecipes );
    trimQuestions( policy.maxOpenQuestions );

    // Byte budget: progressively tighten caps, then strip heavy planner payloads.
    int capLimit = std::min( policy.maxCapabilities, static_cast<int>( bundle.capabilities.size() ) );
    int recipeLimit = std::min( policy.maxRecipes, static_cast<int>( bundle.recipes.size() ) );
    int qLimit = std::min( policy.maxOpenQuestions, static_cast<int>( bundle.openQuestions.size() ) );
    for ( int pass = 0; pass < 24; ++pass )
    {
        const std::string cur = serializeBundle( bundle );
        if ( static_cast<int>( cur.size() ) <= policy.maxBytes )
            break;
        meta.truncated = true;
        if ( recipeLimit > 0 )
        {
            --recipeLimit;
            trimRecipes( recipeLimit );
            continue;
        }
        if ( capLimit > 0 )
        {
            --capLimit;
            trimCaps( capLimit );
            continue;
        }
        if ( qLimit > 0 )
        {
            --qLimit;
            trimQuestions( qLimit );
            continue;
        }
        if ( !bundle.planner.inputFacts.empty() )
        {
            bundle.planner.inputFacts = Json::Value( Json::objectValue );
            meta.sections.push_back( "planner_input_facts" );
            continue;
        }
        if ( !bundle.assets.empty() )
        {
            bundle.assets.clear();
            meta.sections.push_back( "assets" );
            continue;
        }
        // Last resort: drop observability blob.
        if ( !bundle.observability.empty() )
        {
            bundle.observability = Json::Value( Json::objectValue );
            meta.sections.push_back( "observability" );
            continue;
        }
        break;
    }

    std::sort( meta.sections.begin(), meta.sections.end() );
    meta.sections.erase( std::unique( meta.sections.begin(), meta.sections.end() ),
                         meta.sections.end() );

    const std::string after = serializeBundle( bundle );
    meta.finalBytes = static_cast<int>( after.size() );
    bundle.truncation = meta;
    bundle.constraints.maxBytes = policy.maxBytes;
    bundle.constraints.maxRecipes = policy.maxRecipes;
    bundle.constraints.maxCapabilities = policy.maxCapabilities;
}

} // namespace sicnu::science_context
