// src/processing/algorithms/sar/sar_insar.cpp — InSAR base-chain kernels.
// Contracts and honesty notes: sar_insar.h.
#include "sar_insar.h"

#include "processing/algorithms/primitives/dense_linalg.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace sicnu::sar
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool finiteComplex( const std::complex<double> &v )
{
    return std::isfinite( v.real() ) && std::isfinite( v.imag() );
}
} // anonymous namespace

double interferogramPhase( std::complex<double> s1, std::complex<double> s2 )
{
    if ( !finiteComplex( s1 ) || !finiteComplex( s2 ) )
        return kNaN;
    const std::complex<double> ifg = s1 * std::conj( s2 );
    // Phase of the zero sample is undefined (masked like invalid geometry).
    if ( ifg.real() == 0.0 && ifg.imag() == 0.0 )
        return kNaN;
    return std::arg( ifg );
}

std::complex<double> interferogramSample( std::complex<double> s1, std::complex<double> s2 )
{
    if ( !finiteComplex( s1 ) || !finiteComplex( s2 ) )
        return { kNaN, kNaN };
    return s1 * std::conj( s2 );
}

double windowCoherence( const std::complex<float> *s1, const std::complex<float> *s2,
                        int w, int h, int cx, int cy, int radius )
{
    if ( !s1 || !s2 || radius < 0 )
        return kNaN;
    const int x0 = std::max( 0, cx - radius );
    const int x1 = std::min( w - 1, cx + radius );
    const int y0 = std::max( 0, cy - radius );
    const int y1 = std::min( h - 1, cy + radius );
    if ( x1 < x0 || y1 < y0 )
        return kNaN;

    std::complex<double> sumCross = {};
    double sum1 = 0.0;
    double sum2 = 0.0;
    long pairs = 0;
    for ( int y = y0; y <= y1; ++y )
    {
        for ( int x = x0; x <= x1; ++x )
        {
            const std::complex<double> a = s1[static_cast<size_t>( y ) * w + x];
            const std::complex<double> b = s2[static_cast<size_t>( y ) * w + x];
            if ( !finiteComplex( a ) || !finiteComplex( b ) )
                continue;
            sumCross += a * std::conj( b );
            sum1 += std::norm( a );
            sum2 += std::norm( b );
            ++pairs;
        }
    }
    if ( pairs == 0 )
        return kNaN;
    const double denom = std::sqrt( sum1 * sum2 );
    if ( !( denom > 0.0 ) )
        return kNaN;
    return std::abs( sumCross ) / denom;
}

double goldsteinPhase( const std::complex<float> *ifg, int w, int h,
                       int cx, int cy, int radius, double alpha )
{
    if ( !ifg || radius < 0 )
        return kNaN;
    const int x0 = std::max( 0, cx - radius );
    const int x1 = std::min( w - 1, cx + radius );
    const int y0 = std::max( 0, cy - radius );
    const int y1 = std::min( h - 1, cy + radius );

    std::complex<double> acc = {};
    double weightSum = 0.0;
    bool any = false;
    for ( int y = y0; y <= y1; ++y )
    {
        for ( int x = x0; x <= x1; ++x )
        {
            const std::complex<double> z = ifg[static_cast<size_t>( y ) * w + x];
            if ( !finiteComplex( z ) )
                continue;
            const double mag = std::abs( z );
            const double weight = ( mag > 0.0 ) ? std::pow( mag, alpha ) : 0.0;
            // A zero-magnitude sample carries no phase information — it
            // contributes to the weighted average with weight 0.
            if ( weight > 0.0 )
            {
                acc += weight * ( z / mag );
                weightSum += weight;
            }
            any = true;
        }
    }
    if ( !any || !( weightSum > 0.0 ) )
        return kNaN;
    return std::arg( acc );
}

double evalPhaseRamp( const PhaseRampModel &m, int x, int y )
{
    if ( !m.quadratic )
        return m.coef[0] + m.coef[1] * x + m.coef[2] * y;
    return m.coef[0] + m.coef[1] * x + m.coef[2] * y + m.coef[3] * ( static_cast<double>( x ) * x )
           + m.coef[4] * ( static_cast<double>( x ) * y ) + m.coef[5] * ( static_cast<double>( y ) * y );
}

namespace
{
// Generic design row: [1, x, y] or [1, x, y, x², xy, y²].
void rampDesignRow( int x, int y, bool quadratic, double *row )
{
    row[0] = 1.0;
    row[1] = x;
    row[2] = y;
    if ( quadratic )
    {
        row[3] = static_cast<double>( x ) * x;
        row[4] = static_cast<double>( x ) * y;
        row[5] = static_cast<double>( y ) * y;
    }
}
} // anonymous namespace

void PhaseRampFitter::addSample( int x, int y, double phase )
{
    if ( !std::isfinite( phase ) )
        return;
    ++m_samples;
    m_reservoir.add( x, y, phase );
}

bool PhaseRampFitter::fit( bool quadratic, int robustIterations, PhaseRampModel *out )
{
    if ( !out )
        return false;
    const int nCoef = quadratic ? 6 : 3;
    const size_t n = m_reservoir.phase.size();
    if ( static_cast<long>( n ) < nCoef )
        return false;

    // Weights over the reservoir (IQR clip applied after each refit).
    std::vector<double> weight( n, 1.0 );
    double coef[6] = {};

    for ( int round = 0; round <= std::max( 0, robustIterations ); ++round )
    {
        std::vector<double> normal( static_cast<size_t>( nCoef ) * nCoef, 0.0 );
        std::vector<double> rhs( nCoef, 0.0 );
        for ( size_t k = 0; k < n; ++k )
        {
            const double wt = weight[k];
            if ( !( wt > 0.0 ) )
                continue;
            double row[6];
            rampDesignRow( m_reservoir.xy[k].first, m_reservoir.xy[k].second, quadratic, row );
            for ( int r = 0; r < nCoef; ++r )
            {
                rhs[r] += wt * wt * row[r] * m_reservoir.phase[k];
                for ( int c = 0; c < nCoef; ++c )
                    normal[static_cast<size_t>( r ) * nCoef + c] += wt * wt * row[r] * row[c];
            }
        }

        if ( !sicnu::primitives::invertDenseMatrixInPlace( normal, nCoef ) )
            return false;
        for ( int r = 0; r < nCoef; ++r )
        {
            double v = 0.0;
            for ( int c = 0; c < nCoef; ++c )
                v += normal[static_cast<size_t>( r ) * nCoef + c] * rhs[c];
            coef[r] = v;
        }

        if ( round == robustIterations || n < 4 )
            break;

        // Re-clip weights from reservoir residuals.
        std::vector<double> residuals( n );
        for ( size_t k = 0; k < n; ++k )
        {
            double row[6];
            rampDesignRow( m_reservoir.xy[k].first, m_reservoir.xy[k].second, quadratic, row );
            double model = 0.0;
            for ( int r = 0; r < nCoef; ++r )
                model += coef[r] * row[r];
            residuals[k] = m_reservoir.phase[k] - model;
        }
        std::vector<double> sorted = residuals;
        std::sort( sorted.begin(), sorted.end() );
        const double q1 = sorted[sorted.size() / 4];
        const double q3 = sorted[( 3 * sorted.size() ) / 4];
        const double iqr = q3 - q1;
        const double lo = q1 - 1.5 * iqr;
        const double hi = q3 + 1.5 * iqr;
        for ( size_t k = 0; k < n; ++k )
            weight[k] = ( residuals[k] >= lo && residuals[k] <= hi ) ? 1.0 : 0.0;
    }

    *out = PhaseRampModel{};
    out->quadratic = quadratic;
    for ( int r = 0; r < nCoef; ++r )
        out->coef[r] = coef[r];
    return true;
}

bool fitPhaseRamp( const double *phase, int w, int h, bool quadratic, PhaseRampModel *out )
{
    if ( !phase || w <= 0 || h <= 0 )
        return false;
    PhaseRampFitter fitter;
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
            fitter.addSample( x, y, phase[static_cast<size_t>( y ) * w + x] );
    return fitter.fit( quadratic, 3, out );
}

bool qualityGuidedUnwrap( const double *wrapped, const double *quality,
                          int w, int h, UnwrapResult *out )
{
    if ( !wrapped || !out || w <= 0 || h <= 0 )
        return false;

    const size_t n = static_cast<size_t>( w ) * h;
    out->unwrapped.assign( n, kNaN );
    out->unwrappedCount = 0;

    struct Candidate
    {
        double quality;
        int row;
        int col;
        long seq;
        // Max-heap on (quality, then earliest row/col/seq) — the comparator
        // reverses for std::priority_queue.
        bool operator<( const Candidate &o ) const
        {
            if ( quality != o.quality )
                return quality < o.quality;
            if ( row != o.row )
                return row > o.row;
            if ( col != o.col )
                return col > o.col;
            return seq > o.seq;
        }
    };

    std::vector<uint8_t> visited( n, 0 );
    std::priority_queue<Candidate> queue;
    long seqCounter = 0;
    out->seeds = 0;

    auto push = [&]( int row, int col, long seqVal ) {
        if ( row < 0 || row >= h || col < 0 || col >= w )
            return; // seed/edge neighbors can leave the raster
        const size_t i = static_cast<size_t>( row ) * w + col;
        if ( visited[i] || !std::isfinite( wrapped[i] ) )
            return;
        const double q = quality ? ( std::isfinite( quality[i] ) ? quality[i]
                                                                 : -std::numeric_limits<double>::infinity() )
                                 : 0.0;
        queue.push( { q, row, col, seqVal } );
    };

    const int neighbors[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    const double twoPi = 2.0 * M_PI;

    // Re-seed per connected valid component: NaN holes / decorrelated
    // barriers split the field, and each component gets its own absolute
    // phase anchor (relative offsets ACROSS components are undefined by
    // construction and stay so — reported via `seeds`).
    while ( true )
    {
        // Seed: highest-quality valid unvisited pixel (uniform quality when
        // null → earliest raster position, deterministic).
        int seedIdx = -1;
        double bestQ = -std::numeric_limits<double>::infinity();
        for ( size_t i = 0; i < n; ++i )
        {
            if ( visited[i] || !std::isfinite( wrapped[i] ) )
                continue;
            const double q = quality ? ( std::isfinite( quality[i] ) ? quality[i]
                                                                     : -std::numeric_limits<double>::infinity() )
                                     : 0.0;
            if ( q > bestQ )
            {
                bestQ = q;
                seedIdx = static_cast<int>( i );
            }
        }
        if ( seedIdx < 0 )
            break; // everything valid has been unwrapped

        out->unwrapped[static_cast<size_t>( seedIdx )] = wrapped[static_cast<size_t>( seedIdx )];
        visited[static_cast<size_t>( seedIdx )] = 1;
        out->unwrappedCount++;
        out->seeds++;
        const int seedRow = seedIdx / w;
        const int seedCol = seedIdx % w;
        for ( const auto &nb : neighbors )
            push( seedRow + nb[0], seedCol + nb[1], seqCounter++ );

        while ( !queue.empty() )
        {
            const Candidate c = queue.top();
            queue.pop();
            const size_t i = static_cast<size_t>( c.row ) * w + c.col;
            if ( visited[i] )
                continue;

            // Unwrap relative to the already-visited neighbor with the best
            // quality (deterministic scan order).
            int refRow = -1;
            int refCol = -1;
            double refQ = -std::numeric_limits<double>::infinity();
            for ( const auto &nb : neighbors )
            {
                const int nr = c.row + nb[0];
                const int nc = c.col + nb[1];
                if ( nr < 0 || nr >= h || nc < 0 || nc >= w )
                    continue;
                const size_t ri = static_cast<size_t>( nr ) * w + nc;
                if ( !visited[ri] )
                    continue;
                const double q = quality ? ( std::isfinite( quality[ri] ) ? quality[ri] : 0.0 )
                                         : 0.0;
                if ( q > refQ )
                {
                    refQ = q;
                    refRow = nr;
                    refCol = nc;
                }
            }
            if ( refRow < 0 )
                continue; // stale entry

            const double phi = wrapped[i];
            const double phiRef = out->unwrapped[static_cast<size_t>( refRow ) * w + refCol];
            const double unwrapped = phi + twoPi * std::round( ( phiRef - phi ) / twoPi );
            out->unwrapped[i] = unwrapped;
            visited[i] = 1;
            out->unwrappedCount++;

            for ( const auto &nb : neighbors )
                push( c.row + nb[0], c.col + nb[1], seqCounter++ );
        }
    }

    return true;
}

double losDisplacementM( double unwrappedPhaseRad, double wavelengthM )
{
    if ( !std::isfinite( unwrappedPhaseRad ) || !( wavelengthM > 0.0 ) )
        return kNaN;
    return -wavelengthM * unwrappedPhaseRad / ( 4.0 * M_PI );
}

double phaseDiscontinuityRatio( const double *phase, int w, int h )
{
    if ( !phase || w <= 0 || h <= 0 )
        return kNaN;
    long pairs = 0;
    long jumps = 0;
    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const double v = phase[static_cast<size_t>( y ) * w + x];
            if ( x + 1 < w )
            {
                const double r = phase[static_cast<size_t>( y ) * w + x + 1];
                if ( std::isfinite( v ) && std::isfinite( r ) )
                {
                    ++pairs;
                    if ( std::abs( v - r ) > M_PI )
                        ++jumps;
                }
            }
            if ( y + 1 < h )
            {
                const double d = phase[static_cast<size_t>( y + 1 ) * w + x];
                if ( std::isfinite( v ) && std::isfinite( d ) )
                {
                    ++pairs;
                    if ( std::abs( v - d ) > M_PI )
                        ++jumps;
                }
            }
        }
    }
    if ( pairs == 0 )
        return kNaN;
    return static_cast<double>( jumps ) / static_cast<double>( pairs );
}

bool coregistrationShift( const std::complex<float> *master, const std::complex<float> *slave,
                          int w, int h, int searchRadius, int patchSize, int patchStride,
                          double minPeakRatio, CoregisterShift *out )
{
    if ( !master || !slave || !out || w <= 0 || h <= 0 )
        return false;
    if ( searchRadius <= 0 || patchSize <= 0 || patchStride <= 0 )
        return false;

    std::vector<double> offsetsX;
    std::vector<double> offsetsY;
    std::vector<double> peakRatios;

    for ( int py = 0; py + patchSize <= h; py += patchStride )
    {
        for ( int px = 0; px + patchSize <= w; px += patchStride )
        {
            // Master patch energy.
            double mEnergy = 0.0;
            for ( int y = py; y < py + patchSize; ++y )
                for ( int x = px; x < px + patchSize; ++x )
                {
                    const std::complex<double> m = master[static_cast<size_t>( y ) * w + x];
                    if ( finiteComplex( m ) )
                        mEnergy += std::norm( m );
                }
            if ( !( mEnergy > 0.0 ) )
                continue;

            double bestScore = -1.0;
            double secondScore = -1.0;
            double bestDx = 0.0;
            double bestDy = 0.0;

            for ( int dy = -searchRadius; dy <= searchRadius; ++dy )
            {
                for ( int dx = -searchRadius; dx <= searchRadius; ++dx )
                {
                    const int sxBase = px + dx;
                    const int syBase = py + dy;
                    if ( sxBase < 0 || syBase < 0 || sxBase + patchSize > w
                         || syBase + patchSize > h )
                        continue;

                    double cross = 0.0;
                    double sEnergy = 0.0;
                    bool valid = true;
                    for ( int y = 0; y < patchSize && valid; ++y )
                    {
                        for ( int x = 0; x < patchSize; ++x )
                        {
                            const std::complex<double> m =
                                master[static_cast<size_t>( py + y ) * w + px + x];
                            const std::complex<double> s =
                                slave[static_cast<size_t>( syBase + y ) * w + sxBase + x];
                            if ( !finiteComplex( m ) || !finiteComplex( s ) )
                            {
                                valid = false;
                                break;
                            }
                            cross += std::abs( m ) * std::abs( s );
                            sEnergy += std::norm( s );
                        }
                    }
                    if ( !valid || !( sEnergy > 0.0 ) )
                        continue;
                    const double denom = std::sqrt( mEnergy * sEnergy );
                    if ( !( denom > 0.0 ) )
                        continue;
                    const double score = cross / denom;
                    if ( score > bestScore )
                    {
                        secondScore = bestScore;
                        bestScore = score;
                        bestDx = dx;
                        bestDy = dy;
                    }
                    else if ( score > secondScore )
                    {
                        secondScore = score;
                    }
                }
            }

            if ( bestScore <= 0.0 )
                continue;
            // Flat correlation surface (uniform/decorrelated patches score
            // the same for every shift): the "peak" is meaningless — drop
            // the patch instead of inventing an offset.
            if ( secondScore >= 0.0 && bestScore - secondScore <= 1e-9 )
                continue;
            if ( secondScore > 0.0 && bestScore < minPeakRatio * secondScore )
                continue; // ambiguous peak — drop the patch

            // Parabolic sub-pixel refinement needs the correlation samples
            // around the peak; re-score the two axis neighbors.
            double refinedX = static_cast<double>( bestDx );
            double refinedY = static_cast<double>( bestDy );
            auto scoreAt = [&]( int dx, int dy ) -> double {
                const int sxBase = px + dx;
                const int syBase = py + dy;
                if ( sxBase < 0 || syBase < 0 || sxBase + patchSize > w || syBase + patchSize > h )
                    return -1.0;
                double cross = 0.0;
                double sEnergy = 0.0;
                for ( int y = 0; y < patchSize; ++y )
                    for ( int x = 0; x < patchSize; ++x )
                    {
                        const std::complex<double> m =
                            master[static_cast<size_t>( py + y ) * w + px + x];
                        const std::complex<double> s =
                            slave[static_cast<size_t>( syBase + y ) * w + sxBase + x];
                        if ( !finiteComplex( m ) || !finiteComplex( s ) )
                            return -1.0;
                        cross += std::abs( m ) * std::abs( s );
                        sEnergy += std::norm( s );
                    }
                const double denom = std::sqrt( mEnergy * sEnergy );
                return denom > 0.0 ? cross / denom : -1.0;
            };
            const double sL = scoreAt( bestDx - 1, bestDy );
            const double sR = scoreAt( bestDx + 1, bestDy );
            const double denomX = sL - 2.0 * bestScore + sR;
            if ( sL > 0.0 && sR > 0.0 && std::abs( denomX ) > 1e-15 )
            {
                const double shift = 0.5 * ( sL - sR ) / denomX;
                if ( std::abs( shift ) <= 1.0 )
                    refinedX += shift;
            }
            const double sU = scoreAt( bestDx, bestDy - 1 );
            const double sD = scoreAt( bestDx, bestDy + 1 );
            const double denomY = sU - 2.0 * bestScore + sD;
            if ( sU > 0.0 && sD > 0.0 && std::abs( denomY ) > 1e-15 )
            {
                const double shift = 0.5 * ( sU - sD ) / denomY;
                if ( std::abs( shift ) <= 1.0 )
                    refinedY += shift;
            }

            offsetsX.push_back( refinedX );
            offsetsY.push_back( refinedY );
            peakRatios.push_back( secondScore > 0.0 ? bestScore / secondScore : bestScore );
        }
    }

    if ( offsetsX.size() < 3 )
        return false;

    auto medianOf = []( std::vector<double> v ) {
        std::sort( v.begin(), v.end() );
        const size_t n = v.size();
        return n % 2 == 1 ? v[n / 2] : 0.5 * ( v[n / 2 - 1] + v[n / 2] );
    };

    out->dx = medianOf( offsetsX );
    out->dy = medianOf( offsetsY );
    out->confidentPatches = static_cast<long>( offsetsX.size() );
    out->meanPeakRatio = medianOf( peakRatios );
    return true;
}

void shiftComplexBilinear( const std::complex<float> *src, int w, int h,
                           double dx, double dy, std::complex<float> *dst )
{
    if ( !src || !dst || w <= 0 || h <= 0 )
        return;
    const std::complex<float> nanSample{ kNaN, kNaN };
    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
        {
            const double sx = static_cast<double>( x ) - dx;
            const double sy = static_cast<double>( y ) - dy;
            const int x0 = static_cast<int>( std::floor( sx ) );
            const int y0 = static_cast<int>( std::floor( sy ) );
            const double tx = sx - x0;
            const double ty = sy - y0;
            // Only actually-tapped neighbors must be in range: a zero
            // fractional weight never reads its corner (dy=0 keeps the last
            // row valid; dx=0 keeps the last column).
            bool ok = true;
            std::complex<double> acc = {};
            auto tap = [&]( int xx, int yy, double weight ) {
                if ( !ok || weight == 0.0 )
                    return;
                if ( xx < 0 || yy < 0 || xx >= w || yy >= h )
                {
                    ok = false;
                    return;
                }
                const std::complex<double> s = src[static_cast<size_t>( yy ) * w + xx];
                if ( !finiteComplex( s ) )
                {
                    ok = false;
                    return;
                }
                acc += weight * s;
            };
            tap( x0, y0, ( 1.0 - tx ) * ( 1.0 - ty ) );
            tap( x0 + 1, y0, tx * ( 1.0 - ty ) );
            tap( x0, y0 + 1, ( 1.0 - tx ) * ty );
            tap( x0 + 1, y0 + 1, tx * ty );
            dst[static_cast<size_t>( y ) * w + x] =
                ok ? static_cast<std::complex<float>>( acc ) : nanSample;
        }
    }
}

} // namespace sicnu::sar
