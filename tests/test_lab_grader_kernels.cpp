/***************************************************************************
  tests/test_lab_grader_kernels.cpp — grader 2.0 kernels (teaching-lab-11).

  Independent oracle policy: every expected statistic is computed in the TEST
  from the same closed-form scene the test writes with raw GDAL — the kernel
  must agree with arithmetic, not with itself. Covers:
    zone_stats (mean bounds / ENL ratio vs reference raster),
    band_layout (band count, per-band valid fraction, exact valid counts),
    spatial_agreement (binary hit/false-alarm, per-zone label accuracy),
    spectral_signature (SAM angle of constructed spectra),
    budget behaviour (tile splitting under a tiny --max-bytes; typed error
    below one pixel), and file_check (PNG page geometry, MapSpec validation).
 ***************************************************************************/

#include "agent/lab_grader_kernels.h"

#include "geospatial/raster/raster_reader.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <gdal_priv.h>
#include <zlib.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using sicnu::agent::LabKernelOutcome;
using sicnu::agent::LabKernelSpec;
using sicnu::agent::runLabFileChecks;
using sicnu::agent::runLabKernelWalks;
using sicnu::agent::validateLabKernelParams;

namespace {

struct GdalInit
{
    GdalInit() { GDALAllRegister(); }
};
const GdalInit s_gdalInit;

constexpr int kSize = 32;

/// Writes a single-band Byte raster with per-cell codes.
void writeByteRaster( const QString &path, const std::vector<uint8_t> &cells )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), kSize, kSize, 1,
                                           GDT_Byte, nullptr );
    REQUIRE( dataset );
    double geotransform[6] = { 102.5, 0.001, 0.0, 30.5, 0.0, -0.001 };
    dataset->SetGeoTransform( geotransform );
    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    dataset->SetProjection( wkt );
    CPLFree( wkt );
    GDALRasterBand *band = dataset->GetRasterBand( 1 );
    REQUIRE( band->RasterIO( GF_Write, 0, 0, kSize, kSize,
                             const_cast<uint8_t *>( cells.data() ), kSize, kSize, GDT_Byte, 0,
                             0 )
             == CE_None );
    GDALClose( dataset );
}

/// Writes a float raster with @p bands planes (row-major kSize x kSize).
void writeFloatRaster( const QString &path, const std::vector<std::vector<float>> &bands )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), kSize, kSize,
                                           static_cast<int>( bands.size() ), GDT_Float32,
                                           nullptr );
    REQUIRE( dataset );
    double geotransform[6] = { 102.5, 0.001, 0.0, 30.5, 0.0, -0.001 };
    dataset->SetGeoTransform( geotransform );
    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    dataset->SetProjection( wkt );
    CPLFree( wkt );
    for ( int i = 0; i < static_cast<int>( bands.size() ); ++i )
    {
        GDALRasterBand *band = dataset->GetRasterBand( i + 1 );
        // Band 2 uses 255 as the NoData sentinel (see band_layout test).
        if ( bands.size() == 2 && i == 1 )
            band->SetNoDataValue( 255.0 );
        REQUIRE( band->RasterIO( GF_Write, 0, 0, kSize, kSize,
                                 const_cast<float *>( bands[static_cast<size_t>( i )].data() ),
                                 kSize, kSize, GDT_Float32, 0, 0 )
                 == CE_None );
    }
    GDALClose( dataset );
}

/// Three horizontal stripes: zone 1 (top third), 2 (middle), 3 (bottom).
std::vector<uint8_t> threeStripeZones()
{
    std::vector<uint8_t> cells( static_cast<size_t>( kSize ) * kSize );
    for ( int y = 0; y < kSize; ++y )
        for ( int x = 0; x < kSize; ++x )
            cells[static_cast<size_t>( y ) * kSize + x] =
              static_cast<uint8_t>( y < 10 ? 1 : ( y < 20 ? 2 : 3 ) );
    return cells;
}

/// Minimal grayscale-8-bit PNG writer (zlib IDAT, filter-0 scanlines) —
/// the bundled GDAL build has no create-capable PNG driver.
bool writeMinimalPng( const QString &path, int width, int height )
{
    auto be32 = []( uint32_t v )
    {
        return std::vector<uint8_t>{ static_cast<uint8_t>( v >> 24 ),
                                     static_cast<uint8_t>( v >> 16 ),
                                     static_cast<uint8_t>( v >> 8 ),
                                     static_cast<uint8_t>( v ) };
    };
    auto chunk = [&be32]( const char *type, const std::vector<uint8_t> &data )
    {
        std::vector<uint8_t> out = be32( static_cast<uint32_t>( data.size() ) );
        out.insert( out.end(), type, type + 4 );
        out.insert( out.end(), data.begin(), data.end() );
        const uint32_t crc = static_cast<uint32_t>(
          ::crc32( 0L, out.data() + 4, static_cast<uInt>( data.size() + 4 ) ) );
        const auto crcBytes = be32( crc );
        out.insert( out.end(), crcBytes.begin(), crcBytes.end() );
        return out;
    };
    std::vector<uint8_t> ihdr;
    const auto w = be32( static_cast<uint32_t>( width ) );
    const auto h = be32( static_cast<uint32_t>( height ) );
    ihdr.insert( ihdr.end(), w.begin(), w.end() );
    ihdr.insert( ihdr.end(), h.begin(), h.end() );
    ihdr.push_back( 8 ); // bit depth
    ihdr.push_back( 0 ); // grayscale
    ihdr.push_back( 0 );
    ihdr.push_back( 0 );
    ihdr.push_back( 0 );
    std::vector<uint8_t> raw;
    raw.reserve( static_cast<size_t>( height ) * ( width + 1 ) );
    for ( int y = 0; y < height; ++y )
    {
        raw.push_back( 0 );
        for ( int x = 0; x < width; ++x )
            raw.push_back( static_cast<uint8_t>( ( x / 16 + y / 16 ) % 2 ? 0xAA : 0x55 ) );
    }
    uLongf size = ::compressBound( static_cast<uLong>( raw.size() ) );
    std::vector<uint8_t> idat( size );
    if ( ::compress2( idat.data(), &size, raw.data(), static_cast<uLong>( raw.size() ),
                      Z_BEST_SPEED )
         != Z_OK )
        return false;
    idat.resize( size );

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    static const uint8_t kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const auto writeAll = [ &file ]( const std::vector<uint8_t> &bytes ) -> bool
    {
        return file.write( reinterpret_cast<const char *>( bytes.data() ),
                           static_cast<qint64>( bytes.size() ) )
               == static_cast<qint64>( bytes.size() );
    };
    return file.write( reinterpret_cast<const char *>( kSignature ), 8 ) == 8
           && writeAll( chunk( "IHDR", ihdr ) ) && writeAll( chunk( "IDAT", idat ) )
           && writeAll( chunk( "IEND", {} ) );
}

struct WalkResult
{
    bool ok = false;
    bool usageClass = false;
    QString error;
    std::map<QString, LabKernelOutcome> outcomes;
};

WalkResult walk( const QString &artifact, const std::vector<LabKernelSpec> &specs,
                 const QString &rulesDir, std::size_t maxBytes = 64ull << 20 )
{
    auto reader = sicnu::geo::RasterReader::open( artifact.toUtf8().constData() );
    WalkResult result;
    result.ok = runLabKernelWalks( reader, specs, rulesDir, maxBytes, result.outcomes,
                                   &result.usageClass, &result.error );
    return result;
}

} // namespace

TEST_CASE( "zone_stats: per-zone means match in-test arithmetic",
           "[lab_grader_kernels][zone_stats]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    std::vector<uint8_t> values( static_cast<size_t>( kSize ) * kSize );
    for ( int y = 0; y < kSize; ++y )
        for ( int x = 0; x < kSize; ++x )
            values[static_cast<size_t>( y ) * kSize + x] = static_cast<uint8_t>( y % 251 );
    const QString artifact = dir.filePath( "series.tif" );
    writeByteRaster( artifact, values );

    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );

    // In-test expected means over each stripe.
    const auto meanOf = [&values]( int y0, int y1 )
    {
        double sum = 0.0;
        int n = 0;
        for ( int y = y0; y < y1; ++y )
            for ( int x = 0; x < kSize; ++x )
            {
                sum += values[static_cast<size_t>( y ) * kSize + x];
                ++n;
            }
        return sum / n;
    };
    const double mean1 = meanOf( 0, 10 );
    const double mean2 = meanOf( 10, 20 );

    Json::Value params;
    params["band"] = 1;
    params["zones"]["path"] = zonesPath.toStdString();
    params["stat"] = "mean";
    Json::Value bounds;
    Json::Value bound1;
    bound1["zone"] = 1;
    bound1["min"] = mean1 - 1e-6;
    bound1["max"] = mean1 + 1e-6;
    Json::Value bound2;
    bound2["zone"] = 2;
    bound2["min"] = mean2 - 1e-6;
    bound2["max"] = mean2 + 1e-6;
    bounds.append( bound1 );
    bounds.append( bound2 );
    params["bounds"] = bounds;

    const WalkResult result = walk(
      artifact, { LabKernelSpec{ "z.mean", "zone_stats", params } }, dir.path() );
    REQUIRE( result.ok );
    const LabKernelOutcome &outcome = result.outcomes.at( "z.mean" );
    REQUIRE( outcome.passed );
    // Observed per-(band,zone) means agree with the in-test arithmetic.
    const Json::Value &cells = outcome.observed["per_band_zone"];
    bool sawZone1 = false;
    for ( const auto &cell : cells )
    {
        if ( cell["zone"].asInt() == 1 && cell["band"].asInt() == 1 )
        {
            sawZone1 = true;
            REQUIRE( std::fabs( cell["mean"].asDouble() - mean1 ) < 1e-9 );
            REQUIRE( cell["n"].asInt64() == 10 * kSize );
        }
    }
    REQUIRE( sawZone1 );

    // Now the negative: tighten zone 2's bound so the true mean violates it.
    bound2["min"] = mean2 + 0.5;
    bound2["max"] = mean2 + 1.0;
    params["bounds"] = Json::Value( Json::arrayValue );
    params["bounds"].append( bound1 );
    params["bounds"].append( bound2 );
    const WalkResult failing = walk(
      artifact, { LabKernelSpec{ "z.mean", "zone_stats", params } }, dir.path() );
    REQUIRE( failing.ok );
    REQUIRE( !failing.outcomes.at( "z.mean" ).passed );
    REQUIRE( failing.outcomes.at( "z.mean" ).hasDelta );
}

TEST_CASE( "zone_stats: ENL ratio against a reference raster",
           "[lab_grader_kernels][enl]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // "Filtered": constant 0.25 inside the zone (ENL -> huge with variance
    // floor). "Raw": alternating 0.2/0.3 (variance 0.0025, ENL = 25).
    std::vector<float> filtered( static_cast<size_t>( kSize ) * kSize, 0.25f );
    std::vector<float> raw( static_cast<size_t>( kSize ) * kSize );
    for ( int y = 0; y < kSize; ++y )
        for ( int x = 0; x < kSize; ++x )
            raw[static_cast<size_t>( y ) * kSize + x] =
              ( ( x + y ) % 2 == 0 ) ? 0.20f : 0.30f;

    const QString artifact = dir.filePath( "filtered.tif" );
    writeFloatRaster( artifact, { filtered } );
    const QString reference = dir.filePath( "raw.tif" );
    writeFloatRaster( reference, { raw } );
    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );

    Json::Value params;
    params["band"] = 1;
    params["zones"]["path"] = zonesPath.toStdString();
    params["stat"] = "enl";
    params["zone"] = 2; // middle stripe
    params["reference"]["path"] = reference.toStdString();
    params["enl_min_ratio"] = 2.0;  // huge/25 passes with margin
    params["max_relative_mean_shift"] = 0.05;

    const WalkResult result = walk(
      artifact, { LabKernelSpec{ "z.enl", "zone_stats", params } }, dir.path() );
    REQUIRE( result.ok );
    const LabKernelOutcome &outcome = result.outcomes.at( "z.enl" );
    REQUIRE( outcome.passed );
    // ENL(raw) is exactly mean^2/var = 0.25^2/0.0025 = 25 on alternating
    // 0.2/0.3; the filtered raster is constant, so ENL(filtered) hits the
    // variance floor -> ratio is huge. The evidence must carry the closed
    // forms, not just a boolean.
    const Json::Value &cell = outcome.observed["enl_per_zone"]["2"];
    REQUIRE( cell.isObject() );
    REQUIRE( std::fabs( cell["enl_raw"].asDouble() - 25.0 ) < 1e-4 ); // float32 storage
    REQUIRE( cell["ratio"].asDouble() >= params["enl_min_ratio"].asDouble() );
    REQUIRE( std::fabs( cell["mean_shift"].asDouble() ) < 1e-6 ); // float32 storage
}

TEST_CASE( "series_separation: pooled slope equals the per-pixel regression",
           "[lab_grader_kernels][series_separation]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 4 bands, zone 2 = rows 10..19; pixel value = 2 * x_band (x = band index)
    // so the exact least-squares slope of every pixel's series is 2.0 — the
    // pooled per-zone slope must reproduce it (regression: an unweighted
    // variant was low by the band count).
    std::vector<std::vector<float>> bands;
    for ( int b = 1; b <= 4; ++b )
        bands.emplace_back( static_cast<size_t>( kSize ) * kSize,
                            static_cast<float>( 2.0 * b ) );
    const QString artifact = dir.filePath( "series.tif" );
    writeFloatRaster( artifact, bands );
    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );

    Json::Value params;
    params["zones"]["path"] = zonesPath.toStdString();
    Json::Value x;
    for ( int b = 1; b <= 4; ++b )
        x.append( b );
    params["x"] = x;
    Json::Value bounds;
    Json::Value bound;
    bound["zone"] = 2;
    bound["min"] = 1.999;
    bound["max"] = 2.001;
    bounds.append( bound );
    params["bounds"] = bounds;

    const WalkResult ok = walk(
      artifact, { LabKernelSpec{ "ss.slope", "series_separation", params } }, dir.path() );
    REQUIRE( ok.ok );
    const LabKernelOutcome &outcome = ok.outcomes.at( "ss.slope" );
    INFO( outcome.message.toStdString() );
    REQUIRE( outcome.passed );
    REQUIRE( std::fabs( outcome.observed["slopes"]["2"]["slope"].asDouble() - 2.0 ) < 1e-9 );
}

TEST_CASE( "band_layout: count, valid fractions and exact valid-pixel counts",
           "[lab_grader_kernels][band_layout]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Band 1 fully valid, band 2 half NoData (declared sentinel 255).
    std::vector<float> full( static_cast<size_t>( kSize ) * kSize, 1.0f );
    std::vector<float> half( static_cast<size_t>( kSize ) * kSize );
    for ( int y = 0; y < kSize; ++y )
        for ( int x = 0; x < kSize; ++x )
            half[static_cast<size_t>( y ) * kSize + x] = y < kSize / 2 ? 1.0f : 255.0f;
    const QString artifact = dir.filePath( "stack.tif" );
    writeFloatRaster( artifact, { full, half } );

    Json::Value meta;
    meta["band_count"] = 2;
    meta["min_valid_fraction"] = 0.4; // band 2 = 0.5 passes
    const WalkResult ok = walk(
      artifact, { LabKernelSpec{ "b.meta", "band_layout", meta } }, dir.path() );
    REQUIRE( ok.ok );
    REQUIRE( ok.outcomes.at( "b.meta" ).passed );
    REQUIRE( std::fabs( ok.outcomes.at( "b.meta" ).observed["valid_fraction_per_band"]["2"].asDouble()
                        - 0.5 )
             < 1e-9 );

    // Exact valid count for band 2 = kSize * kSize/2.
    meta["expected_valid_pixels"] = Json::Value( Json::objectValue );
    meta["expected_valid_pixels"]["2"] = Json::Int64( kSize * ( kSize / 2 ) );
    const WalkResult exact = walk(
      artifact, { LabKernelSpec{ "b.exact", "band_layout", meta } }, dir.path() );
    REQUIRE( exact.ok );
    REQUIRE( exact.outcomes.at( "b.exact" ).passed );

    // Wrong declared count (off by one pixel) must fail with the delta.
    meta["expected_valid_pixels"]["2"] = Json::Int64( kSize * ( kSize / 2 ) + 1 );
    const WalkResult off = walk(
      artifact, { LabKernelSpec{ "b.off", "band_layout", meta } }, dir.path() );
    REQUIRE( off.ok );
    REQUIRE( !off.outcomes.at( "b.off" ).passed );

    // Wrong band count must fail.
    Json::Value wrongCount;
    wrongCount["band_count"] = 3;
    const WalkResult count = walk(
      artifact, { LabKernelSpec{ "b.count", "band_layout", wrongCount } }, dir.path() );
    REQUIRE( count.ok );
    REQUIRE( !count.outcomes.at( "b.count" ).passed );
}

TEST_CASE( "spatial_agreement binary: hit and false-alarm rates match arithmetic",
           "[lab_grader_kernels][spatial]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Truth block 10x10 at (11..20); artifact detects 6 of those 10 rows plus
    // 5 false rows below (exactly: truth positives 100; hits 60; false alarms
    // 160; truth negatives 924; FAR = 160/924).
    std::vector<uint8_t> truth( static_cast<size_t>( kSize ) * kSize, 0 );
    std::vector<uint8_t> artifact( static_cast<size_t>( kSize ) * kSize, 0 );
    for ( int y = 11; y < 21; ++y )
        for ( int x = 0; x < 10; ++x )
            truth[static_cast<size_t>( y ) * kSize + x] = 1;
    for ( int y = 15; y < 21; ++y ) // 6 rows of the block detected
        for ( int x = 0; x < 10; ++x )
            artifact[static_cast<size_t>( y ) * kSize + x] = 1;
    for ( int y = 25; y < 30; ++y ) // 5 rows outside the block = false alarms
        for ( int x = 0; x < kSize; ++x )
            artifact[static_cast<size_t>( y ) * kSize + x] = 1;

    const QString truthPath = dir.filePath( "truth.tif" );
    writeByteRaster( truthPath, truth );
    const QString artifactPath = dir.filePath( "mask.tif" );
    writeByteRaster( artifactPath, artifact );

    const int truthPositives = 100;
    const int hits = 60;
    const int falseAlarms = 5 * kSize;
    const int truthNegatives = kSize * kSize - truthPositives;
    const double expectedHit = static_cast<double>( hits ) / truthPositives;
    const double expectedFar =
      static_cast<double>( falseAlarms ) / truthNegatives;

    Json::Value params;
    params["band"] = 1;
    params["truth"]["path"] = truthPath.toStdString();
    params["truth_positive"] = 1;
    params["artifact_positive"] = 1;
    params["min_hit_rate"] = expectedHit - 1e-9; // exactly at the floor: pass
    params["max_false_alarm_rate"] = expectedFar + 1e-9;

    const WalkResult ok = walk(
      artifactPath, { LabKernelSpec{ "s.binary", "spatial_agreement", params } }, dir.path() );
    REQUIRE( ok.ok );
    const LabKernelOutcome &outcome = ok.outcomes.at( "s.binary" );
    REQUIRE( outcome.passed );
    REQUIRE( std::fabs( outcome.observed["hit_rate"].asDouble() - expectedHit ) < 1e-9 );
    REQUIRE( std::fabs( outcome.observed["false_alarm_rate"].asDouble() - expectedFar )
             < 1e-9 );

    // One notch tighter on the hit floor must fail.
    params["min_hit_rate"] = expectedHit + 1e-6;
    const WalkResult failing = walk(
      artifactPath, { LabKernelSpec{ "s.binary", "spatial_agreement", params } }, dir.path() );
    REQUIRE( failing.ok );
    REQUIRE( !failing.outcomes.at( "s.binary" ).passed );
}

TEST_CASE( "spatial_agreement zone accuracy: per-zone label agreement",
           "[lab_grader_kernels][spatial][zone_accuracy]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );
    // Labels: correct mapping (zone-1) except the middle stripe's first row
    // mislabelled -> zone 2 accuracy = 31/32.
    std::vector<uint8_t> labels( threeStripeZones() );
    for ( auto &v : labels )
        v = static_cast<uint8_t>( v - 1 ); // SAM label convention: label = zone - 1
    for ( int x = 0; x < kSize; ++x )
        labels[static_cast<size_t>( 10 ) * kSize + x] = 0; // mislabel row 10
    const QString artifactPath = dir.filePath( "labels.tif" );
    writeByteRaster( artifactPath, labels );

    Json::Value params;
    params["band"] = 1;
    params["truth"]["path"] = zonesPath.toStdString();
    Json::Value zoneExpected;
    for ( int z = 1; z <= 3; ++z )
    {
        Json::Value entry;
        entry["zone"] = z;
        entry["label"] = z - 1;
        zoneExpected.append( entry );
    }
    params["zone_expected"] = zoneExpected;

    // Fixture oracle: read the labels back through the SAME reader seam the
    // kernel uses — the expected accuracies below are meaningless unless the
    // stored values are the intended ones.
    {
        auto reader = sicnu::geo::RasterReader::open( artifactPath.toUtf8().constData() );
        const auto plane =
          reader.readWindow( { 1 }, { 0, 0, kSize, kSize }, 1ull << 20 );
        REQUIRE( plane.size() == static_cast<size_t>( kSize ) * kSize );
        REQUIRE( plane[0] == 0.0 );              // zone 1 -> label 0
        REQUIRE( plane[10 * kSize] == 0.0 );     // mislabelled row
        REQUIRE( plane[11 * kSize] == 1.0 );     // zone 2 -> label 1
        REQUIRE( plane[31 * kSize] == 2.0 );     // zone 3 -> label 2
    }

    // Row 10 (32 px of zone 2's 320) mislabelled -> zone 2 accuracy 288/320.
    const double zone2Accuracy = 288.0 / 320.0;
    params["min_accuracy"] = zone2Accuracy - 1e-9;

    const WalkResult ok = walk(
      artifactPath, { LabKernelSpec{ "s.zone", "spatial_agreement", params } }, dir.path() );
    REQUIRE( ok.ok );
    INFO( "zone outcome: " << Json::writeString( Json::StreamWriterBuilder(),
                                                 ok.outcomes.at( "s.zone" ).observed )
                           << " | " << ok.outcomes.at( "s.zone" ).message.toStdString() );
    REQUIRE( ok.outcomes.at( "s.zone" ).passed );
    REQUIRE( std::fabs( ok.outcomes.at( "s.zone" )
                          .observed["accuracy_per_zone"]["2"]["accuracy"].asDouble()
                        - zone2Accuracy )
             < 1e-9 );

    params["min_accuracy"] = zone2Accuracy + 1e-6;
    const WalkResult failing = walk(
      artifactPath, { LabKernelSpec{ "s.zone", "spatial_agreement", params } }, dir.path() );
    REQUIRE( failing.ok );
    REQUIRE( !failing.outcomes.at( "s.zone" ).passed );
    REQUIRE( failing.outcomes.at( "s.zone" ).message.contains( QLatin1String( "Zone 2" ) ) );
}

TEST_CASE( "spectral_signature: SAM angle of identical spectra is zero",
           "[lab_grader_kernels][spectral]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 3 bands: pixel spectra are (1,2,2) scaled variants of the reference.
    std::vector<float> b1( static_cast<size_t>( kSize ) * kSize, 0.5f );
    std::vector<float> b2( static_cast<size_t>( kSize ) * kSize, 1.0f );
    std::vector<float> b3( static_cast<size_t>( kSize ) * kSize, 1.0f );
    const QString artifact = dir.filePath( "spectra.tif" );
    writeFloatRaster( artifact, { b1, b2, b3 } );
    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );

    Json::Value params;
    Json::Value bands;
    bands.append( 1 );
    bands.append( 2 );
    bands.append( 3 );
    params["bands"] = bands;
    Json::Value references;
    Json::Value ref;
    ref["name"] = "vegetation";
    Json::Value spectrum;
    spectrum.append( 0.5 );
    spectrum.append( 1.0 );
    spectrum.append( 1.0 );
    ref["spectrum"] = spectrum;
    references.append( ref );
    params["references"] = references;
    params["zones"]["path"] = zonesPath.toStdString();
    params["sam_max_mean_degrees"] = 0.0; // identical direction: 0 degrees

    const WalkResult ok = walk(
      artifact, { LabKernelSpec{ "sp.sam", "spectral_signature", params } }, dir.path() );
    REQUIRE( ok.ok );
    const LabKernelOutcome &outcome = ok.outcomes.at( "sp.sam" );
    REQUIRE( outcome.passed );

    // The measured mean SAM angle must be exactly 0 for identical direction.
    REQUIRE( std::fabs( ok.outcomes.at( "sp.sam" )
                          .observed["sam_per_zone_reference"]["1:0"]["sam_mean_degrees"]
                          .asDouble() )
             < 1e-9 );

    // A rotated reference must break the zero-degree bound. Pixel spectra are
    // (0.5,1,1), the rotated reference (1,0.5,0.5): cos = 1.5/(1.5*sqrt(1.5))
    // -> 35.26 degrees.
    const double expectedAngle =
      std::acos( 1.0 / std::sqrt( 1.5 ) ) * 180.0 / 3.14159265358979323846;
    Json::Value rotated = params;
    rotated["references"] = references;
    rotated["references"][0]["spectrum"][0] = 1.0;
    rotated["references"][0]["spectrum"][1] = 0.5;
    rotated["references"][0]["spectrum"][2] = 0.5;
    rotated["sam_max_mean_degrees"] = expectedAngle - 1e-3;
    const WalkResult failing = walk(
      artifact, { LabKernelSpec{ "sp.sam", "spectral_signature", rotated } }, dir.path() );
    REQUIRE( failing.ok );
    REQUIRE( !failing.outcomes.at( "sp.sam" ).passed );
    REQUIRE( std::fabs( failing.outcomes.at( "sp.sam" )
                          .observed["sam_per_zone_reference"]["1:0"]["sam_mean_degrees"]
                          .asDouble()
                        - expectedAngle )
             < 1e-2 );
}

TEST_CASE( "kernels honour the byte budget (tiles) and type budget errors",
           "[lab_grader_kernels][budget]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString zonesPath = dir.filePath( "zones.tif" );
    writeByteRaster( zonesPath, threeStripeZones() );
    std::vector<float> values( static_cast<size_t>( kSize ) * kSize, 0.5f );
    const QString artifact = dir.filePath( "series.tif" );
    writeFloatRaster( artifact, { values } );

    Json::Value params;
    params["band"] = 1;
    params["zones"]["path"] = zonesPath.toStdString();
    params["stat"] = "mean";
    Json::Value bound;
    bound["zone"] = 1;
    bound["min"] = 0.4;
    bound["max"] = 0.6;
    params["bounds"].append( bound );

    // A tiny-but-valid budget forces many tiles: same answer (determinism
    // under tiling).
    const WalkResult tiled = walk(
      artifact, { LabKernelSpec{ "z.tile", "zone_stats", params } }, dir.path(),
      8 * sizeof( double ) * 7 ); // 7 pixels per read
    REQUIRE( tiled.ok );
    REQUIRE( tiled.outcomes.at( "z.tile" ).passed );

    // A budget below one pixel is a typed artifact-class error, not a crash.
    const WalkResult absurd = walk(
      artifact, { LabKernelSpec{ "z.absurd", "zone_stats", params } }, dir.path(),
      sizeof( double ) / 2 );
    REQUIRE( !absurd.ok );
    REQUIRE( !absurd.usageClass );
    REQUIRE( absurd.error.contains( QLatin1String( "budget" ) ) );
}

TEST_CASE( "kernel params validation rejects ambiguous usage",
           "[lab_grader_kernels][params][negative]" )
{
    QString error;
    Json::Value params;
    params["truth"]["path"] = "t.tif";
    // spatial_agreement with NO mode discriminator.
    REQUIRE( !validateLabKernelParams( "spatial_agreement", params, &error ) );
    // Two modes at once is ambiguous.
    params["min_hit_rate"] = 0.65;
    params["tolerance"] = 0.1;
    REQUIRE( !validateLabKernelParams( "spatial_agreement", params, &error ) );
    // Exactly one mode is accepted (hit/false-alarm bounds may be split
    // across assertions, so either bound alone is a valid binary mode).
    params.removeMember( "tolerance" );
    REQUIRE( validateLabKernelParams( "spatial_agreement", params, &error ) );
    params.removeMember( "min_hit_rate" );
    params["max_false_alarm_rate"] = 0.02;
    REQUIRE( validateLabKernelParams( "spatial_agreement", params, &error ) );
    // zone_stats without bounds (stat=mean) is a usage error.
    Json::Value zoneParams;
    zoneParams["zones"]["path"] = "z.tif";
    REQUIRE( !validateLabKernelParams( "zone_stats", zoneParams, &error ) );
}

TEST_CASE( "file_check: PNG page geometry and MapSpec validation",
           "[lab_grader_kernels][file_check]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Write a real PNG with a minimal zlib encoder (the bundled GDAL has no
    // create-capable PNG driver). A4 landscape @200 dpi nominal: 2339x1654.
    const QString png = dir.filePath( "export.png" );
    REQUIRE( writeMinimalPng( png, 2339, 1654 ) );
    // A minimal map spec document the platform validator accepts.
    const char *mapspecJson = R"JSON({
  "schema_version": "1.0",
  "kind": "map_spec",
  "spec_version": 5,
  "layout_name": "kernel_test_map",
  "page": { "width_mm": 297, "height_mm": 210 },
  "map_frames": [
    { "id": "map-1", "rect_mm": [15, 28, 185, 160],
      "extent": [102.5, 30.244, 102.756, 30.5], "layers": ["layer-1"] }
  ],
  "titles": [
    { "id": "title-1", "text": "Kernel test", "semantic_role": "title.main",
      "rect_mm": [15, 8, 185, 16] }
  ],
  "legends": [ { "id": "legend-1", "map_ref": "map-1", "rect_mm": [208, 30, 60, 70] } ],
  "scale_bars": [ { "id": "sb-1", "map_ref": "map-1" } ],
  "north_arrows": [ { "id": "na-1", "map_ref": "map-1" } ],
  "source_notes": [ { "id": "note-1", "text": "test", "rect_mm": [15, 194, 265, 8] } ],
  "output": { "formats": ["png"], "dpi": 200, "dir": "out" },
  "annotations": [], "charts": [], "colorbars": [], "constraints": [],
  "grids": [], "inset_maps": [], "labels": [], "layers": [], "symbols": []
})JSON";
    const QString mapspecPath = dir.filePath( "map.mapspec.json" );
    {
        QFile file( mapspecPath );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( mapspecJson );
    }

    Json::Value pngCheck;
    pngCheck["exists"] = true;
    pngCheck["path"] = "export.png";
    Json::Value pngSpec;
    pngSpec["page_width_mm"] = 297;
    pngSpec["page_height_mm"] = 210;
    pngSpec["dpi"] = 200;
    pngSpec["size_tolerance"] = 0.05;
    pngCheck["png"] = pngSpec;

    Json::Value mapspecCheck;
    mapspecCheck["exists"] = true;
    mapspecCheck["mapspec"] = Json::Value( Json::objectValue );
    mapspecCheck["mapspec"]["max_problems"] = 0;

    std::map<QString, LabKernelOutcome> outcomes;
    QString usageError;
    const bool ok = runLabFileChecks(
      mapspecPath,
      { LabKernelSpec{ "f.png", "file_check", pngCheck },
        LabKernelSpec{ "f.mapspec", "file_check", mapspecCheck } },
      dir.path(), outcomes, &usageError );
    REQUIRE( ok );
    REQUIRE( outcomes.at( "f.png" ).passed );
    INFO( "mapspec problems: "
          << Json::writeString( Json::StreamWriterBuilder(),
                                outcomes.at( "f.mapspec" ).observed ) );
    INFO( "mapspec message: "
          << outcomes.at( "f.mapspec" ).message.toStdString() );
    REQUIRE( outcomes.at( "f.mapspec" ).passed );
    REQUIRE( outcomes.at( "f.png" ).observed["png_width_px"].asInt64() == 2339 );
    REQUIRE( outcomes.at( "f.png" ).observed["png_height_px"].asInt64() == 1654 );

    // Declared page smaller than the pixels -> geometry fails with evidence.
    pngSpec["page_width_mm"] = 148.0; // A5 portrait width
    pngCheck["png"] = pngSpec;
    outcomes.clear();
    const bool geometryFails = runLabFileChecks(
      mapspecPath, { LabKernelSpec{ "f.png", "file_check", pngCheck } }, dir.path(),
      outcomes, &usageError );
    REQUIRE( geometryFails );
    REQUIRE( !outcomes.at( "f.png" ).passed );
}

// lab platform 12.0 — provenance kernel: foundry dataset metadata assertions.
TEST_CASE( "provenance: generator/seed metadata must match the declared foundry scene",
           "[lab_grader_kernels][provenance]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    std::vector<float> values( static_cast<size_t>( kSize ) * kSize, 0.5f );
    const QString artifact = dir.filePath( "foundry_stamped.tif" );
    writeFloatRaster( artifact, { values } );

    // Stamp the artifact with the same dataset metadata the foundry writes.
    {
        GDALAllRegister();
        GDALDataset *ds = static_cast<GDALDataset *>(
            GDALOpen( artifact.toUtf8().constData(), GA_Update ) );
        REQUIRE( ds != nullptr );
        ds->SetMetadataItem( "SICNU_GENERATOR", "sicnu_generate_samples" );
        ds->SetMetadataItem( "SICNU_VERSION", "1.0.0" );
        ds->SetMetadataItem( "SICNU_SEED", "42" );
        ds->SetMetadataItem( "SICNU_PROFILE", "lab" );
        ds->SetMetadataItem( "SICNU_PRODUCT", "landsat_sample" );
        GDALClose( ds );
    }

    // Matching expectations pass.
    {
        Json::Value params;
        params["generator"] = "sicnu_generate_samples";
        params["seed"] = Json::UInt64( 42 );
        params["product"] = "landsat_sample";
        params["profile"] = "lab";
        const WalkResult ok = walk(
            artifact, { LabKernelSpec{ "p.ok", "provenance", params } }, dir.path() );
        REQUIRE( ok.ok );
        REQUIRE( ok.outcomes.at( "p.ok" ).passed );
    }

    // A wrong seed fails with the mismatch named.
    {
        Json::Value params;
        params["seed"] = Json::UInt64( 7 );
        const WalkResult wrong = walk(
            artifact, { LabKernelSpec{ "p.seed", "provenance", params } }, dir.path() );
        REQUIRE( wrong.ok );
        REQUIRE( !wrong.outcomes.at( "p.seed" ).passed );
        REQUIRE( wrong.outcomes.at( "p.seed" ).message.contains( QLatin1String( "SICNU_SEED mismatch" ) ) );
    }

    // An expectation the artifact does not carry at all fails as missing:
    // probe a plain raster without foundry stamps.
    {
        const QString plain = dir.filePath( "plain.tif" );
        writeFloatRaster( plain, { values } );
        Json::Value params;
        params["generator"] = "sicnu_generate_samples";
        const WalkResult missing = walk(
            plain, { LabKernelSpec{ "p.missing", "provenance", params } }, dir.path() );
        REQUIRE( missing.ok );
        REQUIRE( !missing.outcomes.at( "p.missing" ).passed );
        REQUIRE( missing.outcomes.at( "p.missing" ).message.contains( QLatin1String( "missing provenance metadata" ) ) );
    }
}
