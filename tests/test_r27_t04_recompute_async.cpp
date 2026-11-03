// tests/test_r27_t04_recompute_async.cpp — R27-T04 (#1794)
//
// Contract under test: QgsClassificationMainWindow::recomputeSpectralCurves
// performs its scanline GDAL re-read OFF the GUI thread (bounded scan pool)
// and lands curves through the event loop; a superseding recompute drops
// the stale landing. On HEAD the full read ran synchronously inside the
// slot — the RED signal is curves set immediately after the call.
// (Numeric fidelity of the landed curves is pinned by
// test_r27_t04_classification_orphan.cpp.)
#include <catch2/catch_test_macros.hpp>

#include "classification/qgsclassificationmainwindow.h"
#include "classification/rs_spectral_curve_widget.h"

#include <QApplication>
#include <QElapsedTimer>
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
  char fake_argv0[] = "test_r27_t04_recompute_async";
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

  QString makeRaster( const QString &path )
  {
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), 48, 48, 3, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    ds->SetGeoTransform( gt );
    for ( int b = 1; b <= 3; ++b )
    {
      std::vector<float> line( 48, 5.f * b );
      for ( int r = 0; r < 48; ++r )
        ds->GetRasterBand( b )->RasterIO( GF_Write, 0, r, 48, 1, line.data(), 48, 1,
                                          GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return {};
  }

  void addSample( QgsVectorLayer *layer, int classId, double x0, double y0, double size = 6.0 )
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

  QgsVectorLayer *sampleLayerOf( QgsClassificationMainWindow &w )
  {
    return w.findChild<QgsVectorLayer *>( QStringLiteral( "rsClassifySampleLayer" ) );
  }
} // namespace

TEST_CASE( "Spectral-curve recompute runs off the GUI thread (#1794)",
           "[classify][recompute][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString raster = tmp.filePath( QStringLiteral( "src.tif" ) );
  REQUIRE( makeRaster( raster ).isEmpty() );

  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.openSourceRaster( raster ) );
  addSample( sampleLayerOf( w ), 1, 2.0, -20.0 );

  auto *curves = w.findChild<RsSpectralCurveWidget *>();
  REQUIRE( curves != nullptr );
  REQUIRE( curves->curves().isEmpty() );

  w.recomputeSpectralCurves();

  // RED on HEAD: the curves were computed synchronously inside the slot.
  REQUIRE( curves->curves().isEmpty() );

  QElapsedTimer t;
  t.start();
  while ( curves->curves().isEmpty() )
  {
    REQUIRE( t.elapsed() < 10000 );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }
  REQUIRE( curves->curves().size() == 1 );

  // A second dispatch stays in flight at scope exit; window destruction
  // runs the destructor's scan cancellation + the QPointer-guarded landing
  // drop (exercised by the destructor path itself, no post-teardown pump).
  w.recomputeSpectralCurves();
  QApplication::processEvents();
}
