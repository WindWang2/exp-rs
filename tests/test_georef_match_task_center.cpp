// test_georef_match_task_center.cpp — #34 SIFT/template via Task Center seam
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "app/georeferencer/rs_georeferencing_session.h"

#include <chrono>
#include <thread>
#include <atomic>
#include <memory>

namespace
{

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

} // namespace

TEST_CASE( "Georef match Task Center completes SIFT algorithm id",
           "[georef][match][task_center][sift]" )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:sift";
  req.title = "sift seam";
  req.source = "test";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "fake sift" );
      Json::Value result( Json::objectValue );
      result["totalMatches"] = 10;
      result["inliers"] = 8;
      result["inlierRatio"] = 0.8;
      return result;
    },
    {},
    false );

  REQUIRE( taskId > 0 );
  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "module:georef:sift" ) );
  REQUIRE( info.resultPayload["inliers"].asInt() == 8 );
}

TEST_CASE( "Georef match Task Center completes template_match algorithm id",
           "[georef][match][task_center][template]" )
{
  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:template_match";
  req.title = "template seam";
  req.source = "test";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    []( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "fake template" );
      Json::Value result( Json::objectValue );
      result["accepted"] = 5;
      result["attempted"] = 12;
      return result;
    },
    {},
    false );

  REQUIRE( taskId > 0 );
  const auto info = waitForTerminalTask( taskId );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( info.algorithmId == QStringLiteral( "module:georef:template_match" ) );
  REQUIRE( info.resultPayload["accepted"].asInt() == 5 );
}

TEST_CASE( "Georef match Task Center cancel waits for worker exit",
           "[georef][match][task_center][cancel]" )
{
  auto release = std::make_shared<std::atomic<bool>>( false );
  auto canceledHook = std::make_shared<std::atomic<bool>>( false );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:sift";
  req.title = "blocking sift";
  req.source = "test";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    [release]( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      while ( !release->load() && !ctx.isCancelled() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
      if ( ctx.isCancelled() )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      Json::Value result( Json::objectValue );
      result["inliers"] = 0;
      return result;
    },
    [canceledHook]() { canceledHook->store( true ); },
    false );

  REQUIRE( taskId > 0 );
  for ( int i = 0; i < 500; ++i )
  {
    if ( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status == sicnu::TaskStatus::Running )
      break;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Running );

  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  // The canceledHook runs on the worker thread asynchronously; under heavy
  // parallel test load the worker may not be scheduled for several seconds,
  // so poll with a generous budget instead of asserting immediately.
  for ( int i = 0; i < 6000 && !canceledHook->load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  REQUIRE( canceledHook->load() );
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Running );

  release->store( true );
  const auto terminal = waitForTerminalTask( taskId );
  REQUIRE( terminal.status == sicnu::TaskStatus::Canceled );
}

TEST_CASE( "Accept path pushes match pairs into Session",
           "[georef][match][session]" )
{
  // Simulates bulk-Yes acceptance: pairs go straight into the session.
  RsGeoreferencingSession session;
  QVector<QgsGcpPoint> fromSift;
  fromSift.append( QgsGcpPoint( QgsPointXY( 0, 0 ), QgsPointXY( 100, 200 ), QgsCoordinateReferenceSystem(), true ) );
  fromSift.append( QgsGcpPoint( QgsPointXY( 10, 10 ), QgsPointXY( 110, 210 ), QgsCoordinateReferenceSystem(), true ) );
  session.appendGcps( fromSift );
  REQUIRE( session.gcps().size() == 2 );
}
