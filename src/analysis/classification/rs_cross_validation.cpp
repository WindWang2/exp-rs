// rs_cross_validation.cpp — Phase 10A.1.2 implementation.
#include "rs_cross_validation.h"
#include "rs_feature_scaler.h"
#include "rs_hungarian_assignment.h"
#include "sicnu_logging.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <random>

RsCrossValidation::Result RsCrossValidation::kFold(
  const cv::Mat &X,
  const cv::Mat &y,
  std::function<std::unique_ptr<RsClassifierBackend>()> factory,
  int k,
  bool scaleFeatures,
  std::function<bool()> isCanceled,
  unsigned int seed )
{
  Result r;
  if ( X.empty() || y.empty() )
  {
    r.errorMessage = QStringLiteral( "Empty training matrix" );
    return r;
  }
  if ( X.rows != y.rows || X.rows < 2 )
  {
    r.errorMessage = QStringLiteral( "Insufficient samples for CV" );
    return r;
  }
  if ( k < 2 )
  {
    r.errorMessage = QStringLiteral( "Fold count k must be >= 2" );
    return r;
  }
  if ( !factory )
  {
    r.errorMessage = QStringLiteral( "No backend factory" );
    return r;
  }

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Starting %1-fold cross-validation: %2 samples, %3 features, scale=%4" )
      .arg( k ).arg( X.rows ).arg( X.cols ).arg( scaleFeatures ? "on" : "off" ) );

  // Group sample indices by class label.
  QHash<int, QVector<int>> byClass;
  for ( int i = 0; i < y.rows; ++i )
    byClass[y.at<int>( i, 0 )].append( i );

  // Shuffle each bucket with a deterministic seed.
  std::mt19937 rng( seed );
  for ( auto it = byClass.begin(); it != byClass.end(); ++it )
  {
    std::shuffle( it.value().begin(), it.value().end(), rng );
  }

  // Round-robin assign indices to folds (stratified).
  // Distribute all samples so that even classes with < k samples are tested across folds.
  QVector<QVector<int>> foldTest( k );
  for ( auto it = byClass.constBegin(); it != byClass.constEnd(); ++it )
  {
    const QVector<int> &bucket = it.value();
    for ( int i = 0; i < bucket.size(); ++i )
      foldTest[i % k].append( bucket[i] );
  }

  QVector<double> accs;
  accs.reserve( k );

  for ( int fi = 0; fi < k; ++fi )
  {
    if ( isCanceled && isCanceled() )
    {
      SICNU_LOG_INFO( SicnuLogTags::Classification,
                      QString( "Cross-validation cancelled before fold %1" ).arg( fi + 1 ) );
      r.errorMessage = QStringLiteral( "Cancelled" );
      return r;
    }

    QVector<int> trainIdx, testIdx;
    testIdx = foldTest[fi];
    for ( int fj = 0; fj < k; ++fj )
    {
      if ( fj == fi )
        continue;
      trainIdx += foldTest[fj];
    }

    if ( trainIdx.isEmpty() || testIdx.isEmpty() )
    {
      // Degenerate fold; skip.
      continue;
    }

    cv::Mat trainX( trainIdx.size(), X.cols, X.type() );
    cv::Mat trainY( trainIdx.size(), 1, CV_32S );
    for ( int i = 0; i < trainIdx.size(); ++i )
    {
      X.row( trainIdx[i] ).copyTo( trainX.row( i ) );
      trainY.at<int>( i, 0 ) = y.at<int>( trainIdx[i], 0 );
    }
    cv::Mat testX( testIdx.size(), X.cols, X.type() );
    cv::Mat testY( testIdx.size(), 1, CV_32S );
    for ( int i = 0; i < testIdx.size(); ++i )
    {
      X.row( testIdx[i] ).copyTo( testX.row( i ) );
      testY.at<int>( i, 0 ) = y.at<int>( testIdx[i], 0 );
    }

    // Mirror Apply: fit scaler on fold train only, transform train + test.
    if ( scaleFeatures )
    {
      RsFeatureScaler scaler;
      if ( !scaler.fit( trainX ) )
        continue;
      trainX = scaler.transform( trainX );
      testX = scaler.transform( testX );
      if ( trainX.empty() || testX.empty() )
        continue;
    }

    auto backend = factory();
    if ( !backend )
    {
      r.errorMessage = QStringLiteral( "Factory returned null" );
      return r;
    }
    if ( !backend->fit( trainX, trainY ) )
      continue;

    // For clustering backends (e.g. KMeans), remap cluster IDs to true class IDs via Hungarian assignment
    QHash<int, int> clusterRemap;
    if ( backend->needsLabelRemap() )
    {
      try
      {
        cv::Mat trainPred = backend->predict( trainX );
        QSet<int> trueSet, clusterSet;
        for ( int i = 0; i < trainY.rows; ++i )
        {
          const int yVal = trainY.at<int>( i, 0 );
          if ( yVal > 0 )
            trueSet.insert( yVal );
        }
        for ( int i = 0; i < trainPred.rows; ++i )
        {
          const int cVal = trainPred.at<int>( i, 0 );
          if ( cVal > 0 )
            clusterSet.insert( cVal );
        }

        if ( !trueSet.isEmpty() && !clusterSet.isEmpty() )
        {
          QList<int> tList( trueSet.begin(), trueSet.end() );
          QList<int> cList( clusterSet.begin(), clusterSet.end() );
          std::sort( tList.begin(), tList.end() );
          std::sort( cList.begin(), cList.end() );
          const int N = tList.size();
          const int M = cList.size();
          cv::Mat cost = cv::Mat::zeros( N, M, CV_64F );
          for ( int i = 0; i < trainY.rows; ++i )
          {
            const int ti = tList.indexOf( trainY.at<int>( i, 0 ) );
            const int ci = cList.indexOf( trainPred.at<int>( i, 0 ) );
            if ( ti >= 0 && ci >= 0 )
              cost.at<double>( ti, ci ) -= 1.0;
          }

          const QVector<int> assign = RsHungarianAssignment::solve( cost );
          for ( int i = 0; i < N && i < assign.size(); ++i )
          {
            const int clusterIdx = assign[i];
            if ( clusterIdx >= 0 && clusterIdx < M )
              clusterRemap[cList[clusterIdx]] = tList[i];
          }
        }
      }
      catch ( ... )
      {
        // Fallback to raw prediction if Hungarian remap fails
      }
    }

    cv::Mat pred;
    try
    {
      pred = backend->predict( testX );
    }
    catch ( ... )
    {
      continue;
    }
    if ( pred.empty() || pred.rows != testY.rows )
      continue;

    int correct = 0;
    for ( int i = 0; i < pred.rows; ++i )
    {
      int pVal = pred.at<int>( i, 0 );
      if ( backend->needsLabelRemap() )
        pVal = clusterRemap.isEmpty() ? pVal : clusterRemap.value( pVal, 0 );
      if ( pVal == testY.at<int>( i, 0 ) )
        ++correct;
    }
    accs.append( double( correct ) / pred.rows );
  }

  if ( accs.isEmpty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "Cross-validation: all %1 folds failed" ).arg( k ) );
    r.errorMessage = QStringLiteral( "All folds failed" );
    return r;
  }
  r.foldAccuracies = accs;

  double sum = 0;
  for ( double a : accs )
    sum += a;
  r.meanAccuracy = sum / accs.size();

  double sq = 0;
  for ( double a : accs )
    sq += ( a - r.meanAccuracy ) * ( a - r.meanAccuracy );
  r.stdAccuracy = std::sqrt( sq / accs.size() );

  SICNU_LOG_SUCCESS( SicnuLogTags::Classification, QString( "Cross-validation complete: mean=%1, std=%2, folds=%3" )
      .arg( r.meanAccuracy, 0, 'f', 4 ).arg( r.stdAccuracy, 0, 'f', 4 ).arg( accs.size() ) );
  return r;
}
