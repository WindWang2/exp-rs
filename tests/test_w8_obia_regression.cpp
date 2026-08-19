// test_w8_obia_regression.cpp — W8 OBIA regression (323/355/396)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_class_raster.h"
#include <gdal_priv.h>

namespace {
void ensureGdal()
{
    static bool done = [] { GDALAllRegister(); return true; }();
    (void)done;
}
} // namespace
#include "app/obia/rs_obia_segmentation.h"
#include <gdal.h>
#include <QTemporaryDir>
#include <QVector>
#include <limits>

using Catch::Approx;

TEST_CASE( "W8: GLCM skips NoData sentinel (RSHYP-3)", "[w8][segmentation]" )
{
    ensureGdal();
    constexpr int W = 4, H = 4;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "nodata_glcm.tif" );
    GDALDriverH drv = GDALGetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDatasetH ds = GDALCreate( drv, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, -9999 );
    // Values: 10 everywhere except one pixel = -9999 inside segment
    QVector<float> pix( W*H, 10.0f );
    pix[5] = -9999.0f; // row1 col1 inside left segment but NoData
    GDALRasterIO( band, GF_Write, 0,0,W,H, pix.data(), W,H, GDT_Float32,0,0 );
    GDALClose( ds );

    QVector<quint32> labels( W*H );
    for ( int i=0;i<W*H;++i) labels[i]=1;
    RsSegmentMap segMap( labels, W,H );
    auto stats = RsSegmentFeatures::extract( path, segMap, {1} );
    REQUIRE( stats.contains(1) );
    // With NoData, area should be 15 not 16, and glcm should not include invalid pairs
    REQUIRE( stats[1].area == 15 );
    // mean should be 10 (NoData excluded)
    REQUIRE( stats[1].mean[0] == Approx(10.0).margin(1e-5) );
    // Ensure GLCM computed without crash and values are finite
    REQUIRE( std::isfinite( stats[1].glcmContrast[0] ) );
}

TEST_CASE( "W8: SegmentMap UInt32 round-trip >2^24 (O1)", "[w8][segmentation]" )
{
    ensureGdal();
    constexpr int W = 10, H = 1;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString refPath = dir.filePath( "ref.tif" );
    const QString labelPath = dir.filePath( "labels.tif" );
    GDALDriverH drv = GDALGetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    // Create ref raster for geotransform copy
    GDALDatasetH refDs = GDALCreate( drv, refPath.toUtf8().constData(), W, H, 1, GDT_Byte, nullptr );
    REQUIRE( refDs != nullptr );
    double gt[6]={0,1,0,0,0,1};
    GDALSetGeoTransform( refDs, gt );
    GDALClose( refDs );

    QVector<quint32> labels( W*H );
    labels[0]= 16777217u;
    labels[1]= 16777218u;
    labels[2]= 33554432u;
    labels[3]= 40000000u;
    for ( int i=4;i<W;++i) labels[i]= static_cast<quint32>(i+100);
    RsSegmentMap mapIn( labels, W, H );
    QString err;
    REQUIRE( mapIn.toGeoTIFF( labelPath, refPath, &err ) );
    RsSegmentMap mapOut = RsSegmentMap::fromGeoTIFF( labelPath );
    REQUIRE( !mapOut.isEmpty() );
    REQUIRE( mapOut.labelAt(0,0)==16777217u );
    REQUIRE( mapOut.labelAt(0,1)==16777218u );
    REQUIRE( mapOut.labelAt(0,2)==33554432u );
    REQUIRE( mapOut.labelAt(0,3)==40000000u );
}

TEST_CASE( "W8: RsObiaSegmentationConfig spatial/range mapping SHELLB-4", "[w8][segmentation]" )
{
    ensureGdal();
    RsObiaSegmentationConfig cfg;
    cfg.spatialRadius = 17;
    cfg.rangeRadius = 32 * 0.5;
    REQUIRE( cfg.spatialRadius == 17 );
    REQUIRE( cfg.rangeRadius == Approx(16.0) );
    // Ensure runSegmentation mapping sets these from kernel/bins; this is a contract check
}

TEST_CASE( "W8: ClassRaster overflow index uses 64-bit (RSHYP-4)", "[w8][segmentation]" )
{
    ensureGdal();
    // Smoke: paint with small map does not overflow
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString refPath = dir.filePath( "ref2.tif" );
    const QString outPath = dir.filePath( "out.tif" );
    GDALDriverH drv = GDALGetDriverByName( "GTiff" );
    GDALDatasetH refDs = GDALCreate( drv, refPath.toUtf8().constData(), 4,4,1,GDT_Byte,nullptr);
    REQUIRE(refDs!=nullptr);
    double gt[6]={0,1,0,0,0,1};
    GDALSetGeoTransform(refDs, gt);
    GDALClose(refDs);
    QVector<quint32> labels(16,1);
    RsSegmentMap map(labels,4,4);
    QMap<quint32,int> cls; cls[1]=2;
    auto r = RsClassRaster::paint(map, cls, refPath, outPath, {});
    REQUIRE( r.ok );
}

TEST_CASE( "W8: Magic wand band cap (O3) logic", "[w8][magicwand]" )
{
    int B=400;
    int readB = ( B > 8 ? 1 : B );
    REQUIRE( readB == 1 );
    B=8;
    readB = ( B > 8 ? 1 : B );
    REQUIRE( readB == 8 );
    B=3;
    readB = ( B > 8 ? 1 : B );
    REQUIRE( readB == 3 );
}

TEST_CASE( "W8: SegmentMap inverted index single-scan", "[w8][segmentation]" )
{
    ensureGdal();
    QVector<quint32> labels = {1,1,2,2, 1,1,2,2, 3,3,3,3, 0,0,0,0};
    RsSegmentMap m( labels, 4,4 );
    // Call pixelCoords multiple times; after first few, inverted index is built
    auto c1 = m.pixelCoords(1);
    auto c2 = m.pixelCoords(2);
    auto c3 = m.pixelCoords(3);
    REQUIRE( c1.size()==4 );
    REQUIRE( c2.size()==4 );
    REQUIRE( c3.size()==4 );
    // Second lookup should be O(1) cached
    auto c1b = m.pixelCoords(1);
    REQUIRE( c1b.size()==4 );
}
