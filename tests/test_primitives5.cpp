// test_primitives5.cpp — Scientific Algorithm Foundation 5.0 Milestone A:
// shared primitives (raster_histogram, morphology, connected_components,
// distance_transform, percentile, window).
//
// Every expectation below is hand-derivable from the contract comments in
// the corresponding primitive header (see derivation notes per case).

#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/primitives/dense_linalg.h"
#include "processing/algorithms/primitives/connected_components.h"
#include "processing/algorithms/primitives/distance_transform.h"
#include "processing/algorithms/primitives/morphology.h"
#include "processing/algorithms/primitives/percentile.h"
#include "processing/algorithms/primitives/raster_histogram.h"
#include "processing/algorithms/primitives/window.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace
{

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

/// Fills a width×height byte mask from rows of '0'/'1'/'x' (x = 255 NoData).
std::vector<uint8_t> maskFrom( int width, int height, const char *const *rows )
{
  std::vector<uint8_t> mask( static_cast<size_t>( width ) * height, 0 );
  for ( int y = 0; y < height; ++y )
    for ( int x = 0; x < width; ++x )
    {
      const char c = rows[y][x];
      mask[static_cast<size_t>( y ) * width + x] =
        c == '1' ? 1 : ( c == 'x' ? 255 : 0 );
    }
  return mask;
}

} // namespace

TEST_CASE( "RasterHistogram explicit-range binning matches the shared convention", "[primitives][histogram]" )
{
  // Shared convention: bin = (v - min)/range * (bins - 1), clamped.
  // range [0,10], bins 4 → width 10/3; v=0→0, v=3.4→1 (1.02 trunc), v=6.7→2,
  // v=10→3, v=-5→0 (clamp), v=99→3 (clamp).
  sicnu::rs::primitives::RasterHistogram h( 4 );
  h.setRange( 0.0, 10.0 );
  REQUIRE( h.hasRange() );
  const double values[] = { 0.0, 3.4, 6.7, 10.0, -5.0, 99.0 };
  for ( const double v : values )
    h.add( v );
  REQUIRE( h.finiteCount() == 6 );
  const auto &counts = h.counts();
  REQUIRE( counts[0] == 2 ); // 0.0 and -5.0
  REQUIRE( counts[1] == 1 );
  REQUIRE( counts[2] == 1 );
  REQUIRE( counts[3] == 2 ); // 10.0 and 99.0

  // Bin edges follow the range/(bins-1) reconstruction (#700).
  REQUIRE( h.binWidth() == Catch::Approx( 10.0 / 3.0 ).margin( 1e-12 ) );
  REQUIRE( h.binLower( 0 ) == 0.0 );
  REQUIRE( h.binLower( 3 ) == Catch::Approx( 10.0 ).margin( 1e-12 ) );

  // Non-finite values are never binned.
  h.add( kNaN );
  h.add( kInf );
  h.add( -kInf );
  REQUIRE( h.finiteCount() == 6 );
}

TEST_CASE( "RasterHistogram auto-range two-pass accumulates the finite extent", "[primitives][histogram]" )
{
  sicnu::rs::primitives::RasterHistogram h( 16 );
  h.beginAutoRange();
  const double values[] = { 2.0, kNaN, 8.0, -1.0, 4.0 };
  for ( const double v : values )
    h.addForRange( v );
  // The range pass counts into rangeCount(); nothing is binned yet.
  REQUIRE( h.rangeCount() == 4 );
  REQUIRE( h.finiteCount() == 0 );
  REQUIRE( h.beginHistogramFromRange() );
  REQUIRE( h.minVal() == -1.0 );
  REQUIRE( h.maxVal() == 8.0 );
  for ( const double v : values )
    h.add( v );
  // Binned population: each finite value exactly once (NaN never bins).
  REQUIRE( h.finiteCount() == 4 );
  REQUIRE( h.counts()[0] == 1 ); // -1.0
  REQUIRE( h.counts()[15] == 1 ); // 8.0
}

TEST_CASE( "RasterHistogram auto-range with no finite value refuses binning", "[primitives][histogram]" )
{
  sicnu::rs::primitives::RasterHistogram h( 16 );
  h.beginAutoRange();
  h.addForRange( kNaN );
  REQUIRE( h.finiteCount() == 0 );
  REQUIRE_FALSE( h.beginHistogramFromRange() );
}

TEST_CASE( "Otsu on a separated bimodal histogram picks the gap middle", "[primitives][histogram][otsu]" )
{
  // values {0,0,0,10,10,10}, range [0,10], bins 16. All empty-gap bins
  // 0..14 tie at the maximum between-class variance; the tied-maxima
  // average is bin 7 → threshold = (7 + 0.5) * 10/15 = 5.0 exactly.
  sicnu::rs::primitives::RasterHistogram h( 16 );
  h.setRange( 0.0, 10.0 );
  for ( int i = 0; i < 3; ++i )
    h.add( 0.0 );
  for ( int i = 0; i < 3; ++i )
    h.add( 10.0 );
  double t = 0.0;
  REQUIRE( h.otsu( &t ) );
  REQUIRE( t == Catch::Approx( 5.0 ).margin( 1e-12 ) );

  // The ChangeDetection facade delegates to the same kernel.
  float facade = 0.0f;
  std::vector<float> valuesF = { 0, 0, 0, 10, 10, 10 };
  REQUIRE( ChangeDetection::otsuThreshold( valuesF.data(), valuesF.size(), &facade, 16 ) );
  REQUIRE( facade == Catch::Approx( 5.0 ).margin( 1e-6 ) );

  // Degenerate: no counts, and constant range.
  double t2 = 0.0;
  REQUIRE_FALSE( sicnu::rs::primitives::otsuFromCounts( 0.0, 10.0, {}, 0, &t2 ) );
  std::vector<double> flat( 16, 0.0 );
  flat[3] = 5.0;
  REQUIRE( sicnu::rs::primitives::otsuFromCounts( 4.0, 4.0, flat, 5, &t2 ) );
  REQUIRE( t2 == 4.0 );
}

TEST_CASE( "Histogram quantile reproduces nearest-rank with in-bin interpolation", "[primitives][histogram]" )
{
  // values {2,4,6,8} over [2,8], bins 4 → one per bin, width 2.
  sicnu::rs::primitives::RasterHistogram h( 4 );
  h.setRange( 2.0, 8.0 );
  for ( const double v : { 2.0, 4.0, 6.0, 8.0 } )
    h.add( v );

  double q = 0.0;
  REQUIRE( h.quantile( 0.0, &q ) );
  REQUIRE( q == Catch::Approx( 2.0 ).margin( 1e-12 ) ); // p=0 → minimum
  REQUIRE( h.quantile( 50.0, &q ) );
  REQUIRE( q == Catch::Approx( 4.0 ).margin( 1e-12 ) ); // nearest-rank median
  REQUIRE( h.quantile( 100.0, &q ) );
  REQUIRE( q == Catch::Approx( 8.0 ).margin( 1e-12 ) );

  // p=75 → rank ceil(3)-1 = 2 → falls exactly at the lower edge of bin 2.
  REQUIRE( h.quantile( 75.0, &q ) );
  REQUIRE( q == Catch::Approx( 6.0 ).margin( 1e-12 ) );

  // Empty histogram refuses.
  sicnu::rs::primitives::RasterHistogram empty( 4 );
  empty.setRange( 0.0, 1.0 );
  REQUIRE_FALSE( empty.quantile( 50.0, &q ) );
}

TEST_CASE( "Histogram mean/stddev uses bin centers with the population convention", "[primitives][histogram]" )
{
  // One value per bin of [2,8]/4: centers 3,5,7,9 → mean 6,
  // population variance (9+1+1+9)/4 = 5.
  sicnu::rs::primitives::RasterHistogram h( 4 );
  h.setRange( 2.0, 8.0 );
  for ( const double v : { 2.0, 4.0, 6.0, 8.0 } )
    h.add( v );
  double mean = 0.0;
  double stddev = 0.0;
  REQUIRE( h.meanStddev( &mean, &stddev ) );
  REQUIRE( mean == Catch::Approx( 6.0 ).margin( 1e-12 ) );
  REQUIRE( stddev == Catch::Approx( std::sqrt( 5.0 ) ).margin( 1e-12 ) );
}

TEST_CASE( "Binary morphology: erode/dilate/open/close with NoData protection", "[primitives][morphology]" )
{
  using namespace sicnu::rs::primitives;
  // 5×5: single center foreground, NoData in the top-left corner.
  const char *rows0[] = { "x0000", "00000", "00100", "00000", "00000" };
  auto mask = maskFrom( 5, 5, rows0 );

  std::vector<uint8_t> dst( 25, 0 );
  dilate( mask.data(), dst.data(), 5, 5, Connectivity::Eight );
  REQUIRE( dst[0] == 255 ); // NoData never grows into
  // 3×3 block around (2,2):
  for ( int y = 1; y <= 3; ++y )
    for ( int x = 1; x <= 3; ++x )
      REQUIRE( dst[static_cast<size_t>( y ) * 5 + x] == 1 );
  REQUIRE( dst[2 * 5 + 4] == 0 ); // outside the block

  // Erode of that single pixel removes it (8-neighbourhood incomplete).
  erode( mask.data(), dst.data(), 5, 5, Connectivity::Eight );
  REQUIRE( dst[2 * 5 + 2] == 0 );
  REQUIRE( dst[0] == 255 );

  // Border acts as foreground: a full foreground 3×3 survives erode.
  const char *rowsFull[] = { "111", "111", "111" };
  auto full = maskFrom( 3, 3, rowsFull );
  std::vector<uint8_t> dst3( 9, 0 );
  erode( full.data(), dst3.data(), 3, 3, Connectivity::Eight );
  for ( const uint8_t v : dst3 )
    REQUIRE( v == 1 );

  // Diagonal pair of sources (0,0) and (1,1). Under 4-conn the grown set is
  // {(0,1),(1,0),(1,2),(2,1)}; under 8-conn additionally {(0,2),(2,0),(2,2)}.
  const char *rowsDiag[] = { "1000", "0100", "0000", "0000" };
  auto diag = maskFrom( 4, 4, rowsDiag );
  std::vector<uint8_t> dst4( 16, 0 );
  dilate( diag.data(), dst4.data(), 4, 4, Connectivity::Four );
  REQUIRE( dst4[0] == 1 ); // source survives
  REQUIRE( dst4[1 * 4 + 1] == 1 ); // source survives
  REQUIRE( dst4[1] == 1 ); // (0,1)
  REQUIRE( dst4[1 * 4 + 0] == 1 ); // (1,0)
  REQUIRE( dst4[1 * 4 + 2] == 1 ); // (1,2)
  REQUIRE( dst4[2 * 4 + 1] == 1 ); // (2,1)
  REQUIRE( dst4[2] == 0 ); // (0,2): diagonal of (1,1) — not grown under 4-conn
  dilate( diag.data(), dst4.data(), 4, 4, Connectivity::Eight );
  REQUIRE( dst4[2] == 1 ); // (0,2) grown into under 8-conn
}

TEST_CASE( "Binary morphology: open removes specks, close fills pinholes", "[primitives][morphology]" )
{
  using namespace sicnu::rs::primitives;
  // 7×7 rasters with a 3×3 block at rows/cols 2..4 — large enough that the
  // close-dilation cannot reach the raster border.
  const char *rowsBlock[] = { "0000000", "0000000", "0011100", "0011100",
                              "0011100", "0000000", "0000000" };
  auto block = maskFrom( 7, 7, rowsBlock );
  block[0 * 7 + 6] = 1; // isolated speck at (0,6)
  std::vector<uint8_t> scratch( 49, 0 );

  open( block.data(), scratch.data(), 7, 7, Connectivity::Eight );
  REQUIRE( block[0 * 7 + 6] == 0 ); // speck removed
  // The 3×3 block erodes to its center and re-dilates to the same 3×3.
  for ( int y = 2; y <= 4; ++y )
    for ( int x = 2; x <= 4; ++x )
      REQUIRE( block[y * 7 + x] == 1 );
  REQUIRE( block[1 * 7 + 1] == 0 );
  REQUIRE( block[4 * 7 + 5] == 0 );

  // Close: same block with a pinhole at (3,3) → pinhole filled, extent
  // restored to the 3×3 block (no border flood on the 7×7 canvas).
  const char *rowsHole[] = { "0000000", "0000000", "0011100", "0010100",
                             "0011100", "0000000", "0000000" };
  auto holed = maskFrom( 7, 7, rowsHole );
  close( holed.data(), scratch.data(), 7, 7, Connectivity::Eight );
  REQUIRE( holed[3 * 7 + 3] == 1 ); // pinhole filled
  for ( int y = 2; y <= 4; ++y )
    for ( int x = 2; x <= 4; ++x )
      REQUIRE( holed[y * 7 + x] == 1 );
  REQUIRE( holed[1 * 7 + 1] == 0 ); // dilation growth re-eroded
}

TEST_CASE( "Connected components: connectivity, raster-order labels, sieve", "[primitives][components]" )
{
  using namespace sicnu::rs::primitives;
  // Diagonal pair: separate under 4-conn, one component under 8-conn.
  const char *rows[] = { "100", "010", "000" };
  auto mask = maskFrom( 3, 3, rows );

  const Labeling four = labelComponents( mask.data(), 3, 3, Connectivity::Four );
  REQUIRE( four.componentCount == 2 );
  REQUIRE( four.labels[0] == 1 ); // (0,0) is the first component
  REQUIRE( four.labels[1 * 3 + 1] == 2 );

  const Labeling eight = labelComponents( mask.data(), 3, 3, Connectivity::Eight );
  REQUIRE( eight.componentCount == 1 );
  REQUIRE( eight.labels[0] == 1 );
  REQUIRE( eight.labels[1 * 3 + 1] == 1 );

  // NoData cells are never labeled.
  const char *rowsND[] = { "x00", "000", "00x" };
  auto ndMask = maskFrom( 3, 3, rowsND );
  const Labeling nd = labelComponents( ndMask.data(), 3, 3, Connectivity::Eight );
  REQUIRE( nd.componentCount == 0 );

  // Sieve: areas 1 and 4; minArea 2 clears the single pixel only.
  const char *rowsSieve[] = { "1100", "1100", "0001", "0000" };
  auto sieveMask = maskFrom( 4, 4, rowsSieve );
  const size_t removed = removeSmallObjects( sieveMask.data(), 4, 4, 2, Connectivity::Eight );
  REQUIRE( removed == 1 );
  REQUIRE( sieveMask[2 * 4 + 3] == 0 );
  REQUIRE( sieveMask[0] == 1 );
  REQUIRE( sieveMask[1 * 4 + 1] == 1 );
}

TEST_CASE( "Euclidean distance transform: closed-form distances", "[primitives][distance]" )
{
  using namespace sicnu::rs::primitives;
  // 5×5, single source at (2,2): rings 0, 1, sqrt2, 2, sqrt(2²+1²), 2sqrt2.
  const char *rows[] = { "00000", "00000", "00100", "00000", "00000" };
  auto mask = maskFrom( 5, 5, rows );
  std::vector<float> out( 25, -1.0f );
  REQUIRE( distanceToForeground( mask.data(), 5, 5, out.data() ) );

  REQUIRE( out[2 * 5 + 2] == 0.0f );
  REQUIRE( out[1 * 5 + 2] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
  REQUIRE( out[1 * 5 + 1] == Catch::Approx( std::sqrt( 2.0 ) ).margin( 1e-6 ) );
  REQUIRE( out[0 * 5 + 2] == Catch::Approx( 2.0f ).margin( 1e-6 ) );
  REQUIRE( out[0 * 5 + 1] == Catch::Approx( std::sqrt( 5.0 ) ).margin( 1e-6 ) );
  REQUIRE( out[0 * 5 + 0] == Catch::Approx( std::sqrt( 8.0 ) ).margin( 1e-6 ) );

  // Two sources: nearest wins.
  const char *rows2[] = { "10000", "00000", "00001", "00000", "00000" };
  auto mask2 = maskFrom( 5, 5, rows2 );
  std::vector<float> out2( 25, -1.0f );
  REQUIRE( distanceToForeground( mask2.data(), 5, 5, out2.data() ) );
  REQUIRE( out2[2 * 5 + 4] == 0.0f );
  REQUIRE( out2[0 * 5 + 0] == 0.0f );
  // (2,0): distance to (0,0) is 2, to (2,4) is 4 → 2.
  REQUIRE( out2[2 * 5 + 0] == Catch::Approx( 2.0f ).margin( 1e-6 ) );

  // No sources → all infinity.
  const char *rowsEmpty[] = { "000", "000" };
  auto none = maskFrom( 3, 2, rowsEmpty );
  std::vector<float> out3( 6, 0.0f );
  REQUIRE( distanceToForeground( none.data(), 3, 2, out3.data() ) );
  for ( const float v : out3 )
    REQUIRE( std::isinf( v ) );

  // 1×N strip.
  const char *rowStrip[] = { "10001" };
  auto strip = maskFrom( 5, 1, rowStrip );
  std::vector<float> out4( 5, 0.0f );
  REQUIRE( distanceToForeground( strip.data(), 5, 1, out4.data() ) );
  REQUIRE( out4[0] == 0.0f );
  REQUIRE( out4[1] == Catch::Approx( 1.0f ).margin( 1e-6 ) );
  REQUIRE( out4[2] == Catch::Approx( 2.0f ).margin( 1e-6 ) ); // ties → 2 either way
  REQUIRE( out4[4] == 0.0f );
}

TEST_CASE( "Exact quantiles: NearestRank matches the platform convention, Linear interpolates", "[primitives][percentile]" )
{
  using namespace sicnu::rs::primitives;
  const std::vector<float> values = { 2, 4, 6, 8 };
  std::vector<float> scratch;
  double q = 0.0;

  REQUIRE( quantileExact( values, 0.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE( q == 2.0 );
  REQUIRE( quantileExact( values, 50.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE( q == 4.0 ); // lower middle sample
  REQUIRE( quantileExact( values, 75.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE( q == 6.0 );
  REQUIRE( quantileExact( values, 100.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE( q == 8.0 );

  // Linear (numpy default): median = 4 + (6-4)*0.5 = 5; p=25 → 2+2*0.75 = 3.5.
  REQUIRE( quantileExact( values, 50.0, QuantileMethod::Linear, &q, &scratch ) );
  REQUIRE( q == Catch::Approx( 5.0 ).margin( 1e-12 ) );
  REQUIRE( quantileExact( values, 25.0, QuantileMethod::Linear, &q, &scratch ) );
  REQUIRE( q == Catch::Approx( 3.5 ).margin( 1e-12 ) );

  // NaN exclusion: {NaN, 1, 3} ranks over [1,3].
  const std::vector<float> dirty = { kNaN, 1, 3 };
  REQUIRE( quantileExact( dirty, 50.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE( q == 1.0 );

  // All-NaN / empty refuse.
  const std::vector<float> allNaN = { kNaN, kNaN };
  REQUIRE_FALSE( quantileExact( allNaN, 50.0, QuantileMethod::NearestRank, &q, &scratch ) );
  REQUIRE_FALSE( quantileExact( {}, 50.0, QuantileMethod::Linear, &q, &scratch ) );
}

TEST_CASE( "WindowSpec: odd sizes, radius, halo", "[primitives][window]" )
{
  using namespace sicnu::rs::primitives;
  const WindowSpec w = WindowSpec::square( 5 );
  REQUIRE( w.valid() );
  REQUIRE( w.radius() == 2 );
  REQUIRE( w.halo() == 2 );
  REQUIRE( w.edge == EdgePolicy::Replicate ); // streaming default

  WindowSpec bad;
  bad.size = 4;
  REQUIRE_FALSE( bad.valid() );
}

// ---------------------------------------------------------------------------
// dense_linalg (Foundation 7.0 consolidation): one Gauss-Jordan inverse
// replacing the former spectral_anomaly / spectral_unmixing copies.
// ---------------------------------------------------------------------------

TEST_CASE( "Dense inverse: known 2x2 inverse and round-trip", "[primitives][linalg]" )
{
    // [[4, 7], [2, 6]] has det = 10 and inverse [[0.6, -0.7], [-0.2, 0.4]].
    const std::vector<double> m = { 4.0, 7.0, 2.0, 6.0 };
    std::vector<double> inv;
    REQUIRE( sicnu::primitives::invertDenseMatrix( m, 2, &inv ) );
    REQUIRE( inv[0] == Catch::Approx( 0.6 ).margin( 1e-12 ) );
    REQUIRE( inv[1] == Catch::Approx( -0.7 ).margin( 1e-12 ) );
    REQUIRE( inv[2] == Catch::Approx( -0.2 ).margin( 1e-12 ) );
    REQUIRE( inv[3] == Catch::Approx( 0.4 ).margin( 1e-12 ) );
    // The input is preserved by the out-of-place form.
    REQUIRE( m[0] == 4.0 );
    // A·A⁻¹ = I.
    for ( int i = 0; i < 2; ++i )
        for ( int j = 0; j < 2; ++j )
        {
            const double dot = m[i * 2] * inv[j] + m[i * 2 + 1] * inv[2 + j];
            REQUIRE( dot == Catch::Approx( i == j ? 1.0 : 0.0 ).margin( 1e-12 ) );
        }
}

TEST_CASE( "Dense inverse: 3x3 shifted system recovers the identity solve", "[primitives][linalg]" )
{
    // A = I + u·uᵀ with u = (1,2,3) (symmetric positive definite, like the
    // RX covariance ridge). Solve by inverting and multiplying.
    const double u[3] = { 1.0, 2.0, 3.0 };
    std::vector<double> a( 9, 0.0 );
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            a[i * 3 + j] = ( i == j ? 1.0 : 0.0 ) + u[i] * u[j];
    std::vector<double> inv;
    REQUIRE( sicnu::primitives::invertDenseMatrix( a, 3, &inv ) );
    const double b[3] = { 2.0, -1.0, 0.5 };
    for ( int i = 0; i < 3; ++i )
    {
        double x = 0.0;
        for ( int j = 0; j < 3; ++j )
            x += inv[i * 3 + j] * b[j];
        // Sherman–Morrison: x = (I − u uᵀ/(1+uᵀu)) b.
        const double unorm = 14.0;
        double corr = 0.0;
        for ( int j = 0; j < 3; ++j )
            corr += u[j] * b[j];
        const double expected = b[i] - u[i] * corr / ( 1.0 + unorm );
        REQUIRE( x == Catch::Approx( expected ).margin( 1e-12 ) );
    }
}

TEST_CASE( "Dense inverse refuses a singular matrix at the shared 1e-12 pivot floor",
           "[primitives][linalg]" )
{
    std::vector<double> singular = { 1.0, 2.0, 2.0, 4.0 }; // rank 1
    std::vector<double> inv;
    REQUIRE_FALSE( sicnu::primitives::invertDenseMatrix( singular, 2, &inv ) );
    // Degenerate zero matrix refuses too.
    std::vector<double> zero( 4, 0.0 );
    REQUIRE_FALSE( sicnu::primitives::invertDenseMatrix( zero, 2, &inv ) );
}
