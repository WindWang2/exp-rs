// ADR 0019 slice S1 — RsTrainingDataExtraction tests.
//
// Exercises the unified extraction module through both entry points
// (in-memory geometry list, OGR vector + classField fallback) on small
// synthetic fixtures built programmatically with GDAL.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <ogr_api.h>

#include <functional>
#include <vector>

#include "rs_training_data_extraction.h"

namespace
{

/// Write a Float32 GeoTIFF; valueFn(b, r, c) gives band b (0-based) values.
/// GT is {0,1,0,H,0,-1} so pixel (r,c) center maps to (c+0.5, H-r-0.5).
void createRaster( const QString &path, int W, int H, int nBands,
                   const std::function<float( int, int, int )> &valueFn,
                   bool setNodata = false, double nodata = -9999.0 )
{
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(),
                                 W, H, nBands, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < nBands; ++b )
  {
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        band[static_cast<size_t>( r * W + c )] = valueFn( b, r, c );
    GDALRasterBand *rb = ds->GetRasterBand( b + 1 );
    rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    if ( setNodata )
      rb->SetNoDataValue( nodata );
  }
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

/// Axis-aligned square in map coordinates: cols [c0,c1), rows [r0,r1) of a
/// raster with GT {0,1,0,H,0,-1}.
QgsGeometry squareGeom( double x0, double y0, double x1, double y1 )
{
  QgsPolygonXY polygon;
  polygon.append( { QgsPointXY( x0, y0 ), QgsPointXY( x1, y0 ),
                    QgsPointXY( x1, y1 ), QgsPointXY( x0, y1 ),
                    QgsPointXY( x0, y0 ) } );
  return QgsGeometry::fromPolygonXY( polygon );
}

RsTrainingGeometry roi( int classId, const QgsGeometry &geom,
                        const QVector<quint64> &pixelIndices = {} )
{
  RsTrainingGeometry tg;
  tg.classId = classId;
  tg.geometry = geom;
  tg.pixelIndices = pixelIndices;
  return tg;
}

/// Create a single-layer GPKG with one integer field and one square feature.
void createVector( const QString &path, const QString &fieldName, int classId,
                   double x0, double y0, double x1, double y1 )
{
  GDALDriverH drv = GDALGetDriverByName( "GPKG" );
  REQUIRE( drv != nullptr );
  GDALDatasetH ds = GDALCreate( drv, path.toUtf8().constData(),
                                0, 0, 0, GDT_Unknown, nullptr );
  REQUIRE( ds != nullptr );
  OGRLayerH lyr = GDALDatasetCreateLayer( ds, "training", nullptr,
                                          wkbPolygon, nullptr );
  REQUIRE( lyr != nullptr );

  OGRFieldDefnH fld = OGR_Fld_Create( fieldName.toUtf8().constData(), OFTInteger );
  REQUIRE( OGR_L_CreateField( lyr, fld, true ) == OGRERR_NONE );
  OGR_Fld_Destroy( fld );

  OGRGeometryH ring = OGR_G_CreateGeometry( wkbLinearRing );
  OGR_G_AddPoint_2D( ring, x0, y0 );
  OGR_G_AddPoint_2D( ring, x1, y0 );
  OGR_G_AddPoint_2D( ring, x1, y1 );
  OGR_G_AddPoint_2D( ring, x0, y1 );
  OGR_G_AddPoint_2D( ring, x0, y0 );
  OGRGeometryH poly = OGR_G_CreateGeometry( wkbPolygon );
  REQUIRE( OGR_G_AddGeometryDirectly( poly, ring ) == OGRERR_NONE );

  OGRFeatureH feat = OGR_F_Create( OGR_L_GetLayerDefn( lyr ) );
  OGR_F_SetFieldInteger( feat, 0, classId );
  REQUIRE( OGR_F_SetGeometryDirectly( feat, poly ) == OGRERR_NONE );
  REQUIRE( OGR_L_CreateFeature( lyr, feat ) == OGRERR_NONE );
  OGR_F_Destroy( feat );
  GDALClose( ds );
}

} // namespace

TEST_CASE(
  "Training extraction: overlapping geometry list, last class wins",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  // band0 = pixel index (r*16+c), band1 = row → scanline reads must agree.
  createRaster( raster, 16, 16, 2,
                []( int b, int r, int c ) {
                  return b == 0 ? static_cast<float>( r * 16 + c )
                                : static_cast<float>( r );
                } );

  // Class 1: rows 0-7, cols 0-7. Class 2: rows 4-11, cols 4-11 (overlap 4x4).
  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 16, 8, 8 ) ),
    roi( 2, squareGeom( 4, 12, 12, 4 ) ),
  };

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, geoms );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows == 112 ); // 64 + 64 - 16 overlap
  REQUIRE( res.X.cols == 2 );
  REQUIRE( res.y.rows == 112 );
  REQUIRE( res.classCounts.value( 1 ) == 48 );
  REQUIRE( res.classCounts.value( 2 ) == 64 );

  for ( int s = 0; s < res.X.rows; ++s )
  {
    const float pix = res.X.at<float>( s, 0 );
    const float row = res.X.at<float>( s, 1 );
    const int r = static_cast<int>( row );
    const int c = static_cast<int>( pix ) - r * 16;
    // Scanline read correctness: band0 (pixel idx) and band1 (row) agree.
    REQUIRE( static_cast<int>( pix ) == r * 16 + c );
    REQUIRE( c >= 0 );
    REQUIRE( c < 16 );
    const int cls = res.y.at<int>( s, 0 );
    if ( r >= 4 && r < 8 && c >= 4 && c < 8 )
      REQUIRE( cls == 2 ); // overlap → last class wins
    else if ( r < 8 && c < 8 )
      REQUIRE( cls == 1 );
    else
      REQUIRE( cls == 2 );
  }
}

TEST_CASE(
  "Training extraction: pre-computed pixel indices are honored",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 16, 16, 1,
                []( int, int r, int c ) { return static_cast<float>( r * 16 + c ); } );

  // Geometry covers rows 0-7/cols 0-7; the cached indices for class 3 hit
  // pixel 0 (inside class-1 region, last wins) and pixel 15 (outside).
  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 16, 8, 8 ) ),
    roi( 3, QgsGeometry(), { 0, 15 } ),
  };

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms );
  REQUIRE( res.ok );
  REQUIRE( res.classCounts.value( 1 ) == 63 ); // pixel 0 stolen by class 3
  REQUIRE( res.classCounts.value( 3 ) == 2 );

  for ( int s = 0; s < res.X.rows; ++s )
  {
    const int p = static_cast<int>( res.X.at<float>( s, 0 ) );
    if ( p == 0 || p == 15 )
      REQUIRE( res.y.at<int>( s, 0 ) == 3 );
    else
      REQUIRE( res.y.at<int>( s, 0 ) == 1 );
  }
}

TEST_CASE(
  "Training extraction: band index out of range fails",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 8, 8, 2, []( int, int, int ) { return 1.0f; } );

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 3 }, { roi( 1, squareGeom( 0, 8, 8, 0 ) ) } );
  REQUIRE( !res.ok );
  REQUIRE( res.error == RsTrainingDataResult::Error::InvalidBand );
}

TEST_CASE(
  "Training extraction: OGR vector path with classField fallback",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  OGRRegisterAll();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 16, 16, 1,
                []( int, int r, int c ) { return static_cast<float>( r * 16 + c ); } );

  SECTION( "falls back to 'class' field" )
  {
    const QString vec = tmp.path() + "/train_class.gpkg";
    createVector( vec, QStringLiteral( "class" ), 2, 0, 16, 8, 8 );

    const RsTrainingDataResult res = RsTrainingDataExtraction::extractFromVector(
      raster, { 1 }, vec, QStringLiteral( "class_id" ) );
    REQUIRE( res.ok );
    REQUIRE( res.featuresRead == 1 );
    REQUIRE( res.X.rows == 64 );
    REQUIRE( res.classCounts.value( 2 ) == 64 );
  }

  SECTION( "falls back to 'id' field" )
  {
    const QString vec = tmp.path() + "/train_id.gpkg";
    createVector( vec, QStringLiteral( "id" ), 5, 8, 8, 16, 0 );

    const RsTrainingDataResult res = RsTrainingDataExtraction::extractFromVector(
      raster, { 1 }, vec, QStringLiteral( "class_id" ) );
    REQUIRE( res.ok );
    REQUIRE( res.classCounts.value( 5 ) == 64 );
  }

  SECTION( "exact classField match wins" )
  {
    const QString vec = tmp.path() + "/train_exact.gpkg";
    createVector( vec, QStringLiteral( "class_id" ), 7, 0, 8, 8, 0 );

    const RsTrainingDataResult res = RsTrainingDataExtraction::extractFromVector(
      raster, { 1 }, vec, QStringLiteral( "class_id" ) );
    REQUIRE( res.ok );
    REQUIRE( res.classCounts.value( 7 ) == 64 );
  }

  SECTION( "missing class field reports ClassFieldNotFound" )
  {
    const QString vec = tmp.path() + "/train_none.gpkg";
    createVector( vec, QStringLiteral( "other" ), 1, 0, 8, 8, 0 );

    const RsTrainingDataResult res = RsTrainingDataExtraction::extractFromVector(
      raster, { 1 }, vec, QStringLiteral( "class_id" ) );
    REQUIRE( !res.ok );
    REQUIRE( res.error == RsTrainingDataResult::Error::ClassFieldNotFound );
  }

  SECTION( "unreadable vector reports VectorOpenFailed" )
  {
    const RsTrainingDataResult res = RsTrainingDataExtraction::extractFromVector(
      raster, { 1 }, tmp.path() + "/missing.gpkg", QStringLiteral( "class_id" ) );
    REQUIRE( !res.ok );
    REQUIRE( res.error == RsTrainingDataResult::Error::VectorOpenFailed );
  }
}

TEST_CASE(
  "Training extraction: NoData and ignore-value filtering",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  // 8x8, all 50 except column 0 which is NoData (-9999).
  createRaster( raster, 8, 8, 1,
                []( int, int, int c ) { return c == 0 ? -9999.0f : 50.0f; },
                true, -9999.0 );

  const QVector<RsTrainingGeometry> geoms = { roi( 1, squareGeom( 0, 8, 8, 0 ) ) };

  SECTION( "source NoData pixels are dropped by default" )
  {
    const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
      raster, { 1 }, geoms );
    REQUIRE( res.ok );
    REQUIRE( res.X.rows == 56 );
    REQUIRE( res.classCounts.value( 1 ) == 56 );
    for ( int s = 0; s < res.X.rows; ++s )
      REQUIRE( res.X.at<float>( s, 0 ) == 50.0f );
  }

  SECTION( "useSourceNodata=false keeps them" )
  {
    RsTrainingDataExtraction::Options options;
    options.ignore.useSourceNodata = false;
    const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
      raster, { 1 }, geoms, options );
    REQUIRE( res.ok );
    REQUIRE( res.X.rows == 64 );
  }

  SECTION( "user ignore values can drain all samples" )
  {
    RsTrainingDataExtraction::Options options;
    options.ignore.ignoreValues = { 50.0 }; // NoData col dropped + all 50s
    const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
      raster, { 1 }, geoms, options );
    REQUIRE( !res.ok );
    REQUIRE( res.error == RsTrainingDataResult::Error::NoValidPixels );
  }

  SECTION( "minSamples policy" )
  {
    RsTrainingDataExtraction::Options options;
    options.minSamples = 100;
    const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
      raster, { 1 }, geoms, options );
    REQUIRE( !res.ok );
    REQUIRE( res.error == RsTrainingDataResult::Error::InsufficientSamples );

    options.minSamples = 56;
    const RsTrainingDataResult res2 = RsTrainingDataExtraction::extract(
      raster, { 1 }, geoms, options );
    REQUIRE( res2.ok );
    REQUIRE( res2.X.rows == 56 );
  }
}

TEST_CASE(
  "Training extraction: maxSamplesPerClass deterministic subsampling",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 16, 16, 1,
                []( int, int r, int c ) { return static_cast<float>( r * 16 + c ); } );

  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 16, 16, 0 ) ), // all 256 pixels
    roi( 2, squareGeom( 0, 4, 4, 0 ) ),   // 16 pixels (rows 12-15, cols 0-3)
  };

  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = 10;

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms, options );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows == 20 );
  REQUIRE( res.classCounts.value( 1 ) == 10 );
  REQUIRE( res.classCounts.value( 2 ) == 10 );

  // Same inputs → identical selection (mt19937(42) + shuffle).
  const RsTrainingDataResult res2 = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms, options );
  REQUIRE( res2.ok );
  REQUIRE( res2.X.rows == res.X.rows );
  for ( int s = 0; s < res.X.rows; ++s )
  {
    REQUIRE( res2.X.at<float>( s, 0 ) == res.X.at<float>( s, 0 ) );
    REQUIRE( res2.y.at<int>( s, 0 ) == res.y.at<int>( s, 0 ) );
  }

  // Unlimited keeps everything.
  const RsTrainingDataResult full = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms );
  REQUIRE( full.ok );
  REQUIRE( full.X.rows == 256 );
}

TEST_CASE(
  "Training extraction: progress sink can cancel",
  "[classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 8, 8, 1, []( int, int, int ) { return 1.0f; } );

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, { roi( 1, squareGeom( 0, 8, 8, 0 ) ) },
    RsTrainingDataExtraction::Options(),
    []( double, const QString & ) { return false; } );
  REQUIRE( !res.ok );
  REQUIRE( res.error == RsTrainingDataResult::Error::Cancelled );
}
