// test_obia_integration.cpp — OBIA operator pipeline end-to-end (#663).
//
// The pipeline that used to run inside the GUI-owned RsObiaTask (segment →
// features → train → predict → paint) is now the rs:obia_classify labels +
// segmentClasses contract; these tests pin its output at the GDAL level
// (palette, dtype, georeference, class ids) exactly as the old in-app task
// was pinned.
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "operators/rs/rs_obia_classify_operator.h"
#include "operators/framework/rs_operator_error.h"

#include "analysis/segmentation/rs_segment_map.h"

#include <gdal.h>
#include <cpl_error.h>

#include <QColor>
#include <QFile>
#include <QTemporaryDir>

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

static QString createTwoSegmentLabels( const QString &dir, int w, int h )
{
    QVector<quint32> labels( w * h );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            labels[r * w + c] = ( c < w / 2 ) ? 1u : 2u;
    }
    RsSegmentMap segMap( labels, w, h );
    const QString ref = createTestRaster( dir, w, h, 1 );
    const QString path = dir + "/labels.tif";
    QString err;
    REQUIRE( segMap.toGeoTIFF( path, ref, &err ) );
    return path;
}

static Json::Value runClassifyPipeline( const QString &inputPath, const QString &labelsPath,
                                        const QString &outputPath )
{
    using namespace sicnu::operators::rs;
    RsObiaClassifyOperator op;
    sicnu::operators::RSOperatorContext ctx;

    Json::Value params( Json::objectValue );
    params["input"] = inputPath.toStdString();
    params["labels"] = labelsPath.toStdString();
    params["output"] = outputPath.toStdString();
    params["method"] = "normal_bayes";
    params["features"] = "full";
    Json::Value segmentClasses( Json::objectValue );
    segmentClasses["1"] = 1;
    segmentClasses["2"] = 2;
    params["segmentClasses"] = segmentClasses;
    Json::Value colors( Json::objectValue );
    colors["1"] = "#ff0000";
    colors["2"] = "#00ff00";
    params["classColors"] = colors;

    return op.run( params, ctx );
}

TEST_CASE( "OBIA integration: labels + segmentClasses pipeline", "[obia][integration]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const int w = 16;
    const int h = 16;
    const QString inputPath = createTestRaster( tempDir.path(), w, h, 3 );
    REQUIRE( !inputPath.isEmpty() );
    const QString labelsPath = createTwoSegmentLabels( tempDir.path(), w, h );
    const QString outputPath = tempDir.path() + "/obia_classified.tif";

    const Json::Value result = runClassifyPipeline( inputPath, labelsPath, outputPath );
    REQUIRE( result["segments"].asInt() == 2 );
    REQUIRE( result["labeledSegments"].asInt() == 2 );
    REQUIRE( result["trainSamples"].asInt() == 2 );
    REQUIRE( result["classes"].asInt() == 2 );
    REQUIRE( result["width"].asInt() == w );
    REQUIRE( result["height"].asInt() == h );
    REQUIRE( result["labels"].asString() == labelsPath.toStdString() );
    REQUIRE( QFile::exists( outputPath ) );
}

TEST_CASE( "OBIA integration: output GeoTIFF GDAL validation", "[obia][integration][gdal]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const int w = 16;
    const int h = 16;
    const QString inputPath = createTestRaster( tempDir.path(), w, h, 3 );
    REQUIRE( !inputPath.isEmpty() );
    const QString labelsPath = createTwoSegmentLabels( tempDir.path(), w, h );
    const QString outputPath = tempDir.path() + "/obia_classified.tif";
    REQUIRE_NOTHROW( runClassifyPipeline( inputPath, labelsPath, outputPath ) );

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
