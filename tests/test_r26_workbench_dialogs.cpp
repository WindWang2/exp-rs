// test_r26_workbench_dialogs.cpp — R26-02 workbench: dialog terminal-state
// honesty. The change-detection dialog's status label used to stay
// "Running..." forever on failed/shapeless runs because only the
// has-statistics success callback ever reset it.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <array>
#include <functional>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/change_detection_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r26_workbench_dialogs";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

QgsRasterLayer *registerRaster( const QString &path, const QString &name )
{
  auto *layer = new QgsRasterLayer( path, name );
  if ( !layer->isValid() )
  {
    delete layer;
    return nullptr;
  }
  QgsProject::instance()->addMapLayer( layer );
  return layer;
}

void pumpUntil( const std::function<bool()> &done, int maxMs = 15000 )
{
  QElapsedTimer timer;
  timer.start();
  while ( !done() && timer.elapsed() < maxMs )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 25 );
    QThread::msleep( 10 );
  }
}

}  // namespace

TEST_CASE( "ChangeDetectionDialog status label leaves the Running state on every terminal path",
           "[app][dialogs][change_detection][r26-workbench]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString beforePath = dir.filePath( QStringLiteral( "before.tif" ) );
  const QString afterPath = dir.filePath( QStringLiteral( "after.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( beforePath, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
  REQUIRE( writeGdalOutput( afterPath, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  QgsRasterLayer *before = registerRaster( beforePath, QStringLiteral( "before" ) );
  QgsRasterLayer *after = registerRaster( afterPath, QStringLiteral( "after" ) );
  REQUIRE( before != nullptr );
  REQUIRE( after != nullptr );

  // Dismiss any critical error box the failure path raises, and remember it.
  QTimer dismissCritical;
  dismissCritical.setInterval( 0 );
  int criticalBoxes = 0;
  QObject::connect( &dismissCritical, &QTimer::timeout, [&criticalBoxes] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *box = qobject_cast<QMessageBox *>( widget ) )
      {
        if ( box->icon() == QMessageBox::Critical )
        {
          ++criticalBoxes;
          box->accept();
        }
      }
    }
  } );
  dismissCritical.start();

  ChangeDetectionDialog dialog;
  dialog.setRasterLayer( before );
  auto *beforePick = dialog.findChild<QComboBox *>( QStringLiteral( "cdBeforeCombo" ) );
  auto *afterPick = dialog.findChild<QComboBox *>( QStringLiteral( "cdAfterCombo" ) );
  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  auto *runButton = dialog.findChild<QPushButton *>( QStringLiteral( "rsPrimaryButton" ) );
  auto *statusLabel = dialog.findChild<QLabel *>( QStringLiteral( "rsChangeDetectionStatus" ) );
  REQUIRE( beforePick != nullptr );
  REQUIRE( afterPick != nullptr );
  REQUIRE( outputEdit != nullptr );
  REQUIRE( runButton != nullptr );
  REQUIRE( statusLabel != nullptr );
  beforePick->setCurrentIndex( beforePick->findData( before->id() ) );
  afterPick->setCurrentIndex( afterPick->findData( after->id() ) );

  SECTION( "a failed run resets the status label (no stale Running...)" )
  {
    // Output under a path that cannot be created: the write must fail inside
    // the operator, exercising the dialog's failure terminal path.
    outputEdit->setText( beforePath + QStringLiteral( "/not-a-dir/change.tif" ) );

    runButton->click();
    pumpUntil( [&] { return criticalBoxes > 0; } );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );

    REQUIRE( criticalBoxes > 0 );  // the failure really happened
    CHECK( statusLabel->text() != QStringLiteral( "Running..." ) );
    dismissCritical.stop();
  }

  SECTION( "a successful run produces a real output raster (operator E2E)" )
  {
    const QString outputPath = dir.filePath( QStringLiteral( "change.tif" ) );
    outputEdit->setText( outputPath );

    runButton->click();
    // The file lands before the GUI-thread completion callback is delivered;
    // wait on the observable end state (label left Running), then assert both.
    pumpUntil( [&] { return statusLabel->text() != QStringLiteral( "Running..." ); } );
    QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );

    CHECK( QFileInfo::exists( outputPath ) );
    // Success also has to leave the Running state — AND the statistics the
    // result callback wrote must survive cleanupRunResources (R1 P1-1).
    CHECK( statusLabel->text() != QStringLiteral( "Running..." ) );
    CHECK_FALSE( statusLabel->text().contains( QStringLiteral( "Run ended" ) ) );
    dismissCritical.stop();
  }

  QgsProject::instance()->removeMapLayer( before );
  QgsProject::instance()->removeMapLayer( after );
}
