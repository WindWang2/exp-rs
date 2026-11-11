// tests/test_r29_t02_published_claim.cpp — R29-T02 WP02/WP03.
//
// D-T05-CHAIN1 closure at the layer that owns it: a chunked run whose
// PUBLISHED marker hits must prove the declared artifact is still current
// (exists, reopens, carries this parameterization's identity stamp) before
// reporting zero-work success. A missing/replaced artifact demotes the hit
// to a rebuild — the #667 self-heal contract, now enforced at the
// chunked-run layer (ResumableTileRun R8) and consumed by
// rs:spectral_index (SICNU_SPECTRAL_IDENTITY stamp, same pattern as the
// change-streaming operator's SICNU_CHANGE_IDENTITY).
//
// Oracle independence: expected NDVI pixels are recomputed in this file
// from the same two-band fixture the operator reads (normalized difference
// of NIR/red); the operator's output is compared against that closed form,
// never against itself.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if !defined( _WIN32 )
#include <unistd.h>
#endif

#include "operators/framework/rs_operator_context.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::operators::rs::RsSpectralIndexOperator;

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 48;

/// Two-band Float32 fixture (NIR band 1, Red band 2) with a deterministic
/// vegetation-like gradient. @p seed offsets the values so two writes with
/// different seeds produce DIFFERENT content at the same path.
void writeTwoBandRaster( const QString &path, int seed = 0 )
{
    ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 2,
                                   GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<size_t>( kWidth ) * kHeight );
    for ( int b = 1; b <= 2; ++b )
    {
        for ( int r = 0; r < kHeight; ++r )
            for ( int c = 0; c < kWidth; ++c )
                band[static_cast<size_t>( r ) * kWidth + c] =
                    b == 1 ? 2000.f + 7.f * seed + 40.f * ( ( r + c ) % 64 )
                           : 1000.f + 11.f * seed + 30.f * ( ( r * 2 + c ) % 64 );
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, kWidth, kHeight, band.data(), kWidth, kHeight,
                      GDT_Float32, 0, 0 );
    }
    double gt[6] = { 0, 1, 0, static_cast<double>( kHeight ), 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
}

/// Closed-form NDVI for the fixture above (no scale declared → raw DN math:
/// (nir - red) / (nir + red)).
float expectedNdvi( int row, int col, int seed = 0 )
{
    const float nir = 2000.f + 7.f * seed + 40.f * ( ( row + col ) % 64 );
    const float red = 1000.f + 11.f * seed + 30.f * ( ( row * 2 + col ) % 64 );
    return ( nir - red ) / ( nir + red );
}

struct RunOutcome
{
    Json::Value result;
    bool ok = false;
};

RunOutcome runNdvi( const QString &input, const QString &output, const QString &workDir )
{
    RsSpectralIndexOperator op;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 1;
    params["red"] = 2;

    sicnu::operators::RSOperatorContext ctx( workDir.toStdString() );
    RunOutcome outcome;
    try
    {
        outcome.result = op.run( params, ctx );
        outcome.ok = true;
    }
    catch ( const std::exception &e )
    {
        CAPTURE( e.what() );
    }
    return outcome;
}

/// One NDVI pixel read back from the output raster (float NaN-safe).
float readOutputPixel( const QString &path, int row, int col )
{
    ensureGdalInit();
    GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_READONLY, nullptr,
                                  nullptr, nullptr );
    REQUIRE( ds != nullptr );
    float value = std::numeric_limits<float>::quiet_NaN();
    REQUIRE( GDALGetRasterCount( ds ) == 1 );
    CPLErr rc = GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, col, row, 1, 1,
                              &value, 1, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    REQUIRE( rc == CE_None );
    return value;
}

std::string outputIdentityStamp( const QString &path )
{
    ensureGdalInit();
    GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(), GDAL_OF_READONLY, nullptr,
                                  nullptr, nullptr );
    if ( !ds )
        return {};
    const char *stamp = GDALGetMetadataItem( ds, "SICNU_SPECTRAL_IDENTITY", nullptr );
    const std::string out = stamp ? stamp : "";
    GDALClose( ds );
    return out;
}

} // namespace

TEST_CASE( "Published claim is backed by a current artifact (D-T05-CHAIN1)",
           "[r29][t02][published-claim]" )
{
    QTemporaryDir dir;
    const QString input = dir.filePath( "in.tif" );
    const QString output = dir.filePath( "ndvi.tif" );
    const QString workDir = dir.filePath( "scratch" );
    writeTwoBandRaster( input );

    // --- H-A: first run writes the artifact and the identity stamp.
    {
        const RunOutcome first = runNdvi( input, output, workDir );
        REQUIRE( first.ok );
        REQUIRE( first.result["already_published"].asBool() == false );
        REQUIRE( first.result["tiles_computed"].asUInt64() > 0 );
        REQUIRE( QFileInfo::exists( output ) );
        REQUIRE( !outputIdentityStamp( output ).empty() );
        // Closed-form oracle at three sample points.
        for ( const auto [r, c] : { std::pair { 0, 0 }, std::pair { 23, 31 },
                                    std::pair { kHeight - 1, kWidth - 1 } } )
        {
            REQUIRE( readOutputPixel( output, r, c )
                     == Catch::Approx( expectedNdvi( r, c ) ).epsilon( 1e-6 ) );
        }
    }

    // --- H-B: an untouched rerun is an honest cache hit.
    const QDateTime beforeHit = QFileInfo( output ).lastModified();
    {
        const RunOutcome second = runNdvi( input, output, workDir );
        REQUIRE( second.ok );
        REQUIRE( second.result["already_published"].asBool() == true );
        REQUIRE( second.result["tiles_computed"].asUInt64() == 0 );
        REQUIRE( QFileInfo( output ).lastModified() == beforeHit );
    }

    // --- H-C (D-T05-CHAIN1 RED): the artifact is deleted but the marker
    // remains — the run must REBUILD, not report a zero-work green.
    {
        REQUIRE( QFile::remove( output ) );
        const RunOutcome third = runNdvi( input, output, workDir );
        REQUIRE( third.ok );
        REQUIRE( third.result["already_published"].asBool() == false );
        REQUIRE( third.result["tiles_computed"].asUInt64() > 0 );
        REQUIRE( QFileInfo::exists( output ) );
        REQUIRE( readOutputPixel( output, 23, 31 )
                 == Catch::Approx( expectedNdvi( 23, 31 ) ).epsilon( 1e-6 ) );
    }

    // --- replaced artifact: a foreign raster at the same path (no stamp)
    // must not be served as this run's cached output.
    {
        const QString foreign = dir.filePath( "foreign.tif" );
        writeTwoBandRaster( foreign, /*seed=*/99 );
        REQUIRE( QFile::remove( output ) );
        REQUIRE( QFile::copy( foreign, output ) );
        REQUIRE( outputIdentityStamp( output ).empty() );
        const RunOutcome fourth = runNdvi( input, output, workDir );
        REQUIRE( fourth.ok );
        REQUIRE( fourth.result["already_published"].asBool() == false );
        // The rebuilt artifact is THIS parameterization's NDVI again.
        REQUIRE( readOutputPixel( output, 23, 31 )
                 == Catch::Approx( expectedNdvi( 23, 31 ) ).epsilon( 1e-6 ) );
        REQUIRE( !outputIdentityStamp( output ).empty() );
    }

    // --- H-D: input CONTENT replaced at the same path drifts the run
    // identity (size/mtime revision token) and forces a recompute whose
    // output matches the NEW content.
    {
        const std::string before = outputIdentityStamp( output );
        writeTwoBandRaster( input, /*seed=*/1 ); // same path, new bytes
        const RunOutcome fifth = runNdvi( input, output, workDir );
        REQUIRE( fifth.ok );
        REQUIRE( fifth.result["already_published"].asBool() == false );
        REQUIRE( fifth.result["tiles_computed"].asUInt64() > 0 );
        REQUIRE( readOutputPixel( output, 23, 31 )
                 == Catch::Approx( expectedNdvi( 23, 31, /*seed=*/1 ) ).epsilon( 1e-6 ) );
        // Different input content ⇒ different identity stamp.
        REQUIRE( outputIdentityStamp( output ) != before );
    }

    // --- H-E (cross-workdir): a DIFFERENT workdir re-running the SAME
    // input/output pair has its own state; it must not fabricate a hit from
    // another workspace's marker, and after H-D the artifact at this path is
    // current — the fresh workdir re-executes and still yields correct bytes.
    {
        const QString otherWorkDir = dir.filePath( "scratch2" );
        const RunOutcome sixth = runNdvi( input, output, otherWorkDir );
        REQUIRE( sixth.ok );
        REQUIRE( QFileInfo::exists( output ) );
        REQUIRE( readOutputPixel( output, 0, 0 )
                 == Catch::Approx( expectedNdvi( 0, 0, /*seed=*/1 ) ).epsilon( 1e-6 ) );
    }
}

TEST_CASE( "ResumableTileRun R8: an unverifiable marker demotes to a rebuild "
           "and keeps committed tiles reusable",
           "[r29][t02][published-claim][chunk]" )
{
    using namespace sicnu::runtime::chunk;
    const std::filesystem::path base =
        std::filesystem::temp_directory_path()
        / ( "sicnu-r29-t02-r8-" + std::to_string( ::getpid() ) );
    std::error_code ec;
    std::filesystem::remove_all( base, ec );
    std::filesystem::create_directories( base, ec );

    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xA29A02A29A02A29Aull;
    spec.identity.inputIdentity = 0xB29B02B29B02B29Bull;
    spec.partition.rasterWidth = 6;
    spec.partition.rasterHeight = 4;
    spec.partition.tileWidth = 3;
    spec.partition.tileHeight = 2;
    spec.partition.bands = 1;
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

    const std::string statePath = ( base / "run" ).generic_string();
    const std::string artifact = ( base / "artifact.bin" ).generic_string();

    auto makeCallbacks = [&artifact]( int *computes, int *publishes,
                                      bool *artifactOnDisk ) {
        ResumableTileRun::Callbacks cb;
        cb.compute = [computes]( const TileSpec &s ) {
            ++( *computes );
            auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount(), 1.f );
            return TilePayload{ s, std::move( buf ) };
        };
        cb.consume = [&artifact]( const TilePayload & ) {
            // Idempotent rebuild: consume rewrites the artifact from tiles.
            QFile out( QString::fromStdString( artifact ) );
            ( void )out.open( QIODevice::WriteOnly | QIODevice::Append );
            out.write( "tile" );
        };
        cb.publish = [publishes, artifactOnDisk]() {
            ++( *publishes );
            *artifactOnDisk = true;
        };
        cb.publishedOutputValid = [artifactOnDisk]( std::string &reason ) {
            if ( !*artifactOnDisk )
            {
                reason = "artifact deleted after publication";
                return false;
            }
            return true;
        };
        return cb;
    };

    TileRunCancelSource noCancel;
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = base.generic_string();
    cfg.statePath = statePath;

    int computes = 0, publishes = 0;
    bool artifactOnDisk = false;

    // Run 1: full compute → publish → marker.
    {
        ResumableTileRun run( spec, cfg );
        const auto r = run.execute( noCancel, makeCallbacks( &computes, &publishes,
                                                              &artifactOnDisk ) );
        REQUIRE( r.tilesComputed == 4 );
        REQUIRE( !r.alreadyPublished );
    }
    // Keep the tile scratch (no cleanupAfterPublish) so the demoted rebuild
    // can prove R2 reuse: journal + tiles survive, only the claim is stale.
    REQUIRE( computes == 4 );
    REQUIRE( publishes == 1 );

    // Run 2: marker hit with a valid artifact → zero work.
    {
        ResumableTileRun run( spec, cfg );
        const auto r = run.execute( noCancel, makeCallbacks( &computes, &publishes,
                                                              &artifactOnDisk ) );
        REQUIRE( r.alreadyPublished );
        REQUIRE( r.tilesComputed == 0 );
        REQUIRE( computes == 4 );
    }

    // Run 3: the artifact disappears → the marker demotes; committed tiles
    // are REUSED (no recompute), consume+publish rebuild the artifact.
    artifactOnDisk = false;
    {
        ResumableTileRun run( spec, cfg );
        const auto r = run.execute( noCancel, makeCallbacks( &computes, &publishes,
                                                              &artifactOnDisk ) );
        REQUIRE( !r.alreadyPublished );
        REQUIRE( r.tilesReused == 4 );
        REQUIRE( r.tilesComputed == 0 );
        REQUIRE( computes == 4 ); // R2: not one extra kernel call
        REQUIRE( publishes == 2 );
        REQUIRE( artifactOnDisk );
    }

    std::filesystem::remove_all( base, ec );
}
