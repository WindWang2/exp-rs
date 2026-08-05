#include "rs_jm_separability.h"

#include <QList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace
{

/// Split extraction output (X rows, y class ids) into per-class row-sample
/// matrices. Classes with fewer than 2 samples are omitted — their covariance
/// would be degenerate (matches the pre-ADR-0055 GUI skip).
QHash<int, cv::Mat> splitByClass( const cv::Mat &X, const cv::Mat &y )
{
  QHash<int, cv::Mat> buckets;
  if ( X.empty() || y.empty() || X.rows != y.rows || X.cols <= 0 )
    return buckets;

  QHash<int, QVector<int>> rowsByClass;
  rowsByClass.reserve( std::max( 1, X.rows / 2 ) );
  for ( int s = 0; s < X.rows; ++s )
    rowsByClass[y.at<int>( s, 0 )].push_back( s );

  for ( auto it = rowsByClass.constBegin(); it != rowsByClass.constEnd(); ++it )
  {
    const QVector<int> &rows = it.value();
    if ( rows.size() < 2 )
      continue;
    cv::Mat m( rows.size(), X.cols, CV_32F );
    for ( int i = 0; i < rows.size(); ++i )
      X.row( rows[i] ).copyTo( m.row( i ) );
    buckets.insert( it.key(), m );
  }
  return buckets;
}

} // namespace

QHash<QPair<int, int>, double> RsJmSeparability::computeAll( const cv::Mat &X,
                                                             const cv::Mat &y )
{
  QHash<QPair<int, int>, double> jm;
  const QHash<int, cv::Mat> buckets = splitByClass( X, y );
  QList<int> ids = buckets.keys();
  std::sort( ids.begin(), ids.end() );
  for ( int i = 0; i < ids.size(); ++i )
  {
    for ( int j = i + 1; j < ids.size(); ++j )
    {
      jm.insert( qMakePair( ids[i], ids[j] ),
                 pairJm( buckets.value( ids[i] ), buckets.value( ids[j] ) ) );
    }
  }
  return jm;
}

double RsJmSeparability::pairJm( const cv::Mat &xA, const cv::Mat &xB )
{
  if ( xA.empty() || xB.empty() )
    return 0.0;
  if ( xA.cols != xB.cols )
    return 0.0;

  const int d = xA.cols;
  const double eps = 1e-6;

  // calcCovarMatrix with COVAR_ROWS treats each row as a sample; the mu
  // output is a 1 x d row vector. SCALE normalizes by 1/N.
  cv::Mat muA;
  cv::Mat muB;
  cv::Mat covA;
  cv::Mat covB;
  cv::calcCovarMatrix( xA, covA, muA,
                       cv::COVAR_NORMAL | cv::COVAR_ROWS | cv::COVAR_SCALE,
                       CV_64F );
  cv::calcCovarMatrix( xB, covB, muB,
                       cv::COVAR_NORMAL | cv::COVAR_ROWS | cv::COVAR_SCALE,
                       CV_64F );

  // Epsilon ridge — prevents singular covariance with constant or very
  // small samples (e.g. a 2-pixel ROI). Tiny relative to typical band
  // variances yet large enough to keep determinant > 0.
  const cv::Mat eye = cv::Mat::eye( d, d, CV_64F );
  covA += eps * eye;
  covB += eps * eye;
  cv::Mat covBar = 0.5 * ( covA + covB );

  // Bhattacharyya distance has two terms:
  //   term1 = (1/8) * (muA - muB)^T * covBar^-1 * (muA - muB)
  //   term2 = (1/2) * ln( det(covBar) / sqrt(det(covA) * det(covB)) )
  cv::Mat diff = ( muA - muB ).t(); // d x 1 column vector
  cv::Mat covBarInv = covBar.inv();
  cv::Mat term1m = diff.t() * covBarInv * diff;
  double term1 = term1m.at<double>( 0, 0 ) / 8.0;

  const double detBar = std::max( cv::determinant( covBar ), 1e-300 );
  const double detA = std::max( cv::determinant( covA ), 1e-300 );
  const double detB = std::max( cv::determinant( covB ), 1e-300 );
  double term2 = 0.5 * std::log( detBar / std::sqrt( detA * detB ) );

  double B = term1 + term2;
  if ( !std::isfinite( B ) || B < 0.0 )
    B = 0.0;
  return 2.0 * ( 1.0 - std::exp( -B ) );
}
