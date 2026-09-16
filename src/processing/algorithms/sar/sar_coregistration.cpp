// sar_coregistration.cpp — see sar_coregistration.h
#include "sar_coregistration.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sicnu::sar
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

template <typename T>
bool finiteComplex( const std::complex<T> &z )
{
    return std::isfinite( z.real() ) && std::isfinite( z.imag() );
}

/// Median with the repo convention (even count → mean of the two middles).
double medianOf( std::vector<double> v )
{
    if ( v.empty() )
        return kNaN;
    std::sort( v.begin(), v.end() );
    const size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * ( v[n / 2 - 1] + v[n / 2] );
}

struct PatchScore
{
    double dx = 0.0;
    double dy = 0.0;
    double peakRatio = 0.0;
    bool confident = false;
};

/// Magnitude-NCC of one patch with integer ±searchRadius scan and axis
/// parabolic refinement — the same score, flat-surface guard and
/// confidence rule as coregistrationShift (sar_insar.cpp), evaluated for a
/// SINGLE patch so the field estimator can keep unconfident nodes visible.
PatchScore scorePatch( const std::complex<float> *master, const std::complex<float> *slave,
                       int w, int h, int px, int py, int patchSize, int searchRadius,
                       double minPeakRatio )
{
    PatchScore result;
    double mEnergy = 0.0;
    for ( int y = py; y < py + patchSize; ++y )
        for ( int x = px; x < px + patchSize; ++x )
        {
            const std::complex<double> m = master[static_cast<size_t>( y ) * w + x];
            if ( finiteComplex( m ) )
                mEnergy += std::norm( m );
        }
    if ( !( mEnergy > 0.0 ) )
        return result;

    double bestScore = -1.0;
    double secondScore = -1.0;
    int bestDx = 0;
    int bestDy = 0;

    const auto nccAt = [&]( int dx, int dy ) -> double {
        const int sxBase = px + dx;
        const int syBase = py + dy;
        if ( sxBase < 0 || syBase < 0 || sxBase + patchSize > w || syBase + patchSize > h )
            return -1.0;
        double cross = 0.0;
        double sEnergy = 0.0;
        for ( int y = 0; y < patchSize; ++y )
        {
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
        }
        const double denom = std::sqrt( mEnergy * sEnergy );
        return denom > 0.0 ? cross / denom : -1.0;
    };

    for ( int dy = -searchRadius; dy <= searchRadius; ++dy )
        for ( int dx = -searchRadius; dx <= searchRadius; ++dx )
        {
            const double score = nccAt( dx, dy );
            if ( score < 0.0 )
                continue;
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

    if ( bestScore <= 0.0 )
        return result;
    if ( secondScore >= 0.0 && bestScore - secondScore <= 1e-9 )
        return result; // flat correlation surface — no meaningful peak
    if ( secondScore > 0.0 && bestScore < minPeakRatio * secondScore )
        return result; // ambiguous peak

    double refinedX = static_cast<double>( bestDx );
    double refinedY = static_cast<double>( bestDy );
    const double sL = nccAt( bestDx - 1, bestDy );
    const double sR = nccAt( bestDx + 1, bestDy );
    const double denomX = sL - 2.0 * bestScore + sR;
    if ( sL > 0.0 && sR > 0.0 && std::abs( denomX ) > 1e-15 )
    {
        const double shift = 0.5 * ( sL - sR ) / denomX;
        if ( std::abs( shift ) <= 1.0 )
            refinedX += shift;
    }
    const double sU = nccAt( bestDx, bestDy - 1 );
    const double sD = nccAt( bestDx, bestDy + 1 );
    const double denomY = sU - 2.0 * bestScore + sD;
    if ( sU > 0.0 && sD > 0.0 && std::abs( denomY ) > 1e-15 )
    {
        const double shift = 0.5 * ( sU - sD ) / denomY;
        if ( std::abs( shift ) <= 1.0 )
            refinedY += shift;
    }

    result.dx = refinedX;
    result.dy = refinedY;
    result.peakRatio = secondScore > 0.0 ? bestScore / secondScore : bestScore;
    result.confident = true;
    return result;
}
} // namespace

bool estimateOffsetField( const std::complex<float> *master, const std::complex<float> *slave,
                          int w, int h, int searchRadius, int patchSize, int patchStride,
                          double minPeakRatio, int medianRadius, OffsetField *out,
                          const std::function<void()> &cancelProbe )
{
    if ( !master || !slave || !out || w <= 0 || h <= 0 )
        return false;
    if ( searchRadius <= 0 || patchSize <= 0 || patchStride <= 0 )
        return false;

    // Coarse model from the global authority (also gates degenerate pairs).
    if ( !coregistrationShift( master, slave, w, h, searchRadius, patchSize, patchStride,
                               minPeakRatio, &out->global, cancelProbe ) )
        return false;

    out->patchSize = patchSize;
    out->patchStride = patchStride;
    out->patchOriginX0 = 0;
    out->patchOriginY0 = 0;
    out->latticeCols = ( w - patchSize ) / patchStride + 1;
    out->latticeRows = ( h - patchSize ) / patchStride + 1;
    if ( out->latticeCols <= 0 || out->latticeRows <= 0 )
        return false;
    out->patches.assign( static_cast<size_t>( out->latticeCols ) * out->latticeRows,
                         OffsetPatch{} );

    long patchesDone = 0;
    for ( int py = 0, row = 0; py + patchSize <= h; py += patchStride, ++row )
    {
        for ( int px = 0, col = 0; px + patchSize <= w; px += patchStride, ++col, ++patchesDone )
        {
            if ( cancelProbe && ( patchesDone % 16 ) == 0 )
                cancelProbe();
            const PatchScore score = scorePatch( master, slave, w, h, px, py, patchSize,
                                                 searchRadius, minPeakRatio );
            OffsetPatch &node =
                out->patches[static_cast<size_t>( row ) * out->latticeCols + col];
            node.dx = score.dx;
            node.dy = score.dy;
            node.peakRatio = score.peakRatio;
            node.confident = score.confident;
        }
    }

    // Median filter over confident neighbors (radius 0 disables).
    out->confidentPatches = 0;
    out->medianAdjusted = 0;
    if ( medianRadius > 0 )
    {
        std::vector<OffsetPatch> filtered = out->patches;
        for ( int row = 0; row < out->latticeRows; ++row )
        {
            for ( int col = 0; col < out->latticeCols; ++col )
            {
                const OffsetPatch &node =
                    out->patches[static_cast<size_t>( row ) * out->latticeCols + col];
                if ( !node.confident )
                    continue;
                std::vector<double> xs;
                std::vector<double> ys;
                for ( int ny = std::max( 0, row - medianRadius );
                      ny <= std::min( out->latticeRows - 1, row + medianRadius ); ++ny )
                    for ( int nx = std::max( 0, col - medianRadius );
                          nx <= std::min( out->latticeCols - 1, col + medianRadius ); ++nx )
                    {
                        const OffsetPatch &neighbor =
                            out->patches[static_cast<size_t>( ny ) * out->latticeCols + nx];
                        if ( neighbor.confident )
                        {
                            xs.push_back( neighbor.dx );
                            ys.push_back( neighbor.dy );
                        }
                    }
                OffsetPatch &target =
                    filtered[static_cast<size_t>( row ) * out->latticeCols + col];
                const double mx = medianOf( xs );
                const double my = medianOf( ys );
                if ( std::isfinite( mx ) && std::isfinite( my ) )
                {
                    if ( mx != node.dx || my != node.dy )
                        ++out->medianAdjusted;
                    target.dx = mx;
                    target.dy = my;
                }
            }
        }
        out->patches = std::move( filtered );
    }
    for ( const OffsetPatch &node : out->patches )
        if ( node.confident )
            ++out->confidentPatches;
    return true;
}

void offsetAtPixel( const OffsetField &field, double x, double y, double *dx, double *dy )
{
    // Degenerate lattice (a single node column/row): the global model IS
    // the field — no interpolation is possible, only honest fallback.
    if ( field.latticeCols < 2 || field.latticeRows < 2 )
    {
        *dx = field.global.dx;
        *dy = field.global.dy;
        return;
    }
    // Node centers sit at patch origin + patchSize/2 on the lattice.
    const double fx = ( x - field.patchOriginX0 - field.patchSize / 2.0 )
                          / static_cast<double>( field.patchStride );
    const double fy = ( y - field.patchOriginY0 - field.patchSize / 2.0 )
                          / static_cast<double>( field.patchStride );
    const int ix = std::clamp( static_cast<int>( std::floor( fx ) ), 0,
                               field.latticeCols - 2 );
    const int iy = std::clamp( static_cast<int>( std::floor( fy ) ), 0,
                               field.latticeRows - 2 );
    const double tx = std::clamp( fx - ix, 0.0, 1.0 );
    const double ty = std::clamp( fy - iy, 0.0, 1.0 );

    const auto nodeAt = [&]( int col, int row ) -> const OffsetPatch & {
        return field.patches[static_cast<size_t>( row ) * field.latticeCols + col];
    };
    const auto offsetX = [&]( int col, int row ) {
        const OffsetPatch &node = nodeAt( col, row );
        return node.confident ? node.dx : field.global.dx;
    };
    const auto offsetY = [&]( int col, int row ) {
        const OffsetPatch &node = nodeAt( col, row );
        return node.confident ? node.dy : field.global.dy;
    };

    *dx = ( 1.0 - tx ) * ( 1.0 - ty ) * offsetX( ix, iy )
          + tx * ( 1.0 - ty ) * offsetX( ix + 1, iy )
          + ( 1.0 - tx ) * ty * offsetX( ix, iy + 1 )
          + tx * ty * offsetX( ix + 1, iy + 1 );
    *dy = ( 1.0 - tx ) * ( 1.0 - ty ) * offsetY( ix, iy )
          + tx * ( 1.0 - ty ) * offsetY( ix + 1, iy )
          + ( 1.0 - tx ) * ty * offsetY( ix, iy + 1 )
          + tx * ty * offsetY( ix + 1, iy + 1 );
}

void warpComplexByOffsetField( const std::complex<float> *slave, int w, int h,
                               const OffsetField &field, std::complex<float> *dst,
                               const std::function<void()> &cancelProbe )
{
    if ( !slave || !dst || w <= 0 || h <= 0 )
        return;
    const std::complex<float> nanSample{ kNaN, kNaN };
    for ( int y = 0; y < h; ++y )
    {
        if ( cancelProbe && ( y % 64 ) == 0 )
            cancelProbe();
        for ( int x = 0; x < w; ++x )
        {
            double dx = 0.0;
            double dy = 0.0;
            offsetAtPixel( field, static_cast<double>( x ), static_cast<double>( y ),
                           &dx, &dy );
            // The field carries CONTENT DISPLACEMENT (the NCC convention of
            // coregistrationShift / rs:sar_coregister: slave(x,y) ~
            // master(x−dx, y−dy)); aligning therefore samples the slave at
            // +(dx, dy) — the negated resampling application.
            const double sx = static_cast<double>( x ) + dx;
            const double sy = static_cast<double>( y ) + dy;
            const int x0 = static_cast<int>( std::floor( sx ) );
            const int y0 = static_cast<int>( std::floor( sy ) );
            const double tx = sx - x0;
            const double ty = sy - y0;
            // Only actually-tapped neighbors must be in range (the
            // shiftComplexBilinear edge convention).
            bool ok = true;
            std::complex<double> acc = {};
            const auto tap = [&]( int xx, int yy, double weight ) {
                if ( !ok || weight == 0.0 )
                    return;
                if ( xx < 0 || yy < 0 || xx >= w || yy >= h )
                {
                    ok = false;
                    return;
                }
                const std::complex<double> s = slave[static_cast<size_t>( yy ) * w + xx];
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
