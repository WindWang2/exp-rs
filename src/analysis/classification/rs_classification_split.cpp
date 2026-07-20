// rs_classification_split.cpp — Phase 10A review patch.

#include "rs_classification_split.h"

#include <algorithm>
#include <map>
#include <random>
#include <vector>

// NOTE: Stratification is by class label at the *pixel* level. Adjacent pixels
// from the same ROI polygon can land in both train and test, so reported OA/Kappa
// is optimistic under spatial autocorrelation. Prefer ROI-level splits for
// scientific accuracy reporting when that API is available.

RsTrainTestSplit RsClassificationSplit::stratifiedSplit( const cv::Mat &X,
                                                         const cv::Mat &y,
                                                         double ratio )
{
  RsTrainTestSplit out;
  if ( X.empty() || y.empty() )
    return out;
  if ( X.rows != y.rows )
    return out;

  // Clamp ratio to a reasonable range; callers may pass 0.7 from a spinbox.
  if ( ratio <= 0.0 )
    ratio = 0.7;
  if ( ratio >= 1.0 )
    ratio = 0.95;

  // Bucket sample row indices by class label.
  std::map<int, std::vector<int>> byClass;
  for ( int r = 0; r < y.rows; ++r )
  {
    const int cls = y.at<int>( r, 0 );
    byClass[cls].push_back( r );
  }

  // Deterministic shuffle for reproducibility (tests + reruns).
  std::mt19937 rng( 42u );

  std::vector<int> trainIdx;
  std::vector<int> testIdx;
  trainIdx.reserve( X.rows );
  testIdx.reserve( X.rows );

  // Threshold below which a class is "tiny" and goes wholly into train.
  // Mirrors the spec ("< 7 samples → all to train").
  constexpr int kMinForSplit = 7;

  for ( auto &kv : byClass )
  {
    std::vector<int> &bucket = kv.second;
    std::shuffle( bucket.begin(), bucket.end(), rng );

    const int total = static_cast<int>( bucket.size() );
    if ( total < kMinForSplit )
    {
      // Tiny class: dump all into train.
      for ( int i : bucket )
        trainIdx.push_back( i );
      continue;
    }

    int nTrain = static_cast<int>( std::round( total * ratio ) );
    if ( nTrain < 1 )
      nTrain = 1;
    if ( nTrain >= total )
      nTrain = total - 1; // ensure at least 1 test sample if we got here

    for ( int i = 0; i < nTrain; ++i )
      trainIdx.push_back( bucket[i] );
    for ( int i = nTrain; i < total; ++i )
      testIdx.push_back( bucket[i] );
  }

  const int B = X.cols;
  out.trainX.create( static_cast<int>( trainIdx.size() ), B, X.type() );
  out.trainY.create( static_cast<int>( trainIdx.size() ), 1, y.type() );
  for ( int r = 0; r < static_cast<int>( trainIdx.size() ); ++r )
  {
    X.row( trainIdx[r] ).copyTo( out.trainX.row( r ) );
    out.trainY.at<int>( r, 0 ) = y.at<int>( trainIdx[r], 0 );
  }

  if ( !testIdx.empty() )
  {
    out.testX.create( static_cast<int>( testIdx.size() ), B, X.type() );
    out.testY.create( static_cast<int>( testIdx.size() ), 1, y.type() );
    for ( int r = 0; r < static_cast<int>( testIdx.size() ); ++r )
    {
      X.row( testIdx[r] ).copyTo( out.testX.row( r ) );
      out.testY.at<int>( r, 0 ) = y.at<int>( testIdx[r], 0 );
    }
  }

  return out;
}
