// Phase 10A Task 10.8 — RsClassifierSvm accuracy test.
//
// Same 3-Gaussian dataset as the NormalBayes test; require >= 0.9 self-acc.
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>

#include "rs_classifier_svm.h"
#include "rs_feature_scaler.h"

TEST_CASE( "SVM RBF: 3 Gaussians separable, accuracy >= 0.9",
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

  RsClassifierSvm clf;
  REQUIRE( clf.fit( X, y ) );

  const cv::Mat pred = clf.predict( X );
  REQUIRE( pred.rows == y.rows );

  int correct = 0;
  for ( int i = 0; i < pred.rows; ++i )
    if ( pred.at<int>( i, 0 ) == y.at<int>( i, 0 ) )
      ++correct;
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.9 );
}

TEST_CASE( "SVM RBF: multi-scale bands need scaler for high accuracy",
           "[classify][backend][scaler]" )
{
  cv::RNG rng( 7 );
  cv::Mat X( 600, 2, CV_32F ), y( 600, 1, CV_32S );
  // class1: band0 ~ N(5,1), band1 ~ N(5000,100)
  // class2: band0 ~ N(8,1), band1 ~ N(5200,100)
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i, 0 ) = 5.0f + static_cast<float>( rng.gaussian( 1.0 ) );
    X.at<float>( i, 1 ) = 5000.0f + static_cast<float>( rng.gaussian( 100.0 ) );
    y.at<int>( i, 0 ) = 1;
    X.at<float>( i + 300, 0 ) = 8.0f + static_cast<float>( rng.gaussian( 1.0 ) );
    X.at<float>( i + 300, 1 ) = 5200.0f + static_cast<float>( rng.gaussian( 100.0 ) );
    y.at<int>( i + 300, 0 ) = 2;
  }
  RsFeatureScaler sc;
  REQUIRE( sc.fit( X ) );
  const cv::Mat Xs = sc.transform( X );
  RsClassifierSvm clf;
  REQUIRE( clf.fit( Xs, y ) );
  const cv::Mat pred = clf.predict( Xs );
  int correct = 0;
  for ( int i = 0; i < pred.rows; ++i )
    if ( pred.at<int>( i, 0 ) == y.at<int>( i, 0 ) )
      ++correct;
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.9 );
}
