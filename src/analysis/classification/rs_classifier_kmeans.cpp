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

// Deterministic seeding (R10-A/AN-4): cv::kmeans draws its kmeans++ initial
// centres from OpenCV's thread-local global RNG. Without resetting it, a
// second fit inside the same process (GUI re-train, a CV factory that
// rebuilds the backend per fold) inherits an advanced RNG state and may
// converge to a different split — bypassing the pipeline's documented
// determinism policy. A fixed seed makes every fit of identical data
// reproducible, run after run.
static constexpr uint64_t kKMeansSeed = 42ull;

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
  // Validate y before touching it: at<int> on a non-CV_32S, multi-column or
  // row-mismatched matrix is a type-punning read, and Mat::empty() is
  // total()==0 — so a (rows>0, cols==0) matrix passes every empty() guard
  // while at<int>(i,0) lands on no data. An empty label matrix is the
  // documented unsupervised dummy and keeps raw cluster ids; anything else
  // malformed is a caller bug and fails closed like
  // RsClassifierCvBackend::fit.
  m_remapNeeded = false;
  if ( !y.empty() )
  {
    if ( y.type() != CV_32S || y.cols != 1 || y.rows != X.rows )
    {
      SICNU_LOG_ERROR( SicnuLogTags::Classification,
                       QStringLiteral( "KMeans::fit — label matrix must be single-column CV_32S with one row per sample: got %1x%2 (type %3), X has %4 rows" )
                         .arg( y.rows ).arg( y.cols ).arg( y.type() ).arg( X.rows ) );
      return false;
    }
    for ( int i = 0; i < y.rows && !m_remapNeeded; ++i )
      m_remapNeeded = ( y.at<int>( i, 0 ) != 0 );
  }

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
    // Pin the thread-local OpenCV RNG before k-means++ seeding: KMEANS_PP_CENTERS
    // draws its initial centers from it, so an unseeded run made cluster ids
    // (and the Hungarian remap onto class ids) differ run-to-run on the same
    // data (r14-A determinism; the repo's own test hedged with "centre
    // ordering is non-deterministic"). Same seed every fit → same centers.
    cv::setRNGSeed( static_cast<int>( kKMeansSeed ) );
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

    // Guard against heap over-read when input feature dimension differs from
    // the fitted cluster centers (see Issue #405).
    if ( data.cols != m_centers.cols )
    {
      SICNU_LOG_ERROR( SicnuLogTags::Classification,
                       QStringLiteral( "KMeans::predict — feature dimension mismatch: input has %1 cols, model expects %2" )
                         .arg( data.cols ).arg( m_centers.cols ) );
      return cv::Mat();
    }

    const int nRows = data.rows;
    const int nCols = data.cols;
    const int nCenters = m_centers.rows;

    for ( int i = 0; i < nRows; ++i )
    {
      const float *x = data.ptr<float>( i );
      float bestDistSq = std::numeric_limits<float>::max();
      int bestK = 0;
      for ( int k = 0; k < nCenters; ++k )
      {
        const float *c = m_centers.ptr<float>( k );
        float distSq = 0.0f;
        for ( int j = 0; j < nCols; ++j )
        {
          const float diff = x[j] - c[j];
          distSq += diff * diff;
        }
        if ( distSq < bestDistSq )
        {
          bestDistSq = distSq;
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

bool RsClassifierKMeans::save( const QString &path ) const
{
  if ( m_centers.empty() )
    return false;
  try
  {
    cv::FileStorage fs( path.toStdString(), cv::FileStorage::WRITE );
    if ( !fs.isOpened() )
      return false;
    fs << "kmeans_model" << "{";
    fs << "k" << m_k;
    fs << "centers" << m_centers;
    fs << "remapNeeded" << ( m_remapNeeded ? 1 : 0 );
    fs << "}";
    fs.release();
    return true;
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "KMeans::save — OpenCV error: %1" ).arg( e.what() ) );
    return false;
  }
  catch ( const std::exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "KMeans::save — error: %1" ).arg( e.what() ) );
    return false;
  }
}

bool RsClassifierKMeans::load( const QString &path )
{
  try
  {
    cv::FileStorage fs( path.toStdString(), cv::FileStorage::READ );
    if ( !fs.isOpened() )
      return false;
    cv::FileNode node = fs["kmeans_model"];
    if ( !node.empty() && node.isMap() )
    {
      node["k"] >> m_k;
      node["centers"] >> m_centers;
      int remap = 0;
      node["remapNeeded"] >> remap;
      m_remapNeeded = ( remap != 0 );
    }
    else
    {
      fs["k"] >> m_k;
      fs["centers"] >> m_centers;
      int remap = 0;
      fs["remapNeeded"] >> remap;
      m_remapNeeded = ( remap != 0 );
    }
    fs.release();
    return !m_centers.empty();
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "KMeans::load — OpenCV error: %1" ).arg( e.what() ) );
    return false;
  }
  catch ( const std::exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "KMeans::load — error: %1" ).arg( e.what() ) );
    return false;
  }
}

