// rs_classifier_isodata.cpp — see rs_classifier_isodata.h
#include "rs_classifier_isodata.h"
#include "sicnu_logging.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
double distSq( const cv::Mat &centers, int k, const float *x, int cols )
{
    const float *c = centers.ptr<float>( k );
    double d = 0.0;
    for ( int j = 0; j < cols; ++j )
    {
        const double diff = x[j] - c[j];
        d += diff * diff;
    }
    return d;
}

int nearestCenter( const cv::Mat &centers, const float *x, int cols )
{
    double best = std::numeric_limits<double>::max();
    int bestK = 0;
    for ( int k = 0; k < centers.rows; ++k )
    {
        const double d = distSq( centers, k, x, cols );
        if ( d < best )
        {
            best = d;
            bestK = k;
        }
    }
    return bestK;
}
} // namespace

RsClassifierIsodata::RsClassifierIsodata()
  : RsClassifierIsodata( Params() )
{
}

RsClassifierIsodata::RsClassifierIsodata( const Params &params )
  : m_params( params )
{
  if ( m_params.targetClusters < 1 )
    m_params.targetClusters = 1;
  if ( m_params.maxIterations < 1 )
    m_params.maxIterations = 1;
  if ( m_params.minSamplesPerCluster < 1 )
    m_params.minSamplesPerCluster = 1;
}

bool RsClassifierIsodata::fit( const cv::Mat &X, const cv::Mat &y )
{
  // ADR 0061 — identical remap contract to K-Means: any non-zero true label
  // means the caller trained against real classes and the pipeline must
  // Hungarian-remap the arbitrary cluster ids.
  m_remapNeeded = false;
  for ( int i = 0; i < y.rows && !m_remapNeeded; ++i )
    m_remapNeeded = ( y.at<int>( i, 0 ) != 0 );

  m_centers = cv::Mat();
  if ( X.empty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification, "ISODATA::fit — empty training matrix" );
    return false;
  }
  cv::Mat data = X;
  if ( data.type() != CV_32F )
    data.convertTo( data, CV_32F );

  const int n = data.rows;
  const int d = data.cols;
  const int initialK = std::min( m_params.targetClusters, n );
  if ( n < m_params.targetClusters )
    SICNU_LOG_WARN( SicnuLogTags::Classification,
                       QString( "ISODATA::fit — %1 samples < target %2 clusters; starting with %3" )
                         .arg( n ).arg( m_params.targetClusters ).arg( initialK ) );

  // Deterministic initialisation: evenly spaced rows of the training
  // matrix (no RNG — bit-stable across runs by construction).
  cv::Mat centers( initialK, d, CV_32F );
  for ( int k = 0; k < initialK; ++k )
  {
    const long long row = ( static_cast<long long>( k ) * n ) / initialK;
    data.row( static_cast<int>( row ) ).copyTo( centers.row( k ) );
  }

  std::vector<int> assignment( n, -1 );
  for ( int iter = 0; iter < m_params.maxIterations; ++iter )
  {
    // 1. Assign to the nearest centre.
    bool unchanged = true;
    for ( int i = 0; i < n; ++i )
    {
      const int k = nearestCenter( centers, data.ptr<float>( i ), d );
      if ( k != assignment[i] )
      {
        unchanged = false;
        assignment[i] = k;
      }
    }
    if ( unchanged && iter > 0 )
      break;

    // 2. Cluster statistics.
    const int kCount = centers.rows;
    std::vector<int> counts( kCount, 0 );
    cv::Mat sums( kCount, d, CV_64F, cv::Scalar( 0.0 ) );
    for ( int i = 0; i < n; ++i )
    {
      const int k = assignment[i];
      ++counts[k];
      const float *x = data.ptr<float>( i );
      for ( int j = 0; j < d; ++j )
        sums.at<double>( k, j ) += x[j];
    }

    // 3. Discard clusters below the minimum sample count (their samples
    //    reassign to the survivors on the next pass).
    std::vector<int> keep;
    for ( int k = 0; k < kCount; ++k )
      if ( counts[k] >= m_params.minSamplesPerCluster )
        keep.push_back( k );
    if ( static_cast<int>( keep.size() ) < kCount )
    {
      if ( keep.empty() )
        break; // everything below the floor: stop with the current centres
      cv::Mat reduced( static_cast<int>( keep.size() ), d, CV_32F );
      for ( size_t r = 0; r < keep.size(); ++r )
        centers.row( keep[r] ).copyTo( reduced.row( static_cast<int>( r ) ) );
      centers = reduced;
      std::fill( assignment.begin(), assignment.end(), -1 );
      continue;
    }

    // 4. Update the centres to the cluster means.
    cv::Mat updated( kCount, d, CV_32F );
    for ( int k = 0; k < kCount; ++k )
      for ( int j = 0; j < d; ++j )
        updated.at<float>( k, j ) =
          static_cast<float>( sums.at<double>( k, j ) / std::max( 1, counts[k] ) );
    centers = updated;

    // 5. Split the most over-dispersed cluster while below the target count
    //    (along its most variable component, half a sigma either side).
    if ( centers.rows < m_params.targetClusters )
    {
      std::vector<double> overall( d, 0.0 );
      for ( int j = 0; j < d; ++j )
      {
        double mean = 0.0;
        for ( int k = 0; k < kCount; ++k )
          mean += centers.at<float>( k, j ) * counts[k];
        mean /= std::max( 1, n );
        double var = 0.0;
        for ( int i = 0; i < n; ++i )
        {
          const double diff = data.ptr<float>( i )[j] - mean;
          var += diff * diff;
        }
        overall[j] = std::sqrt( var / std::max( 1, n ) );
      }
      int splitK = -1;
      int splitJ = -1;
      double worst = 0.0;
      for ( int k = 0; k < kCount; ++k )
      {
        // Per-cluster component sigma needs a second pass; approximate with
        // the cluster's own component spread.
        for ( int j = 0; j < d; ++j )
        {
          double var = 0.0;
          double mean = centers.at<float>( k, j );
          for ( int i = 0; i < n; ++i )
          {
            if ( assignment[i] != k )
              continue;
            const double diff = data.ptr<float>( i )[j] - mean;
            var += diff * diff;
          }
          const double sigma = std::sqrt( var / std::max( 1, counts[k] ) );
          const double ratio = overall[j] > 0.0 ? sigma / overall[j] : 0.0;
          if ( ratio > m_params.sigmaFactor && ratio > worst
               && counts[k] >= 2 * m_params.minSamplesPerCluster )
          {
            worst = ratio;
            splitK = k;
            splitJ = j;
          }
        }
      }
      if ( splitK >= 0 )
      {
        double sigma = 0.0;
        {
          double var = 0.0;
          const double mean = centers.at<float>( splitK, splitJ );
          for ( int i = 0; i < n; ++i )
          {
            if ( assignment[i] != splitK )
              continue;
            const double diff = data.ptr<float>( i )[splitJ] - mean;
            var += diff * diff;
          }
          sigma = std::sqrt( var / std::max( 1, counts[splitK] ) );
        }
        cv::Mat split( centers.rows + 1, d, CV_32F );
        for ( int k = 0; k < centers.rows; ++k )
          centers.row( k ).copyTo( split.row( k ) );
        centers.row( splitK ).copyTo( split.row( centers.rows ) );
        split.at<float>( splitK, splitJ ) =
          static_cast<float>( centers.at<float>( splitK, splitJ ) - 0.5 * sigma );
        split.at<float>( centers.rows, splitJ ) =
          static_cast<float>( centers.at<float>( splitK, splitJ ) + 0.5 * sigma );
        centers = split;
        std::fill( assignment.begin(), assignment.end(), -1 );
        continue;
      }
    }

    // 6. Merge centre pairs closer than the minimum distance (first pair per
    //    pass, index order — deterministic).
    if ( m_params.mergeDistance > 0.0 && centers.rows >= 2 )
    {
      const double mergeSq = m_params.mergeDistance * m_params.mergeDistance;
      int mergeA = -1;
      int mergeB = -1;
      for ( int a = 0; a < centers.rows && mergeA < 0; ++a )
        for ( int b = a + 1; b < centers.rows; ++b )
        {
          double d2 = 0.0;
          for ( int j = 0; j < d; ++j )
          {
            const double diff =
              centers.at<float>( a, j ) - centers.at<float>( b, j );
            d2 += diff * diff;
          }
          if ( d2 < mergeSq )
          {
            mergeA = a;
            mergeB = b;
            break;
          }
        }
      if ( mergeA >= 0 )
      {
        const int wA = counts[mergeA];
        const int wB = counts[mergeB];
        cv::Mat merged( centers.rows - 1, d, CV_32F );
        int out = 0;
        for ( int k = 0; k < centers.rows; ++k )
        {
          if ( k == mergeB )
            continue;
          if ( k == mergeA )
          {
            for ( int j = 0; j < d; ++j )
              merged.at<float>( out, j ) =
                static_cast<float>( ( centers.at<float>( mergeA, j ) * wA
                                      + centers.at<float>( mergeB, j ) * wB )
                                    / std::max( 1, wA + wB ) );
          }
          else
          {
            centers.row( k ).copyTo( merged.row( out ) );
          }
          ++out;
        }
        centers = merged;
        std::fill( assignment.begin(), assignment.end(), -1 );
        continue;
      }
    }

    if ( unchanged )
      break;
  }

  m_centers = centers;
  SICNU_LOG_INFO( SicnuLogTags::Classification,
                  QString( "ISODATA training complete: %1 clusters" ).arg( m_centers.rows ) );
  return !m_centers.empty();
}

cv::Mat RsClassifierIsodata::predict( const cv::Mat &X ) const
{
  cv::Mat out( X.rows, 1, CV_32S );
  if ( m_centers.empty() || X.empty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     "ISODATA::predict — model not trained or empty input" );
    out.setTo( 0 );
    return out;
  }
  cv::Mat data = X;
  if ( data.type() != CV_32F )
    data.convertTo( data, CV_32F );
  if ( data.cols != m_centers.cols )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "ISODATA::predict — feature dimension mismatch: input %1 cols, model %2" )
                       .arg( data.cols ).arg( m_centers.cols ) );
    return cv::Mat();
  }
  for ( int i = 0; i < data.rows; ++i )
    out.at<int>( i, 0 ) =
      nearestCenter( m_centers, data.ptr<float>( i ), data.cols ) + 1; // 1-based
  return out;
}

bool RsClassifierIsodata::save( const QString &path ) const
{
  if ( m_centers.empty() )
    return false;
  try
  {
    cv::FileStorage fs( path.toStdString(), cv::FileStorage::WRITE );
    if ( !fs.isOpened() )
      return false;
    fs << "isodata_model" << "{";
    fs << "targetClusters" << m_params.targetClusters;
    fs << "maxIterations" << m_params.maxIterations;
    fs << "minSamplesPerCluster" << m_params.minSamplesPerCluster;
    fs << "sigmaFactor" << m_params.sigmaFactor;
    fs << "mergeDistance" << m_params.mergeDistance;
    fs << "centers" << m_centers;
    fs << "remapNeeded" << ( m_remapNeeded ? 1 : 0 );
    fs << "}";
    fs.release();
    return true;
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "ISODATA::save — OpenCV error: %1" ).arg( e.what() ) );
    return false;
  }
}

bool RsClassifierIsodata::load( const QString &path )
{
  try
  {
    cv::FileStorage fs( path.toStdString(), cv::FileStorage::READ );
    if ( !fs.isOpened() )
      return false;
    cv::FileNode node = fs["isodata_model"];
    if ( node.empty() || !node.isMap() )
    {
      fs.release();
      return false;
    }
    node["targetClusters"] >> m_params.targetClusters;
    node["maxIterations"] >> m_params.maxIterations;
    node["minSamplesPerCluster"] >> m_params.minSamplesPerCluster;
    node["sigmaFactor"] >> m_params.sigmaFactor;
    node["mergeDistance"] >> m_params.mergeDistance;
    node["centers"] >> m_centers;
    int remap = 0;
    node["remapNeeded"] >> remap;
    m_remapNeeded = ( remap != 0 );
    fs.release();
    return !m_centers.empty();
  }
  catch ( const cv::Exception &e )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Classification,
                     QStringLiteral( "ISODATA::load — OpenCV error: %1" ).arg( e.what() ) );
    return false;
  }
}
