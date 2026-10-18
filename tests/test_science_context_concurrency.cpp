// tests/test_science_context_concurrency.cpp
//
// Milestone 2 Concurrency & Anti-Drift Test Suite:
// - Tier 1 Feature 1.4: Science Context Cache Thread Safety
// - Tier 2 Boundary 2.5: Cache LRU Bounding & Eviction Conservation
// - ScopedSharedBrokerOverride RAII Lifetime & Global Broker Thread Safety
// - Anti-Drift Alignment: Builtin rs:spectral_index operator resolution & options
// - Closed 28-Intent Vocabulary Goal Recognition

#include <catch2/catch_test_macros.hpp>

#include "science_context/agent_adapter.h"
#include "science_context/asset_state_provider.h"
#include "science_context/broker.h"
#include "science_context/bundle.h"
#include "science_context/capability_router.h"
#include "science_context/context_cache.h"
#include "science_context/observability.h"
#include "scientific_state/asset_state_types.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::science_context;
using namespace sicnu::science_context::agent_adapter;
using namespace sicnu::state;

// ===========================================================================
// Tier 1 Feature 1.4: Science Context Cache Thread Safety
// ===========================================================================

TEST_CASE( "TC_SCC_01_ConcurrentCacheReads", "[science_context][concurrency][thread_safety]" )
{
    ContextCache cache;
    for ( int i = 0; i < 20; ++i )
    {
        ScientificContextBundle b;
        b.bundleId = "bundle_" + std::to_string( i );
        cache.put( "key_" + std::to_string( i ), b );
    }
    REQUIRE( cache.size() == 20 );
    REQUIRE( cache.capacity() == 128 );

    constexpr int kThreads = 8;
    constexpr int kReadsPerThread = 500;
    std::atomic<int> successCount{ 0 };
    std::barrier startBarrier( kThreads );
    std::vector<std::thread> workers;
    workers.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        workers.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kReadsPerThread; ++i )
            {
                const std::string key = "key_" + std::to_string( ( t * 17 + i ) % 20 );
                auto opt = cache.get( key );
                if ( opt && !opt->bundleId.empty() )
                {
                    ++successCount;
                }
            }
        } );
    }

    for ( auto &w : workers )
    {
        w.join();
    }

    CHECK( successCount == kThreads * kReadsPerThread );
    CHECK( cache.hits() == static_cast<std::uint64_t>( kThreads * kReadsPerThread ) );
    CHECK( cache.misses() == 0 );
    CHECK( cache.size() == 20 );
}

TEST_CASE( "TC_SCC_02_ConcurrentReadWriteLruSplice", "[science_context][concurrency][thread_safety]" )
{
    ContextCache cache;
    for ( int i = 0; i < 64; ++i )
    {
        ScientificContextBundle b;
        b.bundleId = "initial_" + std::to_string( i );
        cache.put( "key_" + std::to_string( i ), b );
    }

    std::atomic<bool> stop{ false };
    std::barrier startBarrier( 8 );
    std::atomic<int> getOps{ 0 };
    std::atomic<int> putOps{ 0 };
    std::vector<std::thread> threads;
    threads.reserve( 8 );

    // 4 reader threads
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            int i = 0;
            while ( !stop.load( std::memory_order_relaxed ) )
            {
                std::string key = "key_" + std::to_string( ( t * 13 + i ) % 100 );
                cache.get( key );
                ++getOps;
                ++i;
            }
        } );
    }

    // 4 writer threads
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            int i = 0;
            while ( !stop.load( std::memory_order_relaxed ) )
            {
                std::string key = "key_" + std::to_string( ( t * 31 + i ) % 150 );
                ScientificContextBundle b;
                b.bundleId = "write_" + std::to_string( t ) + "_" + std::to_string( i );
                cache.put( key, b );
                ++putOps;
                ++i;
            }
        } );
    }

    // Run for 1500ms under continuous contention
    std::this_thread::sleep_for( std::chrono::milliseconds( 1500 ) );
    stop.store( true, std::memory_order_relaxed );

    for ( auto &th : threads )
    {
        th.join();
    }

    CHECK( getOps > 0 );
    CHECK( putOps > 0 );
    CHECK( cache.size() <= ContextCache::kMaxEntries );
}

TEST_CASE( "TC_SCC_03_AssetStateProviderConcurrentResolve", "[science_context][concurrency][thread_safety]" )
{
    AssetStateProvider provider;
    provider.setResolver( []( const std::string &key ) -> PassportResolution {
        if ( key.find( "missing" ) != std::string::npos )
        {
            return PassportResolution{ std::nullopt, "asset_not_found", "key: " + key };
        }
        RemoteSensingAssetState st;
        st.assetId = key;
        st.revision = "rev_1";
        st.sensor.modality = sicnu::state::Modality::Optical;
        st.radiometric.unit = "surface_reflectance";
        BandState b1;
        b1.index = 1;
        b1.role = "red";
        b1.name = "B04";
        st.bands.push_back( b1 );
        BandState b2;
        b2.index = 2;
        b2.role = "nir";
        b2.name = "B08";
        st.bands.push_back( b2 );
        return PassportResolution{ st, "", "" };
    } );

    constexpr int kThreads = 8;
    constexpr int kResolvesPerThread = 200;
    std::barrier startBarrier( kThreads );
    std::atomic<int> okCount{ 0 };
    std::vector<std::thread> threads;
    threads.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kResolvesPerThread; ++i )
            {
                std::string key = "asset_" + std::to_string( ( t * 10 ) + ( i % 25 ) );
                AssetResolveRequest req;
                req.assetKey = key;
                auto res = provider.resolve( req );
                if ( res.ok && res.summary.assetId == key )
                {
                    ++okCount;
                }
            }
        } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    CHECK( okCount == kThreads * kResolvesPerThread );

    // Invalidation & clear coverage
    provider.invalidate( "asset_0" );
    provider.invalidateAll();
    provider.clear();
    provider.clearCache();
}

TEST_CASE( "TC_SCC_04_BrokerObservabilityThreadSafety", "[science_context][concurrency][thread_safety]" )
{
    BrokerObservability obs;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 500;
    std::barrier startBarrier( kThreads );
    std::vector<std::thread> threads;
    threads.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kOpsPerThread; ++i )
            {
                bool hit = ( ( t + i ) % 2 == 0 );
                bool trunc = ( i % 5 == 0 );
                bool blocked = ( i % 10 == 0 );
                std::string intent = ( i % 3 == 0 ) ? "ndvi" : "evi";
                obs.recordSynthesize( hit, trunc, blocked, intent, false, false );
                if ( i % 4 == 0 )
                {
                    obs.recordBudgetViolation();
                }
                if ( i % 50 == 0 )
                {
                    auto m = obs.metrics();
                    (void)m.synthesizeCount;
                }
            }
        } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    auto m = obs.metrics();
    CHECK( m.synthesizeCount == kThreads * kOpsPerThread );
    CHECK( m.cacheHits + m.cacheMisses == kThreads * kOpsPerThread );
    CHECK( m.budgetViolations == kThreads * ( kOpsPerThread / 4 ) );
    CHECK( m.lastIntentPath.size() <= 16 );

    auto json = obs.toJson();
    CHECK( json["synthesize_count"].asUInt64() == kThreads * kOpsPerThread );
    CHECK( json["budget_violations"].asUInt64() == m.budgetViolations );

    obs.reset();
    CHECK( obs.metrics().synthesizeCount == 0 );
    CHECK( obs.metrics().budgetViolations == 0 );
}

TEST_CASE( "TC_SCC_05_CacheOverflowEvictionUnderContention", "[science_context][concurrency][thread_safety]" )
{
    ContextCache cache;
    constexpr int kThreads = 4;
    constexpr int kEntriesPerThread = 50;
    std::barrier startBarrier( kThreads );
    std::vector<std::thread> threads;
    threads.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kEntriesPerThread; ++i )
            {
                std::string key = "unique_t" + std::to_string( t ) + "_i" + std::to_string( i );
                ScientificContextBundle b;
                b.bundleId = "b_" + key;
                cache.put( key, b );
            }
        } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    CHECK( cache.size() == 128 );
    CHECK( cache.evictions() == 72 ); // 200 total - 128 capacity
}

// ===========================================================================
// Tier 2 Boundary 2.5: Cache LRU Bounding & Eviction Conservation
// ===========================================================================

TEST_CASE( "TC_BND_CCH_01_StrictCapacityBound128", "[science_context][concurrency][boundary]" )
{
    ContextCache cache;
    constexpr int kThreads = 5;
    constexpr int kEntriesPerThread = 100;
    std::barrier startBarrier( kThreads );
    std::vector<std::thread> threads;
    threads.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [&, t]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kEntriesPerThread; ++i )
            {
                std::string key = "bound_t" + std::to_string( t ) + "_i" + std::to_string( i );
                ScientificContextBundle b;
                b.bundleId = "bundle_" + key;
                cache.put( key, b );
            }
        } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    CHECK( cache.size() == 128 );
    CHECK( cache.evictions() == ( kThreads * kEntriesPerThread - 128 ) );
}

TEST_CASE( "TC_BND_CCH_02_EvictionCountConservation", "[science_context][boundary]" )
{
    ContextCache cache;
    constexpr int kTotalEntries = 1000;
    for ( int i = 0; i < kTotalEntries; ++i )
    {
        ScientificContextBundle b;
        b.bundleId = "seq_" + std::to_string( i );
        cache.put( "seq_key_" + std::to_string( i ), b );
    }
    CHECK( cache.size() == 128 );
    CHECK( cache.evictions() == ( kTotalEntries - 128 ) );
}

TEST_CASE( "TC_BND_CCH_05_CacheKeyDeterminism", "[science_context][boundary]" )
{
    CacheKeyMaterial material;
    material.assetDigest = "sha256:abc123def456";
    material.catalogGeneration = 42;
    material.registryRevision = 7;
    material.recipePackDigest = "sha256:pack999";
    material.autonomyRevision = "L2";
    material.goal = "compute ndvi surface reflectance";
    material.intent = "ndvi";
    material.offline = false;
    material.maxBytes = 8192;
    material.maxRecipes = 5;
    material.maxCapabilities = 8;
    material.maxOpenQuestions = 4;
    material.maxAssets = 2;
    material.determinismRequired = true;
    material.capabilityAuthority = "broker_builtin_spec";
    material.capabilityRevision = 1;

    const std::string baseline = makeCacheKey( material );
    REQUIRE( !baseline.empty() );
    REQUIRE( baseline.size() == 16 );

    for ( int i = 0; i < 10000; ++i )
    {
        CHECK( makeCacheKey( material ) == baseline );
    }
}

// ===========================================================================
// ScopedSharedBrokerOverride RAII Lifetime & Global Broker Thread Safety
// ===========================================================================

TEST_CASE( "TC_LIF_01_ScopedSharedBrokerOverrideHygiene", "[science_context][lifetime][thread_safety]" )
{
    // Ensure defaultBroker is accessible
    ScienceContextBroker &def = sharedBroker();
    (void)def;

    ScienceContextBroker customBroker1;
    ScienceContextBroker customBroker2;

    {
        ScopedSharedBrokerOverride override1( &customBroker1 );
        CHECK( &sharedBroker() == &customBroker1 );

        {
            ScopedSharedBrokerOverride override2( &customBroker2 );
            CHECK( &sharedBroker() == &customBroker2 );
        }

        // Must restore to customBroker1 after inner scope exits
        CHECK( &sharedBroker() == &customBroker1 );
    }

    // Must restore to defaultBroker after outer scope exits
    CHECK( &sharedBroker() == &def );
}

TEST_CASE( "TC_LIF_02_ConcurrentSharedBrokerAccess", "[science_context][lifetime][thread_safety]" )
{
    ScienceContextBroker testBroker;
    ScopedSharedBrokerOverride guard( &testBroker );

    constexpr int kThreads = 8;
    constexpr int kIterations = 500;
    std::barrier startBarrier( kThreads );
    std::atomic<int> successCount{ 0 };
    std::vector<std::thread> threads;
    threads.reserve( kThreads );

    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [&]() {
            startBarrier.arrive_and_wait();
            for ( int i = 0; i < kIterations; ++i )
            {
                ScienceContextBroker &b = sharedBroker();
                if ( &b == &testBroker )
                {
                    ++successCount;
                }
            }
        } );
    }

    for ( auto &th : threads )
    {
        th.join();
    }

    CHECK( successCount == kThreads * kIterations );
}

// ===========================================================================
// Anti-Drift Alignment: Builtin rs:spectral_index operator resolution & options
// ===========================================================================

TEST_CASE( "TC_DRF_01_BuiltinCapabilityOperatorIdsResolveAuthoritative", "[science_context][anti_drift]" )
{
    // When no live authority is wired, builtinSpec() runs
    CapabilityQuery qNdvi;
    qNdvi.intent = "ndvi";
    auto resNdvi = routeCapabilities( qNdvi );
    REQUIRE( !resNdvi.entries.empty() );
    // Must be rs:spectral_index, NOT stale rs:spectral_index/ndvi
    CHECK( resNdvi.entries[0].capabilityId == "rs:spectral_index" );

    CapabilityQuery qEvi;
    qEvi.intent = "evi";
    auto resEvi = routeCapabilities( qEvi );
    REQUIRE( !resEvi.entries.empty() );
    // Must be rs:spectral_index, NOT stale rs:spectral_index/evi
    CHECK( resEvi.entries[0].capabilityId == "rs:spectral_index" );
}

// ===========================================================================
// Closed 28-Intent Vocabulary Goal Recognition
// ===========================================================================

TEST_CASE( "TC_DRF_02_ResolveIntentFromGoalClosedVocabulary28", "[science_context][anti_drift]" )
{
    const std::vector<std::pair<std::string, std::string>> testCases = {
        { "compute NDVI for vegetation", "ndvi" },
        { "calculate enhanced vegetation index EVI", "evi" },
        { "soil adjusted vegetation index savi", "savi" },
        { "red edge ndre analysis", "ndre" },
        { "water extraction with ndwi", "ndwi" },
        { "modified water index mndwi", "mndwi" },
        { "snow cover mapping ndsi", "ndsi" },
        { "burn severity with nbr", "nbr" },
        { "post-fire differenced dnbr", "dnbr" },
        { "urban area ndbi built-up index", "ndbi" },
        { "bare soil index bsi", "bsi" },
        { "water body mapping", "water" },
        { "flood inundation mapping", "flood" },
        { "sar radar amplitude analysis", "sar" },
        { "maritime ship and vessel detection", "ship" },
        { "bitemporal optical change detection", "change" },
        { "land cover classification", "classify" },
        { "growing season phenology", "phenology" },
        { "temporal time series analysis", "temporal" },
        { "dem terrain slope and aspect", "terrain" },
        { "confusion matrix accuracy assessment", "accuracy" },
        { "cloud mask qa filtering", "qa" },
        { "preprocessing mosaic and reproject", "preprocess" },
        { "deep learning inference unet", "inference" },
        { "per-zone zonal statistics", "zonal" },
        // Compound SAR intents
        { "sar flood inundation detection", "sar_flood" },
        { "sar change detection in backscatter", "sar_change" },
        { "sar water body extraction", "sar_water" },
    };

    for ( const auto &[ goal, expectedIntent ] : testCases )
    {
        std::string status;
        const std::string resolved = resolveIntentFromGoal( goal, &status );
        INFO( "Goal: '" << goal << "', expected: '" << expectedIntent << "', got: '" << resolved << "'" );
        CHECK( status == "resolved" );
        CHECK( resolved == expectedIntent );
        CHECK( isKnownBrokerIntent( resolved ) == true );
    }

    // Boundary edge cases
    std::string unresStatus;
    CHECK( resolveIntentFromGoal( "", &unresStatus ).empty() );
    CHECK( unresStatus == "unresolved" );

    CHECK( resolveIntentFromGoal( "unrelated query about pizza", &unresStatus ).empty() );
    CHECK( unresStatus == "unresolved" );
}
