// rs_classification_split.cpp — Phase 10A review patch.

#include "rs_classification_split.h"

#include <algorithm>
#include <map>
#include <random>
#include <vector>

// Stratification is stratified by class; when groupIds is supplied the
// split is group/ROI-level (whole ROI polygon stays in one split), so
// reported OA/Kappa is not optimistically biased by spatial
// autocorrelation. Pixel-level split is the fallback when groupIds is
// empty or size-mismatched.

RsTrainTestSplit RsClassificationSplit::stratifiedSplit( const cv::Mat &X,
                                                         const cv::Mat &y,
                                                         double ratio,
                                                         unsigned int seed,
                                                         const std::vector<int> &groupIds )
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

  std::mt19937 rng( seed );

  std::vector<int> trainIdx;
  std::vector<int> testIdx;
  trainIdx.reserve( X.rows );
  testIdx.reserve( X.rows );

  constexpr int kMinForSplit = 7;

  if ( !groupIds.empty() && groupIds.size() == static_cast<size_t>( y.rows ) )
  {
    // Group/ROI-level split: group samples by class label and group ID.
    // classId -> (groupId -> list of sample row indices)
    std::map<int, std::map<int, std::vector<int>>> classGroupSamples;
    for ( int r = 0; r < y.rows; ++r )
    {
      const int cls = y.at<int>( r, 0 );
      const int grp = groupIds[r];
      classGroupSamples[cls][grp].push_back( r );
    }

    for ( auto &cg : classGroupSamples )
    {
      std::vector<int> groups;
      groups.reserve( cg.second.size() );
      for ( const auto &grpPair : cg.second )
        groups.push_back( grpPair.first );

      std::shuffle( groups.begin(), groups.end(), rng );
      const int totalGroups = static_cast<int>( groups.size() );

      if ( totalGroups < 2 )
      {
        // A single group cannot be split at group level without leaving the
        // held-out set empty, which would silently drop every accuracy
        // metric. Fall back to a pixel-level split for this class: with one
        // ROI there is no cross-ROI leakage to guard against (#325's goal).
        std::vector<int> bucket;
        size_t nSamples = 0;
        for ( const auto &grpPair : cg.second )
          nSamples += grpPair.second.size();
        bucket.reserve( nSamples );
        for ( const auto &grpPair : cg.second )
          for ( int r : grpPair.second )
            bucket.push_back( r );
        std::shuffle( bucket.begin(), bucket.end(), rng );
        const int total = static_cast<int>( bucket.size() );
        if ( total < kMinForSplit )
        {
          for ( int i : bucket )
            trainIdx.push_back( i );
          continue;
        }
        int nTrain = static_cast<int>( std::round( total * ratio ) );
        if ( nTrain < 1 )
          nTrain = 1;
        if ( nTrain >= total )
          nTrain = total - 1;
        for ( int i = 0; i < nTrain; ++i )
          trainIdx.push_back( bucket[i] );
        for ( int i = nTrain; i < total; ++i )
          testIdx.push_back( bucket[i] );
        continue;
      }

      int nTrainGroups = static_cast<int>( std::round( totalGroups * ratio ) );
      if ( nTrainGroups < 1 )
        nTrainGroups = 1;
      if ( nTrainGroups >= totalGroups )
        nTrainGroups = totalGroups - 1;

      for ( int g = 0; g < nTrainGroups; ++g )
      {
        for ( int r : cg.second[groups[g]] )
          trainIdx.push_back( r );
      }
      for ( int g = nTrainGroups; g < totalGroups; ++g )
      {
        for ( int r : cg.second[groups[g]] )
          testIdx.push_back( r );
      }
    }
  }
  else
  {
    // Pixel-level split.
    // Bucket sample row indices by class label.
    std::map<int, std::vector<int>> byClass;
    for ( int r = 0; r < y.rows; ++r )
    {
      const int cls = y.at<int>( r, 0 );
      byClass[cls].push_back( r );
    }

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
