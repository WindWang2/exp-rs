// SICNU GEO RS — Happy-path test for QgsImageWarper::warpFile (WarpResult API).
//
// Verifies: 4-point pure-translation warp produces a GeoTIFF whose
// GeoTransform origin matches the GCP translation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsfeedback.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"
#include "warper_test_helpers.h"

#include <QTemporaryDir>
#include <QVector>
#include <gdal.h>
#include <gdal_priv.h>

using Catch::Approx;
using TM = QgsGcpTransformerInterface::TransformMethod;

TEST_CASE( "ImageWarper: pure translation produces correct GeoTransform", "[georef][warper]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = makeSynthetic64Raster( tmp.path() );
  REQUIRE_FALSE( src.isEmpty() );
  const QString out = tmp.path() + QStringLiteral( "/out.tif" );

  // 4 GCPs: (px,py) -> (px+100, py+200) in destination coords.
  // Note: src GCPs use the raster's own pixel coords; raster has no existing
  // GeoTransform so QgsGeorefTransform treats them directly.
  QVector<QgsPointXY> srcPts = { { 0, 0 }, { 63, 0 }, { 0, 63 }, { 63, 63 } };
  QVector<QgsPointXY> dstPts = { { 100, 200 }, { 163, 200 }, { 100, 263 }, { 163, 263 } };

  QgsGeorefTransform transform( TM::PolynomialOrder1 );
  REQUIRE( transform.updateParametersFromGcps( srcPts, dstPts, false ) );
  REQUIRE( transform.parametersInitialized() );

  QgsFeedback fb;
  QgsImageWarper warper( &fb );
  auto result = warper.warpFile(
    src, out, &transform,
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    /*useZeroAsTrans*/ false, /*zeroIsTransparent*/ false,
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ),
    QSize(), 1.0, 1.0 );

  INFO( "Warp error message: " << result.errorMessage.toStdString() );
  REQUIRE( result.status == QgsImageWarper::WarpStatus::Ok );
  REQUIRE( result.outputBytes > 0 );

  // Verify output GeoTransform.  X origin should sit near +100 (the
  // translation of the (0,0) source corner), Y origin near +263 (the top
  // of the warped extent given y-axis flip).
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen( out.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( ds );
  double gt[6];
  REQUIRE( GDALGetGeoTransform( ds, gt ) == CE_None );
  CHECK( gt[0] == Approx( 100.0 ).margin( 1.0 ) );
  CHECK( gt[3] == Approx( 263.0 ).margin( 1.0 ) );
  GDALClose( ds );
}
