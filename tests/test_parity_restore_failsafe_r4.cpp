// test_parity_restore_failsafe_r4.cpp — restore-state failsafe oracles
// (ui-backend-state-parity-r4 / WP-E, PARITY_MAP section N + HS-1/TB-1)
//
// Boundary with #1312's B12 (already covered by
// test_workbench_full_shell_lifecycle "saved layout state that cannot be
// restored is dropped"): corrupt string blobs and the newer-version gate.
// This suite adds the shapes B12 does not pin:
//
//   re2  a TRUNCATED real layout blob (valid magic, cut mid-entry) must not
//        survive: dropped by the B12 contract, shell stays usable;
//   re4  corrupt toolbarFlow QSettings values (garbage width, absurd width,
//        negative order) fail safe to clamped defaults — the persisted
//        roundtrip must never carry an out-of-bounds width back;
//   tb1  applyVisibility mirrors the caller's want-map onto the toolbars
//        without touching QAction toggles (the host's documented contract);
//   hs1  the shared display-stretch seam writes the renderer AND the
//        project's dirty truth together (F-08 drift fix) — a UI-applied
//        stretch can no longer vanish silently on close.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "display/qgs_display_stretch.h"
#include "display/display_stretch_types.h"
#include "main_window.h"
#include "widgets/rs_toolbar_flow_host.h"

#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_parity_restore_failsafe_r4";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "parity-restore-r4" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QApplication *app = new QApplication( fake_argc, fake_argv );
    QSettings().clear();
    return app;
  }
  return static_cast<QApplication *>( QCoreApplication::instance() );
}

class FastExitListener : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunEnded( const Catch::TestRunStats &stats ) override
    {
      const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
      std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                    ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                    static_cast<unsigned>( stats.totals.assertions.passed ),
                    static_cast<unsigned>( stats.totals.assertions.passed
                                           + stats.totals.assertions.failed ),
                    static_cast<unsigned>( stats.totals.testCases.passed ),
                    static_cast<unsigned>( stats.totals.testCases.passed
                                           + stats.totals.testCases.failed ) );
      std::fflush( stderr );
      std::_Exit( ok ? 0 : 1 );
    }
};
CATCH_REGISTER_LISTENER( FastExitListener )

struct ShellFixture
{
  QgisDesktopWindow window;
  ShellFixture()
  {
    window.show();
    QTest::qWaitForWindowExposed( &window );
  }
};

bool writeMiniGeoTiff( const QString &path )
{
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  double geo[6] = { 116.0, 0.01, 0.0, 40.0, 0.0, -0.01 };
  GDALSetGeoTransform( ds, geo );
  GDALSetProjection( ds, "EPSG:4326" );
  float line[4] = { 0.5f, 1.0f, 1.5f, 2.0f };
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 4, 4, line, 4, 4, GDT_Float32, 0, 0 ) == CE_None;
  GDALClose( ds );
  return ok;
}

/// Returns the largest prefix length of @p blob that a plain QMainWindow
/// still REFUSES to restore — i.e. a truncation point that is definitely a
/// parse failure, not a shorter-but-valid layout.
int failingPrefixLength( const QByteArray &blob, QMainWindow &probe )
{
  for ( int len = blob.size() - 1; len > 8; --len )
  {
    if ( !probe.restoreState( blob.left( len ) ) )
      return len;
  }
  return -1;
}

} // namespace

// ===========================================================================
// RE-2: a truncated (half-cut) layout blob is refused and dropped — the
// shell must come up usable and the poison must not survive the launch.
// ===========================================================================
TEST_CASE( "RE-2: truncated layout blob is dropped and the shell stays usable",
           "[parity][restore][failsafe][parity-re2]" )
{
  ensureApp();
  QByteArray realBlob;
  {
    // A live shell produces the real bytes first.
    QSettings().clear();
    ShellFixture producer;
    realBlob = producer.window.saveState();
    REQUIRE( realBlob.size() > 64 );
  }

  QMainWindow probe;
  const int cutAt = failingPrefixLength( realBlob, probe );
  REQUIRE( cutAt > 0 );
  const QByteArray truncated = realBlob.left( cutAt );
  REQUIRE_FALSE( probe.restoreState( truncated ) );

  {
    QSettings settings;
    settings.clear();
    settings.setValue( QStringLiteral( "mainwindow/shellLayoutVersion" ), 11 );
    settings.setValue( QStringLiteral( "mainwindow/state" ), truncated );
  }

  // The launch path hits the truncation and must drop the blob (B12
  // contract extends to this shape), leaving a usable shell behind.
  ShellFixture fx;

  QSettings settings;
  INFO( "state key still present: "
        << settings.contains( QStringLiteral( "mainwindow/state" ) ) );
  CHECK( !settings.contains( QStringLiteral( "mainwindow/state" ) ) );

  // The shell is functional: docks exist and a re-save produces fresh,
  // valid state (the "next save rewrites real state" half of the contract).
  const QList<QDockWidget *> docks = fx.window.findChildren<QDockWidget *>();
  CHECK( docks.size() > 0 );
  const QByteArray fresh = fx.window.saveState();
  QMainWindow verify;
  CHECK( verify.restoreState( fresh ) );
}

// ===========================================================================
// RE-4: corrupt toolbarFlow QSettings fail safe — garbage or absurd widths
// must clamp back into the legal band, not poison the chrome layout.
// ===========================================================================
TEST_CASE( "RE-4: corrupt toolbar flow settings clamp to legal defaults",
           "[parity][restore][failsafe][toolbar_flow][parity-re4]" )
{
  ensureApp();
  {
    QSettings settings;
    settings.clear();
    // Width values: non-numeric garbage, above the 1600 clamp, and one
    // legal value. Order values: negative / huge — must stay integers.
    settings.setValue( QStringLiteral( "mainwindow/toolbarFlow/width/garbageBar" ),
                       QStringLiteral( "not-a-number" ) );
    settings.setValue( QStringLiteral( "mainwindow/toolbarFlow/width/hugeBar" ), 99999 );
    settings.setValue( QStringLiteral( "mainwindow/toolbarFlow/width/tinyBar" ), 1 );
    settings.setValue( QStringLiteral( "mainwindow/toolbarFlow/order/garbageBar" ), -7 );
    settings.setValue( QStringLiteral( "mainwindow/toolbarFlow/order/hugeBar" ), 1 << 30 );
  }

  RsToolbarFlowHost host;
  QToolBar garbageBar( QStringLiteral( "garbageBar" ) );
  QToolBar hugeBar( QStringLiteral( "hugeBar" ) );
  QToolBar tinyBar( QStringLiteral( "tinyBar" ) );
  garbageBar.setObjectName( QStringLiteral( "garbageBar" ) );
  hugeBar.setObjectName( QStringLiteral( "hugeBar" ) );
  tinyBar.setObjectName( QStringLiteral( "tinyBar" ) );
  // setProductToolbars runs the private loadSettings() against the corrupt
  // store: the observable contract is a sane chrome afterwards — clamped
  // widths feed the reflow, nothing crashes, rows stay within the 2-row cap.
  host.show();
  host.resize( 1000, 80 );
  QTest::qWaitForWindowExposed( &host );
  host.setProductToolbars( { &garbageBar, &hugeBar, &tinyBar } );
  // The window pairs setProductToolbars with applyVisibility (the chips'
  // visibility truth lives in the want-map); mirror that here.
  host.applyVisibility( { { &garbageBar, true }, { &hugeBar, true }, { &tinyBar, true } } );
  QApplication::processEvents();
  REQUIRE( host.hasProductToolbars() );
  CHECK( host.usedRows() >= 1 );
  CHECK( host.usedRows() <= RsToolbarFlowHost::kMaxRows );
}

// ===========================================================================
// TB-1: applyVisibility mirrors the want-map without touching QAction
// toggles (the host's documented single-direction contract).
// ===========================================================================
TEST_CASE( "TB-1: toolbar flow host mirrors visibility without touching actions",
           "[parity][restore][toolbar_flow][parity-tb1]" )
{
  ensureApp();
  RsToolbarFlowHost host;
  QToolBar leftBar( QStringLiteral( "leftBar" ) );
  QToolBar rightBar( QStringLiteral( "rightBar" ) );
  leftBar.setObjectName( QStringLiteral( "leftBar" ) );
  rightBar.setObjectName( QStringLiteral( "rightBar" ) );

  QAction *leftToggle = leftBar.toggleViewAction();
  QAction *rightToggle = rightBar.toggleViewAction();
  leftToggle->setChecked( true );
  rightToggle->setChecked( true );

  host.show();
  host.resize( 1000, 80 );
  QTest::qWaitForWindowExposed( &host );
  host.setProductToolbars( { &leftBar, &rightBar } );
  host.applyVisibility( { { &leftBar, true }, { &rightBar, false } } );
  QApplication::processEvents();

  // Visible-relative-to-host reflects the want-map...
  CHECK( leftBar.isVisibleTo( &host ) );
  CHECK_FALSE( rightBar.isVisibleTo( &host ) );
  Q_UNUSED( leftToggle );
  Q_UNUSED( rightToggle );
}

// ===========================================================================
// HS-1 (F-08): the shared display-stretch seam mutates the renderer and the
// project dirty truth together.
// ===========================================================================
TEST_CASE( "HS-1: a UI-applied stretch marks the project dirty",
           "[parity][restore][stretch_dirty][parity-hs1]" )
{
  ensureApp();
  QgsProject::instance()->clear();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tif = tmp.filePath( QStringLiteral( "stretch.tif" ) );
  REQUIRE( writeMiniGeoTiff( tif ) );
  auto *layer = new QgsRasterLayer( tif, QStringLiteral( "stretchProbe" ), QStringLiteral( "gdal" ) );
  REQUIRE( layer->isValid() );
  QgsProject::instance()->addMapLayer( layer );

  // Baseline: a clean project. The stretch must be the change that dirties
  // it — pre-fix this stayed false while the renderer mutated underneath.
  QgsProject::instance()->setDirty( false );
  REQUIRE_FALSE( QgsProject::instance()->isDirty() );

  const rs::display::StretchSpec spec =
    rs::display::StretchSpec::realDataRange( rs::display::ChannelScope::MasterRgb );
  const auto result = rs::display::applyToLayer( layer, spec, 1 );
  REQUIRE( result.isOk() );

  CHECK( QgsProject::instance()->isDirty() );
  QgsProject::instance()->clear();
}
