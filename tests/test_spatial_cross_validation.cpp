// test_spatial_cross_validation.cpp — F12 Oracle 1: a synthetic spatial
// leak must be caught. All expectations are hand-derived from the fold
// construction rules, never from the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_spatial_cross_validation.h"
#include "rs_classifier_normalbayes.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <random>
#include <set>

using Catch::Approx;

namespace
{
// Four tight clusters at the corners of a [0,100]² plane, one class per
// cluster, unique jittered coordinates (mt19937, fixed seed — the test's
// own generator, independent of library code).
struct LeakFixture
{
    cv::Mat X, y;
    std::vector<double> coords;
    std::vector<int> groupIds;

    explicit LeakFixture( int perCluster = 40 )
    {
        const double cx[4] = { 25.0, 75.0, 25.0, 75.0 };
        const double cy[4] = { 25.0, 25.0, 75.0, 75.0 };
        std::mt19937 rng( 12345 );
        std::uniform_real_distribution<double> jitter( -1.0, 1.0 );
        std::normal_distribution<float> featureNoise( 0.0f, 0.3f );
        X.create( perCluster * 4, 2, CV_32F );
        y.create( perCluster * 4, 1, CV_32S );
        coords.reserve( static_cast<size_t>( perCluster * 4 ) * 2 );
        groupIds.reserve( static_cast<size_t>( perCluster * 4 ) );
        int row = 0;
        for ( int c = 0; c < 4; ++c )
        {
            for ( int i = 0; i < perCluster; ++i, ++row )
            {
                const double x = cx[c] + jitter( rng );
                const double yy = cy[c] + jitter( rng );
                coords.push_back( x );
                coords.push_back( yy );
                groupIds.push_back( c + 1 );
                // Two class-discriminating features with cluster-level offset:
                // the spatial cluster IS the leak channel.
                X.at<float>( row, 0 ) = static_cast<float>( c % 2 ) + featureNoise( rng );
                X.at<float>( row, 1 ) = static_cast<float>( c / 2 ) + featureNoise( rng );
                y.at<int>( row, 0 ) = c + 1;
            }
        }
    }
};
} // namespace

TEST_CASE( "groupFolds: atomic groups, balanced partition, full coverage",
           "[classify][spatialcv]" )
{
    // Hand-built groups: 1 → {0..4}, 2 → {5..7}, 3 → {8,9}.
    std::vector<int> groupIds = { 1, 1, 1, 1, 1, 2, 2, 2, 3, 3 };
    const auto folds = RsSpatialCrossValidation::groupFolds( groupIds, 3 );
    REQUIRE( folds.size() == 3 );

    std::multiset<int> seenAsTest;
    std::multiset<int> seenAsTrain;
    for ( const auto &fold : folds )
    {
        REQUIRE( !fold.testIndices.empty() );
        REQUIRE( !fold.trainIndices.empty() );
        // Atomicity: all test rows share ONE group id.
        int testGroup = groupIds[fold.testIndices[0]];
        for ( int idx : fold.testIndices )
        {
            REQUIRE( groupIds[idx] == testGroup );
            seenAsTest.insert( idx );
        }
        // Train never contains a test row.
        for ( int idx : fold.trainIndices )
        {
            REQUIRE( groupIds[idx] != testGroup );
            seenAsTrain.insert( idx );
        }
    }
    // Across the k folds every row is tested exactly once and every row is
    // trained on exactly k-1 times.
    REQUIRE( seenAsTest.size() == groupIds.size() );
    REQUIRE( seenAsTrain.size() == groupIds.size() * ( folds.size() - 1 ) );
    // Balance-first: largest group (1, size 5) is alone in its fold.
    int foldOfSize5 = -1;
    for ( int j = 0; j < static_cast<int>( folds.size() ); ++j )
    {
        if ( folds[j].testIndices.size() == 5 )
            foldOfSize5 = j;
    }
    REQUIRE( foldOfSize5 >= 0 );
    REQUIRE( groupIds[folds[foldOfSize5].testIndices[0]] == 1 );
}

TEST_CASE( "groupFolds: refuses when fewer groups than folds", "[classify][spatialcv]" )
{
    std::vector<int> groupIds = { 1, 1, 2, 2 };
    REQUIRE( RsSpatialCrossValidation::groupFolds( groupIds, 3 ).isEmpty() );
}

TEST_CASE( "blockFolds: grid partition covers every sample once as test",
           "[classify][spatialcv]" )
{
    LeakFixture fx( 10 );
    const auto folds = RsSpatialCrossValidation::blockFolds( fx.coords, fx.X.rows, 2, 2 );
    REQUIRE( folds.size() == 4 );
    std::multiset<int> seen;
    for ( const auto &fold : folds )
    {
        for ( int idx : fold.testIndices )
            seen.insert( idx );
        REQUIRE( fold.excludedBuffer.isEmpty() );
    }
    REQUIRE( seen.size() == fx.X.rows );
    // 2x2 grid over the corner-cluster fixture: every block is one pure
    // cluster → each fold's test rows share one class.
    for ( const auto &fold : folds )
    {
        int cls = fx.y.at<int>( fold.testIndices[0], 0 );
        for ( int idx : fold.testIndices )
            REQUIRE( fx.y.at<int>( idx, 0 ) == cls );
    }
}

TEST_CASE( "bufferedBlockFolds: excluded buffer enforces the isolation invariant",
           "[classify][spatialcv]" )
{
    LeakFixture fx( 10 );
    const double buffer = 55.0;
    const auto folds =
      RsSpatialCrossValidation::bufferedBlockFolds( fx.coords, fx.X.rows, 2, 2, buffer );
    REQUIRE( folds.size() == 4 );
    for ( const auto &fold : folds )
    {
        // Every excluded/retained row is accounted for exactly.
        std::set<int> retained( fold.trainIndices.begin(), fold.trainIndices.end() );
        std::set<int> excluded( fold.excludedBuffer.begin(), fold.excludedBuffer.end() );
        for ( int idx : fold.testIndices )
        {
            REQUIRE( !retained.count( idx ) );
            REQUIRE( !excluded.count( idx ) );
        }
        // Isolation invariant: ||p_train − p_eval|| > buffer for retained.
        for ( int t : fold.trainIndices )
        {
            for ( int s : fold.testIndices )
            {
                const double dx = fx.coords[2 * t] - fx.coords[2 * s];
                const double dy = fx.coords[2 * t + 1] - fx.coords[2 * s + 1];
                REQUIRE( std::sqrt( dx * dx + dy * dy ) > buffer );
            }
        }
    }
    // Clusters sit ~50 apart (nearest edges ≈ 48), so a 55 buffer must
    // absorb the neighbouring clusters' train rows while the diagonal
    // cluster (≈ 70 away) stays in every fold.
    int excludedTotal = 0;
    for ( const auto &fold : folds )
        excludedTotal += fold.excludedBuffer.size();
    REQUIRE( excludedTotal > 0 );
    // But the fold never starves: the diagonal cluster is retained.
    for ( const auto &fold : folds )
        REQUIRE( !fold.trainIndices.isEmpty() );
}

TEST_CASE( "F12 Oracle 1 — synthetic spatial leak is caught: random folds "
           "inflate accuracy and fail the audit; spatial folds reveal it",
           "[classify][spatialcv][oracle]" )
{
    LeakFixture fx( 40 );
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };

    // Random folds: train and test intermingle within each cluster.
    const auto randomFolds = RsSpatialCrossValidation::randomFolds( fx.y, 4, 42u );
    const auto randomResult = RsSpatialCrossValidation::evaluate(
      fx.X, fx.y, fx.coords, fx.groupIds, randomFolds, factory );
    REQUIRE( randomResult.ok() );

    // Block folds aligned with the clusters: every test cluster is unseen.
    const auto blockFolds = RsSpatialCrossValidation::blockFolds( fx.coords, fx.X.rows, 2, 2 );
    const auto blockResult = RsSpatialCrossValidation::evaluate(
      fx.X, fx.y, fx.coords, fx.groupIds, blockFolds, factory );
    REQUIRE( blockResult.ok() );

    // Accuracy contrast (the inflation itself).
    INFO( "random mean=" << randomResult.meanAccuracy
                         << " block mean=" << blockResult.meanAccuracy );
    REQUIRE( randomResult.meanAccuracy >= 0.9 );
    REQUIRE( blockResult.meanAccuracy <= 0.25 );

    // The audit catches the leak on the random folds: groups split across
    // train/test (group overlap per fold > 0) and train/test points nearly
    // coincide (min distance ≈ 0 relative to the cluster separation).
    for ( const auto &f : randomResult.audit.folds )
    {
        REQUIRE( f.groupOverlap > 0 );
        REQUIRE( f.minTrainTestDistance < 1.0 );
    }
    REQUIRE( !randomResult.audit.spatiallyClean() );

    // The spatial folds are clean: groups intact, large separation.
    REQUIRE( blockResult.audit.spatiallyClean() );
    REQUIRE( blockResult.audit.overallMinDistance() > 25.0 );
}

TEST_CASE( "evaluate: cancelled between folds", "[classify][spatialcv]" )
{
    LeakFixture fx( 5 );
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    const auto folds = RsSpatialCrossValidation::randomFolds( fx.y, 2, 42u );
    const auto result = RsSpatialCrossValidation::evaluate(
      fx.X, fx.y, fx.coords, fx.groupIds, folds, factory, true,
      []() { return true; } );
    REQUIRE( !result.ok() );
    REQUIRE( result.errorMessage == QLatin1String( "Cancelled" ) );
}

TEST_CASE( "evaluate: degenerate folds are a typed failure", "[classify][spatialcv]" )
{
    LeakFixture fx( 5 );
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    // A fold with an empty test side is refused, not silently scored.
    QVector<RsSpatialCrossValidation::Fold> folds;
    RsSpatialCrossValidation::Fold f;
    f.trainIndices = { 0, 1, 2, 3 };
    folds.append( f );
    const auto result = RsSpatialCrossValidation::evaluate(
      fx.X, fx.y, fx.coords, fx.groupIds, folds, factory );
    REQUIRE( !result.ok() );
}
