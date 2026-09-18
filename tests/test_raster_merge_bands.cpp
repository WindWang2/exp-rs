// tests/test_raster_merge_bands.cpp — Tests for RasterMergeBandsAlgorithm
//
// Regression coverage for issue #1035: the algorithm used to pass the first
// block's data type as eBufType to GDALRasterIO for every band while handing
// it blocks[b]->bits(). With mixed-type inputs GDAL either read past the end
// of a narrower block (OOB heap read) or read only a prefix of a wider block
// (silent corruption). The output band type is now the GDALDataTypeUnion of
// all input types and each band is written with its own buffer type.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsapplication.h>
#include <qgsexception.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>

#include "processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.h"

#include <vector>

namespace
{

void ensureQgis()
{
    if ( QgsApplication::instance() )
        return;
    static int argc = 1;
    static char name[] = "test_raster_merge_bands";
    static char *argv[] = { name, nullptr };
    static auto *app = new QgsApplication( argc, argv, false );
    ( void )app;
    QgsApplication::initQgis();
}

// Creates a single-band GeoTIFF. Values are passed as doubles and written with
// eBufType = GDT_Float64 so GDAL performs the conversion to the band type.
QString createRaster( const QString &dir, const QString &name, int width, int height,
                      GDALDataType type, const std::vector<double> &data,
                      bool hasNoData = false, double noData = 0.0 )
{
    const QString path = dir + "/" + name;
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1, type, nullptr );
    REQUIRE( ds != nullptr );

    double geoTransform[6] = { 500000.0, 1.0, 0.0, 4500000.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, geoTransform );

    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( band != nullptr );
    if ( hasNoData )
        GDALSetRasterNoDataValue( band, noData );

    const CPLErr err = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                     const_cast<double *>( data.data() ), width, height,
                                     GDT_Float64, 0, 0 );
    GDALClose( ds );
    REQUIRE( err == CE_None );
    return path;
}

struct ReadBack
{
    int bandCount = 0;
    GDALDataType bandType = GDT_Unknown;
    std::vector<double> values;
    bool hasNoData = false;
    double noData = 0.0;
};

ReadBack readBand( const QString &path, int bandIndex, int width, int height )
{
    ReadBack rb;
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    rb.bandCount = GDALGetRasterCount( ds );
    GDALRasterBandH band = GDALGetRasterBand( ds, bandIndex );
    REQUIRE( band != nullptr );
    rb.bandType = GDALGetRasterDataType( band );
    int hasNoDataFlag = 0;
    rb.noData = GDALGetRasterNoDataValue( band, &hasNoDataFlag );
    rb.hasNoData = ( hasNoDataFlag != 0 );
    rb.values.resize( static_cast<size_t>( width ) * static_cast<size_t>( height ) );
    const CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, width, height,
                                     rb.values.data(), width, height,
                                     GDT_Float64, 0, 0 );
    GDALClose( ds );
    REQUIRE( err == CE_None );
    return rb;
}

QVariantMap runMerge( const QStringList &inputs, const QString &output )
{
    RasterMergeBandsAlgorithm alg;
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params.insert( QStringLiteral( "INPUT_LAYERS" ), inputs );
    params.insert( QStringLiteral( "OUTPUT" ), output );
    bool ok = false;
    QVariantMap results = alg.run( params, context, &feedback, &ok, QVariantMap(), false );
    REQUIRE( ok );
    return results;
}

} // namespace

TEST_CASE( "RasterMergeBandsAlgorithm metadata", "[raster][merge_bands][algorithm]" )
{
    RasterMergeBandsAlgorithm alg;
    CHECK( alg.name() == "raster_merge_bands" );
    CHECK_FALSE( alg.displayName().isEmpty() );
    CHECK( alg.groupId() == "raster" );
}

TEST_CASE( "RasterMergeBandsAlgorithm merges Byte + Float32 (Byte first) without corruption (#1035)",
           "[raster][merge_bands][mixedtype]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    constexpr int W = 16;
    constexpr int H = 16;
    std::vector<double> byteData( W * H );
    std::vector<double> floatData( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        byteData[i] = static_cast<double>( i % 251 );      // 0..250 fits Byte
        floatData[i] = -12.5 + i * 0.75;                    // fractional, out of Byte range
    }

    const QString bytePath = createRaster( dir.path(), "a_byte.tif", W, H, GDT_Byte, byteData );
    const QString floatPath = createRaster( dir.path(), "b_float.tif", W, H, GDT_Float32, floatData );
    const QString outPath = dir.path() + "/merged_byte_first.tif";

    const QVariantMap results = runMerge( { bytePath, floatPath }, outPath );
    REQUIRE( QFile::exists( outPath ) );
    CHECK( results.value( QStringLiteral( "OUTPUT" ) ).toString() == outPath );

    // Output type contract: GDALDataTypeUnion(Byte, Float32) = Float32.
    const ReadBack band1 = readBand( outPath, 1, W, H );
    const ReadBack band2 = readBand( outPath, 2, W, H );
    CHECK( band1.bandCount == 2 );
    CHECK( band1.bandType == GDT_Float32 );
    CHECK( band2.bandType == GDT_Float32 );

    for ( int i = 0; i < W * H; ++i )
    {
        REQUIRE_THAT( band1.values[i], Catch::Matchers::WithinAbs( byteData[i], 1e-6 ) );
        // Float32 values must survive: pre-fix only the first byte of each
        // float was read, collapsing every value to a wrong byte.
        REQUIRE_THAT( band2.values[i], Catch::Matchers::WithinAbs( floatData[i], 1e-4 ) );
    }
}

TEST_CASE( "RasterMergeBandsAlgorithm merges Float32 + Byte (Float32 first) without OOB read (#1035)",
           "[raster][merge_bands][mixedtype]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 64x64: the pre-fix eBufType=Float32 made GDALRasterIO read 4x4096 = 16384
    // bytes from a 4096-byte Byte block — an ASan-detectable 12 KiB over-read.
    constexpr int W = 64;
    constexpr int H = 64;
    std::vector<double> floatData( W * H );
    std::vector<double> byteData( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        floatData[i] = 1000.25 + i;
        byteData[i] = static_cast<double>( ( i * 7 ) % 256 );
    }

    const QString floatPath = createRaster( dir.path(), "a_float.tif", W, H, GDT_Float32, floatData );
    const QString bytePath = createRaster( dir.path(), "b_byte.tif", W, H, GDT_Byte, byteData );
    const QString outPath = dir.path() + "/merged_float_first.tif";

    runMerge( { floatPath, bytePath }, outPath );
    REQUIRE( QFile::exists( outPath ) );

    const ReadBack band1 = readBand( outPath, 1, W, H );
    const ReadBack band2 = readBand( outPath, 2, W, H );
    CHECK( band1.bandCount == 2 );
    CHECK( band1.bandType == GDT_Float32 );
    CHECK( band2.bandType == GDT_Float32 );

    for ( int i = 0; i < W * H; ++i )
    {
        REQUIRE_THAT( band1.values[i], Catch::Matchers::WithinAbs( floatData[i], 1e-3 ) );
        // The Byte band must land in band 2 as its own values, not whatever
        // heap bytes followed the 4096-byte buffer pre-fix.
        REQUIRE_THAT( band2.values[i], Catch::Matchers::WithinAbs( byteData[i], 1e-6 ) );
    }
}

TEST_CASE( "RasterMergeBandsAlgorithm promotes Int16 + Byte to Int16 and keeps NoData (#1035)",
           "[raster][merge_bands][mixedtype][nodata]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<double> int16Data( W * H );
    std::vector<double> byteData( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        int16Data[i] = -32768.0 + i * 1000;   // signed extremes incl. -32768
        byteData[i] = static_cast<double>( 255 - ( i % 256 ) );
    }
    int16Data[0] = -9999.0;                    // nodata pixel

    const QString int16Path = createRaster( dir.path(), "a_int16.tif", W, H, GDT_Int16, int16Data, true, -9999.0 );
    const QString bytePath = createRaster( dir.path(), "b_byte.tif", W, H, GDT_Byte, byteData );
    const QString outPath = dir.path() + "/merged_int16_byte.tif";

    runMerge( { int16Path, bytePath }, outPath );
    REQUIRE( QFile::exists( outPath ) );

    // GDALDataTypeUnion(Int16, Byte) = Int16 — signed values preserved.
    const ReadBack band1 = readBand( outPath, 1, W, H );
    const ReadBack band2 = readBand( outPath, 2, W, H );
    CHECK( band1.bandCount == 2 );
    CHECK( band1.bandType == GDT_Int16 );
    CHECK( band2.bandType == GDT_Int16 );

    for ( int i = 0; i < W * H; ++i )
    {
        CHECK( band1.values[i] == int16Data[i] );
        CHECK( band2.values[i] == byteData[i] );
    }
    CHECK( band1.hasNoData );
    CHECK( band1.noData == -9999.0 );
}

TEST_CASE( "RasterMergeBandsAlgorithm keeps same-type Byte + Byte output as Byte",
           "[raster][merge_bands]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<double> d1( W * H ), d2( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        d1[i] = static_cast<double>( i % 256 );
        d2[i] = static_cast<double>( 200 - ( i % 100 ) );
    }

    const QString p1 = createRaster( dir.path(), "b1.tif", W, H, GDT_Byte, d1 );
    const QString p2 = createRaster( dir.path(), "b2.tif", W, H, GDT_Byte, d2 );
    const QString outPath = dir.path() + "/merged_byte_byte.tif";

    runMerge( { p1, p2 }, outPath );
    REQUIRE( QFile::exists( outPath ) );

    const ReadBack band1 = readBand( outPath, 1, W, H );
    const ReadBack band2 = readBand( outPath, 2, W, H );
    CHECK( band1.bandCount == 2 );
    CHECK( band1.bandType == GDT_Byte );
    CHECK( band2.bandType == GDT_Byte );
    for ( int i = 0; i < W * H; ++i )
    {
        CHECK( band1.values[i] == d1[i] );
        CHECK( band2.values[i] == d2[i] );
    }
}

TEST_CASE( "RasterMergeBandsAlgorithm unions UInt16 + Int16 to Int32 (#1035)",
           "[raster][merge_bands][mixedtype]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    constexpr int W = 8;
    constexpr int H = 8;
    std::vector<double> uint16Data( W * H );
    std::vector<double> int16Data( W * H );
    for ( int i = 0; i < W * H; ++i )
    {
        uint16Data[i] = static_cast<double>( 60000 + i );  // exceeds Int16 range
        int16Data[i] = -20000.0 + i * 500;               // below UInt16 range
    }

    const QString uint16Path = createRaster( dir.path(), "a_uint16.tif", W, H, GDT_UInt16, uint16Data );
    const QString int16Path = createRaster( dir.path(), "b_int16.tif", W, H, GDT_Int16, int16Data );
    const QString outPath = dir.path() + "/merged_uint16_int16.tif";

    runMerge( { uint16Path, int16Path }, outPath );
    REQUIRE( QFile::exists( outPath ) );

    // Neither input type can hold the other's range: the union must promote to
    // Int32 so both bands stay lossless.
    const ReadBack band1 = readBand( outPath, 1, W, H );
    const ReadBack band2 = readBand( outPath, 2, W, H );
    CHECK( band1.bandCount == 2 );
    CHECK( band1.bandType == GDT_Int32 );
    CHECK( band2.bandType == GDT_Int32 );

    for ( int i = 0; i < W * H; ++i )
    {
        CHECK( band1.values[i] == uint16Data[i] );
        CHECK( band2.values[i] == int16Data[i] );
    }
}

TEST_CASE( "RasterMergeBandsAlgorithm rejects mismatched dimensions",
           "[raster][merge_bands][grid]" )
{
    ensureQgis();
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    std::vector<double> a( 16, 1.0 ), b( 64, 2.0 );
    const QString p1 = createRaster( dir.path(), "a4.tif", 4, 4, GDT_Float32, a );
    const QString p2 = createRaster( dir.path(), "b8.tif", 8, 8, GDT_Float32, b );
    const QString outPath = dir.path() + "/merged_mismatch.tif";

    RasterMergeBandsAlgorithm alg;
    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params.insert( QStringLiteral( "INPUT_LAYERS" ), QStringList{ p1, p2 } );
    params.insert( QStringLiteral( "OUTPUT" ), outPath );

    bool ok = true;
    try
    {
        ( void )alg.run( params, context, &feedback, &ok, QVariantMap(), false );
        FAIL( "expected QgsProcessingException for dimension mismatch" );
    }
    catch ( const QgsProcessingException &e )
    {
        CHECK( e.what().contains( QStringLiteral( "do not match" ) ) );
    }
    CHECK_FALSE( QFile::exists( outPath ) );
}
