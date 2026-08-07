// spectral_classification.cpp — SAM + Continuum Removal kernels.
#include "spectral_classification.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralClassification
{

double spectralAngle( const float *t, const float *r, size_t bands, float nodata )
{
    if ( !t || !r || bands == 0 )
        return std::numeric_limits<double>::quiet_NaN();

    double dot = 0.0, normT = 0.0, normR = 0.0;
    for ( size_t b = 0; b < bands; ++b )
    {
        if ( t[b] == nodata || r[b] == nodata ||
             std::isnan( t[b] ) || std::isnan( r[b] ) )
            return std::numeric_limits<double>::quiet_NaN();
        double tv = static_cast<double>( t[b] );
        double rv = static_cast<double>( r[b] );
        dot += tv * rv;
        normT += tv * tv;
        normR += rv * rv;
    }

    if ( normT <= 0.0 || normR <= 0.0 )
        return std::numeric_limits<double>::quiet_NaN();

    double denom = std::sqrt( normT ) * std::sqrt( normR );
    // Clamp to [-1, 1] to guard against FP overshoot of arccos domain.
    double cosTheta = std::clamp( dot / denom, -1.0, 1.0 );
    return std::acos( cosTheta );
}

double spectralDivergence( const float *t, const float *r, size_t bands, float nodata )
{
    if ( !t || !r || bands == 0 )
        return std::numeric_limits<double>::quiet_NaN();

    double sumT = 0.0, sumR = 0.0;
    for ( size_t b = 0; b < bands; ++b )
    {
        if ( t[b] == nodata || r[b] == nodata ||
             std::isnan( t[b] ) || std::isnan( r[b] ) )
            return std::numeric_limits<double>::quiet_NaN();
        if ( t[b] < 0.0f || r[b] < 0.0f )
            return std::numeric_limits<double>::quiet_NaN(); // not reflectance-like
        sumT += t[b];
        sumR += r[b];
    }
    if ( sumT <= 0.0 || sumR <= 0.0 )
        return std::numeric_limits<double>::quiet_NaN();

    // Symmetric KL divergence over the normalized (probability) spectra.
    double sid = 0.0;
    for ( size_t b = 0; b < bands; ++b )
    {
        const double p = static_cast<double>( t[b] ) / sumT;
        const double q = static_cast<double>( r[b] ) / sumR;
        if ( p > 0.0 && q > 0.0 )
            sid += p * std::log( p / q ) + q * std::log( q / p );
    }
    return sid;
}

bool sidClassify( const float *pixels, size_t count, int bands,
                  const float *refs, int refCount,
                  int *labels, float *divergences, float nodata )
{
    if ( !pixels || !refs || !labels || count == 0 || bands <= 0 || refCount <= 0 )
        return false;

    for ( size_t p = 0; p < count; ++p )
    {
        const float *t = pixels + p * static_cast<size_t>( bands );

        bool pixelValid = true;
        for ( int b = 0; b < bands; ++b )
        {
            if ( t[b] == nodata || std::isnan( t[b] ) )
            {
                pixelValid = false;
                break;
            }
        }

        int best = -1;
        double bestDiv = std::numeric_limits<double>::infinity();
        if ( pixelValid )
        {
            for ( int c = 0; c < refCount; ++c )
            {
                const float *r = refs + static_cast<size_t>( c ) * bands;
                const double div = spectralDivergence( t, r, static_cast<size_t>( bands ), nodata );
                // NaN divergence here is reference-side (the pixel is valid):
                // the reference is degenerate. Skip it rather than discarding
                // the pixel.
                if ( std::isnan( div ) )
                    continue;
                if ( div < bestDiv )
                {
                    bestDiv = div;
                    best = c;
                }
            }
        }
        labels[p] = best;
        if ( divergences )
            divergences[p] = std::isfinite( bestDiv ) ? static_cast<float>( bestDiv )
                                                      : std::numeric_limits<float>::quiet_NaN();
    }
    return true;
}

bool samClassify( const float *pixels, size_t count, int bands,
                  const float *refs, int refCount,
                  int *labels, float *angles, float nodata )
{
    if ( !pixels || !refs || !labels || count == 0 || bands <= 0 || refCount <= 0 )
        return false;

    for ( size_t p = 0; p < count; ++p )
    {
        const float *t = pixels + p * static_cast<size_t>( bands );

        // Pixel-side nodata (any nodata/NaN band) makes the whole pixel
        // unclassifiable regardless of the references. Determine this once,
        // up front, so a single bad reference does not invalidate the pixel.
        bool pixelValid = true;
        for ( int b = 0; b < bands; ++b )
        {
            if ( t[b] == nodata || std::isnan( t[b] ) )
            {
                pixelValid = false;
                break;
            }
        }

        int best = -1;
        double bestAngle = std::numeric_limits<double>::infinity();
        if ( pixelValid )
        {
            for ( int c = 0; c < refCount; ++c )
            {
                const float *r = refs + static_cast<size_t>( c ) * bands;
                double ang = spectralAngle( t, r, static_cast<size_t>( bands ), nodata );
                // A NaN angle here is reference-side (the pixel is known
                // valid): the reference is degenerate (zero-norm or contains
                // nodata). Skip it rather than discarding the pixel.
                if ( std::isnan( ang ) )
                    continue;
                if ( ang < bestAngle )
                {
                    bestAngle = ang;
                    best = c;
                }
            }
        }
        labels[p] = best;
        if ( angles )
            angles[p] = std::isfinite( bestAngle ) ? static_cast<float>( bestAngle )
                                                   : std::numeric_limits<float>::quiet_NaN();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Continuum removal via upper convex hull.
//
// Treats each sample as a point (x=i, y=spectrum[i]) and computes the upper
// convex hull with a monotone-deque Andrew/Graham scan. The continuum at each
// band is then obtained by linearly interpolating the hull tie-line that spans
// that band.
// ---------------------------------------------------------------------------

// 2-D cross product of OA and OB vectors (O first):
//   > 0  -> O->A->B is a left turn (counterclockwise)
//   == 0 -> collinear
//   < 0  -> right turn (clockwise)
static double crossTurn( double ox, double oy, double ax, double ay, double bx, double by )
{
    return ( ax - ox ) * ( by - oy ) - ( ay - oy ) * ( bx - ox );
}

bool continuumRemoval( const float *spectrum, float *out, int bands, float nodata )
{
    if ( !spectrum || !out || bands <= 0 )
        return false;

    // Reject if any nodata sample.
    for ( int i = 0; i < bands; ++i )
        if ( spectrum[i] == nodata || std::isnan( spectrum[i] ) )
        {
            for ( int j = 0; j < bands; ++j )
                out[j] = nodata;
            return false;
        }

    if ( bands == 1 )
    {
        out[0] = 1.0f;
        return true;
    }

    // Upper convex hull (indices into spectrum). Andrew's monotone chain:
    // walk left→right; for the UPPER hull we keep vertices where the turn is a
    // right turn (clockwise), popping vertices that make a left turn.
    std::vector<int> hull;
    hull.reserve( static_cast<size_t>( bands ) );
    for ( int i = 0; i < bands; ++i )
    {
        double ix = static_cast<double>( i );
        double iy = static_cast<double>( spectrum[i] );
        while ( hull.size() >= 2 )
        {
            int a = hull[hull.size() - 2];
            int b = hull[hull.size() - 1];
            // For upper hull we remove b while a->b->i is NOT a right turn,
            // i.e. while crossTurn(a,b,i) >= 0 (collinear or left turn).
            if ( crossTurn( static_cast<double>( a ), spectrum[a],
                            static_cast<double>( b ), spectrum[b],
                            ix, iy ) >= 0.0 )
            {
                hull.pop_back();
            }
            else
            {
                break;
            }
        }
        hull.push_back( i );
    }

    // Linearly interpolate the continuum across each segment and divide.
    // Segment k spans bands [hull[k], hull[k+1]].
    size_t seg = 0;
    for ( int i = 0; i < bands; ++i )
    {
        // Advance segment until i falls within [hull[seg], hull[seg+1]].
        while ( seg + 1 < hull.size() && i > hull[seg + 1] )
            ++seg;

        double continuum;
        if ( seg + 1 >= hull.size() || i == hull[seg] )
        {
            continuum = static_cast<double>( spectrum[hull[seg]] );
        }
        else
        {
            int x0 = hull[seg], x1 = hull[seg + 1];
            double y0 = static_cast<double>( spectrum[x0] );
            double y1 = static_cast<double>( spectrum[x1] );
            double t = ( static_cast<double>( i ) - x0 ) / ( x1 - x0 );
            continuum = y0 + t * ( y1 - y0 );
        }

        double v = static_cast<double>( spectrum[i] );
        // Guard against a zero continuum (e.g. zero-spectrum endpoints).
        out[i] = ( std::abs( continuum ) > 1e-12 )
                     ? static_cast<float>( v / continuum )
                     : 1.0f;
    }

    return true;
}

} // namespace SpectralClassification
