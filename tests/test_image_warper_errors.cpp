// SICNU GEO RS — Failure-mode tests for QgsImageWarper.
//
// Covers:
//   - Unwritable output path -> GdalError (or DiskFull on weird systems)
//   - Singular GCP set -> SingularException from the analysis layer

#include <catch2/catch_test_macros.hpp>

#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsgcptransformer.h"
#include "qgsleastsquares.h"
#include "qgsfeedback.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"
#include "warper_test_helpers.h"

#include <QTemporaryDir>
#include <QVector>
#include <memory>

using TM = QgsGcpTransformerInterface::TransformMethod;

TEST_CASE( "ImageWarper: unwritable output path returns GdalError", "[georef][warper][error]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString src = makeSynthetic64Raster( tmp.path() );
  REQUIRE_FALSE( src.isEmpty() );

  // Use a path in a non-existent directory — GDALCreate() will fail with an
  // OS error.  We don't care which exact status we get; only that it is one
  // of the two "real" failure modes and that an error message is set.
  const QString out = QDir::tempPath() + QStringLiteral( "/sicnu_nonexistent_dir/nope/out.tif" );

  QgsGeorefTransform transform( TM::PolynomialOrder1 );
  QVector<QgsPointXY> s = { { 0, 0 }, { 63, 0 }, { 0, 63 } };
  QVector<QgsPointXY> d = { { 100, 200 }, { 163, 200 }, { 100, 263 } };
  REQUIRE( transform.updateParametersFromGcps( s, d, false ) );

  QgsFeedback fb;
  QgsImageWarper warper( &fb );
  auto result = warper.warpFile(
    src, out, &transform,
    QgsImageWarper::ResamplingMethod::NearestNeighbour,
    false, false,
    QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ),
    QSize(), 1.0, 1.0 );

  INFO( "errorMessage=" << result.errorMessage.toStdString() );
  REQUIRE( ( result.status == QgsImageWarper::WarpStatus::GdalError ||
             result.status == QgsImageWarper::WarpStatus::DiskFull ) );
  REQUIRE_FALSE( result.errorMessage.isEmpty() );
}

TEST_CASE( "Transform fit: collinear GCPs throws SingularException in solver and returns false in transformer", "[georef][transformer][error]" )
{
  // QgsLeastSquares::linear() (used by TM::Linear) throws
  // SingularException when deltaX == 0 || deltaY == 0.
  // All-x-equal GCPs force deltaX = n*sumPx^2 - (sumPx)^2 = 0.
  std::unique_ptr<QgsGcpTransformerInterface> t(
    QgsGcpTransformerInterface::create( TM::Linear ) );
  REQUIRE( t );

  QVector<QgsPointXY> s = {
    { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 }
  };
  QVector<QgsPointXY> d = {
    { 0, 0 }, { 0, 2 }, { 0, 4 }, { 0, 6 }
  };

  QgsPointXY origin;
  double scaleX = 0.0, scaleY = 0.0;
  REQUIRE_THROWS_AS(
    QgsLeastSquares::linear( s, d, origin, scaleX, scaleY ),
    QgsLeastSquares::SingularException );

  CHECK_FALSE( t->updateParametersFromGcps( s, d, false ) );
}
