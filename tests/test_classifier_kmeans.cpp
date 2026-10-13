// Phase 10A Task 10.8 — RsClassifierKMeans cluster-centre test.
//
// 3 Gaussians with means (5,5)/(20,20)/(5,20), σ=2. After K-Means with K=3 we
// require that each true mean has *some* centre within 1.5 absolute units
// (≈ 0.75σ). Centre ordering is non-deterministic so we match each true mean
// to the nearest learned centre.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
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


TEST_CASE( "KMeans: same data trains bit-identical centers and labels [r14a]",
           "[classify][backend][determinism]" )
{
  // Determinism pin (r14-A): cv::kmeans with KMEANS_PP_CENTERS seeds from
  // OpenCV's thread-local RNG, which the backend now pins with setRNGSeed(42)
  // before every fit. Two fits on the same data must agree bit-for-bit —
  // centers AND the labels predict() assigns — so the Hungarian remap onto
  // class ids is stable across runs and persisted models stay reproducible.
  cv::RNG rng( 7 );
  cv::Mat X( 300, 2, CV_32F );
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + ( i % 3 == 0 ? 5.0f : 20.0f );
    X.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + ( i % 3 == 1 ? 5.0f : 20.0f );
  }

  RsClassifierKMeans a( 3 ), b( 3 );
  REQUIRE( a.fit( X, cv::Mat() ) );
  REQUIRE( b.fit( X, cv::Mat() ) );

  const cv::Mat ca = a.centers();
  const cv::Mat cb = b.centers();
  REQUIRE( ca.rows == 3 );
  REQUIRE( ca.size == cb.size );
  for ( int k = 0; k < ca.rows; ++k )
    for ( int f = 0; f < ca.cols; ++f )
      REQUIRE( ca.at<float>( k, f ) == cb.at<float>( k, f ) ); // bit-exact

  const cv::Mat la = a.predict( X );
  const cv::Mat lb = b.predict( X );
  REQUIRE( la.rows == X.rows );
  REQUIRE( lb.rows == X.rows );
  for ( int i = 0; i < X.rows; ++i )
    REQUIRE( la.at<int>( i, 0 ) == lb.at<int>( i, 0 ) );
}

TEST_CASE( "KMeans: repeated fits of identical data agree (RNG seed discipline, R10-A/AN-4)",
           "[classify][backend][r10a]" )
{
  // Four corners of the unit square, K=2: kmeans++ draws its first centre
  // from OpenCV's thread-local global RNG. Without seeding, a second fit in
  // the same process inherits an advanced RNG state and converges to the
  // mirrored split — the pipeline's documented determinism policy is
  // bypassed by its own backend. Every fit must be reproducible.
  cv::Mat X( 4, 2, CV_32F );
  const float corners[4][2] = { { 0.0f, 0.0f }, { 0.0f, 1.0f },
                                { 1.0f, 0.0f }, { 1.0f, 1.0f } };
  for ( int i = 0; i < 4; ++i )
  {
    X.at<float>( i, 0 ) = corners[i][0];
    X.at<float>( i, 1 ) = corners[i][1];
  }

  cv::Mat first;
  for ( int run = 0; run < 12; ++run )
  {
    RsClassifierKMeans clf( 2 );
    REQUIRE( clf.fit( X, cv::Mat() ) );
    const cv::Mat centers = clf.centers();
    REQUIRE( centers.rows == 2 );
    REQUIRE( centers.cols == 2 );
    if ( run == 0 )
    {
      first = centers.clone();
      continue;
    }
    for ( int r = 0; r < 2; ++r )
      for ( int c = 0; c < 2; ++c )
      {
        CAPTURE( run, r, c );
        REQUIRE( centers.at<float>( r, c ) == Catch::Approx( first.at<float>( r, c ) ).margin( 1e-6 ) );
      }
  }
}
