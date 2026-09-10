// ir_mad_kernels.cpp — see ir_mad_kernels.h
#include "ir_mad_kernels.h"

#include <algorithm>
#include <cmath>

namespace ChangeDetectionMAD
{

double chiSquareUpperCdf( double k, double x )
{
    if ( x <= 0.0 ) return 1.0;
    if ( k <= 0.0 ) return 0.0;
    const double a = k * 0.5;
    const double z = x * 0.5;
    if ( k == 2.0 ) {
        return std::exp( -z );
    }
    if ( k == 1.0 ) {
        return std::erfc( std::sqrt( z ) );
    }
    if ( z < a + 1.0 ) {
        double sum = 1.0 / a;
        double term = 1.0 / a;
        for ( int n = 1; n < 100; ++n ) {
            term *= z / ( a + n );
            sum += term;
            if ( term < sum * 1e-12 ) break;
        }
        double lower = sum * std::exp( -z + a * std::log( z ) - std::lgamma( a ) );
        return std::clamp( 1.0 - lower, 0.0, 1.0 );
    } else {
        double b = z + 1.0 - a;
        double c = 1.0 / 1e-30;
        double d = 1.0 / b;
        double h = d;
        for ( int n = 1; n < 100; ++n ) {
            double an = -static_cast<double>( n ) * ( static_cast<double>( n ) - a );
            b += 2.0;
            d = an * d + b;
            if ( std::abs( d ) < 1e-30 ) d = 1e-30;
            c = b + an / c;
            if ( std::abs( c ) < 1e-30 ) c = 1e-30;
            d = 1.0 / d;
            double delta = d * c;
            h *= delta;
            if ( std::abs( delta - 1.0 ) < 1e-12 ) break;
        }
        double q = std::exp( -z + a * std::log( z ) - std::lgamma( a ) ) * h;
        return std::clamp( q, 0.0, 1.0 );
    }
}

cv::Mat madSqrtInv( const cv::Mat &M )
{
    cv::Mat w, u, vt;
    cv::SVD::compute( M, w, u, vt );
    cv::Mat wInvSqrt = cv::Mat::zeros( M.rows, M.cols, CV_64F );
    for ( int i = 0; i < M.rows; ++i ) {
        const double val = w.at<double>( i );
        wInvSqrt.at<double>( i, i ) = ( val > 1e-12 ) ? ( 1.0 / std::sqrt( val ) ) : 0.0;
    }
    return u * wInvSqrt * vt;
}

} // namespace ChangeDetectionMAD
