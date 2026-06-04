// Phase 10A.1.3 — RsClassifierBackend isFitted() + save/load round-trip.
//
// Validates that NormalBayes and SVM backends correctly report their fitted
// state, persist to YAML, and reproduce identical predictions after reload.
#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include "rs_classifier_normalbayes.h"
#include "rs_classifier_svm.h"

namespace
{
void makeData( cv::Mat &X, cv::Mat &y )
{
  cv::RNG rng( 42 );
  X.create( 300, 2, CV_32F );
  y.create( 300, 1, CV_32S );
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    X.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    y.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( 100 + i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    X.at<float>( 100 + i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    y.at<int>( 100 + i, 0 ) = 2;
  }
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( 200 + i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    X.at<float>( 200 + i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    y.at<int>( 200 + i, 0 ) = 3;
  }
}
} // namespace

TEST_CASE( "isFitted: false before fit, true after", "[classify][persist]" )
{
  RsClassifierNormalBayes clf;
  REQUIRE_FALSE( clf.isFitted() );
  cv::Mat X, y;
  makeData( X, y );
  REQUIRE( clf.fit( X, y ) );
  REQUIRE( clf.isFitted() );
}

TEST_CASE( "NormalBayes save+load: predictions identical", "[classify][persist]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + QStringLiteral( "/nb.yml" );
  cv::Mat X, y;
  makeData( X, y );
  RsClassifierNormalBayes a;
  REQUIRE( a.fit( X, y ) );
  REQUIRE( a.save( path ) );
  RsClassifierNormalBayes b;
  REQUIRE_FALSE( b.isFitted() );
  REQUIRE( b.load( path ) );
  REQUIRE( b.isFitted() );
  const cv::Mat predA = a.predict( X );
  const cv::Mat predB = b.predict( X );
  REQUIRE( predA.rows == predB.rows );
  for ( int i = 0; i < predA.rows; ++i )
    REQUIRE( predA.at<int>( i, 0 ) == predB.at<int>( i, 0 ) );
}

TEST_CASE( "SVM save+load: predictions identical", "[classify][persist]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString path = tmp.path() + QStringLiteral( "/svm.yml" );
  cv::Mat X, y;
  makeData( X, y );
  RsClassifierSvm a;
  REQUIRE( a.fit( X, y ) );
  REQUIRE( a.save( path ) );
  RsClassifierSvm b;
  REQUIRE_FALSE( b.isFitted() );
  REQUIRE( b.load( path ) );
  REQUIRE( b.isFitted() );
  const cv::Mat predA = a.predict( X );
  const cv::Mat predB = b.predict( X );
  REQUIRE( predA.rows == predB.rows );
  for ( int i = 0; i < predA.rows; ++i )
    REQUIRE( predA.at<int>( i, 0 ) == predB.at<int>( i, 0 ) );
}

TEST_CASE( "Load invalid path returns false", "[classify][persist]" )
{
  RsClassifierNormalBayes clf;
  REQUIRE_FALSE( clf.load( QStringLiteral( "/does/not/exist.yml" ) ) );
  REQUIRE_FALSE( clf.isFitted() );
}
