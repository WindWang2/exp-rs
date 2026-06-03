// rs_accuracy_assessment.cpp — Phase 10A Task 10.9.

#include "rs_accuracy_assessment.h"

#include <QSet>

#include <algorithm>

RsAccuracyAssessment::Result
RsAccuracyAssessment::compute( const QVector<int> &yt, const QVector<int> &yp )
{
  Result r;
  if ( yt.size() != yp.size() || yt.isEmpty() )
    return r;

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
  r.overallAccuracy = static_cast<double>( diag ) / total;

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
  pe /= static_cast<double>( total ) * total;
  if ( r.overallAccuracy == 1.0 )
    r.kappa = 1.0;
  else if ( pe == 1.0 )
    r.kappa = 1.0; // degenerate single-class case
  else
    r.kappa = ( r.overallAccuracy - pe ) / ( 1.0 - pe );

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
    r.producerAcc[id] = colSum ? static_cast<double>( d ) / colSum : 0.0;
    r.userAcc[id]     = rowSum ? static_cast<double>( d ) / rowSum : 0.0;
    const double p = r.producerAcc[id];
    const double u = r.userAcc[id];
    r.f1[id] = ( p + u ) > 0 ? 2.0 * p * u / ( p + u ) : 0.0;
  }

  return r;
}
