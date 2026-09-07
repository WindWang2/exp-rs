// test_classifier_foundation5.cpp — Foundation 5.0 Milestone H: kNN and the
// statistical backends (minimum distance, Mahalanobis) + factory mapping.
//
// Training data: two clearly separable 2-D classes; every expected label is
// derivable by hand. Backends use positive class ids in any order — the
// assertions use the ids fed to fit().

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <opencv2/core.hpp>

#include "rs_classifier_backend_factory.h"
#include "rs_classifier_knn.h"
#include "rs_classifier_statistical.h"

namespace
{

/// Separable two-class set: class 7 around (0,0), class 42 around (10,10).
void makeSeparable( cv::Mat &X, cv::Mat &y )
{
    const float samples[6][2] = {
        { 0, 0 }, { 1, 1 }, { -1, 0 },   // class 7
        { 10, 10 }, { 11, 9 }, { 9, 11 } // class 42
    };
    const int labels[6] = { 7, 7, 7, 42, 42, 42 };
    X.create( 6, 2, CV_32F );
    y.create( 6, 1, CV_32S );
    for ( int i = 0; i < 6; ++i )
    {
        X.at<float>( i, 0 ) = samples[i][0];
        X.at<float>( i, 1 ) = samples[i][1];
        y.at<int>( i, 0 ) = labels[i];
    }
}

} // namespace

TEST_CASE( "Factory maps the new classical backends", "[classification][factory]" )
{
    REQUIRE( RsClassifierBackendFactory::create( QStringLiteral( "knn" ) )->name()
               .contains( QStringLiteral( "kNN" ) ) );
    REQUIRE( RsClassifierBackendFactory::create( QStringLiteral( "min_distance" ) )->name()
               .contains( QStringLiteral( "MinimumDistance" ) ) );
    REQUIRE( RsClassifierBackendFactory::create( QStringLiteral( "mahalanobis" ) )->name()
               .contains( QStringLiteral( "Mahalanobis" ) ) );
    // Established mappings unchanged.
    REQUIRE( RsClassifierBackendFactory::create( QStringLiteral( "svm" ) ) != nullptr );
    REQUIRE( RsClassifierBackendFactory::create( QStringLiteral( "normal_bayes" ) ) != nullptr );
}

TEST_CASE( "kNN classifies the separable set", "[classification][knn]" )
{
    cv::Mat X, y;
    makeSeparable( X, y );

    RsClassifierKnn knn( 3 );
    REQUIRE( knn.fit( X, y ) );
    REQUIRE( knn.isFitted() );

    cv::Mat probes( 2, 2, CV_32F );
    probes.at<float>( 0, 0 ) = 0.5f;
    probes.at<float>( 0, 1 ) = 0.0f;
    probes.at<float>( 1, 0 ) = 10.5f;
    probes.at<float>( 1, 1 ) = 10.0f;
    const cv::Mat out = knn.predict( probes );
    REQUIRE( out.rows == 2 );
    REQUIRE( out.at<int>( 0, 0 ) == 7 );
    REQUIRE( out.at<int>( 1, 0 ) == 42 );
}

TEST_CASE( "Minimum distance: nearest class mean", "[classification][min_distance]" )
{
    cv::Mat X, y;
    makeSeparable( X, y );

    RsClassifierMinDistance md;
    REQUIRE( md.fit( X, y ) );
    REQUIRE( md.isFitted() );

    cv::Mat probes( 2, 2, CV_32F );
    probes.at<float>( 0, 0 ) = 0.0f;
    probes.at<float>( 0, 1 ) = 1.0f;
    probes.at<float>( 1, 0 ) = 9.0f;
    probes.at<float>( 1, 1 ) = 10.0f;
    const cv::Mat out = md.predict( probes );
    REQUIRE( out.at<int>( 0, 0 ) == 7 );
    REQUIRE( out.at<int>( 1, 0 ) == 42 );

    // Unfitted predict yields an empty mat.
    RsClassifierMinDistance fresh;
    REQUIRE( fresh.predict( probes ).empty() );
    // Empty training refuses.
    REQUIRE_FALSE( md.fit( cv::Mat(), cv::Mat() ) );
}

TEST_CASE( "Mahalanobis: pooled-covariance nearest class", "[classification][mahalanobis]" )
{
    cv::Mat X, y;
    makeSeparable( X, y );

    RsClassifierMahalanobis mah;
    REQUIRE( mah.fit( X, y ) );
    REQUIRE( mah.isFitted() );

    cv::Mat probes( 2, 2, CV_32F );
    probes.at<float>( 0, 0 ) = -0.5f;
    probes.at<float>( 0, 1 ) = 0.5f;
    probes.at<float>( 1, 0 ) = 10.0f;
    probes.at<float>( 1, 1 ) = 9.5f;
    const cv::Mat out = mah.predict( probes );
    REQUIRE( out.at<int>( 0, 0 ) == 7 );
    REQUIRE( out.at<int>( 1, 0 ) == 42 );
}
