#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "rs_cv_task.h"
#include "rs_post_process_task.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

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
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  return info;
}

long submitPostProcess( std::unique_ptr<RsPostProcessTask> &task )
{
  sicnu::jobs::JobRequest request;
  request.algorithmId = "module:classify:postprocess";
  request.source = "test";

  return sicnu::TaskCenter::instance().submitJob(
    request,
    [worker = task.get()]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext &ctx ) {
      const bool ok = worker->run();
      if ( ctx.isCancelled()
           || ( !ok && worker->result().errorMessage == QStringLiteral( "Cancelled" ) ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !ok )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          worker->result().errorMessage.toStdString() );
      }
      Json::Value payload( Json::objectValue );
      payload["output"] = worker->config().outputRasterPath.toStdString();
      payload["durationMs"] = worker->result().durationMs;
      return payload;
    },
    [worker = task.get()] { worker->cancel(); } );
}

long submitCrossValidation( std::unique_ptr<RsCvTask> &task )
{
  sicnu::jobs::JobRequest request;
  request.algorithmId = "module:classify:cv";
  request.source = "test";

  return sicnu::TaskCenter::instance().submitJob(
    request,
    [worker = task.get()]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext &ctx ) {
      const bool ok = worker->run();
      if ( ctx.isCancelled()
           || ( !ok && worker->result().errorMessage == QStringLiteral( "Cancelled" ) ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !ok )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          worker->result().errorMessage.isEmpty()
            ? "Cross validation failed"
            : worker->result().errorMessage.toStdString() );
      }
      Json::Value payload( Json::objectValue );
      payload["meanAccuracy"] = worker->result().meanAccuracy;
      payload["stdAccuracy"] = worker->result().stdAccuracy;
      return payload;
    },
    [worker = task.get()] { worker->cancel(); } );
}

} // namespace

TEST_CASE( "Classification Task Center completes post-processing workers", "[classify][post]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  auto worker = std::make_unique<RsPostProcessTask>( validPostProcessConfig( tmp ) );
  const auto info = waitForTerminalTask( submitPostProcess( worker ) );

  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.resultPayload["output"].asString()
           == worker->config().outputRasterPath.toStdString() );
  REQUIRE( QFile::exists( worker->config().outputRasterPath ) );
}

TEST_CASE( "Classification Task Center reports post-processing failures", "[classify][post]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  auto cfg = validPostProcessConfig( tmp );
  cfg.inputPath.clear();
  auto worker = std::make_unique<RsPostProcessTask>( std::move( cfg ) );

  const auto failed = waitForTerminalTask( submitPostProcess( worker ) );

  REQUIRE( failed.status == sicnu::TaskStatus::Failed );
  REQUIRE( failed.errorMessage.contains( QStringLiteral( "Empty input path" ) ) );
}

TEST_CASE( "Classification Task Center keeps cancellation running until its worker exits", "[classify][cancel]" )
{
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  std::atomic_bool started = false;
  std::atomic_bool cancelHookCalled = false;
  std::atomic_bool releaseWorker = false;

  sicnu::jobs::JobRequest request;
  request.algorithmId = "callable:classification-cancel";
  request.source = "test";
  const long taskId = sicnu::TaskCenter::instance().submitJob(
    request,
    [&started, &releaseWorker]( const sicnu::jobs::JobRequest &,
                                sicnu::operators::RSOperatorContext & ) {
      started.store( true );
      while ( !releaseWorker.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      return Json::Value( Json::objectValue );
    },
    [&cancelHookCalled] { cancelHookCalled.store( true ); } );

  for ( int attempt = 0; attempt < 100 && !started.load(); ++attempt )
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  REQUIRE( started.load() );
  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  REQUIRE( cancelHookCalled.load() );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Running );
  releaseWorker.store( true );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Canceled );
}
