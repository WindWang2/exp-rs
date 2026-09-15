// rs_spatial_cross_validation.cpp — see rs_spatial_cross_validation.h.
#include "rs_spatial_cross_validation.h"

#include "rs_feature_scaler.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();

double dist2( const std::vector<double> &coords, int a, int b )
{
  const double dx = coords[static_cast<size_t>( 2 * a )] - coords[static_cast<size_t>( 2 * b )];
  const double dy = coords[static_cast<size_t>( 2 * a + 1 )] - coords[static_cast<size_t>( 2 * b + 1 )];
  return dx * dx + dy * dy;
}
} // namespace

bool RsSpatialCrossValidation::AuditReport::spatiallyClean() const
{
  for ( const FoldAudit &f : folds )
  {
    if ( !( f.minTrainTestDistance > 0.0 ) )
      return false;
    if ( f.groupOverlap != 0 )
      return false;
  }
  return true;
}

double RsSpatialCrossValidation::AuditReport::overallMinDistance() const
{
  double m = kInf;
  for ( const FoldAudit &f : folds )
    m = std::min( m, f.minTrainTestDistance );
  return m;
}

QVector<RsSpatialCrossValidation::Fold>
RsSpatialCrossValidation::groupFolds( const std::vector<int> &groupIds, int k )
{
  QVector<Fold> folds;
  if ( k <= 0 || groupIds.empty() )
    return folds;

  // Group sample rows by group id.
  QHash<int, QVector<int>> groups;
  for ( size_t i = 0; i < groupIds.size(); ++i )
    groups[groupIds[i]].append( static_cast<int>( i ) );
  if ( groups.size() < k )
    return folds; // not enough groups — caller must not split groups

  // Standard GroupKFold partition: groups (atomic units) are assigned to
  // the currently smallest fold in descending-size order (ties by
  // ascending group id). Fold test = its own groups; train = the rest.
  QList<int> ids = groups.keys();
  std::sort( ids.begin(), ids.end(), [&]( int a, int b ) {
    const int sa = groups[a].size();
    const int sb = groups[b].size();
    if ( sa != sb )
      return sa > sb;
    return a < b;
  } );

  folds.resize( k );
  QVector<int> foldSizes( k, 0 );
  QVector<int> foldOfGroup;
  foldOfGroup.reserve( ids.size() );
  for ( int id : ids )
  {
    int target = 0;
    for ( int j = 1; j < k; ++j )
    {
      if ( foldSizes[j] < foldSizes[target] )
        target = j;
    }
    foldOfGroup.append( target );
    foldSizes[target] += groups[id].size();
  }

  QHash<int, int> foldOf; // group id -> fold index
  for ( int g = 0; g < ids.size(); ++g )
    foldOf.insert( ids[g], foldOfGroup[g] );

  for ( int j = 0; j < k; ++j )
  {
    for ( int id : ids )
    {
      if ( foldOf.value( id ) == j )
        folds[j].testIndices += groups[id];
      else
        folds[j].trainIndices += groups[id];
    }
  }
  return folds;
}

QVector<RsSpatialCrossValidation::Fold>
RsSpatialCrossValidation::blockFolds( const std::vector<double> &coords,
                                      int sampleCount, int nx, int ny )
{
  QVector<Fold> folds;
  if ( sampleCount <= 0 || nx <= 0 || ny <= 0 ||
       static_cast<int>( coords.size() ) != 2 * sampleCount )
    return folds;

  double minX = kInf, minY = kInf, maxX = -kInf, maxY = -kInf;
  for ( int i = 0; i < sampleCount; ++i )
  {
    minX = std::min( minX, coords[static_cast<size_t>( 2 * i )] );
    maxX = std::max( maxX, coords[static_cast<size_t>( 2 * i )] );
    minY = std::min( minY, coords[static_cast<size_t>( 2 * i + 1 )] );
    maxY = std::max( maxY, coords[static_cast<size_t>( 2 * i + 1 )] );
  }
  const double spanX = maxX - minX;
  const double spanY = maxY - minY;

  // Assign samples to blocks; block id = bx * ny + by.
  QHash<int, QVector<int>> blocks;
  for ( int i = 0; i < sampleCount; ++i )
  {
    const double x = coords[static_cast<size_t>( 2 * i )];
    const double y = coords[static_cast<size_t>( 2 * i + 1 )];
    int bx = spanX > 0.0
               ? static_cast<int>( ( x - minX ) / spanX * nx )
               : 0;
    int by = spanY > 0.0
               ? static_cast<int>( ( y - minY ) / spanY * ny )
               : 0;
    bx = std::clamp( bx, 0, nx - 1 );
    by = std::clamp( by, 0, ny - 1 );
    blocks[bx * ny + by].append( i );
  }

  // One fold per non-empty block.
  QList<int> blockIds = blocks.keys();
  std::sort( blockIds.begin(), blockIds.end() );
  for ( int id : blockIds )
  {
    Fold fold;
    fold.testIndices = blocks[id];
    for ( int other : blockIds )
    {
      if ( other != id )
        fold.trainIndices += blocks[other];
    }
    folds.append( fold );
  }
  return folds;
}

QVector<RsSpatialCrossValidation::Fold>
RsSpatialCrossValidation::bufferedBlockFolds( const std::vector<double> &coords,
                                              int sampleCount, int nx, int ny,
                                              double bufferDistance )
{
  QVector<Fold> folds = blockFolds( coords, sampleCount, nx, ny );
  if ( bufferDistance <= 0.0 || folds.isEmpty() )
    return folds;
  const double buffer2 = bufferDistance * bufferDistance;
  for ( Fold &fold : folds )
  {
    QVector<int> retained;
    retained.reserve( fold.trainIndices.size() );
    for ( int t : fold.trainIndices )
    {
      bool nearTest = false;
      for ( int s : fold.testIndices )
      {
        if ( dist2( coords, t, s ) <= buffer2 )
        {
          nearTest = true;
          break;
        }
      }
      if ( nearTest )
        fold.excludedBuffer.append( t );
      else
        retained.append( t );
    }
    fold.trainIndices = retained;
  }
  return folds;
}

QVector<RsSpatialCrossValidation::Fold>
RsSpatialCrossValidation::randomFolds( const cv::Mat &y, int k, unsigned int seed )
{
  QVector<Fold> folds;
  if ( k <= 0 || y.empty() )
    return folds;

  // Class-bucketed deterministic shuffle (mirrors the RsCrossValidation
  // round-robin baseline for comparability).
  QHash<int, QVector<int>> byClass;
  for ( int i = 0; i < y.rows; ++i )
    byClass[y.at<int>( i, 0 )].append( i );

  std::mt19937 rng( seed );
  QVector<QVector<int>> shuffling( k );
  QList<int> classes = byClass.keys();
  std::sort( classes.begin(), classes.end() );
  for ( int c : classes )
  {
    QVector<int> rows = byClass[c];
    std::shuffle( rows.begin(), rows.end(), rng );
    for ( int j = 0; j < rows.size(); ++j )
      shuffling[j % k].append( rows[j] );
  }
  folds.resize( k );
  for ( int j = 0; j < k; ++j )
  {
    folds[j].testIndices = shuffling[j];
    for ( int t = 0; t < k; ++t )
    {
      if ( t != j )
        folds[j].trainIndices += shuffling[t];
    }
  }
  return folds;
}

RsSpatialCrossValidation::Result
RsSpatialCrossValidation::evaluate( const cv::Mat &X,
                                    const cv::Mat &y,
                                    const std::vector<double> &coords,
                                    const std::vector<int> &groupIds,
                                    const QVector<Fold> &folds,
                                    std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                                    bool scaleFeatures,
                                    std::function<bool()> isCanceled )
{
  Result result;
  if ( !factory || X.empty() || y.empty() || X.rows != y.rows || folds.isEmpty() )
  {
    result.errorMessage = QStringLiteral( "Invalid spatial CV input" );
    return result;
  }
  const bool hasCoords = static_cast<int>( coords.size() ) == 2 * X.rows;
  const bool hasGroups = static_cast<int>( groupIds.size() ) == X.rows;

  for ( const Fold &fold : folds )
  {
    if ( isCanceled && isCanceled() )
    {
      result.errorMessage = QStringLiteral( "Cancelled" );
      return result;
    }
    if ( fold.testIndices.isEmpty() || fold.trainIndices.isEmpty() )
    {
      result.errorMessage = QStringLiteral( "Degenerate fold (empty train or test side)" );
      return result;
    }

    cv::Mat trainX( static_cast<int>( fold.trainIndices.size() ), X.cols, CV_32F );
    cv::Mat trainY( static_cast<int>( fold.trainIndices.size() ), 1, CV_32S );
    for ( int r = 0; r < static_cast<int>( fold.trainIndices.size() ); ++r )
    {
      X.row( fold.trainIndices[r] ).copyTo( trainX.row( r ) );
      trainY.at<int>( r, 0 ) = y.at<int>( fold.trainIndices[r], 0 );
    }
    cv::Mat testX( static_cast<int>( fold.testIndices.size() ), X.cols, CV_32F );
    cv::Mat testY( static_cast<int>( fold.testIndices.size() ), 1, CV_32S );
    for ( int r = 0; r < static_cast<int>( fold.testIndices.size() ); ++r )
    {
      X.row( fold.testIndices[r] ).copyTo( testX.row( r ) );
      testY.at<int>( r, 0 ) = y.at<int>( fold.testIndices[r], 0 );
    }

    if ( scaleFeatures )
    {
      RsFeatureScaler scaler;
      if ( !scaler.fit( trainX ) )
      {
        result.errorMessage = QStringLiteral( "Fold scaler fit failed" );
        return result;
      }
      trainX = scaler.transform( trainX );
      testX = scaler.transform( testX );
    }

    std::unique_ptr<RsClassifierBackend> backend = factory();
    if ( !backend || !backend->fit( trainX, trainY ) )
    {
      result.errorMessage = QStringLiteral( "Fold backend fit failed" );
      return result;
    }
    const cv::Mat pred = backend->predict( testX );
    if ( pred.empty() || pred.rows != testX.rows )
    {
      result.errorMessage = QStringLiteral( "Fold prediction failed" );
      return result;
    }
    int correct = 0;
    for ( int r = 0; r < testY.rows; ++r )
    {
      if ( pred.at<int>( r, 0 ) == testY.at<int>( r, 0 ) )
        ++correct;
    }
    result.foldAccuracies.append(
      static_cast<double>( correct ) / static_cast<double>( testY.rows ) );

    // Audit on the retained sets.
    FoldAudit audit;
    audit.minTrainTestDistance = kInf;
    if ( hasCoords )
    {
      for ( int t : fold.trainIndices )
      {
        for ( int s : fold.testIndices )
        {
          const double d = std::sqrt( dist2( coords, t, s ) );
          audit.minTrainTestDistance = std::min( audit.minTrainTestDistance, d );
        }
      }
    }
    else
    {
      audit.minTrainTestDistance = kInf; // no coordinates — nothing to audit
    }
    if ( hasGroups )
    {
      QSet<int> trainGroups;
      for ( int t : fold.trainIndices )
        trainGroups.insert( groupIds[static_cast<size_t>( t )] );
      for ( int s : fold.testIndices )
      {
        const int g = groupIds[static_cast<size_t>( s )];
        if ( trainGroups.contains( g ) )
          ++audit.groupOverlap;
      }
    }
    result.audit.folds.append( audit );
  }

  const double mean = std::accumulate( result.foldAccuracies.cbegin(),
                                       result.foldAccuracies.cend(), 0.0 )
                      / static_cast<double>( result.foldAccuracies.size() );
  result.meanAccuracy = mean;
  double var = 0.0;
  for ( double a : result.foldAccuracies )
    var += ( a - mean ) * ( a - mean );
  result.stdAccuracy = std::sqrt( var / static_cast<double>( result.foldAccuracies.size() ) );
  return result;
}
