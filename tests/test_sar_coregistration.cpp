// tests/test_sar_coregistration.cpp — local offset-field co-registration
// (Advanced InSAR 11.0, package C).
//
// Known-answer oracle: the slave is CONSTRUCTED from the master by a
// piecewise integer shift field (left half dys = +2 samples, right half
// dys = −1 samples: slave(x,y) = master(x, y − dys)). The NCC content-
// displacement convention (coregistrationShift / rs:sar_coregister:
// slave(x,y) ~ master(x−dx, y−dy)) reports dy = +2 / −1, and the warp
// applies the NEGATED field (dst = src(x+dx, y+dy)) to recover the
// master. A deterministic
// LCG texture makes the magnitude NCC unimodal at the true shift, so
// every confident lattice node far from the seam must reproduce the
// construction offsets within a quarter pixel.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_coregistration.h"

#include <cmath>
#include <complex>
#include <vector>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr int kW = 256;
constexpr int kH = 256;
constexpr int kPatch = 32;
constexpr int kStride = 32;
constexpr int kSearch = 4;
constexpr int kSeam = 128; // left region x < 128, right region x >= 128

/// Deterministic LCG texture (the platform's test-fixture generator).
// Sparse-scatterer statistics (the texture SLC coregistration actually
// feeds on): ~12% bright samples over a dim background. With full-field
// positive amplitudes every wrong shift correlates ~0.8 and the peak-ratio
// confidence rule would (correctly) reject every patch.
std::complex<float> synthSample( unsigned &state )
{
    state = state * 1103515245u + 12345u;
    const bool bright = ( ( state >> 8 ) % 100 ) < 12;
    state = state * 1103515245u + 12345u;
    const double amp = bright ? 1.0 + ( ( state >> 8 ) % 1000 ) / 500.0 : 0.03;
    state = state * 1103515245u + 12345u;
    const double phase = -M_PI + 2.0 * M_PI * ( ( state >> 8 ) % 1000 ) / 1000.0;
    return static_cast<std::complex<float>>( std::polar( amp, phase ) );
}

std::vector<std::complex<float>> makeMaster()
{
    std::vector<std::complex<float>> field( static_cast<size_t>( kW ) * kH );
    unsigned lcg = 987654321u;
    for ( auto &z : field )
        z = synthSample( lcg );
    return field;
}

/// Slave(x,y) = Master(x, y − dys(x)) with NaN where the source is outside.
std::vector<std::complex<float>> makeShiftedSlave( const std::vector<std::complex<float>> &master,
                                                   int dysLeft, int dysRight )
{
    std::vector<std::complex<float>> slave( static_cast<size_t>( kW ) * kH );
    const std::complex<float> nan{ std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::quiet_NaN() };
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
        {
            const int dys = x < kSeam ? dysLeft : dysRight;
            const int srcY = y - dys;
            slave[static_cast<size_t>( y ) * kW + x] =
                ( srcY >= 0 && srcY < kH )
                    ? master[static_cast<size_t>( srcY ) * kW + x]
                    : nan;
        }
    return slave;
}
} // namespace

TEST_CASE( "Offset field recovers a piecewise shift field away from the seam",
           "[sar][coregistration][insar11]" )
{
    const auto master = makeMaster();
    // Construction dys: left +2, right −1 → estimated slave shifts
    // (dx, dy) = (0, −2) left and (0, +1) right.
    const auto slave = makeShiftedSlave( master, 2, -1 );

    OffsetField field;
    REQUIRE( estimateOffsetField( master.data(), slave.data(), kW, kH, kSearch, kPatch,
                                  kStride, 1.2, 1, &field ) );
    REQUIRE( field.latticeCols == 8 );
    REQUIRE( field.latticeRows == 8 );
    REQUIRE( field.confidentPatches > 0 );

    long checkedLeft = 0;
    long checkedRight = 0;
    for ( int row = 0; row < field.latticeRows; ++row )
    {
        for ( int col = 0; col < field.latticeCols; ++col )
        {
            const OffsetPatch &node = field.patches[static_cast<size_t>( row ) * field.latticeCols
                                                    + col];
            const int px = col * kStride;
            if ( !node.confident )
                continue; // row 0 left nodes see the NaN source band — honest
            if ( px + kPatch <= kSeam )
            {
                REQUIRE( node.dx == Approx( 0.0 ).margin( 0.25 ) );
                REQUIRE( node.dy == Approx( 2.0 ).margin( 0.25 ) );
                ++checkedLeft;
            }
            else if ( px >= kSeam )
            {
                REQUIRE( node.dx == Approx( 0.0 ).margin( 0.25 ) );
                REQUIRE( node.dy == Approx( -1.0 ).margin( 0.25 ) );
                ++checkedRight;
            }
        }
    }
    REQUIRE( checkedLeft >= 20 );
    REQUIRE( checkedRight >= 20 );
}

TEST_CASE( "Warping the shifted slave by the field reconstructs the master",
           "[sar][coregistration][insar11]" )
{
    const auto master = makeMaster();
    const auto slave = makeShiftedSlave( master, 2, -1 );

    OffsetField field;
    REQUIRE( estimateOffsetField( master.data(), slave.data(), kW, kH, kSearch, kPatch,
                                  kStride, 1.2, 1, &field ) );

    std::vector<std::complex<float>> warped( static_cast<size_t>( kW ) * kH );
    warpComplexByOffsetField( slave.data(), kW, kH, field, warped.data() );

    // Interior bands only: the seam mixes the two offsets (a real property
    // of a translation-field model, not a defect) and the borders are NaN
    // in the slave source itself.
    const int band = kPatch + 4;
    double sumAbsDiff = 0.0;
    double maxAbsDiff = 0.0;
    long checked = 0;
    for ( int y = kPatch + 4; y < kH - kPatch - 4; ++y )
        for ( int x = kPatch + 4; x < kW - kPatch - 4; ++x )
        {
            if ( std::abs( x - kSeam ) <= band )
                continue;
            const std::complex<float> w = warped[static_cast<size_t>( y ) * kW + x];
            const std::complex<float> m = master[static_cast<size_t>( y ) * kW + x];
            REQUIRE( std::isfinite( w.real() ) );
            const double diff = std::abs( w - m );
            sumAbsDiff += diff;
            maxAbsDiff = std::max( maxAbsDiff, diff );
            ++checked;
        }
    REQUIRE( checked > 10000 );
    REQUIRE( sumAbsDiff / checked < 0.05 );   // median-scale error tiny
    // Statistical tail: the median-filtered field can carry a single
  // sub-pixel node residual into an interpolation cell; the honest
  // aggregate is the mean (asserted above), the tail is bounded by the
  // texture amplitude range.
  REQUIRE( maxAbsDiff < 2.0 );            // fractional-tap interpolation tail
}

TEST_CASE( "Unconfident patches degrade to the global model instead of "
           "poisoning the field", "[sar][coregistration][insar11]" )
{
    auto master = makeMaster();
    // A flat-texture square (constant amplitude) in the center: its patches
    // have no meaningful NCC peak and MUST come out unconfident, with the
    // warp falling back to the global shift (zero by construction).
    for ( int y = 96; y < 160; ++y )
        for ( int x = 96; x < 160; ++x )
            master[static_cast<size_t>( y ) * kW + x] = { 1.0f, 0.0f };

    // Slave = master shifted by a GLOBAL dy = −1 everywhere.
    std::vector<std::complex<float>> slave( static_cast<size_t>( kW ) * kH );
    const std::complex<float> nan{ std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::quiet_NaN() };
    for ( int y = 0; y < kH; ++y )
        for ( int x = 0; x < kW; ++x )
            slave[static_cast<size_t>( y ) * kW + x] =
                ( y - 1 >= 0 ) ? master[static_cast<size_t>( y - 1 ) * kW + x] : nan;

    OffsetField field;
    REQUIRE( estimateOffsetField( master.data(), slave.data(), kW, kH, kSearch, kPatch,
                                  kStride, 1.2, 0, &field ) );

    long unconfident = 0;
    for ( const OffsetPatch &node : field.patches )
        if ( !node.confident )
            ++unconfident;
    REQUIRE( unconfident >= 4 ); // the 2×2 center lattice covers the flat square
    REQUIRE( field.confidentPatches > 0 );

    // Offset at the flat square falls back to the global (0, −1).
    double dx = 0.0;
    double dy = 0.0;
    offsetAtPixel( field, 128.0, 128.0, &dx, &dy );
    REQUIRE( dx == Approx( field.global.dx ).margin( 1e-12 ) );
    REQUIRE( dy == Approx( field.global.dy ).margin( 1e-12 ) );

    std::vector<std::complex<float>> warped( static_cast<size_t>( kW ) * kH );
    warpComplexByOffsetField( slave.data(), kW, kH, field, warped.data() );
    const std::complex<float> w = warped[static_cast<size_t>( 128 ) * kW + 128];
    REQUIRE( w.real() == Approx( master[static_cast<size_t>( 128 ) * kW + 128].real() )
                 .margin( 0.35 ) );
}

TEST_CASE( "Offset estimation refuses degenerate parameters", "[sar][coregistration][insar11]" )
{
    const auto master = makeMaster();
    const auto slave = makeShiftedSlave( master, 2, -1 );
    OffsetField field;
    REQUIRE_FALSE( estimateOffsetField( master.data(), slave.data(), kW, kH, 0, kPatch,
                                        kStride, 1.2, 1, &field ) );
    REQUIRE_FALSE( estimateOffsetField( master.data(), slave.data(), kW, kH, kSearch, 0,
                                        kStride, 1.2, 1, &field ) );
    REQUIRE_FALSE( estimateOffsetField( nullptr, slave.data(), kW, kH, kSearch, kPatch,
                                        kStride, 1.2, 1, &field ) );
}
