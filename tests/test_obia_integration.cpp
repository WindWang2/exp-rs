// test_obia_integration.cpp — Phase 10B Task 10B.6
//
// End-to-end OBIA pipeline and GDAL output validation.
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_task.h"
#include "analysis/classification/rs_classifier_normalbayes.h"
#include "operators/rs/rs_obia_classify_operator.h"
#include "operators/framework/rs_operator_error.h"

#include <gdal.h>
#include <cpl_error.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QTemporaryDir>

static int fake_argc = 1;
static char fake_argv0[] = "test_obia_integration";
static char *fake_argv[] = { fake_argv0, nullptr };

static QApplication *ensureApp()
{
    if ( !QApplication::instance() )
        return new QApplication( fake_argc, fake_argv );
    return static_cast<QApplication *>( QApplication::instance() );
}

static bool g_gdalInit = ( GDALAllRegister(), true );

static QString createTestRaster( const QString &dir, int w, int h, int nBands )
{
    QString path = dir + "/test_input.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, nBands, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    QVector<float> row( w );
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
                row[c] = ( c < w / 2 ) ? 100.0f : 200.0f;
            GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
        }
    }

    GDALClose( ds );
    return path;
}

static RsSegmentMap createTwoSegmentMap( int w, int h )
{
    QVector<quint32> labels( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            labels[r * w + c] = ( c < w / 2 ) ? 1u : 2u;
    }
    return RsSegmentMap( labels, w, h );
}

static bool runObiaPipeline( const QString &inputPath, const QString &outputPath, RsObiaTask::Result &outResult )
{
    RsObiaTask::Config cfg;
    cfg.sourceRaster = inputPath;
    cfg.outputRaster = outputPath;
    cfg.bandIndices = { 1, 2, 3 };
    cfg.useOtb = false;
    cfg.existingSegMap = createTwoSegmentMap( 16, 16 );
    cfg.segmentLabels = { { 1, 1 }, { 2, 2 } };
    cfg.classColors = { { 1, QColor( 255, 0, 0 ) }, { 2, QColor( 0, 255, 0 ) } };
    cfg.backend = std::make_unique<RsClassifierNormalBayes>();
    cfg.algoName = "NormalBayes";

    RsObiaTask task( std::move( cfg ) );
    if ( !task.run() )
        return false;

    outResult = task.result();
    return outResult.ok;
}

TEST_CASE( "OBIA integration: segment features to classification pipeline", "[obia][integration]" )
{
    ensureApp();

    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString inputPath = createTestRaster( tempDir.path(), 16, 16, 3 );
    REQUIRE( !inputPath.isEmpty() );

    const QString outputPath = tempDir.path() + "/obia_classified.tif";
    RsObiaTask::Result result;
    REQUIRE( runObiaPipeline( inputPath, outputPath, result ) );

    REQUIRE( result.totalSegments == 2 );
    REQUIRE( result.labeledSegments == 2 );
    REQUIRE( result.totalPixels == 16 * 16 );
    REQUIRE( result.durationMs >= 0 );
    REQUIRE( QFile::exists( outputPath ) );
}

TEST_CASE( "OBIA integration: output GeoTIFF GDAL validation", "[obia][integration][gdal]" )
{
    ensureApp();

    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const int w = 16;
    const int h = 16;
    const QString inputPath = createTestRaster( tempDir.path(), w, h, 3 );
    REQUIRE( !inputPath.isEmpty() );

    const QString outputPath = tempDir.path() + "/obia_classified.tif";
    RsObiaTask::Result result;
    REQUIRE( runObiaPipeline( inputPath, outputPath, result ) );

    GDALDatasetH ds = GDALOpen( outputPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );

    REQUIRE( GDALGetRasterXSize( ds ) == w );
    REQUIRE( GDALGetRasterYSize( ds ) == h );
    REQUIRE( GDALGetRasterCount( ds ) == 1 );

    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALGetRasterDataType( band ) == GDT_Byte );
    REQUIRE( GDALGetRasterColorTable( band ) != nullptr );
    REQUIRE( GDALGetRasterColorInterpretation( band ) == GCI_PaletteIndex );

    QVector<GByte> row( w );
    for ( int r = 0; r < h; ++r )
    {
        REQUIRE( GDALRasterIO( band, GF_Read, 0, r, w, 1, row.data(), w, 1, GDT_Byte, 0, 0 ) == CE_None );
        for ( int c = 0; c < w; ++c )
        {
            const GByte expected = ( c < w / 2 ) ? 1 : 2;
            REQUIRE( row[c] == expected );
        }
    }

    GDALClose( ds );
}

TEST_CASE( "OBIA classify operator: parameter validation guards", "[obia][operator]" )
{
    using namespace sicnu::operators;
    using namespace sicnu::operators::rs;
    RsObiaClassifyOperator op;
    RSOperatorContext ctx;

    Json::Value params( Json::objectValue );
    params["input"] = "non_existent_input.tif";
    params["training"] = "non_existent_training.shp";
    params["output"] = "output.tif";

    SECTION( "cellSize <= 0" )
    {
        params["cellSize"] = 0;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }

    SECTION( "smoothKernel <= 0 or even" )
    {
        params["cellSize"] = 16;
        params["smoothKernel"] = 4; // even kernel
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }

    SECTION( "quantizeBins <= 0" )
    {
        params["cellSize"] = 16;
        params["smoothKernel"] = 3;
        params["quantizeBins"] = 0;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
}

#endif // SICNU_HAS_OPENCV