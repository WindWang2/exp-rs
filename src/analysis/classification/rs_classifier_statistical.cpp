// rs_classifier_statistical.cpp — see rs_classifier_statistical.h.

#include "rs_classifier_statistical.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <opencv2/core.hpp>

namespace
{

/// Groups row indices by class label, with stable (first-seen) class-id
/// ordering so means vector order is deterministic.
std::map<int, std::vector<int>> classRowIndices( const cv::Mat &y )
{
    std::map<int, std::vector<int>> groups;
    for ( int r = 0; r < y.rows; ++r )
        groups[y.at<int>( r, 0 )].push_back( r );
    return groups;
}

/// Converts the samples of @a rows into a double vector per column mean.
cv::Mat classMean( const cv::Mat &X, const std::vector<int> &rows )
{
    const int bands = X.cols;
    cv::Mat mean = cv::Mat::zeros( 1, bands, CV_64F );
    for ( const int r : rows )
        for ( int b = 0; b < bands; ++b )
            mean.at<double>( 0, b ) += static_cast<double>( X.at<float>( r, b ) );
    for ( int b = 0; b < bands; ++b )
        mean.at<double>( 0, b ) /= static_cast<double>( rows.size() );
    return mean;
}

} // namespace

bool RsClassifierMinDistance::fit( const cv::Mat &X, const cv::Mat &y )
{
    if ( X.empty() || y.empty() || X.rows != y.rows )
        return false;
    m_classIds.clear();
    m_means.clear();
    const auto groups = classRowIndices( y );
    for ( const auto &[classId, rows] : groups )
    {
        if ( rows.empty() )
            continue;
        m_classIds.push_back( classId );
        m_means.push_back( classMean( X, rows ) );
    }
    return !m_classIds.empty();
}

cv::Mat RsClassifierMinDistance::predict( const cv::Mat &X ) const
{
    cv::Mat out;
    if ( X.empty() || m_means.empty() )
        return out;
    out.create( X.rows, 1, CV_32S );
    for ( int r = 0; r < X.rows; ++r )
    {
        double best = std::numeric_limits<double>::infinity();
        int bestClass = m_classIds.front();
        for ( size_t c = 0; c < m_means.size(); ++c )
        {
            double dist = 0.0;
            for ( int b = 0; b < X.cols; ++b )
            {
                const double diff = static_cast<double>( X.at<float>( r, b ) ) -
                                    m_means[c].at<double>( 0, b );
                dist += diff * diff;
            }
            if ( dist < best )
            {
                best = dist;
                bestClass = m_classIds[c];
            }
        }
        out.at<int>( r, 0 ) = bestClass;
    }
    return out;
}

bool RsClassifierMahalanobis::fit( const cv::Mat &X, const cv::Mat &y )
{
    if ( X.empty() || y.empty() || X.rows != y.rows || X.rows < 2 )
        return false;
    const int bands = X.cols;
    const auto groups = classRowIndices( y );

    // Pooled within-class covariance (sample convention, ÷(N − #classes)).
    cv::Mat pooled = cv::Mat::zeros( bands, bands, CV_64F );
    for ( const auto &[classId, rows] : groups )
    {
        if ( rows.size() < 2 )
            continue;
        const cv::Mat mean = classMean( X, rows );
        cv::Mat centered( static_cast<int>( rows.size() ), bands, CV_64F );
        for ( int r = 0; r < static_cast<int>( rows.size() ); ++r )
            for ( int b = 0; b < bands; ++b )
                centered.at<double>( r, b ) =
                    static_cast<double>( X.at<float>( rows[r], b ) ) - mean.at<double>( 0, b );
        pooled += centered.t() * centered;
    }
    const int df = static_cast<int>( X.rows ) - static_cast<int>( groups.size() );
    pooled /= std::max( 1, df );

    // Ridge for singular small-sample stacks: 1e-6 × mean diagonal.
    double diagMean = 0.0;
    for ( int b = 0; b < bands; ++b )
        diagMean += pooled.at<double>( b, b );
    diagMean /= bands;
    const double ridge = std::max( 1e-12, 1e-6 * diagMean );
    for ( int b = 0; b < bands; ++b )
        pooled.at<double>( b, b ) += ridge;

    if ( cv::invert( pooled, m_pooledInverse ) == 0 )
    {
        qWarning() << "Mahalanobis backend: pooled covariance is singular";
        return false;
    }

    m_classIds.clear();
    m_means.clear();
    for ( const auto &[classId, rows] : groups )
    {
        if ( rows.empty() )
            continue;
        m_classIds.push_back( classId );
        m_means.push_back( classMean( X, rows ) );
    }
    return !m_classIds.empty();
}

cv::Mat RsClassifierMahalanobis::predict( const cv::Mat &X ) const
{
    cv::Mat out;
    if ( X.empty() || m_means.empty() || m_pooledInverse.empty() )
        return out;
    out.create( X.rows, 1, CV_32S );
    cv::Mat diff( 1, X.cols, CV_64F );
    for ( int r = 0; r < X.rows; ++r )
    {
        double best = std::numeric_limits<double>::infinity();
        int bestClass = m_classIds.front();
        for ( size_t c = 0; c < m_means.size(); ++c )
        {
            for ( int b = 0; b < X.cols; ++b )
                diff.at<double>( 0, b ) =
                    static_cast<double>( X.at<float>( r, b ) ) - m_means[c].at<double>( 0, b );
            cv::Mat Mah = diff * m_pooledInverse * diff.t();
            const double dist = Mah.at<double>( 0, 0 );
            if ( dist < best )
            {
                best = dist;
                bestClass = m_classIds[c];
            }
        }
        out.at<int>( r, 0 ) = bestClass;
    }
    return out;
}
