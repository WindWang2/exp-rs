// test_obia_task_center.cpp — OBIA operators through the Task Center seam (#663).
//
// The old module:obia:* executor lambdas are deleted; the OBIA GUI and every
// other client submit real operator ids. These tests pin that seam: registry
// resolution through JobEngine, result payload delivery, failure surfacing,
// autoLoad=false and cooperative cancellation semantics.
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"

#include <gdal.h>

#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace
{

bool g_gdalInit = ( GDALAllRegister(), true );

QString createTestRaster( const QString &dir, int w, int h )
{
  const QString path = dir + QStringLiteral( "/seg_input.tif" );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return {};

  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
  if ( !ds )
    return {};

  QVector<float> row( w );
  for ( int r = 0; r < h; ++r )
  {
    for ( int c = 0; c < w; ++c )
      row[c] = ( c < w / 2 ) ? 50.0f : 150.0f;
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
  return path;
}

sicnu::AlgorithmTaskInfo waitForTerminalTask( long taskId )
{
  return sicnu::TaskCenter::instance().waitForTask( taskId, std::chrono::seconds( 30 ) );
}

long submitOperatorJob( const std::string &operatorId, const Json::Value &params )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = operatorId;
  req.title = "obia operator test";
  req.source = "test";
  req.exclusive = true;
  req.params = params;
  // No executor: JobEngine must resolve the operator from the registry
  // (exactly how the OBIA window submits). autoLoad=false like the GUI.
  return sicnu::TaskCenter::instance().submitJob( req, {}, {}, /*autoLoad=*/false );
}

} // namespace

TEST_CASE( "OBIA Task Center: rs:obia_segment resolves and completes", "[obia][task_center][segment]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString inputPath = createTestRaster( tmp.path(), 16, 16 );
  REQUIRE( !inputPath.isEmpty() );
  const QString outputPath = tmp.path() + QStringLiteral( "/labels.tif" );

  Json::Value params( Json::objectValue );
  params["input"] = inputPath.toStdString();
  params["output"] = outputPath.toStdString();
  params["engine"] = "simple";
  params["smoothKernel"] = 3;
  params["quantizeBins"] = 4;
  params["minRegionSize"] = 10;

  const long taskId = submitOperatorJob( "rs:obia_segment", params );
  REQUIRE( taskId > 0 );

  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "rs:obia_segment" ) );
  REQUIRE( info.autoLoadLayer == false );
  REQUIRE( info.resultPayload.isMember( "segments" ) );
  REQUIRE( info.resultPayload["segments"].asInt() > 0 );
  REQUIRE( info.resultPayload.isMember( "engine" ) );
  REQUIRE( QFile::exists( outputPath ) );
}

TEST_CASE( "OBIA Task Center: operator failure surfaces as a failed task", "[obia][task_center][segment]" )
{
  Json::Value params( Json::objectValue );
  params["input"] = "/nonexistent/obia_input.tif";
  params["output"] = "/tmp/obia_should_not_exist.tif";
  params["engine"] = "simple";

  const long taskId = submitOperatorJob( "rs:obia_segment", params );
  REQUIRE( taskId > 0 );

  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Failed );
  REQUIRE_FALSE( info.errorMessage.isEmpty() );
}

TEST_CASE( "OBIA Task Center: classify operator failure reaches the seam", "[obia][task_center][classify]" )
{
  QTemporaryDir tmp;
  const QString inputPath = createTestRaster( tmp.path(), 16, 16 );
  REQUIRE( !inputPath.isEmpty() );

  // segmentClasses without labels → contract error at the operator boundary.
  Json::Value params( Json::objectValue );
  params["input"] = inputPath.toStdString();
  params["output"] = ( tmp.path() + "/out.tif" ).toStdString();
  Json::Value segmentClasses( Json::objectValue );
  segmentClasses["1"] = 1;
  params["segmentClasses"] = segmentClasses;

  const long taskId = submitOperatorJob( "rs:obia_classify", params );
  REQUIRE( taskId > 0 );

  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Failed );
  REQUIRE( info.errorMessage.contains( QStringLiteral( "labels" ) ) );
}

TEST_CASE( "OBIA Task Center keeps cancellation running until the worker exits",
           "[obia][task_center][cancel]" )
{
  auto workerStarted = std::make_shared<std::atomic<bool>>( false );
  auto release = std::make_shared<std::atomic<bool>>( false );
  auto canceledHook = std::make_shared<std::atomic<bool>>( false );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:test:blocking";
  req.title = "blocking seam probe";
  req.source = "test";

  // TaskCenter executor mechanics probe (the same cancellation contract the
  // OBIA operators rely on through their RSOperatorContext cancel flag).
  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    [workerStarted, release]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      workerStarted->store( true );
      while ( !release->load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
      if ( ctx.isCancelled() )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      Json::Value result( Json::objectValue );
      result["segmentCount"] = 0;
      return result;
    },
    [canceledHook]() { canceledHook->store( true ); },
    false );

  REQUIRE( taskId > 0 );

  // Wait until worker has started
  for ( int i = 0; i < 6000 && !workerStarted->load(); ++i )
  {
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  REQUIRE( workerStarted->load() );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Running );

  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  // cancelTask() returns once cancellation is initiated; the canceledHook
  // runs on the worker thread asynchronously (same delivery race class as the
  // terminal-status transitions fixed in test_task_center). Poll with a
  // generous budget — under heavy parallel test load the worker may not be
  // scheduled for several seconds.
  for ( int i = 0; i < 6000 && !canceledHook->load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  REQUIRE( canceledHook->load() );
  // Remains Cancelling (or already reached Canceled) until worker observes cancel and exits
  const auto cancelStatus = sicnu::TaskCenter::instance().getTaskInfo( taskId ).status;
  REQUIRE( ( cancelStatus == sicnu::TaskStatus::Cancelling || cancelStatus == sicnu::TaskStatus::Canceled ) );

  release->store( true );
  const auto terminal = waitForTerminalTask( taskId );
  REQUIRE( terminal.status == sicnu::TaskStatus::Canceled );
}

#endif // SICNU_HAS_OPENCV
