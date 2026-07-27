#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <gdal_priv.h>
#include <opencv2/core.hpp>

#include "processing/framework/task_center.h"
#include "qgsclassificationmainwindow.h"
#include "rs_classifier_normalbayes.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

// QGIS thread-local QgsProjContext may crash during glibc atexit cleanup
// once qgis_core/qgis_gui has been touched in-process. Mirror the
// FastExitListener pattern from test_georef_window so Catch reports
// results before the destructor sequence runs.
namespace
{
  class FastExitListener : public Catch::EventListenerBase
  {
    public:
      using Catch::EventListenerBase::EventListenerBase;
      void testRunEnded( const Catch::TestRunStats &stats ) override
      {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
      }
  };
}
CATCH_REGISTER_LISTENER( FastExitListener )

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      static QApplication app( fake_argc, fake_argv );
      return &app;
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  RsPostProcessConfig validPostProcessConfig( const QTemporaryDir &tmp )
  {
    GDALAllRegister();
    const QString inputPath = tmp.path() + QStringLiteral( "/labels.tif" );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( !driver )
      throw std::runtime_error( "GTiff driver is unavailable" );

    GDALDataset *dataset = driver->Create( inputPath.toUtf8().constData(),
                                           32, 32, 1, GDT_Int32, nullptr );
    if ( !dataset )
      throw std::runtime_error( "Could not create class-ID raster" );

    std::vector<int> labels( 32 * 32, 1 );
    const CPLErr result = dataset->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, 0, 32, 32, labels.data(), 32, 32, GDT_Int32, 0, 0 );
    GDALClose( dataset );
    if ( result != CE_None )
      throw std::runtime_error( "Could not write class-ID raster" );

    RsPostProcessConfig config;
    config.inputPath = inputPath;
    config.outputRasterPath = tmp.path() + QStringLiteral( "/post-process.tif" );
    config.runSieve = false;
    config.runMajority = false;
    config.runClump = false;
    config.runRecode = false;
    config.runPolygonize = false;
    config.creationOptions.clear();
    return config;
  }

  void makeGaussianData( cv::Mat &features, cv::Mat &labels )
  {
    cv::RNG rng( 42 );
    constexpr int perClass = 200;
    features.create( perClass * 3, 2, CV_32F );
    labels.create( perClass * 3, 1, CV_32S );
    for ( int i = 0; i < perClass; ++i )
    {
      features.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
      features.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
      labels.at<int>( i, 0 ) = 1;
      features.at<float>( perClass + i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
      features.at<float>( perClass + i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
      labels.at<int>( perClass + i, 0 ) = 2;
      features.at<float>( 2 * perClass + i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
      features.at<float>( 2 * perClass + i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
      labels.at<int>( 2 * perClass + i, 0 ) = 3;
    }
  }

  sicnu::AlgorithmTaskInfo waitForTerminalTask( long taskId )
  {
    sicnu::AlgorithmTaskInfo info;
    for ( int attempt = 0; attempt < 1000; ++attempt )
    {
      info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
      if ( info.status == sicnu::TaskStatus::Completed
           || info.status == sicnu::TaskStatus::Failed
           || info.status == sicnu::TaskStatus::Canceled )
        return info;
      QThread::msleep( 5 );
    }
    return info;
  }
}

TEST_CASE( "ClassificationWindow: constructs with 4 docks", "[classify][window]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassListDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassQuickListDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassJmDock" ) != nullptr );
  REQUIRE( w.findChild<QDockWidget *>( "rsClassSpectralDock" ) != nullptr );
}

TEST_CASE( "ClassificationWindow: title and central canvas", "[classify][window]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.windowTitle().contains( "Classification" ) );
  REQUIRE( w.centralWidget() != nullptr );
}

TEST_CASE( "ClassificationWindow: apply and preview submit through Task Center", "[classify][task_center]" )
{
  QFile source( QStringLiteral( SICNU_SOURCE_DIR "/src/app/classification/qgsclassificationmainwindow.cpp" ) );
  REQUIRE( source.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString text = QString::fromUtf8( source.readAll() );

  const auto methodBody = [&text]( const QString &signature, const QString &nextSignature ) {
    const int begin = text.indexOf( signature );
    REQUIRE( begin >= 0 );
    const int end = text.indexOf( nextSignature, begin + signature.size() );
    REQUIRE( end >= 0 );
    return text.mid( begin, end - begin );
  };

  const QString apply = methodBody(
    QStringLiteral( "void QgsClassificationMainWindow::applyClassification()" ),
    QStringLiteral( "void QgsClassificationMainWindow::applyPreview()" ) );
  const QString preview = methodBody(
    QStringLiteral( "void QgsClassificationMainWindow::applyPreview()" ),
    QStringLiteral( "void QgsClassificationMainWindow::openPostProcessDialog" ) );

  REQUIRE( apply.contains( QStringLiteral( "TaskCenter::instance().submitJob" ) ) );
  REQUIRE_FALSE( apply.contains( QStringLiteral( "RsJobRunner::run" ) ) );
  REQUIRE( preview.contains( QStringLiteral( "TaskCenter::instance().submitJob" ) ) );
  REQUIRE_FALSE( preview.contains( QStringLiteral( "RsJobRunner::run" ) ) );
}

TEST_CASE( "ClassificationWindow: public post-process start submits a Task Center job", "[classify][task_center]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  QgsClassificationMainWindow window( nullptr );
  const RsPostProcessConfig config = validPostProcessConfig( tmp );
  const long taskId = window.startPostProcessTask(
    config, false, QStringLiteral( "Test post-process" ),
    QStringLiteral( "module:classify:postprocess" ) );

  REQUIRE( taskId > 0 );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).algorithmId
           == QStringLiteral( "module:classify:postprocess" ) );
  REQUIRE_FALSE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).autoLoadLayer );
  REQUIRE_FALSE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).parameterMap
                   .value( QStringLiteral( "loadOutputsToMain" ) ).toBool() );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Completed );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  REQUIRE( QFile::exists( config.outputRasterPath ) );

  RsPostProcessConfig loadConfig = config;
  loadConfig.outputRasterPath = tmp.path() + QStringLiteral( "/post-process-loaded.tif" );
  const long loadTaskId = window.startPostProcessTask(
    loadConfig, true, QStringLiteral( "Test post-process load" ),
    QStringLiteral( "module:classify:postprocess" ) );

  REQUIRE( loadTaskId > 0 );
  const sicnu::AlgorithmTaskInfo loadInfo =
    sicnu::TaskCenter::instance().getTaskInfo( loadTaskId );
  REQUIRE_FALSE( loadInfo.autoLoadLayer );
  REQUIRE( loadInfo.parameterMap.value( QStringLiteral( "loadOutputsToMain" ) ).toBool() );
  REQUIRE( waitForTerminalTask( loadTaskId ).status == sicnu::TaskStatus::Completed );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  REQUIRE( QFile::exists( loadConfig.outputRasterPath ) );
}

TEST_CASE( "ClassificationWindow: public cross-validation start submits a Task Center job", "[classify][task_center]" )
{
  ensureApp();
  cv::Mat features;
  cv::Mat labels;
  makeGaussianData( features, labels );

  QgsClassificationMainWindow window( nullptr );
  QString resultDialogText;
  QTimer closeResultDialog;
  closeResultDialog.setInterval( 0 );
  QObject::connect( &closeResultDialog, &QTimer::timeout, [&resultDialogText] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *dialog = qobject_cast<QMessageBox *>( widget ) )
      {
        resultDialogText = dialog->text();
        dialog->accept();
      }
    }
  } );
  closeResultDialog.start();

  const long taskId = window.startCrossValidationTask(
    features, labels,
    []() -> std::unique_ptr<RsClassifierBackend> {
      return std::make_unique<RsClassifierNormalBayes>();
    } );

  REQUIRE( taskId > 0 );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).algorithmId
           == QStringLiteral( "module:classify:cv" ) );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Completed );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  closeResultDialog.stop();
  REQUIRE( resultDialogText.contains( QStringLiteral( "\n\n" ) ) );
  REQUIRE_FALSE( resultDialogText.contains( QStringLiteral( "\\n" ) ) );
  REQUIRE( window.centralWidget() != nullptr );
}
