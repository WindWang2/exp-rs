// src/science_context/context_budget.cpp
#include "science_context/context_budget.h"

#include <algorithm>

namespace sicnu::science_context {

void applyContextBudget( ScientificContextBundle &bundle, const BudgetPolicy &policy )
{
    TruncationMeta meta;
    bundle.truncation = TruncationMeta{};
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

    // Byte budget: progressively tighten caps, then strip heavy payloads.
    // Every pass measures the EMITTED form — the truncation metadata itself
    // is part of the payload, so it is stored before measuring. Otherwise the
    // loop undershoots by the size of the metadata it must eventually write.
    int capLimit = std::min( policy.maxCapabilities, static_cast<int>( bundle.capabilities.size() ) );
    int recipeLimit = std::min( policy.maxRecipes, static_cast<int>( bundle.recipes.size() ) );
    int qLimit = std::min( policy.maxOpenQuestions, static_cast<int>( bundle.openQuestions.size() ) );

    // Converged measurement: serializing with finalBytes = last measurement
    // is a fixed point after two rounds (only the digit count can move).
    auto measureEmitted = [&]() {
        meta.finalBytes = 0;
        bundle.truncation = meta;
        meta.finalBytes = static_cast<int>( serializeBundle( bundle ).size() );
        bundle.truncation = meta;
        return static_cast<int>( serializeBundle( bundle ).size() );
    };
    int emitted = measureEmitted();

    for ( int pass = 0; pass < 24 && emitted > policy.maxBytes; ++pass )
    {
        meta.truncated = true;
        if ( recipeLimit > 0 )
        {
            --recipeLimit;
            trimRecipes( recipeLimit );
        }
        else if ( capLimit > 0 )
        {
            --capLimit;
            trimCaps( capLimit );
        }
        else if ( qLimit > 0 )
        {
            --qLimit;
            trimQuestions( qLimit );
        }
        else if ( !bundle.planner.inputFacts.empty() )
        {
            bundle.planner.inputFacts = Json::Value( Json::objectValue );
            meta.sections.push_back( "planner_input_facts" );
        }
        else if ( !bundle.assets.empty() )
        {
            bundle.assets.clear();
            meta.sections.push_back( "assets" );
        }
        else if ( !bundle.observability.empty() )
        {
            bundle.observability = Json::Value( Json::objectValue );
            meta.sections.push_back( "observability" );
        }
        else
        {
            break; // nothing left to trim
        }
        emitted = measureEmitted();
    }

    std::sort( meta.sections.begin(), meta.sections.end() );
    meta.sections.erase( std::unique( meta.sections.begin(), meta.sections.end() ),
                         meta.sections.end() );
    if ( emitted > policy.maxBytes )
        meta.truncated = true; // floor reached; the report must say so

    // Settle the final measurement: setting finalBytes changes the payload by
    // its own digit width, which is a fixed point after two iterations.
    for ( int pass = 0; pass < 3; ++pass )
    {
        meta.finalBytes = emitted;
        bundle.truncation = meta;
        const int reMeasured = static_cast<int>( serializeBundle( bundle ).size() );
        if ( reMeasured == emitted )
            break;
        emitted = reMeasured;
    }
    meta.finalBytes = emitted;
    bundle.truncation = meta;
    bundle.constraints.maxBytes = policy.maxBytes;
    bundle.constraints.maxRecipes = policy.maxRecipes;
    bundle.constraints.maxCapabilities = policy.maxCapabilities;
}

} // namespace sicnu::science_context
