// SICNU GEO RS — Cancellation test for QgsImageWarper.
//
// Verifies:
//   - warpFile honours QgsFeedback::cancel() via the GDAL progress callback
//   - exit happens within 500ms of the cancel signal
//   - the partial output file is removed on cancel

#include <catch2/catch_test_macros.hpp>

#include "qgsimagewarper.h"
#include "qgsgeoreftransform.h"
#include "qgsfeedback.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointxy.h"
#include "warper_test_helpers.h"

#include <QTemporaryDir>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QFuture>
#include <QFile>
#include <QThread>
#include <QVector>

using TM = QgsGcpTransformerInterface::TransformMethod;

TEST_CASE( "ImageWarper: cancellation exits within 500ms and removes output", "[georef][warper][cancel]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  // Larger raster so the warp takes more than a few ms — gives the cancel
  // signal a chance to arrive mid-flight.
  const QString src = makeSyntheticRaster( tmp.path(), 4096, 4096 );
  REQUIRE_FALSE( src.isEmpty() );
  const QString out = tmp.path() + QStringLiteral( "/out.tif" );

  QgsGeorefTransform transform( TM::PolynomialOrder1 );
  QVector<QgsPointXY> s = { { 0, 0 }, { 4095, 0 }, { 0, 4095 }, { 4095, 4095 } };
  QVector<QgsPointXY> d = { { 100, 200 }, { 4195, 200 }, { 100, 4295 }, { 4195, 4295 } };
  REQUIRE( transform.updateParametersFromGcps( s, d, false ) );

  QgsFeedback fb;
  QgsImageWarper warper( &fb );

  auto fut = QtConcurrent::run( [ & ] {
    return warper.warpFile(
      src, out, &transform,
      QgsImageWarper::ResamplingMethod::Cubic,
      false, false,
      QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ),
      QSize(), 1.0, 1.0 );
  } );

  // Give the warp a head-start so we actually exercise the in-flight cancel
  // path (vs. cancelling before the warp loop has started).
  QThread::msleep( 10 );

  QElapsedTimer t;
  t.start();
  fb.cancel();
  auto result = fut.result();
  const qint64 exitMs = t.elapsed();

  INFO( "exitMs=" << exitMs << " errorMessage=" << result.errorMessage.toStdString() );
  REQUIRE( result.status == QgsImageWarper::WarpStatus::Cancelled );
  REQUIRE( exitMs <= 500 );
  REQUIRE_FALSE( QFile::exists( out ) );
}
