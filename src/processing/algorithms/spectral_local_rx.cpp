// src/processing/algorithms/spectral_local_rx.cpp — dual-window RX detector
#include "spectral_local_rx.h"

#include "processing/algorithms/primitives/dense_linalg.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace SpectralLocalRx
{

namespace
{
    /// Same valid-pixel predicate as SpectralAnomaly's accumulators: a band
    /// value is invalid when non-finite or — when that band declares NoData —
    /// equal to the declared value. A pixel is valid when every band is valid.
    bool pixelValid( const float *pixel, int bands,
                     const float *noDataBands, const uint8_t *hasNoDataBands )
    {
        for ( int b = 0; b < bands; ++b )
        {
            const float v = pixel[b];
            if ( !std::isfinite( v ) )
                return false;
            if ( noDataBands && ( !hasNoDataBands || hasNoDataBands[b] ) && v == noDataBands[b] )
                return false;
        }
        return true;
    }

    /// Scaled diagonal loading: alpha * (tr(Sigma)/B) * I (DECISIONS D2).
    /// Returns the per-diagonal-entry loading value for a covariance trace.
    double loadingValue( double trace, int bands, double alpha )
    {
        if ( bands <= 0 )
            return 0.0;
        return alpha * ( trace / bands );
    }
} // namespace

const char *covarianceModeText( CovarianceMode mode )
{
    switch ( mode )
    {
        case CovarianceMode::Full:
            return "full";
        case CovarianceMode::Diagonal:
            return "diagonal";
    }
    return "unknown";
}

bool covarianceModeFromText( const std::string &text, CovarianceMode *out )
{
    if ( text == "full" )
    {
        *out = CovarianceMode::Full;
        return true;
    }
    if ( text == "diagonal" )
    {
        *out = CovarianceMode::Diagonal;
        return true;
    }
    return false;
}

bool scorePixel( const float *spectrum,
                 const float *background, size_t backgroundCount, int bands,
                 const Config &config,
                 const float *noDataBands,
                 const uint8_t *hasNoDataBands,
                 float *score,
                 PixelScoreStatus *status,
                 QString *errorMessage )
{
    const auto finish = [&]( bool ok, PixelScoreStatus outcome )
    {
        if ( status )
            *status = outcome;
        return ok;
    };
    if ( !spectrum || !background || !score || bands <= 0 || backgroundCount == 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid local RX scorePixel arguments" );
        return finish( false, PixelScoreStatus::InvalidArguments );
    }
    if ( !pixelValid( spectrum, bands, noDataBands, hasNoDataBands ) )
        return finish( false, PixelScoreStatus::InvalidCenter );

    size_t validCount = 0;
    for ( size_t p = 0; p < backgroundCount; ++p )
    {
        if ( pixelValid( background + p * static_cast<size_t>( bands ), bands,
                         noDataBands, hasNoDataBands ) )
            ++validCount;
    }
    const int minSamples = config.minBackgroundSamples > 0
                               ? config.minBackgroundSamples
                               : ( config.covarianceMode == CovarianceMode::Full
                                       ? 2 * bands + 2
                                       : bands + 1 );
    if ( static_cast<int>( validCount ) < minSamples )
        return finish( false, PixelScoreStatus::InsufficientBackground );

    // Mean over valid background pixels.
    std::vector<double> mu( static_cast<size_t>( bands ), 0.0 );
    for ( size_t p = 0; p < backgroundCount; ++p )
    {
        const float *pixel = background + p * static_cast<size_t>( bands );
        if ( !pixelValid( pixel, bands, noDataBands, hasNoDataBands ) )
            continue;
        for ( int b = 0; b < bands; ++b )
            mu[static_cast<size_t>( b )] += pixel[b];
    }
    for ( int b = 0; b < bands; ++b )
        mu[static_cast<size_t>( b )] /= static_cast<double>( validCount );

    // Centered moments (sample covariance, N-1 denominator — the estimator
    // the RX detector is defined on, matching SpectralAnomaly::finalizeCovariance).
    const double denom = static_cast<double>( validCount - 1 );
    if ( denom <= 0.0 )
        return finish( false, PixelScoreStatus::InsufficientBackground );
    if ( config.covarianceMode == CovarianceMode::Full )
    {
        std::vector<double> cov( static_cast<size_t>( bands ) * bands, 0.0 );
        for ( size_t p = 0; p < backgroundCount; ++p )
        {
            const float *pixel = background + p * static_cast<size_t>( bands );
            if ( !pixelValid( pixel, bands, noDataBands, hasNoDataBands ) )
                continue;
            for ( int i = 0; i < bands; ++i )
            {
                const double di = pixel[i] - mu[static_cast<size_t>( i )];
                const size_t rowOffset = static_cast<size_t>( i ) * bands;
                for ( int j = i; j < bands; ++j )
                {
                    const double dj = pixel[j] - mu[static_cast<size_t>( j )];
                    cov[rowOffset + j] += di * dj;
                }
            }
        }
        // Normalize to the sample covariance FIRST, then accumulate the
        // trace — the loading must be alpha * tr(Sigma)/B of the ESTIMATED
        // covariance, not of the raw scatter (an (N-1)-fold overshoot would
        // make scores depend on window sample counts).
        for ( int i = 0; i < bands; ++i )
        {
            for ( int j = i; j < bands; ++j )
            {
                const double v = cov[static_cast<size_t>( i ) * bands + j] / denom;
                cov[static_cast<size_t>( i ) * bands + j] = v;
                cov[static_cast<size_t>( j ) * bands + i] = v;
            }
        }
        double trace = 0.0;
        for ( int i = 0; i < bands; ++i )
            trace += cov[static_cast<size_t>( i ) * bands + i];
        const double load = loadingValue( trace, bands, std::max( 0.0, config.loading ) );
        for ( int i = 0; i < bands; ++i )
            cov[static_cast<size_t>( i ) * bands + i] += load;
        std::vector<double> invCov;
        if ( !sicnu::primitives::invertDenseMatrix( cov, bands, &invCov ) )
            return finish( false, PixelScoreStatus::SingularBackground );
        double rx = 0.0;
        for ( int i = 0; i < bands; ++i )
        {
            double row = 0.0;
            const size_t rowOffset = static_cast<size_t>( i ) * bands;
            for ( int j = 0; j < bands; ++j )
                row += invCov[rowOffset + j]
                       * ( static_cast<double>( spectrum[j] ) - mu[static_cast<size_t>( j )] );
            rx += ( static_cast<double>( spectrum[i] ) - mu[static_cast<size_t>( i )] ) * row;
        }
        if ( !std::isfinite( rx ) )
            return finish( false, PixelScoreStatus::NonFiniteScore );
        *score = static_cast<float>( std::max( 0.0, rx ) );
        return finish( true, PixelScoreStatus::Scored );
    }

    // Diagonal mode: per-band variances + the same scaled loading on each.
    std::vector<double> var( static_cast<size_t>( bands ), 0.0 );
    for ( size_t p = 0; p < backgroundCount; ++p )
    {
        const float *pixel = background + p * static_cast<size_t>( bands );
        if ( !pixelValid( pixel, bands, noDataBands, hasNoDataBands ) )
            continue;
        for ( int b = 0; b < bands; ++b )
        {
            const double d = pixel[b] - mu[static_cast<size_t>( b )];
            var[static_cast<size_t>( b )] += d * d;
        }
    }
    double trace = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
        var[static_cast<size_t>( b )] /= denom;
        trace += var[static_cast<size_t>( b )];
    }
    const double load = loadingValue( trace, bands, std::max( 0.0, config.loading ) );
    double rx = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
        const double d = static_cast<double>( spectrum[b] ) - mu[static_cast<size_t>( b )];
        rx += d * d / ( var[static_cast<size_t>( b )] + load );
    }
    if ( !std::isfinite( rx ) )
        return finish( false, PixelScoreStatus::NonFiniteScore );
    *score = static_cast<float>( std::max( 0.0, rx ) );
    return finish( true, PixelScoreStatus::Scored );
}

bool dualWindowRx( const float *pixels, int width, int height, int bands,
                   const Config &config,
                   const float *noDataBands,
                   const uint8_t *hasNoDataBands,
                   Result *result,
                   QString *errorMessage )
{
    if ( !pixels || !result || width <= 0 || height <= 0 || bands <= 0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invalid dual-window RX arguments" );
        return false;
    }
    if ( config.outerWindow < 1 || config.outerWindow % 2 == 0
         || config.innerWindow < 1 || config.innerWindow % 2 == 0
         || config.innerWindow >= config.outerWindow )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Invalid window geometry: outer and inner windows must be odd "
                "with inner < outer (got outer=%1, inner=%2)" )
                                .arg( config.outerWindow )
                                .arg( config.innerWindow );
        return false;
    }
    if ( config.loading < 0.0 || !std::isfinite( config.loading ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Loading must be a finite value >= 0" );
        return false;
    }

    const int outerRadius = config.outerWindow / 2;
    const int innerRadius = config.innerWindow / 2;
    const int minSamples = config.minBackgroundSamples > 0
                               ? config.minBackgroundSamples
                               : ( config.covarianceMode == CovarianceMode::Full
                                       ? 2 * bands + 2
                                       : bands + 1 );
    if ( config.covarianceMode == CovarianceMode::Full
         && static_cast<size_t>( bands ) * bands > ( size_t{ 1 } << 26 ) )
    {
        // Defensive memory bound: a full local covariance is B^2 doubles of
        // working state; beyond 8192 bands the request is refused instead of
        // thrashing (the Diagonal mode exists precisely for this range).
        if ( errorMessage )
            *errorMessage = QStringLiteral(
                "Full covariance mode requires bands <= 8192; use diagonal mode above" );
        return false;
    }

    const size_t count = static_cast<size_t>( width ) * height;
    result->scores.assign( count, std::numeric_limits<float>::quiet_NaN() );
    result->scored.assign( count, 0 );
    result->backgroundSamples.assign( count, 0 );
    result->centerValid.assign( count, 0 );
    result->covarianceMode = config.covarianceMode;

    // Working buffer for the per-pixel background gather (window minus guard).
    std::vector<float> background;
    background.reserve( static_cast<size_t>( config.outerWindow )
                            * static_cast<size_t>( config.outerWindow )
                        * static_cast<size_t>( bands ) );

    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            const size_t idx = static_cast<size_t>( y ) * width + x;
            const float *center = pixels + idx * bands;
            if ( !pixelValid( center, bands, noDataBands, hasNoDataBands ) )
                continue; // stays NaN / unscored / 0 samples / centerValid=0
            result->centerValid[idx] = 1;

            // Clamp the outer window to the raster and cut the guard hole.
            // Clamping (not replication) keeps border statistics unbiased.
            const int x0 = std::max( 0, x - outerRadius );
            const int x1 = std::min( width - 1, x + outerRadius );
            const int y0 = std::max( 0, y - outerRadius );
            const int y1 = std::min( height - 1, y + outerRadius );
            const int gx0 = std::max( 0, x - innerRadius );
            const int gx1 = std::min( width - 1, x + innerRadius );
            const int gy0 = std::max( 0, y - innerRadius );
            const int gy1 = std::min( height - 1, y + innerRadius );

            background.clear();
            for ( int wy = y0; wy <= y1; ++wy )
            {
                // Skip the guard band rows entirely when outside the hole.
                for ( int wx = x0; wx <= x1; ++wx )
                {
                    if ( wx >= gx0 && wx <= gx1 && wy >= gy0 && wy <= gy1 )
                        continue;
                    const float *pixel = pixels + ( static_cast<size_t>( wy ) * width + wx ) * bands;
                    if ( !pixelValid( pixel, bands, noDataBands, hasNoDataBands ) )
                        continue;
                    background.insert( background.end(), pixel, pixel + bands );
                }
            }
            result->backgroundSamples[idx] = static_cast<int32_t>( background.size() / bands );
            if ( static_cast<int>( background.size() / bands ) < minSamples )
                continue; // honest unscored: NaN + scored=0, count recorded

            float score = 0.0f;
            PixelScoreStatus pixelStatus = PixelScoreStatus::InvalidArguments;
            if ( !scorePixel( center, background.data(), background.size() / bands, bands,
                              config, noDataBands, hasNoDataBands, &score, &pixelStatus ) )
            {
                // Expected per-window degeneracy: leave unscored (the caller
                // sees scored=0 and the sample shortfall), not an error.
                if ( pixelStatus == PixelScoreStatus::InsufficientBackground
                     || pixelStatus == PixelScoreStatus::SingularBackground )
                    continue;
                if ( errorMessage )
                    *errorMessage = QStringLiteral(
                        "Local RX scoring failed unexpectedly (status %1)" )
                                        .arg( static_cast<int>( pixelStatus ) );
                return false;
            }
            result->scores[idx] = score;
            result->scored[idx] = 1;
        }
    }
    return true;
}

} // namespace SpectralLocalRx
