// test_obia_task_center.cpp — #30 OBIA segmentation via Task Center seam
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_segmentation.h"
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
  sicnu::AlgorithmTaskInfo info;
  for ( int attempt = 0; attempt < 2000; ++attempt )
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

struct SegWork
{
  RsObiaSegmentationResult seg;
};

long submitSegmentation( const RsObiaSegmentationConfig &cfg,
                         std::shared_ptr<SegWork> work,
                         std::shared_ptr<std::atomic<bool>> canceled )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:obia:segment";
  req.title = "OBIA segmentation test";
  req.source = "test";
  req.exclusive = true;
  req.params["input"] = cfg.rasterPath.toStdString();

  return sicnu::TaskCenter::instance().submitJob(
    req,
    [cfg, work, canceled]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext &ctx ) {
      work->seg = RsObiaSegmentation::run( cfg, [canceled, &ctx]() {
        return canceled->load() || ctx.isCancelled();
      } );
      if ( ctx.isCancelled() || canceled->load()
           || work->seg.errorMessage.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !work->seg.ok )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          work->seg.errorMessage.toStdString() );
      }
      Json::Value result( Json::objectValue );
      result["segmentCount"] = static_cast<int>( work->seg.segMap.segmentCount() );
      result["usedOtb"] = work->seg.usedOtb;
      return result;
    },
    [canceled]() { canceled->store( true ); },
    /*autoLoad=*/false );
}

} // namespace

TEST_CASE( "OBIA Task Center completes flat segmentation without UI JobEngine submit",
           "[obia][task_center][segment]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString inputPath = createTestRaster( tmp.path(), 16, 16 );
  REQUIRE( !inputPath.isEmpty() );

  RsObiaSegmentationConfig cfg;
  cfg.rasterPath = inputPath;
  cfg.bandIndices = { 1 };
  cfg.preferOtb = false;
  cfg.smoothKernel = 3;
  cfg.quantizeBins = 4;
  cfg.minRegionSize = 10;

  auto work = std::make_shared<SegWork>();
  auto canceled = std::make_shared<std::atomic<bool>>( false );
  const long taskId = submitSegmentation( cfg, work, canceled );
  REQUIRE( taskId > 0 );

  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "module:obia:segment" ) );
  REQUIRE( info.resultPayload.isMember( "segmentCount" ) );
  REQUIRE( info.resultPayload["segmentCount"].asInt() > 0 );
  REQUIRE( work->seg.ok );
  REQUIRE( work->seg.segMap.segmentCount() > 0 );
}

TEST_CASE( "OBIA Task Center reports segmentation failure",
           "[obia][task_center][segment]" )
{
  RsObiaSegmentationConfig cfg;
  cfg.rasterPath.clear(); // invalid
  cfg.bandIndices = { 1 };
  cfg.preferOtb = false;

  auto work = std::make_shared<SegWork>();
  auto canceled = std::make_shared<std::atomic<bool>>( false );
  const long taskId = submitSegmentation( cfg, work, canceled );
  REQUIRE( taskId > 0 );

  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Failed );
  REQUIRE_FALSE( info.errorMessage.isEmpty() );
}

TEST_CASE( "OBIA Task Center hierarchy job id enters the same seam",
           "[obia][task_center][hierarchy]" )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:obia:hierarchy";
  req.title = "hierarchy seam";
  req.source = "test";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "fake hierarchy" );
      Json::Value result( Json::objectValue );
      result["levels"] = 2;
      result["fineSegments"] = 4;
      result["coarseSegments"] = 2;
      return result;
    },
    {},
    false );

  REQUIRE( taskId > 0 );
  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "module:obia:hierarchy" ) );
  REQUIRE( info.resultPayload["levels"].asInt() == 2 );
  REQUIRE( info.resultPayload["fineSegments"].asInt() == 4 );
}

TEST_CASE( "OBIA Task Center hierarchy_classify failure surfaces at the seam",
           "[obia][task_center][hierarchy_classify]" )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:obia:hierarchy_classify";
  req.title = "hierarchy classify fail";
  req.source = "test";

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value {
      throw sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::ComputationError, "no training labels" );
    },
    {},
    false );

  REQUIRE( taskId > 0 );
  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Failed );
  REQUIRE( info.errorMessage.contains( QStringLiteral( "no training labels" ) ) );
}

TEST_CASE( "OBIA Task Center flat classify algorithm id completes",
           "[obia][task_center][classify]" )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:obia:classify";
  req.title = "flat classify seam";
  req.source = "test";
  req.params["output"] = "/tmp/obia_out.tif";

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &request,
        sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "fake flat classify" );
      Json::Value result( Json::objectValue );
      result["output"] = request.params.get( "output", "" ).asString();
      result["durationMs"] = 1;
      return result;
    },
    {},
    false );

  REQUIRE( taskId > 0 );
  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "module:obia:classify" ) );
  REQUIRE( info.resultPayload["output"].asString() == "/tmp/obia_out.tif" );
}

TEST_CASE( "OBIA Task Center keeps cancellation running until the worker exits",
           "[obia][task_center][segment][cancel]" )
{
  auto workerStarted = std::make_shared<std::atomic<bool>>( false );
  auto release = std::make_shared<std::atomic<bool>>( false );
  auto canceledHook = std::make_shared<std::atomic<bool>>( false );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:obia:segment";
  req.title = "blocking segment";
  req.source = "test";

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    [workerStarted, release]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      workerStarted->store( true );
      while ( !release->load() && !ctx.isCancelled() )
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

  // Wait until the worker thread has actually started running the job
  for ( int i = 0; i < 1000 && !workerStarted->load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  REQUIRE( workerStarted->load() );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Running );

  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  for ( int i = 0; i < 6000 && !canceledHook->load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  REQUIRE( canceledHook->load() );

  release->store( true );
  const auto terminal = waitForTerminalTask( taskId );
  REQUIRE( terminal.status == sicnu::TaskStatus::Canceled );
}

#endif // SICNU_HAS_OPENCV
