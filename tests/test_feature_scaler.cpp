#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include <QFile>
#include <QTemporaryDir>
#include <opencv2/core.hpp>
#include "rs_feature_scaler.h"

using Catch::Approx;

TEST_CASE( "FeatureScaler: fit then transform has ~0 mean", "[classify][scaler]" )
{
  cv::Mat X( 100, 2, CV_32F );
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i, 0 ) = 10.0f + static_cast<float>( i % 5 );
    X.at<float>( i, 1 ) = 1000.0f + static_cast<float>( i % 7 );
  }
  RsFeatureScaler s;
  REQUIRE( s.fit( X ) );
  REQUIRE( s.isFitted() );
  const cv::Mat Z = s.transform( X );
  REQUIRE( Z.rows == 100 );
  REQUIRE( Z.cols == 2 );
  cv::Scalar mean, stddev;
  cv::meanStdDev( Z.col( 0 ), mean, stddev );
  REQUIRE( mean[0] == Approx( 0.0 ).margin( 1e-3 ) );
  REQUIRE( stddev[0] == Approx( 1.0 ).margin( 1e-2 ) );
}

TEST_CASE( "FeatureScaler: constant column uses std=1", "[classify][scaler]" )
{
  cv::Mat X( 20, 1, CV_32F, cv::Scalar( 5.0f ) );
  RsFeatureScaler s;
  REQUIRE( s.fit( X ) );
  const cv::Mat Z = s.transform( X );
  for ( int i = 0; i < 20; ++i )
    REQUIRE( Z.at<float>( i, 0 ) == Approx( 0.0f ).margin( 1e-5f ) );
}

TEST_CASE( "FeatureScaler: JSON round-trip", "[classify][scaler]" )
{
  cv::Mat X( 50, 2, CV_32F );
  cv::randn( X, 0, 1 );
  X.col( 0 ) *= 50.0f;
  X.col( 0 ) += 100.0f;
  RsFeatureScaler a;
  REQUIRE( a.fit( X ) );
  QTemporaryDir dir;
  const QString path = dir.filePath( QStringLiteral( "m.scale.json" ) );
  REQUIRE( a.saveJson( path ) );
  RsFeatureScaler b;
  REQUIRE( b.loadJson( path ) );
  const cv::Mat za = a.transform( X.rowRange( 0, 5 ) );
  const cv::Mat zb = b.transform( X.rowRange( 0, 5 ) );
  for ( int i = 0; i < 5; ++i )
    for ( int j = 0; j < 2; ++j )
      REQUIRE( za.at<float>( i, j ) == Approx( zb.at<float>( i, j ) ).margin( 1e-5 ) );
}

// F12 hardening: non-finite training input fails the fit closed instead of
// poisoning mean/stddev (NaN behaviour of cv::meanStdDev is undefined).
TEST_CASE( "FeatureScaler: fit rejects NaN and Inf training values", "[classify][scaler][f12]" )
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  cv::Mat bad( 10, 2, CV_32F );
  for ( int i = 0; i < 10; ++i )
    for ( int j = 0; j < 2; ++j )
      bad.at<float>( i, j ) = static_cast<float>( i + j );
  bad.at<float>( 3, 1 ) = nan;
  RsFeatureScaler s;
  REQUIRE( !s.fit( bad ) );
  REQUIRE( !s.isFitted() );

  bad.at<float>( 3, 1 ) = inf;
  RsFeatureScaler s2;
  REQUIRE( !s2.fit( bad ) );

  // Finite input still fits, and the rejected scaler stays unusable.
  cv::Mat good = bad.clone();
  good.at<float>( 3, 1 ) = 4.0f;
  RsFeatureScaler s3;
  REQUIRE( s3.fit( good ) );
  REQUIRE( s3.isFitted() );
  // transform propagates non-finite positions as NaN (documented sentinel).
  cv::Mat probe( 1, 2, CV_32F );
  probe.at<float>( 0, 0 ) = 1.0f;
  probe.at<float>( 0, 1 ) = nan;
  const cv::Mat out = s3.transform( probe );
  REQUIRE( std::isnan( out.at<float>( 0, 1 ) ) );
}
