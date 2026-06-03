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
