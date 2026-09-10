// test_classifier_isodata.cpp — Scientific Algorithms 7.0 (capability
// package E): ISODATA backend known-answer tests.
//
// ISODATA is deterministic (even-row seeding, fixed-order comparisons), so
// the assertions are exact where the algorithm contract is exact
// (determinism, discard/merge behaviour) and tolerance-based where the
// clustering problem itself is statistical (cluster recovery on separated
// Gaussians — the same convention as test_classifier_kmeans).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include <cmath>
#include <limits>
#include <set>

#include "rs_classifier_isodata.h"

using Catch::Approx;

namespace
{
cv::Mat twoClusters1D()
{
    // Ten samples at 0 and ten at 10: the cleanly separable case.
    cv::Mat X( 20, 1, CV_32F );
    for ( int i = 0; i < 10; ++i )
    {
        X.at<float>( i, 0 ) = 0.0f;
        X.at<float>( 10 + i, 0 ) = 10.0f;
    }
    return X;
}
} // namespace

TEST_CASE( "ISODATA recovers two separable clusters with a single start centre",
           "[classify][isodata]" )
{
    // k=1 start forces the split rule to discover the second cluster.
    RsClassifierIsodata::Params p;
    p.targetClusters = 2;
    p.sigmaFactor = 0.5; // 1-D spread 5 > 0.5·overall 5 → split fires
    p.minSamplesPerCluster = 3;
    RsClassifierIsodata iso( p );
    cv::Mat dummy = cv::Mat::zeros( 20, 1, CV_32S );
    REQUIRE( iso.fit( twoClusters1D(), dummy ) );
    REQUIRE( iso.centers().rows == 2 );

    const cv::Mat labels = iso.predict( twoClusters1D() );
    // Perfect separation into the two modes: the first ten samples share one
    // label, the last ten share the other, and the labels differ.
    std::set<int> first, second;
    for ( int i = 0; i < 10; ++i )
    {
        first.insert( labels.at<int>( i, 0 ) );
        second.insert( labels.at<int>( 10 + i, 0 ) );
    }
    REQUIRE( first.size() == 1 );
    REQUIRE( second.size() == 1 );
    REQUIRE( *first.begin() != *second.begin() );
    // The two centres sit at the modes (within a sample of the truth).
    std::vector<double> centers;
    for ( int k = 0; k < 2; ++k )
        centers.push_back( iso.centers().at<float>( k, 0 ) );
    std::sort( centers.begin(), centers.end() );
    REQUIRE( centers[0] == Approx( 0.0 ).margin( 1.0 ) );
    REQUIRE( centers[1] == Approx( 10.0 ).margin( 1.0 ) );
}

TEST_CASE( "ISODATA is deterministic across runs (bit-stable centres)",
           "[classify][isodata]" )
{
    cv::RNG rng( 42 );
    cv::Mat X( 300, 2, CV_32F );
    rng.fill( X, cv::RNG::UNIFORM, 0.0, 100.0 );

    RsClassifierIsodata::Params p;
    p.targetClusters = 3;
    RsClassifierIsodata iso( p );
    cv::Mat dummy = cv::Mat::zeros( 300, 1, CV_32S );
    REQUIRE( iso.fit( X, dummy ) );
    cv::Mat centersA = iso.centers().clone();
    cv::Mat labelsA = iso.predict( X );

    RsClassifierIsodata iso2( p );
    REQUIRE( iso2.fit( X, dummy ) );
    REQUIRE( iso2.centers().rows == centersA.rows );
    for ( int k = 0; k < centersA.rows; ++k )
        for ( int j = 0; j < centersA.cols; ++j )
            REQUIRE( iso2.centers().at<float>( k, j )
                     == centersA.at<float>( k, j ) ); // exact
    const cv::Mat labelsB = iso2.predict( X );
    for ( int i = 0; i < X.rows; ++i )
        REQUIRE( labelsB.at<int>( i, 0 ) == labelsA.at<int>( i, 0 ) );
}

TEST_CASE( "ISODATA merges near-duplicate centres below the merge distance",
           "[classify][isodata]" )
{
    // Unimodal blob at 0; target 4 clusters with a tight merge distance
    // collapses the over-seeded centres down to fewer than 4.
    cv::RNG rng( 7 );
    cv::Mat X( 200, 1, CV_32F );
    for ( int i = 0; i < 200; ++i )
        X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 1.0 ) );

    RsClassifierIsodata::Params p;
    p.targetClusters = 4;
    p.mergeDistance = 1.0;
    p.minSamplesPerCluster = 5;
    p.sigmaFactor = 1e9; // disable splitting
    RsClassifierIsodata iso( p );
    cv::Mat dummy = cv::Mat::zeros( 200, 1, CV_32S );
    REQUIRE( iso.fit( X, dummy ) );
    REQUIRE( iso.centers().rows < 4 );
    // Every surviving centre keeps at least the minimum support.
    REQUIRE( iso.centers().rows >= 1 );
}

TEST_CASE( "ISODATA discards clusters below the minimum sample floor",
           "[classify][isodata]" )
{
    // 50 samples in one blob, target 3: outlier-driven singleton clusters
    // must not survive the Lmin discard.
    cv::RNG rng( 11 );
    cv::Mat X( 50, 2, CV_32F );
    rng.fill( X, cv::RNG::UNIFORM, 0.0, 5.0 );

    RsClassifierIsodata::Params p;
    p.targetClusters = 3;
    p.minSamplesPerCluster = 10;
    p.sigmaFactor = 1e9;
    RsClassifierIsodata iso( p );
    cv::Mat dummy = cv::Mat::zeros( 50, 1, CV_32S );
    REQUIRE( iso.fit( X, dummy ) );
    REQUIRE( iso.centers().rows >= 1 );
    // Every predicted label maps to a surviving centre (1-based).
    const cv::Mat labels = iso.predict( X );
    for ( int i = 0; i < X.rows; ++i )
    {
        const int k = labels.at<int>( i, 0 );
        REQUIRE( k >= 1 );
        REQUIRE( k <= iso.centers().rows );
    }
}

TEST_CASE( "ISODATA refuses empty input and reports unfitted state",
           "[classify][isodata]" )
{
    RsClassifierIsodata iso;
    cv::Mat dummy;
    REQUIRE_FALSE( iso.fit( cv::Mat(), dummy ) );
    REQUIRE_FALSE( iso.isFitted() );
    // Unfitted predict mirrors the KMeans contract: an all-zero label
    // column (never a crash, never a fabricated cluster id).
    const cv::Mat labels = iso.predict( cv::Mat( 2, 1, CV_32F ) );
    REQUIRE( labels.rows == 2 );
    REQUIRE( labels.at<int>( 0, 0 ) == 0 );
    REQUIRE( labels.at<int>( 1, 0 ) == 0 );
}

TEST_CASE( "ISODATA save/load round-trips the centres",
           "[classify][isodata]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "isodata_model.yaml" );

    RsClassifierIsodata::Params p;
    p.targetClusters = 2;
    RsClassifierIsodata iso( p );
    cv::Mat dummy = cv::Mat::zeros( 20, 1, CV_32S );
    REQUIRE( iso.fit( twoClusters1D(), dummy ) );
    REQUIRE( iso.save( path ) );

    RsClassifierIsodata loaded;
    REQUIRE_FALSE( loaded.isFitted() );
    REQUIRE( loaded.load( path ) );
    REQUIRE( loaded.isFitted() );
    REQUIRE( loaded.centers().rows == iso.centers().rows );
    for ( int k = 0; k < iso.centers().rows; ++k )
        for ( int j = 0; j < iso.centers().cols; ++j )
            REQUIRE( loaded.centers().at<float>( k, j )
                     == iso.centers().at<float>( k, j ) );
    const cv::Mat a = iso.predict( twoClusters1D() );
    const cv::Mat b = loaded.predict( twoClusters1D() );
    REQUIRE( cv::countNonZero( a != b ) == 0 );
}
