// test_edit_roi_semantics.cpp — F11 Package D: ROI ↔ raster semantics.
//
// Oracles are independent of RsRoiSemantics:
//   * the raster fixture is written directly through GDAL, so expected band
//     values are known a priori (value = 20*row + col on band 1);
//   * footprints/stmeans are hand-computed from the center-of-pixel rule
//     (cols {4..9} × rows {2..8} → 42 pixels for the standard ROI);
//   * the CRS oracle is a raw QgsCoordinateTransform built in the test;
//   * NoData mask truth is the 255-block we wrote ourselves.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "editing/rs_edit_session.h"
#include "editing/rs_roi_semantics.h"

#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgsfeature.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <gdal.h>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <vector>

namespace
{

struct RoiFixture
{
    RoiFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_edit_roi_semantics";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        QgsProject::instance()->clear();
    }
    ~RoiFixture() { QgsProject::instance()->clear(); }
};

// 20×20, two Float32 bands, metric geotransform {100000, 10, 0, 3000000, 0, -10}.
// Band 1 value at (row, col) = 20*row + col, except an optional NoData block
// (rows 2..4, cols 4..9) set to 255 with nodata=255. Band 2 = 1000 + band1.
QString makeRoiRaster( const QString &path, bool withNodataBlock )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 100000.0, 10.0, 0.0, 3000000.0, 0.0, -10.0 };
    GDALDatasetH ds = createOutputTiff( path, 20, 20, 2, GDT_Float32, gt, QString() );
    if ( !ds )
        return QStringLiteral( "createOutputTiff failed" );

    const char *wkt =
      "PROJCS[\"WGS 84 / UTM zone 50N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
      "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
      "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",117],"
      "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
      "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";
    GDALSetProjection( ds, wkt );

    for ( int b = 0; b < 2; ++b )
    {
        std::vector<float> band( 20 * 20 );
        for ( int y = 0; y < 20; ++y )
            for ( int x = 0; x < 20; ++x )
                band[static_cast<size_t>( y * 20 + x )] =
                  static_cast<float>( ( b == 0 ? 0 : 1000 ) + y * 20 + x );
        if ( b == 0 && withNodataBlock )
        {
            for ( int y = 2; y <= 4; ++y )
                for ( int x = 4; x <= 9; ++x )
                    band[static_cast<size_t>( y * 20 + x )] = 255.0f;
        }
        if ( GDALRasterIO( GDALGetRasterBand( ds, b + 1 ), GF_Write, 0, 0, 20, 20,
                           band.data(), 20, 20, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( ds );
            return QStringLiteral( "GDALRasterIO failed" );
        }
        if ( b == 0 && withNodataBlock )
            GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), 255.0 );
    }
    GDALClose( ds );
    return {};
}

/// Standard ROI enclosing (never touching) the pixel centers of
/// cols {4..9} × rows {2..8}: centers are 100045/100055/…/100095 easting and
/// 2999975/2999965/…/2999915 northing, so the boundary stays ≥1 m clear of
/// every center and GDAL's inside test is boundary-case-free → 42 pixels.
QgsGeometry standardRoi()
{
    return QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
      QgsPointXY( 100044.0, 2999914.0 ), QgsPointXY( 100096.0, 2999914.0 ),
      QgsPointXY( 100096.0, 2999976.0 ), QgsPointXY( 100044.0, 2999976.0 ),
      QgsPointXY( 100044.0, 2999914.0 ) } } );
}

} // namespace

TEST_CASE( "band stats preview matches the a-priori value grid",
           "[editing][roi][f11][oracle]" )
{
    RoiFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "roi_grid.tif" ) );
    REQUIRE( makeRoiRaster( path, false ).isEmpty() );

    QgsRasterLayer layer( path, QStringLiteral( "grid" ), QStringLiteral( "gdal" ) );
    REQUIRE( layer.isValid() );
    REQUIRE( layer.bandCount() == 2 );

    const RsRoiStatsResult result = RsRoiSemantics::bandStatsPreview(
      &layer, standardRoi(), layer.crs(), {}, RsRoiSemantics::kDefaultMaxPixels,
      QVector<int>{ 1, 2 } );
    REQUIRE( result.ok );
    CHECK( result.pixelCount == 42 );
    CHECK( result.validPixelCount == 42 );

    // Independent oracle: over cols {4..9} × rows {2..8}, value = 20r + c.
    constexpr int kCols[6] = { 4, 5, 6, 7, 8, 9 };
    constexpr int kRows[7] = { 2, 3, 4, 5, 6, 7, 8 };
    double sum = 0.0, sumSq = 0.0;
    double minV = 1e300, maxV = -1e300;
    int n = 0;
    for ( const int r : kRows )
    {
        for ( const int c : kCols )
        {
            const double v = 20.0 * r + c;
            sum += v;
            sumSq += v * v;
            minV = std::min( minV, v );
            maxV = std::max( maxV, v );
            ++n;
        }
    }
    const double mean = sum / n;
    const double stdv = std::sqrt( sumSq / n - mean * mean );

    REQUIRE( result.bands.size() == 2 );
    CHECK( result.bands[0].band == 1 );
    CHECK( result.bands[0].min == minV );
    CHECK( result.bands[0].max == maxV );
    CHECK( result.bands[0].mean == Catch::Approx( mean ).margin( 1e-6 ) );
    CHECK( result.bands[0].stddev == Catch::Approx( stdv ).margin( 1e-6 ) );
    // Band 2 is band 1 + 1000: same spread, shifted location.
    CHECK( result.bands[1].min == minV + 1000.0 );
    CHECK( result.bands[1].mean == Catch::Approx( mean + 1000.0 ).margin( 1e-6 ) );
}

TEST_CASE( "NoData pixels are excluded from validPixelCount but pixelCount covers",
           "[editing][roi][f11][oracle]" )
{
    RoiFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "roi_nodata.tif" ) );
    REQUIRE( makeRoiRaster( path, true ).isEmpty() );

    QgsRasterLayer layer( path, QStringLiteral( "nodata" ), QStringLiteral( "gdal" ) );
    REQUIRE( layer.isValid() );

    const RsRoiStatsResult result = RsRoiSemantics::bandStatsPreview(
      &layer, standardRoi(), layer.crs(), {} );
    REQUIRE( result.ok );
    // Hand-known: the NoData block (rows 2..4 × cols 4..9 = 18 px) sits
    // inside the 42-px ROI.
    CHECK( result.pixelCount == 42 );
    CHECK( result.validPixelCount == 42 - 18 );
    // Band 1 min excludes the 255 sentinel (next value in grid is 20*5+4=104
    // after the nodata rows... rows 2..4 masked; smallest remaining is 20*5+4).
    CHECK( result.bands[0].min == 104.0 );
    CHECK( result.bands[0].max == 20.0 * 8 + 9 );
}

TEST_CASE( "CRS transform of the ROI is applied and fail-closed",
           "[editing][roi][f11][oracle]" )
{
    RoiFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Dedicated fixture: geotransform is DERIVED from the oracle transform
    // of the anchor point, so the transformed ROI provably covers pixels
    // regardless of zone constants.
    const QString path = dir.filePath( QStringLiteral( "roi_utm.tif" ) );
    {
        const QgsCoordinateReferenceSystem utmCrs(
          QStringLiteral( "PROJCS[\"WGS 84 / UTM zone 50N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
                          "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
                          "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
                          "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",117],"
                          "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
                          "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]" ) );
        REQUIRE( utmCrs.isValid() );
        const QgsCoordinateTransform oracle(
          QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ), utmCrs,
          QgsProject::instance()->transformContext() );
        const QgsPointXY anchor = oracle.transform( QgsPointXY( 117.0, 27.1 ) );

        ensureGdalInit();
        std::array<double, 6> gt = { anchor.x() - 100.0, 10.0, 0.0, anchor.y() + 100.0, 0.0, -10.0 };
        GDALDatasetH ds = createOutputTiff( path, 20, 20, 1, GDT_Float32, gt, QString() );
        REQUIRE( ds );
        const char *wkt =
          "PROJCS[\"WGS 84 / UTM zone 50N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
          "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
          "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
          "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",117],"
          "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
          "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";
        GDALSetProjection( ds, wkt );
        std::vector<float> band( 400, 1.0f );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 20, 20,
                               band.data(), 20, 20, GDT_Float32, 0, 0 ) == CE_None );
        GDALClose( ds );
    }

    QgsRasterLayer layer( path, QStringLiteral( "utm" ), QStringLiteral( "gdal" ) );
    REQUIRE( layer.isValid() );

    // The raster CRS read back from the fixture must be the UTM zone we set.
    const QgsCoordinateReferenceSystem utm = layer.crs();
    REQUIRE( utm.isValid() );

    // A degree-CRS ROI around the raster's geographic position: the oracle
    // is a raw transform built in the test, not the function under test.
    const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
    const QgsCoordinateTransform oracle( wgs84, utm,
                                         QgsProject::instance()->transformContext() );
    QgsPointXY center = oracle.transform( QgsPointXY( 117.0, 27.1 ) );

    // The ROI is authored in WGS84 degrees (~60 m radius) — the semantic
    // path must transform it into the raster CRS before windowing.
    QgsGeometry roi = QgsGeometry::fromPointXY( QgsPointXY( 117.0, 27.1 ) ).buffer( 0.0006, 16 );
    REQUIRE_FALSE( roi.isNull() );

    // 1) Semantic path transforms and produces stats (no silent raw coords).
    const RsRoiStatsResult transformed = RsRoiSemantics::bandStatsPreview(
      &layer, roi, wgs84, {} );
    INFO( "transformed error: " << transformed.error.toStdString() );
    INFO( "anchor: " << center.toString().toStdString()
          << " extent: " << layer.extent().toString().toStdString()
          << " roi bbox: " << roi.boundingBox().toString().toStdString()
          << " crs auth: " << layer.crs().authid().toStdString() );
    // Replicate the internal pipeline step by step to expose the divergence.
    {
        QgsGeometry probe = roi;
        QgsCoordinateTransform t( wgs84, layer.crs(), QgsProject::instance()->transformContext() );
        try
        {
            probe.transform( t );
            INFO( "probe transformed bbox: " << probe.boundingBox().toString().toStdString() );
            INFO( "probe intersect extent: " << probe.boundingBox().intersect( layer.extent() ).toString().toStdString()
                  << " width=" << probe.boundingBox().intersect( layer.extent() ).width() );
        }
        catch ( const QgsCsException &e )
        {
            INFO( "probe transform threw" );
        }
    }
    CHECK( transformed.ok );
    CHECK( transformed.pixelCount > 0 );

    // 2) Fail-closed: an invalid CRS is refused outright.
    QgsGeometry out;
    const QString err = RsRoiSemantics::transformToRasterCrs(
      roi, QgsCoordinateReferenceSystem(), &layer, &out );
    CHECK_FALSE( err.isEmpty() );

    // 3) No transform available (unsupported CRS pair) is refused, too.
    const QgsCoordinateReferenceSystem bogus( QStringLiteral( "EPSG:104999" ) );
    const QString err2 = RsRoiSemantics::transformToRasterCrs( roi, bogus, &layer, &out );
    CHECK_FALSE( err2.isEmpty() );
}

TEST_CASE( "preview is cancelable and enforces the pixel budget",
           "[editing][roi][f11][cancel]" )
{
    RoiFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "roi_cancel.tif" ) );
    REQUIRE( makeRoiRaster( path, false ).isEmpty() );

    QgsRasterLayer layer( path, QStringLiteral( "cancel" ), QStringLiteral( "gdal" ) );
    REQUIRE( layer.isValid() );

    // Cancel before any work.
    const RsRoiStatsResult canceled = RsRoiSemantics::bandStatsPreview(
      &layer, standardRoi(), layer.crs(), []() { return true; } );
    CHECK_FALSE( canceled.ok );
    CHECK( canceled.error == QStringLiteral( "canceled" ) );

    // Budget refusal: 42 px > 10 px budget, fail-closed with no partial data.
    const RsRoiStatsResult refused = RsRoiSemantics::bandStatsPreview(
      &layer, standardRoi(), layer.crs(), {}, 10 );
    CHECK_FALSE( refused.ok );
    CHECK( refused.error.contains( QStringLiteral( "budget" ) ) );
    CHECK( refused.bands.isEmpty() );

    // Non-canceled baseline still works after refusals.
    const RsRoiStatsResult fine = RsRoiSemantics::bandStatsPreview(
      &layer, standardRoi(), layer.crs(), []() { return false; } );
    CHECK( fine.ok );

    // ROI outside the raster extent is refused, not faked.
    const QgsGeometry outside = QgsGeometry::fromPointXY( QgsPointXY( 200000.0, 2990000.0 ) );
    const RsRoiStatsResult miss = RsRoiSemantics::bandStatsPreview(
      &layer, outside, layer.crs(), {} );
    CHECK_FALSE( miss.ok );
    CHECK( miss.error.contains( QStringLiteral( "extent" ) ) );
}

TEST_CASE( "writeClassLabel is edit-command wrapped and fail-closed",
           "[editing][roi][f11][oracle1]" )
{
    RoiFixture fx;
    QgsVectorLayer layer( QStringLiteral( "Polygon?crs=EPSG:4326&field=class:int&field=name:string" ),
                          QStringLiteral( "samples" ), QStringLiteral( "memory" ) );
    REQUIRE( layer.isValid() );
    REQUIRE( layer.startEditing() );
    QgsFeature f( layer.fields() );
    f.setGeometry( QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
      QgsPointXY( 0, 0 ), QgsPointXY( 1, 0 ), QgsPointXY( 1, 1 ),
      QgsPointXY( 0, 1 ), QgsPointXY( 0, 0 ) } } ) );
    REQUIRE( layer.addFeature( f ) );
    const QgsFeatureId fid = f.id();

    RsEditSession session;
    REQUIRE( session.attachLayer( &layer ).isEmpty() );

    QString err;
    REQUIRE( RsRoiSemantics::writeClassLabel( &layer, fid, 7, QStringLiteral( "class" ),
                                              &session, &err ) );
    CHECK( err.isEmpty() );
    CHECK( layer.getFeature( fid ).attribute( QStringLiteral( "class" ) ).toInt() == 7 );
    // One undo step restores the label.
    CHECK( session.undo( layer.id() ) );
    CHECK( layer.getFeature( fid ).attribute( QStringLiteral( "class" ) ).isNull() );

    // Negative: missing field.
    REQUIRE( RsRoiSemantics::writeClassLabel( &layer, fid, 7, QStringLiteral( "nope" ),
                                              &session, &err ) == false );
    CHECK( err.contains( QStringLiteral( "class field" ) ) );

    // Negative: locked layer.
    REQUIRE( session.setLocked( layer.id(), true ) );
    REQUIRE( RsRoiSemantics::writeClassLabel( &layer, fid, 9, QStringLiteral( "class" ),
                                              &session, &err ) == false );
    CHECK( err.contains( QStringLiteral( "locked" ) ) );
}
