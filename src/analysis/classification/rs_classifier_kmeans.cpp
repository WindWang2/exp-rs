// rs_classifier_kmeans.cpp — Phase 10A Task 10.8.

#include "rs_classifier_kmeans.h"

#include <algorithm>
#include <limits>

RsClassifierKMeans::RsClassifierKMeans( int k )
  : mK( k > 0 ? k : 1 )
{
}

bool RsClassifierKMeans::fit( const cv::Mat &X, const cv::Mat & /*y*/ )
{
  if ( X.empty() || X.rows < mK )
    return false;
  cv::Mat data = X;
  if ( data.type() != CV_32F )
    data.convertTo( data, CV_32F );

  cv::Mat labels;
  const cv::TermCriteria term( cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS,
                               100, 1.0 );
  cv::kmeans( data, mK, labels, term, 3, cv::KMEANS_PP_CENTERS, mCenters );
  return !mCenters.empty();
}

cv::Mat RsClassifierKMeans::predict( const cv::Mat &X ) const
{
  cv::Mat out( X.rows, 1, CV_32S );
  if ( mCenters.empty() )
  {
    out.setTo( 0 );
    return out;
  }

  cv::Mat data = X;
  if ( data.type() != CV_32F )
    data.convertTo( data, CV_32F );

  for ( int i = 0; i < data.rows; ++i )
  {
    const cv::Mat sample = data.row( i );
    double best = std::numeric_limits<double>::max();
    int bestK = 0;
    for ( int k = 0; k < mCenters.rows; ++k )
    {
      const double d = cv::norm( sample, mCenters.row( k ) );
      if ( d < best )
      {
        best = d;
        bestK = k;
      }
    }
    out.at<int>( i, 0 ) = bestK + 1; // 1-based class ID
  }
  return out;
}
