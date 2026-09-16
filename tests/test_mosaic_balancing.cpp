// tests/test_mosaic_balancing.cpp — F15 Package B oracle tests.
//
// Ground truth: synthetic scenes differ from the reference by exactly
// v_B = gain·v_A + offset. The exact correction is gain* = 1/gain,
// offset* = −offset/gain, and the corrected scene must reproduce the
// reference values. No expectation derives from the implementation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mosaic_test_utils.h"
#include "processing/algorithms/mosaic_balancing.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

using namespace rs::mosaic;
using Catch::Approx;

namespace {

SceneEntry entryFor( const mosaic_test::SyntheticScene &s )
{
    SceneEntry e;
    e.path = "synthetic";
    e.width = s.width;
    e.height = s.height;
    e.geoTransform = { static_cast<double>( s.offsetX ), 1.0, 0.0,
                       -static_cast<double>( s.offsetY ), 0.0, -1.0 };
    e.bandCount = 1;
    e.priority = s.priority;
    return e;
}

std::optional<MosaicPlan> planFor( const std::vector<mosaic_test::SyntheticScene> &scenes,
                                   std::string *err = nullptr )
{
    std::vector<SceneEntry> entries;
    for ( const auto &s : scenes )
        entries.push_back( entryFor( s ) );
    return MosaicPlanner::build( entries, {}, err );
}

} // namespace

TEST_CASE( "Balancing: recovers a known gain/offset exactly",
           "[processing][mosaic][balancing]" )
{
    // B = 1.25·A − 10  ->  correction (0.8, +8).
    auto scenes = mosaic_test::twoSceneCorpus( 24, 24, 8, /*gainB=*/1.25, /*offsetB=*/-10.0 );
    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );

    mosaic_test::SyntheticSampler sampler( scenes );
    BalancingOptions opt;
    std::vector<SceneBalance> balances;
    std::string err;
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, opt, &balances, &err ) );

    REQUIRE( balances.size() == 2 );
    CHECK_FALSE( balances[0].rejected );
    CHECK_FALSE( balances[1].rejected );
    CHECK( balances[0].parentScene == 0 ); // reference
    CHECK( balances[1].parentScene == 0 );
    CHECK( balances[1].referenceHop == 1 );
    CHECK( balances[1].perBand[0].gain == Approx( 0.8 ).margin( 1e-6 ) );
    CHECK( balances[1].perBand[0].bias == Approx( 8.0 ).margin( 1e-6 ) );
    CHECK( balances[1].supportPixels > 0 );
}

TEST_CASE( "Balancing: corrected values reproduce the reference field",
           "[processing][mosaic][balancing]" )
{
    auto scenes = mosaic_test::twoSceneCorpus( 32, 32, 12, 0.8, 7.5 );
    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    mosaic_test::SyntheticSampler sampler( scenes );
    std::vector<SceneBalance> balances;
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, {}, &balances ) );

    // Sample the corrected scene across the overlap and compare to baseValue.
    const int aIdx = 0, bIdx = 1;
    const auto &pa = plan->scenes[aIdx].placement;
    const auto &pb = plan->scenes[bIdx].placement;
    const int overlapX0 = static_cast<int>( std::max<int64_t>( pa.offsetX, pb.offsetX ) );
    int64_t checked = 0;
    double worst = 0.0;
    for ( int gy = 0; gy < 32; gy += 3 )
    {
        for ( int gx = overlapX0; gx < overlapX0 + 12; gx += 3 )
        {
            std::vector<float> raw;
            REQUIRE( sampler.readGridWindow( bIdx, 0, gx, gy, 1, 1, raw ) );
            const double corrected = balances[bIdx].perBand[0].apply( raw[0] );
            const double truth = mosaic_test::baseField( gx, gy );
            worst = std::max( worst, std::abs( corrected - truth ) );
            ++checked;
        }
    }
    CHECK( checked > 20 );
    CHECK( worst < 1e-3 * mosaic_test::baseField( 0, 0 ) );
}

TEST_CASE( "Balancing: chains through an intermediate scene (3 hops graph)",
           "[processing][mosaic][balancing]" )
{
    // A (ref) overlaps B; B overlaps C; C does NOT overlap A.
    std::vector<mosaic_test::SyntheticScene> scenes =
        mosaic_test::twoSceneCorpus( 24, 24, 8, 1.2, -5.0 );
    mosaic_test::SyntheticScene c;
    c.width = 24;
    c.height = 24;
    c.offsetX = 32; // A covers [0,24), B [16,40), C [32,56): B∩C = 8 px, A∩C = ∅
    c.gain = 0.9;
    c.offset = 15.0;
    scenes.push_back( c );

    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    REQUIRE( plan->overlaps.size() == 2 );

    mosaic_test::SyntheticSampler sampler( scenes );
    std::vector<SceneBalance> balances;
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, {}, &balances ) );

    CHECK( balances[0].parentScene == 0 );
    CHECK( balances[1].parentScene == 0 );
    CHECK( balances[2].parentScene == 1 );
    CHECK( balances[2].referenceHop == 2 );
    CHECK( balances[2].perBand[0].gain == Approx( 1 / 0.9 ).margin( 1e-6 ) );
    CHECK( balances[2].perBand[0].bias == Approx( -15.0 / 0.9 ).margin( 1e-6 ) );
}

TEST_CASE( "Balancing: anomalous gain fails closed, or drops by policy",
           "[processing][mosaic][balancing]" )
{
    // B = 0.1·A + 1 -> cumulative gain 10 > maxGain 2 -> anomaly.
    auto scenes = mosaic_test::twoSceneCorpus( 24, 24, 8, 0.1, 1.0 );
    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    mosaic_test::SyntheticSampler sampler( scenes );

    BalancingOptions fail;
    std::vector<SceneBalance> balances;
    std::string err;
    CHECK_FALSE( RadiometricBalancer::balance( *plan, sampler, 1, fail, &balances, &err ) );
    CHECK( err.find( "rejected" ) != std::string::npos );
    CHECK( err.find( "gain" ) != std::string::npos );

    BalancingOptions drop;
    drop.rejectPolicy = BalancingOptions::RejectPolicy::Drop;
    balances.clear();
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, drop, &balances ) );
    REQUIRE( balances.size() == 2 );
    CHECK( balances[1].rejected );
    CHECK_FALSE( balances[1].rejectReason.empty() );
}

TEST_CASE( "Balancing: unreachable scene is rejected with a reason",
           "[processing][mosaic][balancing]" )
{
    auto scenes = mosaic_test::twoSceneCorpus( 24, 24, 8, 1.0, 0.0 );
    mosaic_test::SyntheticScene orphan;
    orphan.width = 10;
    orphan.height = 10;
    orphan.offsetX = 100; // no overlap with anyone
    scenes.push_back( orphan );

    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    mosaic_test::SyntheticSampler sampler( scenes );

    std::vector<SceneBalance> balances;
    std::string err;
    CHECK_FALSE( RadiometricBalancer::balance( *plan, sampler, 1, {}, &balances, &err ) );
    CHECK( err.find( "overlap path" ) != std::string::npos );

    BalancingOptions drop;
    drop.rejectPolicy = BalancingOptions::RejectPolicy::Drop;
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, drop, &balances ) );
    CHECK( balances[2].rejected );
}

TEST_CASE( "Balancing: undersized overlap cannot anchor a fit",
           "[processing][mosaic][balancing]" )
{
    auto scenes = mosaic_test::twoSceneCorpus( 24, 24, 2, 1.1, 2.0 ); // 2x24 overlap
    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    mosaic_test::SyntheticSampler sampler( scenes );

    BalancingOptions opt;
    opt.minOverlapPixels = 1000; // overlap only has 48 pixels
    std::vector<SceneBalance> balances;
    std::string err;
    CHECK_FALSE( RadiometricBalancer::balance( *plan, sampler, 1, opt, &balances, &err ) );
    CHECK( err.find( "overlap" ) != std::string::npos );
}

TEST_CASE( "Balancing: sampler failures propagate as errors",
           "[processing][mosaic][balancing]" )
{
    class FailingSampler : public OverlapSampler
    {
      public:
        bool readGridWindow( int, int, int64_t, int64_t, int64_t, int64_t,
                             std::vector<float> & ) override
        {
            return false;
        }
    };
    auto scenes = mosaic_test::twoSceneCorpus( 16, 16, 8, 1.1, 0.0 );
    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    FailingSampler sampler;
    std::vector<SceneBalance> balances;
    std::string err;
    CHECK_FALSE( RadiometricBalancer::balance( *plan, sampler, 1, {}, &balances, &err ) );
    CHECK( err.find( "read failure" ) != std::string::npos );
}

TEST_CASE( "Balancing: cloud-contaminated pixels are excluded from the fit",
           "[processing][mosaic][balancing]" )
{
    auto scenes = mosaic_test::twoSceneCorpus( 32, 32, 12, 1.2, -4.0 );
    // A bright cloud blob in B inside the overlap: raw values near 0 (shadowed)
    // would drag a naive mean/std match; the robust fit must ignore it.
    scenes[1].cloudX0 = 24;
    scenes[1].cloudX1 = 30;
    scenes[1].cloudY0 = 4;
    scenes[1].cloudY1 = 28;
    scenes[1].cloudValue = 0.0f;

    const auto plan = planFor( scenes );
    REQUIRE( plan.has_value() );
    mosaic_test::SyntheticSampler sampler( scenes );
    std::vector<SceneBalance> balances;
    REQUIRE( RadiometricBalancer::balance( *plan, sampler, 1, {}, &balances ) );
    CHECK( balances[1].perBand[0].gain == Approx( 1 / 1.2 ).margin( 1e-4 ) );
    CHECK( balances[1].perBand[0].bias == Approx( 4.0 / 1.2 ).margin( 1e-3 ) );
}
