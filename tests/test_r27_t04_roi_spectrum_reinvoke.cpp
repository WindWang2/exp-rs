// tests/test_r27_t04_roi_spectrum_reinvoke.cpp — R27-T04 (#1745)
//
// Contract under test: re-invoking the ROI mean-spectrum tool while a
// previous instance is alive must release the abandoned tool exactly once
// and never synchronously delete an object that deactivate() has already
// scheduled for deferred release (unsetMapTool → deactivate → deleteLater
// followed by a sync delete — the flagged double-free/UAF window).
//
// Observable discipline (#1745): after a re-invoke, the abandoned tool is
// still alive until the event loop runs (deferred release); draining the
// loop frees it exactly once. On HEAD the sync `delete abandoned` inside
// activateRoiSpectrumTool removes it immediately — the RED signal is the
// count inside the re-invoke slot.
//
// Stress oracle: 100 re-invokes (each abandoning an unfinished-polygon
// tool) + deferred-delete drains + full window destruction with the tool
// still active must not crash and must leave no live tool behind.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "support/qt_lifecycle.h"

#include "main_window.h"
#include "map_tools/rs_roi_spectrum_tool.h"

#include <QApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <vector>

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_roi_spectrum_reinvoke";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
      QCoreApplication::setApplicationName( QStringLiteral( "r27-t04-roi-reinvoke" ) );
      QSettings::setDefaultFormat( QSettings::IniFormat );
      QApplication *app = new QApplication( fake_argc, fake_argv );
      QSettings().clear();
      return app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  // 2-band 4x2 GeoTIFF (same fixture family as test_roi_spectrum_tool).
  QString makeRaster( const QString &path )
  {
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 8, 1.0f ) );
    for ( int row = 0; row < 2; ++row )
      for ( int col = 0; col < 4; ++col )
      {
        bands[0][static_cast<size_t>( row * 4 + col )] = static_cast<float>( row * 10 + col );
        bands[1][static_cast<size_t>( row * 4 + col )] = 100.0f + static_cast<float>( row * 10 + col );
      }
    QString err;
    REQUIRE( writeGdalOutput( path, 4, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
    return err;
  }

  int liveRoiTools( const QgisDesktopWindow &win )
  {
    return win.findChildren<RsRoiSpectrumTool *>().size();
  }
} // namespace

TEST_CASE( "ROI spectrum re-invoke releases the abandoned tool deferred, exactly once (#1745)",
           "[app][roi][r27t04]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rasterPath = tmp.filePath( QStringLiteral( "roi_reinvoke.tif" ) );
  REQUIRE( makeRaster( rasterPath ).isEmpty() );

  QgisDesktopWindow window;
  auto layer = std::make_unique<QgsRasterLayer>( rasterPath, QStringLiteral( "roi_reinvoke" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer.get() );
  window.mapCanvas()->setCurrentLayer( layer.get() );

  // First invoke: fresh tool, no polygon finished yet (m_finished == false),
  // so the *next* invoke hits the abandoned-tool disposal path.
  window.activateRoiSpectrumTool();
  REQUIRE( liveRoiTools( window ) == 1 );
  REQUIRE( window.mapCanvas()->mapTool() != nullptr );

  // Re-invoke while the polygon is unfinished. The abandoned tool must not
  // be freed synchronously inside the slot (#1745): deactivate() already
  // scheduled its deferred release via unsetMapTool.
  window.activateRoiSpectrumTool();
  REQUIRE( liveRoiTools( window ) == 2 );

  // Drain the event loop: the abandoned tool is freed exactly once, the
  // replacement stays alive as the active canvas tool.
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  QApplication::processEvents();
  REQUIRE( liveRoiTools( window ) == 1 );
  REQUIRE( window.mapCanvas()->mapTool() != nullptr );

  layer->setName( QStringLiteral( "kept" ) ); // layer still owned by project
  QgsProject::instance()->removeMapLayer( layer.get() );
  layer.release();
}

TEST_CASE( "ROI spectrum tool survives 100 re-invokes and window destruction (#1745)",
           "[app][roi][r27t04][stress]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rasterPath = tmp.filePath( QStringLiteral( "roi_stress.tif" ) );
  REQUIRE( makeRaster( rasterPath ).isEmpty() );

  QPointer<QgisDesktopWindow> windowGuard;
  {
    QgisDesktopWindow window;
    windowGuard = &window;
    auto layer = std::make_unique<QgsRasterLayer>( rasterPath, QStringLiteral( "roi_stress" ) );
    REQUIRE( layer->isValid() );
    QgsProject::instance()->addMapLayer( layer.get() );
    window.mapCanvas()->setCurrentLayer( layer.get() );

    for ( int i = 0; i < 100; ++i )
    {
      window.activateRoiSpectrumTool();
      // Drain deferred deletes every few rounds so pending releases never
      // pile up behind the loop (the production event loop does this
      // between any two user actions).
      if ( i % 3 == 0 )
      {
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        QApplication::processEvents();
      }
      REQUIRE( window.mapCanvas()->mapTool() != nullptr );
    }
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    QApplication::processEvents();
    REQUIRE( liveRoiTools( window ) == 1 );

    // Window destroyed with the ROI tool still the active map tool and its
    // polygon unfinished — the teardown order canvas/tool/band contract.
    QgsProject::instance()->removeMapLayer( layer.get() );
    layer.release();
  }
  QApplication::processEvents();
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  REQUIRE( windowGuard.isNull() );
}
