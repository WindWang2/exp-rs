// Phase 10A Task 10.8 — RsClassifierNormalBayes accuracy test.
//
// 3 well-separated Gaussian clusters in 2D; require >= 0.9 self-accuracy.
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>

#include "rs_classifier_normalbayes.h"

TEST_CASE( "NormalBayes: 3 Gaussians separable, accuracy >= 0.9",
           "[classify][backend]" )
{
  cv::RNG rng( 42 );
  cv::Mat X( 900, 2, CV_32F ), y( 900, 1, CV_32S );

  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    X.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    y.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i + 300, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    X.at<float>( i + 300, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    y.at<int>( i + 300, 0 ) = 2;
  }
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i + 600, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    X.at<float>( i + 600, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    y.at<int>( i + 600, 0 ) = 3;
  }

  RsClassifierNormalBayes clf;
  REQUIRE( clf.fit( X, y ) );

  const cv::Mat pred = clf.predict( X );
  REQUIRE( pred.rows == y.rows );

  int correct = 0;
  for ( int i = 0; i < pred.rows; ++i )
    if ( pred.at<int>( i, 0 ) == y.at<int>( i, 0 ) )
      ++correct;
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.9 );
}

// Re-homed from the deleted GUI-owned test_obia_task.cpp (#663): the
// single-class posterior normalization is backend behavior, not GUI
// behavior — the pin follows the code it protects.
TEST_CASE( "NormalBayes predictProbabilities normalizes single-class posterior to 1.0 (#474)",
           "[classify][backend][normalbayes][474]" )
{
  RsClassifierNormalBayes nb;
  cv::Mat X = ( cv::Mat_<float>( 4, 2 ) << 1.0f, 2.0f,
              1.1f, 2.1f,
              0.9f, 1.9f,
              1.05f, 2.05f );
  cv::Mat y = ( cv::Mat_<int>( 4, 1 ) << 1, 1, 1, 1 );
  REQUIRE( nb.fit( X, y ) );

  cv::Mat probs = nb.predictProbabilities( X );
  REQUIRE( !probs.empty() );
  REQUIRE( probs.rows == 4 );
  REQUIRE( probs.cols == 1 );
  for ( int i = 0; i < 4; ++i )
  {
    CHECK( probs.at<float>( i, 0 ) == 1.0f );
  }
}
