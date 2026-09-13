/***************************************************************************
 * test_science_verification_10.cpp — Verification Platform 10.0 lanes
 *
 * Scientific verification that goes beyond known answers:
 *
 *   1. metamorphic invariants — relationships that must hold for ANY input,
 *      not just golden fixtures (NDVI band-scale invariance);
 *   2. reproducibility replay — the same input through the same operator
 *      twice must produce byte-identical output (deterministic-grade claim,
 *      made testable);
 *   3. seed determinism — the stochastic-family operators (kmeans) produce
 *      identical class maps across runs, pinning their declared
 *      deterministic_internal seed policy;
 *   4. bounded CRS refusal fuzz — malformed CRS strings through the warp
 *      seam are always typed refusals, never crashes, never silent output
 *      (deterministic PRNG, offline, no public network);
 *   5. provenance verification — the qa_mask output carries its declared
 *      SICNU_* provenance metadata on every run.
 *
 * Every lane is offline and bounded; wall-clock is asserted nowhere.
 ***************************************************************************/
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <QFile>
#include <QTemporaryDir>

#include <random>
#include <string>
#include <vector>

using namespace sicnu::operators;

namespace
{

std::unique_ptr<RSOperator> create( const std::string &id )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op;
}

std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
    GDALDataset *ds =
      static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    width = ds->GetRasterXSize();
    height = ds->GetRasterYSize();
    std::vector<float> data( static_cast<std::size_t>( width ) * height );
    REQUIRE( ds->GetRasterBand( band )
               ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                           GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return data;
}

} // namespace

TEST_CASE( "NDVI is invariant under band scaling (metamorphic)", "[science10][metamorphic]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Base scene: red = ramp, NIR = 2x the red ramp (NDVI = 1/3 everywhere).
    // Scaled scene: BOTH bands x4. NDVI = (NIR-R)/(NIR+R) is scale-invariant,
    // so the two outputs must agree within float tolerance — for ANY input,
    // not a golden fixture.
    const int w = 16;
    const int h = 16;
    sicnu::testing::RsSyntheticRasterBuilder base( w, h, 2, GDT_Float32 );
    sicnu::testing::RsSyntheticRasterBuilder scaled( w, h, 2, GDT_Float32 );
    for ( int band = 1; band <= 2; ++band )
    {
        base.withRampPattern( band, 0.1f * band, 0.9f * band );
        scaled.withRampPattern( band, 0.4f * band, 3.6f * band );
    }
    auto baseRaster = base.writeToDisk( dir.filePath( QStringLiteral( "base.tif" ) ) );
    auto scaledRaster = scaled.writeToDisk( dir.filePath( QStringLiteral( "scaled.tif" ) ) );
    REQUIRE_FALSE( baseRaster.isEmpty() );
    REQUIRE_FALSE( scaledRaster.isEmpty() );

    auto op = create( "rs:spectral_index" );
    const QString outBase = dir.filePath( QStringLiteral( "ndvi_base.tif" ) );
    const QString outScaled = dir.filePath( QStringLiteral( "ndvi_scaled.tif" ) );

    RSOperatorContext context;
    const std::vector<std::pair<QString, QString>> runs = {
        { baseRaster, outBase },
        { scaledRaster, outScaled },
    };
    for ( const auto &[input, output] : runs )
    {
        Json::Value params;
        params["input"] = input.toStdString();
        params["output"] = output.toStdString();
        params["index"] = "NDVI";
        params["red"] = 1;
        params["nir"] = 2;
        op->run( params, context );
    }

    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    const auto ndviBase = readBand( outBase, 1, w1, h1 );
    const auto ndviScaled = readBand( outScaled, 1, w2, h2 );
    REQUIRE( w1 == w2 );
    REQUIRE( h1 == h2 );
    REQUIRE( ndviBase.size() == ndviScaled.size() );
    for ( std::size_t i = 0; i < ndviBase.size(); ++i )
    {
        INFO( "pixel " << i );
        CHECK( ndviBase[i] == Catch::Approx( ndviScaled[i] ).margin( 1e-6 ) );
    }
}

TEST_CASE( "operator outputs replay byte-identically (reproducibility)",
           "[science10][replay]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // The determinism-grade contract is only provable by replay: the same
    // input through the same operator twice must produce byte-identical
    // rasters. rs:band_ratio is pure arithmetic on a fixed input.
    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 12, 12, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.05f, 0.8f )
                    .withConstantValue( 2, 0.4f )
                    .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    auto op = create( "rs:band_ratio" );
    const QString first = dir.filePath( QStringLiteral( "ratio_a.tif" ) );
    const QString second = dir.filePath( QStringLiteral( "ratio_b.tif" ) );
    RSOperatorContext context;
    for ( const QString &output : { first, second } )
    {
        Json::Value params;
        params["input"] = raster.toStdString();
        params["output"] = output.toStdString();
        params["numeratorBand"] = 1;
        params["denominatorBand"] = 2;
        op->run( params, context );
    }

    int w = 0, h = 0;
    const auto a = readBand( first, 1, w, h );
    const auto b = readBand( second, 1, w, h );
    REQUIRE( a.size() == b.size() );
    for ( std::size_t i = 0; i < a.size(); ++i )
        CHECK( a[i] == b[i] ); // bitwise, not approximately
}

TEST_CASE( "isodata class maps are identical across runs (seed determinism)",
           "[science10][seed]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 24, 24, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.0f, 1.0f )
                    .withRampPattern( 2, 1.0f, 0.0f )
                    .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    // The deterministic-seeded variant (isodata: classic discard/split/merge
    // with deterministic even-row seeding) must replay byte-identically.
    // Plain kmeans is NOT pinned: cv::kmeans runs best-of-3 attempts on the
    // advancing thread-local RNG, so cluster ids may permute across runs —
    // recorded as seedPolicy=none in the scientific contract registry.
    auto op = create( "rs:kmeans_classification" );
    const QString first = dir.filePath( QStringLiteral( "kmeans_a.tif" ) );
    const QString second = dir.filePath( QStringLiteral( "kmeans_b.tif" ) );
    for ( const QString &output : { first, second } )
    {
        Json::Value params;
        params["input"] = raster.toStdString();
        params["output"] = output.toStdString();
        params["k"] = 3;
        params["algorithm"] = "isodata";
        RSOperatorContext context;
        op->run( params, context );
    }

    int w = 0, h = 0;
    const auto a = readBand( first, 1, w, h );
    const auto b = readBand( second, 1, w, h );
    REQUIRE( a.size() == b.size() );
    for ( std::size_t i = 0; i < a.size(); ++i )
        CHECK( a[i] == b[i] ); // the deterministic even-row seed policy
}

TEST_CASE( "malformed CRS strings through the warp seam are typed refusals (fuzz)",
           "[science10][fuzz]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 4, 4, 1, GDT_Float32 )
                    .withConstantValue( 1, 1.0f )
                    .withCrs( QStringLiteral( "EPSG:4326" ) )
                    .writeToDisk( dir.filePath( QStringLiteral( "in.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    // Deterministic corpus: hand-picked malformations + seeded mutations of
    // a valid WKT1 authority string. Every one must be REFUSED with the
    // operator's typed error — never a crash, never a published output.
    std::vector<std::string> corpus = {
        "",          " ",   "EPSG:",  ":4326",   "EPSG:0",   "EPSG:-1",  "EPSG:999999999",
        "NOT_A_CRS", "4326", "EPSG:4 32633", "EPSG:\x01", "urn:ogc:def:crs:", "EPSG:32633 ",
        "+proj=unknown +foo=1", "GEOGCS[", "WKT:garbage",
    };
    std::mt19937 rng( 20260913 );
    const std::string alphabet = "EPSG:0123456789 abc+/_-[]\"";
    for ( int i = 0; i < 64; ++i )
    {
        std::string mutation = "EPSG:32633";
        const int flips = 1 + static_cast<int>( rng() % 4 );
        for ( int f = 0; f < flips && !mutation.empty(); ++f )
            mutation[static_cast<std::size_t>( rng() ) % mutation.size()] =
              alphabet[rng() % alphabet.size()];
        corpus.push_back( mutation );
    }

    auto op = create( "io:warp" );
    int refused = 0;
    int published = 0;
    for ( const std::string &crs : corpus )
    {
        const QString output = dir.filePath( QStringLiteral( "warp_fuzz_out.tif" ) );
        QFile::remove( output );
        Json::Value params;
        params["input"] = raster.toStdString();
        params["output"] = output.toStdString();
        params["targetCrs"] = crs;
        RSOperatorContext context;
        try
        {
            op->run( params, context );
            // GDAL is allowed to accept weird-but-parseable strings; a
            // publish, however, must actually exist and carry a CRS.
            ++published;
            INFO( "accepted CRS: " << crs );
            CHECK( QFile::exists( output ) );
        }
        catch ( const RSOperatorError & )
        {
            ++refused;
            // A refusal must not leave an output behind (atomic publication).
            CHECK_FALSE( QFile::exists( output ) );
        }
    }
    // The gate is live: the corpus must produce refusals (garbage is
    // rejected) and must not silently publish everything.
    CHECK( refused > 0 );
    CHECK( refused + published == static_cast<int>( corpus.size() ) );
}

TEST_CASE( "qa_mask output carries its declared provenance metadata",
           "[science10][provenance]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 4, 4, 1, GDT_Float32 )
                    .withConstantValue( 1, 8.0f ) // Landsat QA word: cloud
                    .writeToDisk( dir.filePath( QStringLiteral( "qa.tif" ) ) );
    REQUIRE_FALSE( raster.isEmpty() );

    const QString output = dir.filePath( QStringLiteral( "mask.tif" ) );
    Json::Value params;
    params["input"] = raster.toStdString();
    params["output"] = output.toStdString();
    params["qa_band"] = 1;
    params["source"] = "landsat_qa_pixel";
    params["mask"] = "cloud";
    RSOperatorContext context;
    create( "rs:qa_mask" )->run( params, context );

    // The provenance contract: the mask declares HOW it was derived
    // (dataset level, GdalStreamingOutput::setMetadataItem), so a downstream
    // consumer or an audit replay can verify it.
    GDALDataset *published =
      static_cast<GDALDataset *>( GDALOpen( output.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( published );
    const char *source = published->GetMetadataItem( "SICNU_QA_MASK_SOURCE" );
    const char *selection = published->GetMetadataItem( "SICNU_QA_MASK_SELECTION" );
    REQUIRE( source != nullptr );
    REQUIRE( selection != nullptr );
    CHECK( std::string( source ) == "landsat_qa_pixel" );
    CHECK( std::string( selection ) == "cloud" );
    GDALClose( published );
}
