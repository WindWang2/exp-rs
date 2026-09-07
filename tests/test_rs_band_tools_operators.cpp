/***************************************************************************
 * test_rs_band_tools_operators.cpp — rs:band_ratio / rs:extract_bands /
 * rs:contrast_stretch (Desktop Workbench UX 4.0 thin-client migration).
 *
 * Pins the promoted kernels: outputs must match the legacy dialog lambdas'
 * semantics (ratio = ImageEnhancement::bandRatio; extract preserves order;
 * stretch is the streaming replica of the dialog stretch) and the operator
 * parameter contract must reject invalid band selections with typed errors.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/rs/rs_band_tools_operators.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <cmath>
#include <vector>

using sicnu::operators::RSOperator;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

namespace {

QCoreApplication &testApp()
{
    static int argc = 0;
    static QCoreApplication app( argc, nullptr );
    return app;
}

struct RasterFixture
{
    QTemporaryDir dir;
    QString path;

    RasterFixture()
    {
        testApp();
        GDALAllRegister();
        path = QDir( dir.path() ).filePath( QStringLiteral( "in.tif" ) );
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        constexpr int W = 4, H = 4, B = 3;
        GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, B, GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        double gt[6] = { 0.0, 1.0, 0.0, ( double ) H, 0.0, -1.0 };
        GDALSetGeoTransform( ds, gt );
        // band1 = 10, 20, 30 ... band2 = 2, 4, 6 ... band3 = 1 everywhere
        const std::vector<std::vector<float>> bands = {
            { 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40 },
            { 2, 4, 6, 8, 2, 4, 6, 8, 2, 4, 6, 8, 2, 4, 6, 8 },
            { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        };
        for ( int b = 1; b <= B; ++b )
        {
            GDALRasterBandH band = GDALGetRasterBand( ds, b );
            REQUIRE( band != nullptr );
            REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, W, H,
                                   const_cast<float *>( bands[b - 1].data() ), W, H,
                                   GDT_Float32, 0, 0 ) == CE_None );
        }
        GDALClose( ds );
    }
};

std::vector<float> readBand( const QString &path, int band, int *outBands = nullptr )
{
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    if ( outBands )
        *outBands = GDALGetRasterCount( ds );
    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    std::vector<float> buf( static_cast<size_t>( w ) * h, -999.f );
    GDALRasterBandH b = GDALGetRasterBand( ds, band );
    REQUIRE( b != nullptr );
    REQUIRE( GDALRasterIO( b, GF_Read, 0, 0, w, h, buf.data(), w, h, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return buf;
}

Json::Value runOp( RSOperator &op, const Json::Value &params )
{
    RSOperatorContext ctx;
    return op.run( params, ctx );
}

} // namespace

TEST_CASE( "rs:band_ratio ratio mode divides the selected bands", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsBandRatioOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "ratio.tif" ) ).toStdString();
    params["mode"] = "ratio";
    params["numeratorBand"] = 1;
    params["denominatorBand"] = 2;

    const Json::Value result = runOp( op, params );
    REQUIRE( result["output"].asString() == params["output"].asString() );

    const std::vector<float> out = readBand( QString::fromStdString( result["output"].asString() ), 1 );
    REQUIRE( out.size() == 16 );
    REQUIRE( out[0] == Catch::Approx( 5.0 ).margin( 1e-6 ) );
    REQUIRE( out[3] == Catch::Approx( 5.0 ).margin( 1e-6 ) );
}

TEST_CASE( "rs:band_ratio ihs mode emits three components", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsBandRatioOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "ihs.tif" ) ).toStdString();
    params["mode"] = "ihs";
    params["redBand"] = 1;
    params["greenBand"] = 2;
    params["blueBand"] = 3;

    const Json::Value result = runOp( op, params );
    REQUIRE( result["bands"].asInt() == 3 );

    int bands = 0;
    const std::vector<float> intensity =
        readBand( QString::fromStdString( result["output"].asString() ), 1, &bands );
    REQUIRE( bands == 3 );
    // Raw-DN inputs: components stay finite and non-negative (the kernel
    // does not normalize; panel #380 semantics preserve intensity scale).
    for ( float v : intensity )
    {
        REQUIRE( std::isfinite( v ) );
        REQUIRE( v >= -1e-6 );
    }
}

TEST_CASE( "rs:band_ratio rejects identical numerator/denominator bands", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsBandRatioOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "bad.tif" ) ).toStdString();
    params["mode"] = "ratio";
    params["numeratorBand"] = 2;
    params["denominatorBand"] = 2;

    REQUIRE_THROWS_AS( runOp( op, params ), RSOperatorError );
}

TEST_CASE( "rs:extract_bands preserves order and count", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsExtractBandsOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "extract.tif" ) ).toStdString();
    params["bands"] = Json::Value( Json::arrayValue );
    params["bands"].append( 2 );
    params["bands"].append( 1 );

    const Json::Value result = runOp( op, params );
    REQUIRE( result["bands"].asInt() == 2 );

    int bands = 0;
    const QString outPath = QString::fromStdString( result["output"].asString() );
    const std::vector<float> first = readBand( outPath, 1, &bands );
    REQUIRE( bands == 2 );
    // Output plane 1 is source band 2 (2, 4, 6, 8 pattern).
    REQUIRE( first[0] == 2.0f );
    REQUIRE( first[1] == 4.0f );
    // Output plane 2 is source band 1 (10, 20, 30, 40 pattern).
    const std::vector<float> second = readBand( outPath, 2 );
    REQUIRE( second[0] == 10.0f );
    REQUIRE( second[1] == 20.0f );
}

TEST_CASE( "rs:extract_bands rejects an empty band list", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsExtractBandsOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "empty.tif" ) ).toStdString();
    params["bands"] = Json::Value( Json::arrayValue );

    REQUIRE_THROWS_AS( runOp( op, params ), RSOperatorError );
}

TEST_CASE( "rs:contrast_stretch linear mode spans the data range", "[ux4][operators][band-tools]" )
{
    RasterFixture fx;
    sicnu::operators::rs::RsContrastStretchOperator op;

    Json::Value params( Json::objectValue );
    params["input"] = fx.path.toStdString();
    params["output"] = QDir( fx.dir.path() ).filePath( QStringLiteral( "stretch.tif" ) ).toStdString();
    params["method"] = "linear";

    const Json::Value result = runOp( op, params );
    REQUIRE( result["output"].asString() == params["output"].asString() );

    // Band 3 is constant 1: a linear min-max stretch keeps the constant plane
    // constant (0/0 denominator must not produce NaN garbage).
    const std::vector<float> out = readBand( QString::fromStdString( result["output"].asString() ), 3 );
    REQUIRE( out.size() == 16 );
    for ( float v : out )
        REQUIRE( std::isfinite( v ) );
    // Band 1 ramps 10..40: min maps to 0, max maps to 255 (the display
    // stretch convention the legacy dialog kernel always used).
    const std::vector<float> ramp = readBand( QString::fromStdString( result["output"].asString() ), 1 );
    REQUIRE( ramp[0] == Catch::Approx( 0.0 ).margin( 1e-4 ) );
    REQUIRE( ramp[3] == Catch::Approx( 255.0 ).margin( 1e-4 ) );
}
