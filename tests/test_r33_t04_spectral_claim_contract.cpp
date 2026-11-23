// tests/test_r33_t04_spectral_claim_contract.cpp — R33-T04 / WP10.
//
// The rs:spectral_index published-claim verifier proved geometry + identity
// stamp only — a foreign artifact with the SAME geometry carrying a COPIED
// identity stamp but a different dtype / CRS / affine could still be served
// as this run's product (zero-work success over bytes this run never
// wrote). The claim now proves the full data contract this run would
// produce: Float32, the input's CRS, and the complete affine (rotation and
// pixel-size terms included).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <json/json.h>

#include <string>
#include <vector>

#include "operators/framework/rs_operator_context.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::operators::rs::RsSpectralIndexOperator;

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 48;

void ensureGdal()
{
    GDALAllRegister();
}

void writeTwoBandRaster( const QString &path )
{
    ensureGdal();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 2,
                                   GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<std::size_t>( kWidth ) * kHeight );
    for ( int b = 1; b <= 2; ++b )
    {
        for ( int r = 0; r < kHeight; ++r )
            for ( int c = 0; c < kWidth; ++c )
                band[static_cast<std::size_t>( r ) * kWidth + c] =
                    b == 1 ? 2000.f + 40.f * ( ( r + c ) % 64 )
                           : 1000.f + 30.f * ( ( r * 2 + c ) % 64 );
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, kWidth, kHeight, band.data(), kWidth, kHeight,
                      GDT_Float32, 0, 0 );
    }
    double gt[6] = { 0, 1, 0, static_cast<double>( kHeight ), 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
}

Json::Value runNdvi( const QString &input, const QString &output, const QString &workDir )
{
    RsSpectralIndexOperator op;
    sicnu::operators::RSOperatorContext ctx( workDir.toStdString() );
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 1;
    params["red"] = 2;
    return op.run( params, ctx );
}

std::string identityStamp( const QString &path )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
        return {};
    const char *stamp = GDALGetMetadataItem( ds, "SICNU_SPECTRAL_IDENTITY", nullptr );
    const std::string out = stamp ? stamp : "";
    GDALClose( ds );
    return out;
}

/// Forge a same-geometry artifact that carries the REAL run's identity
/// stamp but a DIFFERENT data contract (Byte dtype / foreign affine).
void forgeForeignArtifact( const QString &path, const std::string &stamp,
                           GDALDataType dtype, const double *gt )
{
    ensureGdal();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), kWidth, kHeight, 1, dtype,
                                   nullptr );
    REQUIRE( ds != nullptr );
    if ( gt )
        ds->SetGeoTransform( const_cast<double *>( gt ) );
    const CPLErr written = ds->GetRasterBand( 1 )->Fill( 7 );
    GDALSetMetadataItem( ds, "SICNU_SPECTRAL_IDENTITY", stamp.c_str(), nullptr );
    GDALClose( ds );
    REQUIRE( written == CE_None );
}
} // namespace

TEST_CASE( "a forged same-geometry artifact with a copied stamp never claims zero-work",
           "[operators][spectral][r33t04]" )
{
    QTemporaryDir dir;
    const QString input = dir.filePath( "in.tif" );
    const QString output = dir.filePath( "ndvi.tif" );
    const QString workDir = dir.filePath( "scratch" );
    writeTwoBandRaster( input );

    // Baseline: the run publishes with its identity stamp.
    {
        const Json::Value first = runNdvi( input, output, workDir );
        REQUIRE( first["already_published"].asBool() == false );
        REQUIRE( !identityStamp( output ).empty() );
        // Untouched rerun is the honest zero-work hit.
        const Json::Value second = runNdvi( input, output, workDir );
        REQUIRE( second["already_published"].asBool() == true );
    }

    SECTION( "foreign dtype with a copied stamp is rebuilt" )
    {
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        forgeForeignArtifact( output, stamp, GDT_Byte, nullptr );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        // The rebuilt artifact is this run's REAL product again.
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds != nullptr );
        CHECK( GDALGetRasterDataType( GDALGetRasterBand( ds, 1 ) ) == GDT_Float32 );
        GDALClose( ds );
    }

    SECTION( "rotated/foreign affine with a copied stamp is rebuilt" )
    {
        const std::string stamp = identityStamp( output );
        REQUIRE( QFile::remove( output ) );
        const double rotated[6] = { 0, 0.5, 0.25, static_cast<double>( kHeight ), 0.1, -0.5 };
        forgeForeignArtifact( output, stamp, GDT_Float32, rotated );

        const Json::Value res = runNdvi( input, output, workDir );
        REQUIRE( res["already_published"].asBool() == false ); // rebuilt
        GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
        REQUIRE( ds != nullptr );
        double gt[6] = { 0, 0, 0, 0, 0, 0 };
        GDALGetGeoTransform( ds, gt );
        CHECK( gt[2] == 0.0 ); // rotation term restored to the input's axis-aligned grid
        GDALClose( ds );
    }
}
