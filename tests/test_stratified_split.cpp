// Phase 10A review patch — RsClassificationSplit::stratifiedSplit() tests.
//
// 1. 100 samples × 3 classes evenly → train 70 / test 30 with class
//    proportions preserved (≈ 23/23/24 train, 7/7/9 test depending on
//    rounding of 0.7 × 30 ≈ 21 / 0.3 × 30 ≈ 9).
// 2. Class with 5 samples → all 5 go to train, none to test; other
//    classes split normally.
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>

#include <map>
#include <utility>
#include <vector>

#include "rs_classification_split.h"

namespace
{
  // Helper: build X (CV_32F NxB) and y (CV_32S Nx1) where row i for class c
  // has value c.0f in every band. This lets us verify class labels survive
  // the split.
  void makeSyntheticDataset( const std::vector<std::pair<int, int>> &classCounts,
                             cv::Mat &X, cv::Mat &y, int bands = 2 )
  {
    int total = 0;
    for ( auto &cc : classCounts )
      total += cc.second;
    X.create( total, bands, CV_32F );
    y.create( total, 1, CV_32S );
    int row = 0;
    for ( auto &cc : classCounts )
    {
      for ( int i = 0; i < cc.second; ++i )
      {
        for ( int b = 0; b < bands; ++b )
          X.at<float>( row, b ) = static_cast<float>( cc.first );
        y.at<int>( row, 0 ) = cc.first;
        ++row;
      }
    }
  }

  void countClasses( const cv::Mat &y, std::map<int, int> &out )
  {
    out.clear();
    for ( int r = 0; r < y.rows; ++r )
      out[y.at<int>( r, 0 )]++;
  }
}

TEST_CASE( "stratifiedSplit: 100×3 evenly → 70/30 with proportions preserved",
           "[classify][split]" )
{
  cv::Mat X, y;
  // 30/30/30 by class — but 30 × 3 = 90; spec says 100 samples × 3 classes
  // → roughly 33-33-34. Use 30/30/30 → train ~21/21/21, test ~9/9/9.
  makeSyntheticDataset( { { 1, 30 }, { 2, 30 }, { 3, 30 } }, X, y );

  const auto split = RsClassificationSplit::stratifiedSplit( X, y, 0.7 );

  REQUIRE( split.trainX.rows + split.testX.rows == X.rows );
  REQUIRE( split.trainY.rows == split.trainX.rows );
  REQUIRE( split.testY.rows == split.testX.rows );

  std::map<int, int> trainCounts, testCounts;
  countClasses( split.trainY, trainCounts );
  countClasses( split.testY, testCounts );

  // Each class should be present in both train and test (30 samples ≥ 7).
  for ( int c : { 1, 2, 3 } )
  {
    REQUIRE( trainCounts[c] > 0 );
    REQUIRE( testCounts[c] > 0 );
    REQUIRE( trainCounts[c] + testCounts[c] == 30 );
    // 70% of 30 = 21 ± 1
    REQUIRE( trainCounts[c] >= 20 );
    REQUIRE( trainCounts[c] <= 22 );
  }

  // Sample values should still match the class label (split preserved rows).
  for ( int r = 0; r < split.trainY.rows; ++r )
  {
    const int c = split.trainY.at<int>( r, 0 );
    REQUIRE( split.trainX.at<float>( r, 0 ) == static_cast<float>( c ) );
  }
  for ( int r = 0; r < split.testY.rows; ++r )
  {
    const int c = split.testY.at<int>( r, 0 );
    REQUIRE( split.testX.at<float>( r, 0 ) == static_cast<float>( c ) );
  }
}

TEST_CASE( "stratifiedSplit: tiny class (5 samples) all goes to train",
           "[classify][split]" )
{
  cv::Mat X, y;
  // class 1 has 5 samples (< 7), classes 2 & 3 have 30 each.
  makeSyntheticDataset( { { 1, 5 }, { 2, 30 }, { 3, 30 } }, X, y );

  const auto split = RsClassificationSplit::stratifiedSplit( X, y, 0.7 );

  REQUIRE( split.trainX.rows + split.testX.rows == X.rows );

  std::map<int, int> trainCounts, testCounts;
  countClasses( split.trainY, trainCounts );
  countClasses( split.testY, testCounts );

  // Tiny class — all 5 in train, none in test.
  REQUIRE( trainCounts[1] == 5 );
  REQUIRE( testCounts[1] == 0 );

  // Other classes still split.
  REQUIRE( trainCounts[2] > 0 );
  REQUIRE( testCounts[2] > 0 );
  REQUIRE( trainCounts[2] + testCounts[2] == 30 );

  REQUIRE( trainCounts[3] > 0 );
  REQUIRE( testCounts[3] > 0 );
  REQUIRE( trainCounts[3] + testCounts[3] == 30 );
}
