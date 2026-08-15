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

  // Verify cluster separation: samples belonging to each true cluster should overwhelmingly
  // share the same predicted label, and the 3 clusters must have distinct labels.
  int majorityLabels[3] = { 0, 0, 0 };
  for ( int c = 0; c < 3; ++c )
  {
    int counts[4] = { 0, 0, 0, 0 };
    for ( int i = 0; i < 300; ++i )
    {
      const int v = pred.at<int>( c * 300 + i, 0 );
      counts[v]++;
    }
    int bestCount = 0;
    int bestLabel = 1;
    for ( int k = 1; k <= 3; ++k )
    {
      if ( counts[k] > bestCount )
      {
        bestCount = counts[k];
        bestLabel = k;
      }
    }
    REQUIRE( bestCount >= 270 ); // >=90% purity on 7.5-sigma separated clusters
    majorityLabels[c] = bestLabel;
  }
  REQUIRE( majorityLabels[0] != majorityLabels[1] );
  REQUIRE( majorityLabels[1] != majorityLabels[2] );
  REQUIRE( majorityLabels[0] != majorityLabels[2] );
}
