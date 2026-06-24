// rs_cross_validation.cpp — Phase 10A.1.2 implementation.
#include "rs_cross_validation.h"
#include "sicnu_logging.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <random>

RsCrossValidation::Result
RsCrossValidation::kFold( const cv::Mat &X, const cv::Mat &y,
                          std::function<std::unique_ptr<RsClassifierBackend>()> factory,
                          int k )
{
  Result r;
  if ( X.empty() || y.empty() || X.rows != y.rows )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, "Cross-validation: empty or mismatched X/y" );
    r.errorMessage = QStringLiteral( "Empty or mismatched X/y" );
    return r;
  }
  if ( k < 2 )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "Cross-validation: k must be >= 2 (got %1)" ).arg( k ) );
    r.errorMessage = QStringLiteral( "k must be >= 2" );
    return r;
  }
  if ( !factory )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, "Cross-validation: no backend factory provided" );
    r.errorMessage = QStringLiteral( "No backend factory" );
    return r;
  }

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Starting %1-fold cross-validation: %2 samples, %3 features" )
      .arg( k ).arg( X.rows ).arg( X.cols ) );

  // Group sample indices by class label.
  QHash<int, QVector<int>> byClass;
  for ( int i = 0; i < y.rows; ++i )
    byClass[y.at<int>( i, 0 )].append( i );

  // Shuffle each bucket with a deterministic seed.
  std::mt19937 rng( 42 );
  for ( auto it = byClass.begin(); it != byClass.end(); ++it )
  {
    std::shuffle( it.value().begin(), it.value().end(), rng );
  }

  // Round-robin assign indices to folds (stratified).
  QVector<QVector<int>> foldTest( k );
  for ( auto it = byClass.constBegin(); it != byClass.constEnd(); ++it )
  {
    const QVector<int> &bucket = it.value();
    if ( bucket.size() < k )
    {
      // Class doesn't have enough samples to spread across folds —
      // keep all in train (foldTest empty for this class).
      continue;
    }
    for ( int i = 0; i < bucket.size(); ++i )
      foldTest[i % k].append( bucket[i] );
  }

  // Build the "train always" set: classes with < k samples.
  QVector<int> trainAlways;
  for ( auto it = byClass.constBegin(); it != byClass.constEnd(); ++it )
  {
    if ( it.value().size() < k )
      trainAlways += it.value();
  }

  QVector<double> accs;
  accs.reserve( k );

  for ( int fi = 0; fi < k; ++fi )
  {
    QVector<int> trainIdx, testIdx;
    testIdx = foldTest[fi];
    for ( int fj = 0; fj < k; ++fj )
    {
      if ( fj == fi )
        continue;
      trainIdx += foldTest[fj];
    }
    trainIdx += trainAlways;

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

    auto backend = factory();
    if ( !backend )
    {
      r.errorMessage = QStringLiteral( "Factory returned null" );
      return r;
    }
    if ( !backend->fit( trainX, trainY ) )
      continue;

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
      if ( pred.at<int>( i, 0 ) == testY.at<int>( i, 0 ) )
        ++correct;
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
