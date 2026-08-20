// Phase 10A Task 10.8 — RsClassifierKMeans cluster-centre test.
//
// 3 Gaussians with means (5,5)/(20,20)/(5,20), σ=2. After K-Means with K=3 we
// require that each true mean has *some* centre within 1.5 absolute units
// (≈ 0.75σ). Centre ordering is non-deterministic so we match each true mean
// to the nearest learned centre.
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>

#include <limits>

#include "rs_classifier_kmeans.h"

TEST_CASE( "KMeans: 3 Gaussians, centres within tolerance of true means",
           "[classify][backend]" )
{
  cv::RNG rng( 42 );
  cv::Mat X( 900, 2, CV_32F );

  const float trueMeans[3][2] = { { 5.0f, 5.0f },
                                  { 20.0f, 20.0f },
                                  { 5.0f, 20.0f } };
  for ( int c = 0; c < 3; ++c )
  {
    for ( int i = 0; i < 300; ++i )
    {
      X.at<float>( c * 300 + i, 0 ) =
        static_cast<float>( rng.gaussian( 2.0 ) ) + trueMeans[c][0];
      X.at<float>( c * 300 + i, 1 ) =
        static_cast<float>( rng.gaussian( 2.0 ) ) + trueMeans[c][1];
    }
  }

  RsClassifierKMeans clf( 3 );
  REQUIRE( clf.fit( X, cv::Mat() ) );

  const cv::Mat centres = clf.centers();
  REQUIRE( centres.rows == 3 );
  REQUIRE( centres.cols == 2 );

  // For each true mean find the nearest learned centre and assert distance < 1.5
  for ( int c = 0; c < 3; ++c )
  {
    double best = std::numeric_limits<double>::max();
    for ( int k = 0; k < centres.rows; ++k )
    {
      const double dx = centres.at<float>( k, 0 ) - trueMeans[c][0];
      const double dy = centres.at<float>( k, 1 ) - trueMeans[c][1];
      const double d = std::sqrt( dx * dx + dy * dy );
      if ( d < best )
        best = d;
    }
    REQUIRE( best < 1.5 );
  }

  // Predict returns 1-based IDs in [1, K]
  cv::Mat pred = clf.predict( X );
  REQUIRE( pred.rows == X.rows );
  for ( int i = 0; i < pred.rows; ++i )
  {
    const int v = pred.at<int>( i, 0 );
    REQUIRE( v >= 1 );
    REQUIRE( v <= 3 );
  }
}

TEST_CASE( "KMeans: predict returns empty matrix on feature dimension mismatch (#405)",
           "[classify][backend]" )
{
  cv::Mat trainX = ( cv::Mat_<float>( 6, 2 ) <<
    1.0f, 1.0f,
    1.1f, 0.9f,
    5.0f, 5.0f,
    5.1f, 4.9f,
    9.0f, 9.0f,
    8.9f, 9.1f );

  RsClassifierKMeans clf( 3 );
  REQUIRE( clf.fit( trainX, cv::Mat() ) );

  // Feature dimension mismatch: input has 4 columns while model expects 2
  cv::Mat mismatchColsX( 10, 4, CV_32F, cv::Scalar( 1.0f ) );
  cv::Mat predMismatch = clf.predict( mismatchColsX );
  REQUIRE( predMismatch.empty() );

  // Feature dimension mismatch: input has 1 column while model expects 2
  cv::Mat mismatchFewColsX( 10, 1, CV_32F, cv::Scalar( 1.0f ) );
  cv::Mat predFew = clf.predict( mismatchFewColsX );
  REQUIRE( predFew.empty() );
}

