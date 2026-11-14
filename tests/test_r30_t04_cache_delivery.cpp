// tests/test_r30_t04_cache_delivery.cpp — R30-T04 WP04/WP05 (D3).
//
// A chunked run whose PUBLISHED marker hits must restore the COMPLETE
// delivery state, not just the file: the asset reference is reconciled with
// the catalog (same catalog → same Asset ID, catalog untouched; new catalog
// after restart → fresh registration from the standing file), and the
// result's quality summary must say what its counters actually cover
// (cache_hit = not measured ≠ zero non-finite pixels).
//
// Oracle independence: the expected non-finite count is recomputed here from
// the same fixture (NDVI denominator (nir+red)==0 → 0/0 → NaN); the
// operator's numbers are compared against that closed form.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <cmath>
#include <string>
#include <vector>

#include "data/data_manager.h"
#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::operators::rs::spectral_index_detail::runSpectralIndexCore;

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// Two-band Float32 fixture (band 1 = NIR, band 2 = Red). Pixels where
// (row % 7 == 0 && col % 5 == 0) get BOTH bands zero → the NDVI denominator
// (nir + red) is 0 → 0/0 = NaN in the output. Everything else is a smooth
// nonzero gradient.
long long writeTwoBandRasterWithZeroSums( const QString &path )
{
    ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 2,
                                   GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    long long zeroSumPixels = 0;
    std::vector<float> nir( static_cast<size_t>( kWidth ) * kHeight );
    std::vector<float> red( static_cast<size_t>( kWidth ) * kHeight );
    for ( int r = 0; r < kHeight; ++r )
        for ( int c = 0; c < kWidth; ++c )
        {
            const bool zeroSum = ( r % 7 == 0 && c % 5 == 0 );
            if ( zeroSum )
                ++zeroSumPixels;
            const size_t i = static_cast<size_t>( r ) * kWidth + c;
            nir[i] = zeroSum ? 0.f : 2000.f + 40.f * ( ( r + c ) % 64 );
            red[i] = zeroSum ? 0.f : 1000.f + 30.f * ( ( r * 2 + c ) % 64 );
        }
    GDALRasterBand *b1 = ds->GetRasterBand( 1 );
    GDALRasterBand *b2 = ds->GetRasterBand( 2 );
    REQUIRE( b1->RasterIO( GF_Write, 0, 0, kWidth, kHeight, nir.data(), kWidth,
                           kHeight, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( b2->RasterIO( GF_Write, 0, 0, kWidth, kHeight, red.data(), kWidth,
                           kHeight, GDT_Float32, 0, 0 ) == CE_None );
    double gt[6] = { 0, 1, 0, static_cast<double>( kHeight ), 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
    return zeroSumPixels;
}

sicnu::operators::ChunkedRunOptions makeDeliveryOpts(
    sicnu::OutputCommitter *committer, const QString &tempPath,
    const QString &stablePath )
{
    sicnu::operators::ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = committer;
    commitSpec.tempPath = tempPath;
    commitSpec.stablePath = stablePath;
    commitSpec.kind = sicnu::data::AssetKind::Raster;
    commitSpec.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    commitSpec.autoLoad = false;
    sicnu::data::DerivationRecord derivation;
    derivation.algorithmId = QStringLiteral( "rs:ndvi" );
    derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    commitSpec.derivation = derivation;

    sicnu::operators::ChunkedRunOptions opts;
    opts.commitSpec = commitSpec;
    return opts;
}

Json::Value runNdviDelivery( const QString &input, const QString &output,
                             const QString &workDir,
                             const sicnu::operators::ChunkedRunOptions &opts )
{
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 1;
    params["red"] = 2;
    sicnu::operators::RSOperatorContext ctx( workDir.toStdString() );
    return runSpectralIndexCore( "NDVI", params, ctx, /*allowIndexOverride=*/false,
                                 "rs:ndvi", opts );
}

// A foreign two-band raster (no SICNU_SPECTRAL_IDENTITY stamp) used to
// overwrite the stable target in the covered-target scenario.
void writeForeignRaster( const QString &path )
{
    ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), 16, 16, 2, GDT_Float32,
                                   nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
}

} // namespace

TEST_CASE( "Cache hit restores the full delivery identity and honest stats coverage",
           "[r30][t04][cache_delivery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString input = dir.filePath( "in.tif" );
    const QString tempOut = dir.filePath( "ndvi_temp.tif" );
    const QString stableOut = dir.filePath( "ndvi_stable.tif" );
    const QString workDir = dir.filePath( "scratch" );

    const long long expectedNonFinite = writeTwoBandRasterWithZeroSums( input );
    REQUIRE( expectedNonFinite > 0 );
    const double expectedFiniteRatio =
        1.0 - static_cast<double>( expectedNonFinite )
                  / static_cast<double>( kWidth * kHeight );

    // --- Run 1 (fresh): full stats coverage + committed asset reference.
    sicnu::data::DataManager catalogA;
    sicnu::OutputCommitter committerA( &catalogA );
    const auto optsA = makeDeliveryOpts( &committerA, tempOut, stableOut );

    Json::Value r1 = runNdviDelivery( input, stableOut, workDir, optsA );
    REQUIRE( r1["already_published"].asBool() == false );
    REQUIRE( r1["tiles_computed"].asUInt64() > 0 );
    REQUIRE( r1.isMember( "published_asset_id" ) );
    const std::string assetA = r1["published_asset_id"].asString();
    REQUIRE( !assetA.empty() );
    CHECK( r1["stats_coverage"].asString() == "full" );
    CHECK( r1["nonfinite_output_pixels"].asInt64() == expectedNonFinite );
    CHECK( r1["valid_pixel_ratio"].asDouble()
           == Catch::Approx( expectedFiniteRatio ).epsilon( 1e-9 ) );

    const quint64 generationAfterRun1 = catalogA.catalogGeneration();

    // --- Run 2 (same catalog): honest cache hit — SAME asset ID, catalog
    // untouched (no refresh, no revision bump: the identity stamp already
    // proved the bytes are current), and UNKNOWN statistics are absent, not
    // fabricated as zeros.
    Json::Value r2 = runNdviDelivery( input, stableOut, workDir, optsA );
    REQUIRE( r2["already_published"].asBool() == true );
    REQUIRE( r2["tiles_computed"].asUInt64() == 0 );
    REQUIRE( r2.isMember( "published_asset_id" ) );
    CHECK( r2["published_asset_id"].asString() == assetA );
    CHECK( catalogA.catalogGeneration() == generationAfterRun1 );
    CHECK( r2["stats_coverage"].asString() == "cache_hit" );
    CHECK_FALSE( r2.isMember( "nonfinite_output_pixels" ) );
    CHECK_FALSE( r2.isMember( "valid_pixel_ratio" ) );
    CHECK_FALSE( r2.isMember( "nodata_pixel_ratio" ) );

    // --- Run 3 (new catalog after restart, same files/workdir): the marker
    // still hits, the file still stands — the reconcile REGISTERS the
    // standing artifact so the fresh catalog carries the same complete
    // delivery state (a brand-new Asset ID, findable by path).
    sicnu::data::DataManager catalogB;
    sicnu::OutputCommitter committerB( &catalogB );
    const auto optsB = makeDeliveryOpts( &committerB, tempOut, stableOut );

    Json::Value r3 = runNdviDelivery( input, stableOut, workDir, optsB );
    REQUIRE( r3["already_published"].asBool() == true );
    REQUIRE( r3.isMember( "published_asset_id" ) );
    const std::string assetB = r3["published_asset_id"].asString();
    REQUIRE( !assetB.empty() );
    CHECK( assetB != assetA ); // fresh catalog → fresh registration
    const auto foundInB = catalogB.findByPath( stableOut );
    REQUIRE( foundInB.has_value() );
    CHECK( foundInB->id().toString().toStdString() == assetB );
    CHECK( r3["stats_coverage"].asString() == "cache_hit" );

    // --- Run 4 (another task overwrote the target): the identity stamp no
    // longer matches → the hit DEMOTES, the run rebuilds and COMMITS again
    // (publish-then-register over the same stable path: catalog A dedups by
    // path and refreshes, per #687). Full stats coverage returns with the
    // same closed-form expectations.
    const QString foreign = dir.filePath( "foreign.tif" );
    writeForeignRaster( foreign );
    REQUIRE( QFile::remove( stableOut ) );
    REQUIRE( QFile::copy( foreign, stableOut ) );

    Json::Value r4 = runNdviDelivery( input, stableOut, workDir, optsA );
    REQUIRE( r4["already_published"].asBool() == false );
    REQUIRE( r4["tiles_computed"].asUInt64() > 0 );
    REQUIRE( r4.isMember( "published_asset_id" ) );
    CHECK( r4["published_asset_id"].asString() == assetA ); // same path dedup
    CHECK( catalogA.catalogGeneration() > generationAfterRun1 );
    CHECK( r4["stats_coverage"].asString() == "full" );
    CHECK( r4["nonfinite_output_pixels"].asInt64() == expectedNonFinite );
}
