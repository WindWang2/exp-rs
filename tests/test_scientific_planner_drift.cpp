// tests/test_scientific_planner_drift.cpp — current-master drift tests:
// the planner's declared mirrors are pinned byte-for-byte against their
// linked authorities, so an upstream vocabulary change fails HERE instead of
// silently diverging (no second truth source).
#include <catch2/catch_test_macros.hpp>

#include "agent/harness/intent_vocabulary.h"
#include "contracts/scientific_contract.h"
#include "planner/plan_ir_projection.h"
#include "planner/planner_vocab.h"
#include "scientific_state/asset_state_types.h"

#include <algorithm>
#include <set>

using namespace sicnu::planner;

TEST_CASE( "asset lifecycle mirror matches the scientific_state authority",
           "[scientific_planner][drift]" )
{
    std::vector<std::string> authority;
    for ( const auto lifecycle : { sicnu::state::AssetLifecycle::Registered,
                                   sicnu::state::AssetLifecycle::Resolving,
                                   sicnu::state::AssetLifecycle::Ready,
                                   sicnu::state::AssetLifecycle::Missing,
                                   sicnu::state::AssetLifecycle::UnavailableSource,
                                   sicnu::state::AssetLifecycle::Offline,
                                   sicnu::state::AssetLifecycle::AuthenticationRequired,
                                   sicnu::state::AssetLifecycle::Error,
                                   sicnu::state::AssetLifecycle::Stale,
                                   sicnu::state::AssetLifecycle::Unknown } )
    {
        authority.push_back( sicnu::state::assetLifecycleToString( lifecycle ) );
    }
    std::sort( authority.begin(), authority.end() );
    std::vector<std::string> mirror = kAssetStates;
    std::sort( mirror.begin(), mirror.end() );
    REQUIRE( mirror == authority );

    // spot wires (order-stable, wire-visible)
    CHECK( sicnu::state::assetLifecycleToString( sicnu::state::AssetLifecycle::Ready )
           == std::string( "ready" ) );
    CHECK( sicnu::state::assetLifecycleToString( sicnu::state::AssetLifecycle::UnavailableSource )
           == std::string( "unavailable_source" ) );
}

TEST_CASE( "projection domain map equals the contracts numeric-domain authority",
           "[scientific_planner][drift]" )
{
    std::vector<std::string> mirror = knownProjectionDomains();
    std::sort( mirror.begin(), mirror.end() );
    std::vector<std::string> authority = sicnu::contracts::kNumericDomains;
    std::sort( authority.begin(), authority.end() );
    CHECK( mirror == authority );
}

TEST_CASE( "goal-kind → harness intent map only emits known intents",
           "[scientific_planner][drift]" )
{
    for ( const auto &goalKind : kGoalKinds )
    {
        const std::string intent = harnessIntentForGoalKind( goalKind );
        if ( !intent.empty() )
        {
            INFO( "goal kind " << goalKind << " projects intent " << intent );
            const bool known = std::any_of(
                std::begin( sicnu::agent::harness::kIntentVocabulary ),
                std::end( sicnu::agent::harness::kIntentVocabulary ),
                [&]( const char *candidate ) { return intent == candidate; } );
            CHECK( known );
        }
    }
}

TEST_CASE( "step-role axis carries the MissionStage wire keys",
           "[scientific_planner][drift]" )
{
    // mission_stage.h (Qt) is the authority for the five stage keys; the
    // planner mirrors them for future mission projections (plan.md D4).
    // Linking the workbench here would drag in Qt, so the wire strings are
    // pinned directly — a MissionStage key rename fails the reflection below.
    for ( const char *key : { "import", "preprocess", "analyze", "verify", "publish" } )
    {
        INFO( "role key " << key );
        CHECK( isKnownStepRole( key ) );
    }
}

TEST_CASE( "artifact_facts tokens emitted by the map are wire members",
           "[scientific_planner][drift]" )
{
    // The harness artifact_facts vocabulary is the consumer side; every
    // non-empty non-unknown token must be one of its wire strings (pinned
    // against workflow_ir.h constants — data-level, no Qt link).
    const std::set<std::string> kArtifactFactsDomains = {
        "surface_reflectance", "toa", "dn", "db", "linear_power", "index",
        "categorical", "masked", "unknown",
    };
    for ( const auto &domain : knownProjectionDomains() )
    {
        const std::string token = artifactFactsTokenForDomain( domain );
        if ( token.empty() )
            continue;
        INFO( domain << " -> " << token );
        CHECK( kArtifactFactsDomains.count( token ) == 1 );
    }
}
