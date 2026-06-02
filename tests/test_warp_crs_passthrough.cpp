// SICNU GEO RS — CRS pass-through test for QgsImageWarper.
//
// When the source raster's CRS matches the destination CRS, the warp must
// succeed without performing a reprojection: the output GeoTransform should
// reflect only the GCP-derived affine.

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

TEST_CASE( "ImageWarper: source==target CRS does not reproject", "[georef][warper][crs]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = makeSynthetic64RasterWithCrs( tmp.path(), QStringLiteral( "EPSG:32650" ) );
  REQUIRE_FALSE( src.isEmpty() );
  const QString out = tmp.path() + QStringLiteral( "/out.tif" );

  QgsGeorefTransform transform( TM::PolynomialOrder1 );
  // Source raster already has a GeoTransform of (0,1,0, 64,0,-1) so its
  // map-coords run from (0,0) bottom-left to (64,64) top-right.  The GCPs
  // here are expressed in those map coords (because loadRaster wasn't
  // called); we translate the origin to (500,1000).
  QVector<QgsPointXY> s = { { 0, 0 }, { 63, 0 }, { 0, 63 } };
  QVector<QgsPointXY> d = { { 500, 1000 }, { 563, 1000 }, { 500, 1063 } };
  REQUIRE( transform.updateParametersFromGcps( s, d, false ) );

  QgsFeedback fb;
  QgsImageWarper warper( &fb );
  auto result = warper.warpFile(
    src, out, &transform,
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    false, false,
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32650" ) ),
    QSize(), 1.0, 1.0 );

  INFO( "errorMessage=" << result.errorMessage.toStdString() );
  REQUIRE( result.status == QgsImageWarper::WarpStatus::Ok );

  GDALAllRegister();
  GDALDatasetH ds = GDALOpen( out.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( ds );
  double gt[6];
  REQUIRE( GDALGetGeoTransform( ds, gt ) == CE_None );
  CHECK( gt[0] == Approx( 500.0 ).margin( 1.0 ) );
  CHECK( gt[3] == Approx( 1063.0 ).margin( 1.0 ) );

  // CRS round-trip: output projection should still be EPSG:32650-ish.
  const char *proj = GDALGetProjectionRef( ds );
  REQUIRE( proj );
  const QString projStr = QString::fromUtf8( proj );
  // WGS 84 / UTM zone 50N — match on the zone identifier rather than a
  // fragile substring of the WKT body.
  CHECK( ( projStr.contains( QStringLiteral( "UTM zone 50" ) ) ||
           projStr.contains( QStringLiteral( "32650" ) ) ) );
  GDALClose( ds );
}
