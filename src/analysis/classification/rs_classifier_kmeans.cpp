// rs_classifier_kmeans.cpp — Phase 10A Task 10.8.

#include "rs_classifier_kmeans.h"
#include "sicnu_logging.h"

#include <QDebug>

#include <algorithm>
#include <limits>

RsClassifierKMeans::RsClassifierKMeans( int k )
  : m_k( k > 0 ? k : 1 )
{
}

// K-Means convergence and initialisation parameters.
// maxIter=100 — sufficient for convergence on typical RS pixel datasets.
// eps=1e-4 — centre-shift tolerance for z-score / unit-variance features
//            (Apply path standardises bands before fit). Still fine on raw DN.
// attempts=3 — run the algorithm 3 times with different seeds and keep the
//              best compactness result, balancing quality vs. runtime.
static constexpr int kKMeansMaxIter   = 100;
static constexpr double kKMeansEps    = 1e-4;
static constexpr int kKMeansAttempts  = 3;

bool RsClassifierKMeans::fit( const cv::Mat &X, const cv::Mat &y )
{
  if ( X.empty() || X.rows < m_k )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "KMeans::fit — insufficient samples: %1 rows, k=%2" )
        .arg( X.rows ).arg( m_k ) );
    return false;
  }

  // ADR 0061 — y is not used for training, but it decides whether the
  // arbitrary cluster ids must later be remapped onto real class ids: any
  // non-zero label means the caller trained against true labels (GUI ROI
  // path); an all-zero y is the unsupervised operator's dummy and keeps raw
  // 1..K cluster ids verbatim.
  m_remapNeeded = false;
  for ( int i = 0; i < y.rows && !m_remapNeeded; ++i )
    m_remapNeeded = ( y.at<int>( i, 0 ) != 0 );

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "KMeans training: %1 samples, %2 features, k=%3" )
      .arg( X.rows ).arg( X.cols ).arg( m_k ) );
  try
  {
    cv::Mat data = X;
    if ( data.type() != CV_32F )
      data.convertTo( data, CV_32F );

    cv::Mat labels;
    const cv::TermCriteria term( cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS,
                                 kKMeansMaxIter, kKMeansEps );
    cv::kmeans( data, m_k, labels, term, kKMeansAttempts, cv::KMEANS_PP_CENTERS, m_centers );
    SICNU_LOG_SUCCESS( SicnuLogTags::Classification, "KMeans training complete" );
    return !m_centers.empty();
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "KMeans::fit — OpenCV error: %1" ).arg( e.what() ) );
    m_centers = cv::Mat();
    return false;
  }
  catch ( const std::exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "KMeans::fit — error: %1" ).arg( e.what() ) );
    m_centers = cv::Mat();
    return false;
  }
}

cv::Mat RsClassifierKMeans::predict( const cv::Mat &X ) const
{
  cv::Mat out( X.rows, 1, CV_32S );
  if ( m_centers.empty() || X.empty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, "KMeans::predict — model not trained or empty input" );
    out.setTo( 0 );
    return out;
  }

  SICNU_LOG_DEBUG( SicnuLogTags::Classification, QString( "KMeans predicting %1 samples" ).arg( X.rows ) );

  try
  {
    cv::Mat data = X;
    if ( data.type() != CV_32F )
      data.convertTo( data, CV_32F );

    for ( int i = 0; i < data.rows; ++i )
    {
      const cv::Mat sample = data.row( i );
      double best = std::numeric_limits<double>::max();
      int bestK = 0;
      for ( int k = 0; k < m_centers.rows; ++k )
      {
        const double d = cv::norm( sample, m_centers.row( k ) );
        if ( d < best )
        {
          best = d;
          bestK = k;
        }
      }
      out.at<int>( i, 0 ) = bestK + 1; // 1-based class ID
    }
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "KMeans::predict — OpenCV error: %1" ).arg( e.what() ) );
    out = cv::Mat();
  }
  catch ( const std::exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, QString( "KMeans::predict — error: %1" ).arg( e.what() ) );
    out = cv::Mat();
  }
  return out;
}
