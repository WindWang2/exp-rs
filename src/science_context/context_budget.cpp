// src/science_context/context_budget.cpp
#include "science_context/context_budget.h"

#include <algorithm>
#include <cstring>

namespace sicnu::science_context {

namespace {

/// ASCII marker appended to every string the budget cut mid-content. Byte
/// stable, and short enough not to dominate tight budgets.
constexpr const char *kTruncationMarker = "[~truncated]";

/// Per-string normalization cap: no single string field may exceed this, so a
/// hostile goal/intent/claim-path cannot hide an oversized payload behind an
/// in-limit item count.
constexpr std::size_t kMaxFieldBytes = 1024;

std::size_t utf8SequenceLength( unsigned char lead )
{
    if ( lead < 0x80 )
        return 1;
    if ( ( lead >> 5 ) == 0x6 )
        return 2;
    if ( ( lead >> 4 ) == 0xE )
        return 3;
    if ( ( lead >> 3 ) == 0x1E )
        return 4;
    return 1; // invalid lead byte: treat as single so cutting always progresses
}

/// Cuts @p text to at most @p maxBytes bytes without splitting a UTF-8
/// sequence, appending the explicit marker when content was dropped.
/// Deterministic: same input, same output.
std::string truncateUtf8( const std::string &text, std::size_t maxBytes )
{
    if ( text.size() <= maxBytes )
        return text;
    const std::size_t markerLen = std::strlen( kTruncationMarker );
    if ( maxBytes <= markerLen )
        return std::string(); // nothing can fit; the section mark carries the signal
    const std::size_t contentLimit = maxBytes - markerLen;
    std::size_t end = 0;
    std::size_t pos = 0;
    while ( pos < text.size() && pos < contentLimit )
    {
        const std::size_t len =
            utf8SequenceLength( static_cast<unsigned char>( text[pos] ) );
        if ( pos + len > contentLimit )
            break;
        pos += len;
        end = pos;
    }
    if ( end == 0 )
        return std::string();
    return text.substr( 0, end ) + kTruncationMarker;
}

} // namespace

void applyContextBudget( ScientificContextBundle &bundle, const BudgetPolicy &policy )
{
    TruncationMeta meta;
    bundle.truncation = TruncationMeta{};
    const std::string before = serializeBundle( bundle );
    meta.originalBytes = static_cast<int>( before.size() );

    // Per-string normalization first: bounded item counts with unbounded item
    // sizes would still leak bytes (and attacker-shaped ones at that).
    auto capCopy = [&]( const std::string &text, const char *section ) -> std::string {
        if ( text.size() <= kMaxFieldBytes )
            return text;
        meta.truncated = true;
        meta.sections.push_back( section );
        return truncateUtf8( text, kMaxFieldBytes );
    };
    auto capString = [&]( std::string &text, const char *section ) {
        text = capCopy( text, section );
    };
    capString( bundle.goal, "goal" );
    capString( bundle.intent, "intent" );
    capString( bundle.planner.goal, "planner_projection" );
    capString( bundle.planner.intent, "planner_projection" );
    capString( bundle.planner.blockReason, "planner_block_reason" );
    for ( auto &q : bundle.openQuestions )
        capString( q, "open_questions" );
    for ( auto &c : bundle.capabilities )
    {
        capString( c.intent, "capabilities" );
        capString( c.capabilityId, "capabilities" );
        for ( auto &r : c.reasons )
            capString( r, "capabilities" );
        for ( auto &p : c.prepActions )
            capString( p, "capabilities" );
    }
    for ( auto &r : bundle.recipes )
    {
        capString( r.title, "recipes" );
        capString( r.intent, "recipes" );
        for ( auto &m : r.matched )
            capString( m, "recipes" );
    }
    for ( auto &a : bundle.assets )
    {
        capString( a.assetId, "assets" );
        capString( a.revision, "assets" );
        capString( a.displayName, "assets" );
        capString( a.pathHint, "assets" );
        for ( auto &p : a.evidencePaths )
            capString( p, "assets" );
        for ( auto &alt : a.conflictAlternatives )
            capString( alt, "assets" );
    }
    for ( Json::Value *list : { &bundle.planner.limitations, &bundle.planner.missingFacts,
                                &bundle.planner.openQuestions } )
    {
        if ( !list->isArray() )
            continue;
        for ( Json::Value::ArrayIndex i = 0; i < list->size(); ++i )
        {
            Json::Value &item = ( *list )[i];
            if ( !item.isString() )
                continue;
            const std::string capped = capCopy( item.asString(), "planner_projection" );
            item = Json::Value( capped );
        }
    }

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
        const int dropped = static_cast<int>( bundle.assets.size() ) - keep;
        bundle.assets.resize( static_cast<std::size_t>( keep ) );
        meta.droppedAssets += dropped;
        meta.truncated = true;
        meta.sections.push_back( "assets" );
    };

    // Planner sub-projections carry the same item budget as open questions —
    // dropped questions must not survive in a secondary section.
    auto trimPlannerList = [&]( Json::Value &arr, int keep, const char *section ) {
        if ( !arr.isArray() )
            return;
        const int size = static_cast<int>( arr.size() );
        if ( size <= keep )
            return;
        Json::Value kept( Json::arrayValue );
        for ( int i = 0; i < keep; ++i )
            kept.append( arr[static_cast<Json::Value::ArrayIndex>( i )] );
        arr = kept;
        meta.truncated = true;
        meta.sections.push_back( section );
    };

    trimAssets( policy.maxAssets );
    trimCaps( policy.maxCapabilities );
    trimRecipes( policy.maxRecipes );
    trimQuestions( policy.maxOpenQuestions );
    trimPlannerList( bundle.planner.limitations, policy.maxOpenQuestions,
                     "planner_limitations" );
    trimPlannerList( bundle.planner.missingFacts, policy.maxOpenQuestions,
                     "planner_missing_facts" );
    trimPlannerList( bundle.planner.openQuestions, policy.maxOpenQuestions,
                     "planner_open_questions" );

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

    // Shrink one identity string by the measured overflow. Deterministic and
    // progressive; the marker keeps the cut visible on the wire.
    auto tightenString = [&]( std::string &text, const char *section ) {
        // Wide arithmetic: policy.maxBytes flows unclamped from tool args.
        const long long overflow =
            static_cast<long long>( emitted ) - static_cast<long long>( policy.maxBytes );
        const std::size_t size = text.size();
        const std::size_t target =
            ( overflow > 0 && static_cast<unsigned long long>( overflow ) < size )
                ? size - static_cast<std::size_t>( overflow )
                : 0;
        text = truncateUtf8( text, target );
        meta.sections.push_back( section );
    };

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
        else if ( !bundle.goal.empty() )
        {
            tightenString( bundle.goal, "goal" );
        }
        else if ( !bundle.intent.empty() )
        {
            tightenString( bundle.intent, "intent" );
        }
        else
        {
            break; // nothing left to trim
        }
        emitted = measureEmitted();
    }

    // The planner projection must mirror the budgeted question set — dropped
    // questions must not survive in a secondary section.
    bundle.planner.openQuestions = Json::Value( Json::arrayValue );
    for ( const auto &q : bundle.openQuestions )
        bundle.planner.openQuestions.append( q );

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
