// test_r33_t04_context_cache_key.cpp — R33-T04 / WP17:
//
//   makeCacheKey joined every string field with a bare '|' separator, so
//   goal="a|b",intent="c" produced the same pre-hash bytes as
//   goal="a",intent="b|c" — the broker then answered a hit with a bundle
//   synthesized for a DIFFERENT request. The key is now length-prefixed;
//   these cases pin the production behavior end-to-end:
//
//   * colliding goal/intent splits never cross-hit through the broker and
//     each answer carries the CURRENT request's goal/intent;
//   * identical requests stay stable hits (no self-invalidating churn);
//   * Unicode, empty strings and other delimiter-bearing fields re-key
//     instead of collapsing;
//   * the direct makeCacheKey contract rejects the collision pairs.
#include <catch2/catch_test_macros.hpp>

#include "science_context/broker.h"
#include "science_context/bundle.h"
#include "science_context/context_cache.h"

#include <string>

using namespace sicnu::science_context;

namespace
{
CacheKeyMaterial baseMaterial()
{
    CacheKeyMaterial m;
    m.assetDigest = "digest";
    m.catalogGeneration = 1;
    m.registryRevision = 2;
    m.recipePackDigest = "recipes";
    m.autonomyRevision = "auto";
    m.offline = false;
    m.maxBytes = 1000;
    m.maxRecipes = 10;
    m.maxCapabilities = 10;
    m.maxOpenQuestions = 10;
    m.maxAssets = 10;
    m.determinismRequired = true;
    m.capabilityAuthority = "builtin";
    m.capabilityRevision = 5;
    m.failureDigest = "";
    return m;
}
} // namespace

TEST_CASE( "length-prefixed cache key: separator-collision requests never cross-hit",
           "[science_context][r33t04]" )
{
    ScienceContextBroker broker;

    SynthesizeRequest first;
    first.goal = "a|b";
    first.intent = "c";
    const auto r1 = broker.synthesize( first );
    CHECK( r1.cacheHit == false );
    CHECK( r1.bundle.goal == "a|b" );
    CHECK( r1.bundle.intent == "c" );

    // Identical request → the stable hit the cache exists for.
    const auto r1Again = broker.synthesize( first );
    CHECK( r1Again.cacheHit == true );
    CHECK( r1Again.bundle.goal == "a|b" );
    CHECK( r1Again.bundle.intent == "c" );

    // The collision pair: under the old bare-'|' join this HIT the first
    // request's entry and answered with its bundle.
    SynthesizeRequest second;
    second.goal = "a";
    second.intent = "b|c";
    const auto r2 = broker.synthesize( second );
    CHECK( r2.cacheHit == false );
    CHECK( r2.bundle.goal == "a" );
    CHECK( r2.bundle.intent == "b|c" );

    // And each request keeps hitting only its own entry afterwards.
    CHECK( broker.synthesize( first ).bundle.goal == "a|b" );
    CHECK( broker.synthesize( second ).bundle.intent == "b|c" );
}

TEST_CASE( "length-prefixed cache key: Unicode and empty fields stay distinct",
           "[science_context][r33t04]" )
{
    ScienceContextBroker broker;

    SynthesizeRequest zh;
    zh.goal = "目标|制图";
    zh.intent = "检测";
    const auto rz1 = broker.synthesize( zh );
    CHECK( rz1.cacheHit == false );

    SynthesizeRequest zhSplit;
    zhSplit.goal = "目标";
    zhSplit.intent = "制图|检测";
    const auto rz2 = broker.synthesize( zhSplit );
    CHECK( rz2.cacheHit == false );
    CHECK( rz2.bundle.goal == "目标" );

    // Empty vs non-empty fields must not fold onto one entry either.
    SynthesizeRequest emptyGoal;
    emptyGoal.goal = "";
    emptyGoal.intent = "b";
    const auto re1 = broker.synthesize( emptyGoal );
    CHECK( re1.cacheHit == false );
    CHECK( re1.bundle.goal.empty() );
    SynthesizeRequest goalB;
    goalB.goal = "b";
    goalB.intent = "";
    const auto re2 = broker.synthesize( goalB );
    CHECK( re2.cacheHit == false );
    CHECK( re2.bundle.goal == "b" );
}

TEST_CASE( "makeCacheKey: delimiter-bearing fields produce distinct keys",
           "[science_context][r33t04]" )
{
    auto m1 = baseMaterial();
    m1.goal = "a|b";
    m1.intent = "c";

    auto m2 = baseMaterial();
    m2.goal = "a";
    m2.intent = "b|c";
    CHECK( makeCacheKey( m1 ) != makeCacheKey( m2 ) );

    // Same collision class through the failure digest and authority fields.
    auto m3 = baseMaterial();
    m3.failureDigest = "x|y";
    m3.capabilityAuthority = "z";
    auto m4 = baseMaterial();
    m4.failureDigest = "x";
    m4.capabilityAuthority = "y|z";
    CHECK( makeCacheKey( m3 ) != makeCacheKey( m4 ) );

    // Byte-identical material still yields the identical key (stability).
    CHECK( makeCacheKey( m1 ) == makeCacheKey( m1 ) );
    auto m1Copy = m1;
    CHECK( makeCacheKey( m1Copy ) == makeCacheKey( m1 ) );
}
