// tests/test_r27_t04_classification_orphan.cpp — R27-T04 (#1746)
//
// Contract under test:
//   1. A sample feature whose cls_id names no existing class (orphan
//      classId — the sample layer is user-editable) must never reach the
//      spectral-curve accumulators. On HEAD the orphan default-inserted an
//      empty per-band vector via QHash::operator[] and the first
//      `bandSums[classId][bi] += val` wrote through a null/zero-size
//      allocation — an out-of-bounds heap write (observed as a null-deref
//      crash on empty vectors).
//   2. The skip is visible: the status bar names the skipped samples /
//      ROIs, and the displayed curves contain only known classes.
//   3. Merge discipline (structural pin): a rolled-back sample relabel must
//      abort the merge before any class definition is dropped — otherwise
//      every remaining ROI of the source classes is orphaned.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "classification/qgsclassificationmainwindow.h"
#include "classification/rs_spectral_curve_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <gdal.h>
#include <gdal_priv.h>

#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
#include <qgsvectorlayer.h>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_classification_orphan";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      return new QApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  /// 32x32, 3-band Float32 unit-geotransform raster (same as r26 suite).
  QString makeThreeBandRaster( const QString &path )
  {
    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( !driver )
      return QStringLiteral( "GTiff driver unavailable" );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(),
                                           32, 32, 3, GDT_Float32, nullptr );
    if ( !dataset )
      return QStringLiteral( "could not create raster" );
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    dataset->SetGeoTransform( gt );
    for ( int b = 1; b <= 3; ++b )
    {
      GDALRasterBand *band = dataset->GetRasterBand( b );
      std::vector<float> line( 32, 10.f * b );
      for ( int row = 0; row < 32; ++row )
        band->RasterIO( GF_Write, 0, row, 32, 1, line.data(), 32, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( dataset );
    return {};
  }

  QgsVectorLayer *sampleLayerOf( QgsClassificationMainWindow &w )
  {
    return w.findChild<QgsVectorLayer *>( QStringLiteral( "rsClassifySampleLayer" ) );
  }

  void addSample( QgsVectorLayer *layer, int classId, double x0, double y0,
                  double size = 5.0 )
  {
    if ( !layer )
      return;
    if ( !layer->isEditable() )
      layer->startEditing();
    QgsFeature f( layer->fields() );
    QgsPolygonXY poly;
    QgsPolylineXY ring = { QgsPointXY( x0, y0 ), QgsPointXY( x0 + size, y0 ),
                           QgsPointXY( x0 + size, y0 + size ),
                           QgsPointXY( x0, y0 + size ), QgsPointXY( x0, y0 ) };
    poly << ring;
    f.setGeometry( QgsGeometry::fromPolygonXY( poly ) );
    f.setAttribute( QStringLiteral( "cls_id" ), classId );
    layer->addFeature( f );
    layer->commitChanges();
    QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  }
} // namespace

TEST_CASE( "Classification: orphan classId samples are skipped, never OOB (#1746)",
           "[classify][orphan][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString raster = tmp.filePath( QStringLiteral( "src.tif" ) );
  REQUIRE( makeThreeBandRaster( raster ).isEmpty() );

  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.openSourceRaster( raster ) );

  auto *sampleLayer = sampleLayerOf( w );
  REQUIRE( sampleLayer != nullptr );

  // One healthy sample for a real class …
  addSample( sampleLayer, 1, 0.0, 0.0 );
  // … and one whose cls_id names no class in the table (hand-edited field).
  addSample( sampleLayer, 99, 10.0, 10.0 );
  REQUIRE( sampleLayer->featureCount() == 2 );

  // HEAD RED: this call crashed writing through the orphan's empty band
  // vector (bandSums[99] default-inserts; [0] += writes out of bounds).
  // #1794: the recompute runs on the bounded scan pool — pump until the
  // curves land (bounded wait, fail on timeout).
  w.recomputeSpectralCurves();
  auto *curveWidget0 = w.findChild<RsSpectralCurveWidget *>();
  REQUIRE( curveWidget0 != nullptr );
  QElapsedTimer timer;
  timer.start();
  while ( curveWidget0->curves().isEmpty() )
  {
    REQUIRE( timer.elapsed() < 10000 );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }

  auto *curveWidget = w.findChild<RsSpectralCurveWidget *>();
  REQUIRE( curveWidget != nullptr );
  const QVector<RsSpectralCurveWidget::Curve> curves = curveWidget->curves();
  REQUIRE( curves.size() == 1 );
  REQUIRE( curves.at( 0 ).classId == 1 );
  // Mean of band 1 over the 5x5 sample of a constant 10.0 band.
  REQUIRE( curves.at( 0 ).bandMeans.at( 0 ) == Catch::Approx( 10.0 ).margin( 1e-9 ) );

  // The skip is visible in the status bar (both the rebuild gate and the
  // recompute gate report; at least one must have fired).
  const QString status = w.statusBar()->currentMessage();
  INFO( "status: " << status.toStdString() );
  CHECK( status.contains( QStringLiteral( "skip" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "Classification: merge rollback keeps class definitions (source contract, #1746)",
           "[classify][merge][r27t04]" )
{
  // A rolled-back relabel cannot be induced deterministically on a memory
  // layer in-process; pin the structural guarantee instead (same approach
  // as the r26 source-scan contracts): the rollBack branch must leave the
  // function before any class definition is dropped, so no ROI can become
  // an orphan when the commit failed.
  QFile source( QStringLiteral( CMAKE_SOURCE_DIR "/src/app/classification/qgsclassificationmainwindow.cpp" ) );
  REQUIRE( source.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString text = QString::fromUtf8( source.readAll() );

  const qsizetype mergeBegin = text.indexOf( QStringLiteral( "void QgsClassificationMainWindow::runMergeClasses" ) );
  REQUIRE( mergeBegin >= 0 );
  const qsizetype mergeEnd = text.indexOf( QStringLiteral( "\nvoid QgsClassificationMainWindow::" ), mergeBegin + 10 );
  REQUIRE( mergeEnd > mergeBegin );
  const QString body = text.mid( mergeBegin, mergeEnd - mergeBegin );

  const qsizetype rollbackPos = body.indexOf( QStringLiteral( "rollBack()" ) );
  REQUIRE( rollbackPos >= 0 );
  const qsizetype afterRollback = body.indexOf( QChar::LineFeed, rollbackPos );
  const qsizetype mergeBodyEnd = body.indexOf( QStringLiteral( "setClassDefs" ), rollbackPos );
  CHECK( mergeBodyEnd < 0 ); // RED pre-fix: defs were dropped after rollback

  // The rollback branch must exit the merge before the recode runs.
  const QString rollbackBlock = body.mid( rollbackPos, afterRollback - rollbackPos + 200 );
  CHECK( rollbackBlock.contains( QStringLiteral( "return" ) ) );
}
