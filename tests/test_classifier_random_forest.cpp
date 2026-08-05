// OBIA Classification Optimization — RsRandomForestBackend unit test.
#include <catch2/catch_test_macros.hpp>
#include <opencv2/core.hpp>

#include "rs_classifier_random_forest.h"
#include "rs_classifier_backend_factory.h"

TEST_CASE( "RandomForest: 3 Gaussians separable, self-accuracy >= 0.9",
           "[classify][backend][rf]" )
{
  cv::RNG rng( 42 );
  cv::Mat X( 900, 2, CV_32F ), y( 900, 1, CV_32S );

  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 5.0f;
    X.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 5.0f;
    y.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i + 300, 0 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 20.0f;
    X.at<float>( i + 300, 1 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 20.0f;
    y.at<int>( i + 300, 0 ) = 2;
  }
  for ( int i = 0; i < 300; ++i )
  {
    X.at<float>( i + 600, 0 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 5.0f;
    X.at<float>( i + 600, 1 ) = static_cast<float>( rng.gaussian( 1.5 ) ) + 20.0f;
    y.at<int>( i + 600, 0 ) = 3;
  }

  RsRandomForestBackend clf( 10, 5, 10 );
  REQUIRE( clf.fit( X, y ) );

  const cv::Mat pred = clf.predict( X );
  REQUIRE( pred.rows == y.rows );

  int correct = 0;
  for ( int i = 0; i < pred.rows; ++i )
  {
    if ( pred.at<int>( i, 0 ) == y.at<int>( i, 0 ) )
      ++correct;
  }
  REQUIRE( static_cast<double>( correct ) / pred.rows >= 0.9 );
}

TEST_CASE( "RandomForest: Factory creation by name",
           "[classify][backend][rf][factory]" )
{
  auto rf = RsClassifierBackendFactory::create( "RandomForest" );
  REQUIRE( rf != nullptr );
}

// Regression guard for the synthetic-probability bug where
// predictProbabilities() used to hardcode numClasses=2 and emit a 0.9/0.1
// heuristic instead of reading real tree votes. With a 3-class problem the
// probability matrix must have 3 columns, rows must sum to ~1, and the
// argmax column must match the true class for well-separated data.
TEST_CASE( "RandomForest: true per-class probabilities from tree votes",
           "[classify][backend][rf][probability]" )
{
  cv::RNG rng( 7 );
  cv::Mat X( 300, 2, CV_32F ), y( 300, 1, CV_32S );
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 0.0f;
    X.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 0.0f;
    y.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i + 100, 0 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 30.0f;
    X.at<float>( i + 100, 1 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 0.0f;
    y.at<int>( i + 100, 0 ) = 2;
  }
  for ( int i = 0; i < 100; ++i )
  {
    X.at<float>( i + 200, 0 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 0.0f;
    X.at<float>( i + 200, 1 ) = static_cast<float>( rng.gaussian( 0.5 ) ) + 30.0f;
    y.at<int>( i + 200, 0 ) = 3;
  }

  RsRandomForestBackend clf( 50, 8, 5 );
  REQUIRE( clf.fit( X, y ) );

  cv::Mat probs = clf.predictProbabilities( X );
  REQUIRE( !probs.empty() );
  REQUIRE( probs.rows == 300 );
  REQUIRE( probs.cols == 3 ); // would have been 2 under the old bug

  for ( int i = 0; i < probs.rows; ++i )
  {
    float sum = probs.at<float>( i, 0 ) + probs.at<float>( i, 1 ) + probs.at<float>( i, 2 );
    REQUIRE( std::abs( sum - 1.0f ) < 1e-4f );

    int bestCol = 0;
    float bestVal = probs.at<float>( i, 0 );
    for ( int c = 1; c < 3; ++c )
    {
      if ( probs.at<float>( i, c ) > bestVal )
      {
        bestVal = probs.at<float>( i, c );
        bestCol = c;
      }
    }
    // Column index maps back to class id via getVotes' first row, which RTrees
    // reports in ascending sorted label order (1, 2, 3) for this dataset.
    REQUIRE( bestCol + 1 == y.at<int>( i, 0 ) );
  }
}
