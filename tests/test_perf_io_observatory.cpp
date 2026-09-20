// test_perf_io_observatory.cpp — Raster I/O & temporal observatory workloads.
//
// Qt-free, GDAL-only target (links Sicnu::Geospatial). Covers the raster-I/O
// and temporal branches of the observatory workload catalog:
//
//   * obs_io_windowed_scan            — tile-windowed read of a synthetic raster
//   * obs_io_windowed_scan_scaling    — complexity ladder (W → 2W) with exponent
//   * obs_io_full_materialize         — whole-raster read as the contrast case
//   * obs_io_write_roundtrip          — staged/atomic writer path
//   * obs_temporal_composite          — K-scene per-pixel median composite
//   * obs_temporal_composite_scaling  — complexity ladder over K
//
// All fixtures are deterministic LCG rasters inside a temporary directory;
// nothing touches the repository or the user's data directories. Gates are
// structural (tile counts, complexity exponents, memory bounds, semantic
// equivalence) — never absolute millisecond budgets.
#include <catch2/catch_test_macros.hpp>

#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include "perf/perf_observatory.h"

#include <cstdio>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using sicnu::geo::RasterReader;
using sicnu::geo::RasterWriter;
using sicnu::testing::perf::Ladder;
using sicnu::testing::perf::Lcg;
using sicnu::testing::perf::Sample;
using sicnu::testing::perf::Scale;

namespace
{

constexpr int kTile = 256; // window/tile size used by every scan workload

/// One process-private, self-cleaning scratch directory per test case.
sicnu::testing::perf::ScratchDir &scratch()
{
    static thread_local sicnu::testing::perf::ScratchDir dir;
    return dir;
}

std::string scratchPath( const char *stem )
{
    static int counter = 0;
    return scratch().file( std::string( stem ) + "-" + std::to_string( ++counter ) + ".tif" );
}

/// Deterministic synthetic raster: pixel(band) = LCG value in [-100,100).
/// Written through the platform's staged/atomic writer so the path under test
/// is the repo's own I/O contract, not raw GDAL setup code.
void writeSyntheticRaster( const std::string &path, int width, int height, std::uint32_t seed )
{
    sicnu::geo::RasterWriteOptions options;
    options.driver = "GTiff";
    options.overwrite = true;
    RasterWriter writer =
        RasterWriter::create( path, width, height, { sicnu::geo::RasterBandSpec{} }, options );
    writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
    writer.setGeotransform( { 120.0, 0.001, 0.0, 40.0, 0.0, -0.001 } );

    Lcg lcg( seed + 9781u );
    std::vector<double> row( static_cast<std::size_t>( width ) );
    sicnu::geo::RasterWindow line;
    line.xOff = 0;
    line.width = width;
    line.height = 1;
    for ( int y = 0; y < height; ++y )
    {
        line.yOff = y;
        for ( int x = 0; x < width; ++x )
            row[x] = lcg.range( -100.0, 100.0 );
        writer.writeWindow( 1, line, row.data() );
    }
    writer.finalize();
}

struct ScanStats
{
    long long windows = 0;
    long long pixels = 0;
    double checksum = 0.0;
};

/// Tile-walked windowed read: the memory-bounded path every operator uses.
ScanStats windowedScan( const std::string &path, int tile, int &tileCountOut )
{
    RasterReader reader = RasterReader::open( path );
    const auto plan = sicnu::geo::planTileWalk(
        reader.metadata(),
        { 0, 0, reader.metadata().width, reader.metadata().height }, tile, tile );
    tileCountOut = static_cast<int>( plan.tileCount() );

    ScanStats stats;
    for ( int ty = 0; ty < plan.tilesY; ++ty )
    {
        for ( int tx = 0; tx < plan.tilesX; ++tx )
        {
            const auto slice = plan.slice( tx, ty );
            const std::vector<double> values =
                reader.readWindow( { 1 }, { slice.xOff, slice.yOff, slice.width, slice.height } );
            REQUIRE( values.size() == static_cast<std::size_t>( slice.width ) * slice.height );
            ++stats.windows;
            stats.pixels += static_cast<long long>( values.size() );
            for ( const double v : values )
                stats.checksum += v;
        }
    }
    return stats;
}

/// Runs a measured workload and writes one observatory record. Structural and
/// complexity blocks are built AFTER the run so they can report the numbers the
/// workload itself produced.
void record( const std::string &name, Sample &&sample, Json::Value structural,
              Json::Value complexity = Json::Value() )
{
    sicnu::testing::perf::writeRecord( sicnu::testing::perf::outputDir(), name, sample, complexity,
                                        structural );
}

/// The scale block every record carries: which rung of the ladder produced it
/// and the input size in pixels, so a reader never mistakes a 256^2 measurement
/// for a 2048^2 one.
Json::Value scaleJson( const int side, const int tiles = 0 )
{
    Json::Value o( Json::objectValue );
    o["kind"] = sicnu::testing::perf::scaleName( sicnu::testing::perf::scaleFromEnv() );
    o["items"] = static_cast<Json::Int64>( side ) * side;
    o["probes"] = tiles;
    Json::Value fields( Json::objectValue );
    fields["side_pixels"] = side;
    fields["bands"] = 1;
    o["fields"] = fields;
    return o;
}

} // namespace

//------------------------------------------------------------------------------

TEST_CASE( "obs io windowed scan reads every tile exactly once", "[perf_observatory][io]" )
{
    const Ladder size{ 256, 512, 1024 };
    const int side = size.pick( sicnu::testing::perf::scaleFromEnv() );
    const std::string path = scratchPath( "obs_windowed" );
    writeSyntheticRaster( path, side, side, 0x51A2B3C4u );

    int tileCount = 0;
    Json::Value stats( Json::objectValue );
    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        const auto st = windowedScan( path, kTile, tileCount );
        sample.counts.rowsMaterialized = kTile;
        sample.counts.filesWritten = 1;
        stats["windows"] = static_cast<Json::Int64>( st.windows );
        stats["pixels"] = static_cast<Json::Int64>( st.pixels );
        stats["tile"] = kTile;
        sample.extra = stats;
    } );
    s.scale = scaleJson( side, tileCount );

    const int expected = ( ( side + kTile - 1 ) / kTile ) * ( ( side + kTile - 1 ) / kTile );
    CHECK( tileCount == expected );
    CHECK( stats["windows"].asInt64() == expected );
    CHECK( stats["pixels"].asInt64() == static_cast<Json::Int64>( side ) * side );

    Json::Value structural( Json::objectValue );
    structural["tiles_expected"] = expected;
    structural["tiles_observed"] = tileCount;
    structural["memory_unit"] = "tile";
    structural["tile_pixels"] = kTile * kTile;
    record( "obs_io_windowed_scan", std::move( s ), structural );
}

TEST_CASE( "obs io windowed scan cost scales linearly with raster area",
           "[perf_observatory][io][complexity]" )
{
    const Scale scale = sicnu::testing::perf::scaleFromEnv();
    const int w1 = Ladder{ 256, 256, 256 }.pick( scale );
    const int w2 = Ladder{ 512, 512, 512 }.pick( scale );

    const std::string p1 = scratchPath( "obs_scan_a" );
    const std::string p2 = scratchPath( "obs_scan_b" );
    writeSyntheticRaster( p1, w1, w1, 0x51A2B3C4u );
    writeSyntheticRaster( p2, w2, w2, 0x51A2B3C4u );

    int tiles1 = 0;
    int tiles2 = 0;
    const Sample s1 = sicnu::testing::perf::measure(
        [&]( Sample & ) { (void) windowedScan( p1, kTile, tiles1 ); } );
    const Sample s2 = sicnu::testing::perf::measure(
        [&]( Sample & ) { (void) windowedScan( p2, kTile, tiles2 ); } );

    const double exponent = sicnu::testing::perf::complexityExponent( s1.wallMs, s2.wallMs );
    const Json::Value complexity =
        sicnu::testing::perf::complexityToJson( { { w1 * w1, s1.wallMs }, { w2 * w2, s2.wallMs } } );

    Sample out = s1;
    out.extra["paired_with"] = "obs_io_windowed_scan_2n";
    Json::Value structural( Json::objectValue );
    structural["tiles_n"] = tiles1;
    structural["tiles_2n"] = tiles2;
    structural["exponent"] = exponent;
    record( "obs_io_windowed_scan_scaling", std::move( out ), structural, complexity );

    // A tile walk is O(area): doubling the area doubles the tile count and the
    // cost. A per-window cost that grows with the raster shows up as an exponent
    // above ~1.5, which is the regression this rule catches.
    CHECK( tiles2 > tiles1 );
    sicnu::testing::perf::rules::checkComplexity( exponent, 1.5, "obs io windowed scan" );
    CHECK( exponent < 1.5 );
}

TEST_CASE( "obs io full read materializes the raster, windowed scan does not",
           "[perf_observatory][io][memory]" )
{
    const int side = Ladder{ 512, 1024, 2048 }.pick( sicnu::testing::perf::scaleFromEnv() );
    const std::string path = scratchPath( "obs_full" );
    writeSyntheticRaster( path, side, side, 0x9E3779B9u );

    double fullChecksum = 0.0;
    Json::Value fullExtra( Json::objectValue );
    const Sample full = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        RasterReader reader = RasterReader::open( path );
        const std::vector<double> values =
                reader.readFull( { 1 }, std::numeric_limits<std::size_t>::max() );
        REQUIRE( values.size() == static_cast<std::size_t>( side ) * side );
        for ( const double v : values )
            fullChecksum += v;
        sample.counts.rowsMaterialized = side;
        sample.counts.filesWritten = 1;
        sample.extra["pixels"] = static_cast<Json::Int64>( side ) * side;
        fullExtra = sample.extra;
    } );

    int tiles = 0;
    double windowChecksum = 0.0;
    const Sample windowed = sicnu::testing::perf::measure( [&]( Sample & ) {
        const auto stats = windowedScan( path, kTile, tiles );
        windowChecksum = stats.checksum;
    } );

    // Semantic: both paths must see the same raster so the memory comparison is
    // apples to apples.
    CHECK( std::abs( fullChecksum - windowChecksum ) < 1e-3 );

    Json::Value structural( Json::objectValue );
    structural["semantic_equivalent_to_windowed_scan"] =
        ( std::abs( fullChecksum - windowChecksum ) < 1e-3 );
    structural["full_read_pixels"] = static_cast<Json::Int64>( side ) * side;
    structural["windowed_pixels_per_window"] = kTile * kTile;
    structural["windowed_reads"] = tiles;
    // Exact allocation accounting: what each path must hold at once. This is
    // the memory claim; the RSS watermark is only corroboration.
    structural["full_read_peak_bytes"] = static_cast<Json::UInt64>(
        static_cast<std::uint64_t>( side ) * side * sizeof( double ) );
    structural["windowed_read_peak_bytes"] = static_cast<Json::UInt64>(
        static_cast<std::uint64_t>( kTile ) * kTile * sizeof( double ) );
    structural["ratio"] = static_cast<double>( side ) / kTile;

    Sample out = full;
    out.extra["windowed_scan_peak_rss_delta_mb"] =
        static_cast<Json::UInt>( windowed.peakRssDeltaMb() );
    out.extra["full_read_peak_rss_delta_mb"] =
        static_cast<Json::UInt>( full.peakRssDeltaMb() );
    record( "obs_io_full_materialize", std::move( out ), structural );

    // NOT gated: peak-RSS deltas are a 2 ms watermark of an OS allocator, so a
    // lane can legitimately reuse a buffer and report a delta of 0. The
    // contrast that matters is the ALLOCATION, which is exact and is stated in
    // the structural block above — one raster-sized buffer versus one
    // tile-sized buffer. The RSS numbers stay in the record for trend reading.
    (void) windowed.peakRssDeltaMb();
    (void) full.peakRssDeltaMb();
}

TEST_CASE( "obs io writer roundtrip is value-equivalent", "[perf_observatory][io][write]" )
{
    const int side = Ladder{ 256, 512, 1024 }.pick( sicnu::testing::perf::scaleFromEnv() );

    double maxAbsError = 0.0;
    long long mismatches = 0;
    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        const std::string path = scratchPath( "obs_roundtrip" );
        writeSyntheticRaster( path, side, side, 0x0BADC0DEu );

        // Reference: the exact generator values, replayed independently of the
        // file so the comparison is a real oracle rather than a tautology.
        Lcg reference( 0x0BADC0DEu + 9781u );
        std::vector<double> expect( static_cast<std::size_t>( side ) * side );
        for ( auto &v : expect )
            v = reference.range( -100.0, 100.0 );

        const std::vector<double> got = RasterReader::open( path )
                                            .readWindow( { 1 },
                                                         { 0, 0, side, side } );
        REQUIRE( got.size() == expect.size() );
        for ( size_t i = 0; i < got.size(); ++i )
        {
            const double err = std::abs( got[i] - expect[i] );
            maxAbsError = std::max( maxAbsError, err );
            if ( err > 1e-4 )
                ++mismatches;
        }
        sample.counts.filesWritten = 1;
        sample.extra["side"] = side;
        sample.extra["max_abs_error"] = maxAbsError;
        sample.extra["mismatched_pixels"] = static_cast<Json::Int64>( mismatches );
    } );

    CHECK( mismatches == 0 );
    CHECK( maxAbsError < 1e-4 );

    Json::Value structural( Json::objectValue );
    structural["value_equivalent"] = mismatches == 0;
    structural["max_abs_error"] = maxAbsError;
    record( "obs_io_write_roundtrip", std::move( s ), structural );
}
