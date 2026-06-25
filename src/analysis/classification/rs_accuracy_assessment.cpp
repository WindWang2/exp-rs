// rs_accuracy_assessment.cpp — Phase 10A Task 10.9.

#include "rs_accuracy_assessment.h"
#include "../../processing/algorithms/math_utils.h"
#include "sicnu_logging.h"

#include <QSet>

#include <algorithm>

RsAccuracyAssessment::Result
RsAccuracyAssessment::compute( const QVector<int> &yt, const QVector<int> &yp )
{
  Result r;
  if ( yt.size() != yp.size() || yt.isEmpty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "Accuracy assessment: mismatched or empty vectors (yt=%1, yp=%2)" )
        .arg( yt.size() ).arg( yp.size() ) );
    return r;
  }

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Computing accuracy assessment: %1 samples" ).arg( yt.size() ) );

  // Collect the union of observed class IDs and sort them for a stable
  // matrix row/column ordering.
  QSet<int> setIds;
  for ( int v : yt )
    setIds.insert( v );
  for ( int v : yp )
    setIds.insert( v );
  r.classIds = QVector<int>( setIds.begin(), setIds.end() );
  std::sort( r.classIds.begin(), r.classIds.end() );

  QHash<int, int> idToRow;
  for ( int i = 0; i < r.classIds.size(); ++i )
    idToRow[r.classIds[i]] = i;

  const int n = r.classIds.size();
  r.confusion = cv::Mat::zeros( n, n, CV_32S );
  for ( int i = 0; i < yt.size(); ++i )
    ++r.confusion.at<int>( idToRow[yt[i]], idToRow[yp[i]] );

  const int total = yt.size();
  int diag = 0;
  for ( int i = 0; i < n; ++i )
    diag += r.confusion.at<int>( i, i );
  r.overallAccuracy = MathUtils::safeDivDouble(diag, total);

  // Cohen's Kappa: (po - pe) / (1 - pe). Guard po==1.0 so a perfect
  // prediction returns kappa=1.0 instead of 0/0.
  double pe = 0.0;
  for ( int i = 0; i < n; ++i )
  {
    int rowSum = 0;
    int colSum = 0;
    for ( int k = 0; k < n; ++k )
    {
      rowSum += r.confusion.at<int>( i, k );
      colSum += r.confusion.at<int>( k, i );
    }
    pe += static_cast<double>( rowSum ) * colSum;
  }
  pe = MathUtils::safeDivDouble(pe, static_cast<double>(total) * total);
  if ( r.overallAccuracy == 1.0 )
    r.kappa = 1.0;
  else if ( pe == 1.0 )
    r.kappa = 1.0; // degenerate single-class case
  else
    r.kappa = MathUtils::safeDivDouble(r.overallAccuracy - pe, 1.0 - pe);

  // Per-class Producer / User / F1.
  for ( int i = 0; i < n; ++i )
  {
    int rowSum = 0;
    int colSum = 0;
    const int d = r.confusion.at<int>( i, i );
    for ( int k = 0; k < n; ++k )
    {
      rowSum += r.confusion.at<int>( i, k );
      colSum += r.confusion.at<int>( k, i );
    }
    const int id = r.classIds[i];
    r.producerAcc[id] = MathUtils::safeDivDouble(d, colSum);
    r.userAcc[id]     = MathUtils::safeDivDouble(d, rowSum);
    const double p = r.producerAcc[id];
    const double u = r.userAcc[id];
    r.f1[id] = MathUtils::safeDivDouble(2.0 * p * u, p + u);
  }

  SICNU_LOG_SUCCESS( SicnuLogTags::Classification, QString( "Accuracy: overall=%1, kappa=%2, classes=%3" )
      .arg( r.overallAccuracy, 0, 'f', 4 ).arg( r.kappa, 0, 'f', 4 ).arg( r.classIds.size() ) );
  return r;
}
