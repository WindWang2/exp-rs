// tests/test_r27_t04_cross_section_async.cpp — R27-T04 (#1790)
//
// Contract under test: CrossSectionWidget::extractProfile performs its
// GDALOpen + per-sample reads OFF the GUI thread (bounded scan pool) and
// lands through the event loop; a superseding request (new profile, layer
// switch, clear) drops the stale landing. On HEAD the whole sampling ran
// synchronously inside extractProfile — the RED signal is a synchronous
// result right after the call.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "widgets/cross_section_widget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <qgsrasterlayer.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <vector>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_cross_section_async";
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

  QString makeRaster( const QString &path, int W = 64, int H = 64 )
  {
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    ds->SetGeoTransform( gt );
    GDALRasterBand *band = ds->GetRasterBand( 1 );
    std::vector<float> line( W );
    for ( int r = 0; r < H; ++r )
    {
      for ( int c = 0; c < W; ++c )
        line[static_cast<size_t>( c )] = static_cast<float>( r * W + c );
      band->RasterIO( GF_Write, 0, r, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return {};
  }

  void pump( int ms )
  {
    QElapsedTimer t;
    t.start();
    while ( t.elapsed() < ms )
    {
      QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
      QThread::msleep( 5 );
    }
  }
} // namespace

TEST_CASE( "CrossSectionWidget samples off the GUI thread and lands async (#1790)",
           "[widgets][crosssection][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString raster = tmp.filePath( QStringLiteral( "transect.tif" ) );
  REQUIRE( makeRaster( raster ).isEmpty() );

  QgsRasterLayer layer( raster, QStringLiteral( "transect" ) );
  REQUIRE( layer.isValid() );

  CrossSectionWidget w;
  w.setRasterLayer( &layer );
  w.resize( 500, 300 );

  w.extractProfile( QgsPointXY( 0.5, -3.5 ), QgsPointXY( 50.5, -3.5 ) );

  // RED on HEAD: the sampling completed synchronously inside the call.
  REQUIRE_FALSE( w.hasData() );

  // Pump until the worker lands the profile (bounded wait).
  QElapsedTimer t;
  t.start();
  while ( !w.hasData() )
  {
    REQUIRE( t.elapsed() < 10000 );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
    QThread::msleep( 5 );
  }

  // Values are correct: gt {0,1,0,0,0,-1} maps y=-3.5 to row 3 (pixel
  // centers), so the transect samples row 3, columns 0..50:
  // value = 3*64 + col.
  REQUIRE( w.values().size() == 50 );
  CHECK( w.values().front() == Catch::Approx( 3.0 * 64 + 0 ).margin( 1e-9 ) );
  CHECK( w.values().back() == Catch::Approx( 3.0 * 64 + 50 ).margin( 1e-9 ) );

  // A superseding extract drops the in-flight landing: start one, then
  // clear — after pumping, no landing repaints data into the cleared state.
  w.extractProfile( QgsPointXY( 0.5, -30.5 ), QgsPointXY( 50.5, -30.5 ) );
  w.clear();
  pump( 500 );
  CHECK_FALSE( w.hasData() );
  CHECK( w.values().isEmpty() );
}
