// tests/test_agent_run_coordinator.cpp
//
// AgentRunCoordinator state-machine coverage using injected stubs.  Avoids real
// TaskCenter/ExecutionPlane to keep the tests deterministic and fast.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <thread>

#include <gdal.h>

#include "agent/agent_run_coordinator.h"
#include "data/data_manager.h"

using namespace sicnu::agent;

namespace
{

QCoreApplication *ensureCoreApp()
{
  if ( QCoreApplication::instance() )
    return QCoreApplication::instance();
  static int argc = 1;
  static char arg0[] = "test_agent_run_coordinator";
  static char *argv[] = { arg0, nullptr };
  // Heap-allocated and intentionally leaked: static QCoreApplication would
  // destruct after Catch2's main and trigger double-free with QGIS singletons.
  return new QCoreApplication( argc, argv );
}

AgentRunRequest simpleRequest()
{
  AgentRunRequest req;
  req.algorithmId = QStringLiteral( "rs:ndvi" );
  req.userRequest = QStringLiteral( "compute NDVI" );
  return req;
}

Json::Value makeSuccessPayload( const QString &outputPath )
{
  Json::Value v( Json::objectValue );
  v["status"] = "success";
  v["output"] = outputPath.toStdString();
  return v;
}

Json::Value makeErrorPayload( const QString &message )
{
  Json::Value v( Json::objectValue );
  v["status"] = "error";
  v["errorMessage"] = message.toStdString();
  return v;
}

Json::Value makePreflight( bool valid )
{
  Json::Value v( Json::objectValue );
  v["valid"] = valid;
  return v;
}

OutputVerification makeVerification( bool ok )
{
  OutputVerification v;
  v.ok = ok;
  v.kind = QStringLiteral( "raster" );
  if ( !ok )
    v.issues.append( QStringLiteral( "all pixels are NoData" ) );
  return v;
}

} // namespace

TEST_CASE( "AgentRunCoordinator happy path reaches Completed", "[agent][coordinator]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );
  AgentRunCoordinator coordinator;

  std::vector<AgentRunStage> stages;
  QObject::connect( &coordinator, &AgentRunCoordinator::runStageChanged,
                    [&]( const AgentRun &run ) { stages.push_back( run.stage ); } );

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  coordinator.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
    return makeSuccessPayload( outPath );
  } );
  coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );

  REQUIRE( run.stage == AgentRunStage::Completed );
  REQUIRE( stages.size() >= 6 );
  REQUIRE( stages[0] == AgentRunStage::Understanding );
  REQUIRE( stages[1] == AgentRunStage::Planning );
  REQUIRE( stages[2] == AgentRunStage::Preflight );
  REQUIRE( stages[3] == AgentRunStage::Running );
  REQUIRE( stages[4] == AgentRunStage::Verifying );
  REQUIRE( stages[5] == AgentRunStage::Presenting );
  REQUIRE( stages.back() == AgentRunStage::Completed );
}

TEST_CASE( "AgentRunCoordinator preflight failure becomes Failed", "[agent][coordinator]" )
{
  ensureCoreApp();
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( false ); } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );
  REQUIRE( run.stage == AgentRunStage::Failed );
}

TEST_CASE( "AgentRunCoordinator repairs execution failure and then succeeds", "[agent][coordinator]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );

  int attempts = 0;
  coordinator.setExecuteFunction( [&attempts, outPath]( const QString &, const QVariantMap &params, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    // Repair strategy sets a "fixed" key; first attempt without it fails.
    if ( params.contains( QStringLiteral( "fixed" ) ) )
      return makeSuccessPayload( outPath );
    return makeErrorPayload( QStringLiteral( "missing fixed" ) );
  } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
    QVariantMap repaired;
    repaired[QStringLiteral( "fixed" )] = true;
    return repaired;
  } );
  coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );

  REQUIRE( attempts == 2 );
  REQUIRE( run.repairAttempts == 1 );
  REQUIRE( run.stage == AgentRunStage::Completed );
}

TEST_CASE( "AgentRunCoordinator gives up after max repair attempts", "[agent][coordinator]" )
{
  ensureCoreApp();
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );

  int attempts = 0;
  coordinator.setExecuteFunction( [&attempts]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    return makeErrorPayload( QStringLiteral( "always fails" ) );
  } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
    return QVariantMap(); // always claims repairable
  } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );

  REQUIRE( attempts == 3 ); // initial + 2 repairs
  REQUIRE( run.repairAttempts == 2 );
  REQUIRE( run.stage == AgentRunStage::Failed );
}

TEST_CASE( "AgentRunCoordinator does not retry unrepairable failures", "[agent][coordinator]" )
{
  ensureCoreApp();
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );

  int attempts = 0;
  coordinator.setExecuteFunction( [&attempts]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    return makeErrorPayload( QStringLiteral( "unrepairable" ) );
  } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );

  REQUIRE( attempts == 1 );
  REQUIRE( run.stage == AgentRunStage::Failed );
}

TEST_CASE( "AgentRunCoordinator cancels a running run", "[agent][coordinator]" )
{
  ensureCoreApp();
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );

  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );
  std::atomic<bool> started{ false };
  coordinator.setExecuteFunction( [&started, outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    started.store( true );
    // Pretend to work while waiting for cancellation.
    for ( int i = 0; i < 200; ++i )
    {
      std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
      QThread::yieldCurrentThread();
    }
    return makeSuccessPayload( outPath );
  } );

  AgentRun finalRun;
  QObject::connect( &coordinator, &AgentRunCoordinator::runCanceled,
                    [&]( const AgentRun &run ) { finalRun = run; } );

  std::thread worker( [&coordinator, &finalRun]() {
    finalRun = coordinator.runSynchronously( simpleRequest() );
  } );

  // Wait until the execution stub has started, then cancel.
  while ( !started.load() )
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
  coordinator.cancelRun( finalRun.id );

  worker.join();

  REQUIRE( finalRun.stage == AgentRunStage::Canceled );
}

TEST_CASE( "AgentRunCoordinator verify failure does not reach Completed", "[agent][coordinator]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );
  AgentRunCoordinator coordinator;

  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  coordinator.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
    return makeSuccessPayload( outPath );
  } );
  coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( false ); } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );

  REQUIRE( run.stage == AgentRunStage::Failed );
  REQUIRE( !run.verification.ok );
}

TEST_CASE( "AgentRunCoordinator final response is gated by verification", "[agent][coordinator]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );

  SECTION( "verified success reaches Completed and calls presenter" )
  {
    AgentRunCoordinator coordinator;
    int presentCalls = 0;
    coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    coordinator.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
      return makeSuccessPayload( outPath );
    } );
    coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );
    coordinator.setPresentFunction( [&presentCalls]( const AgentRun & ) { ++presentCalls; } );

    const AgentRun run = coordinator.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Completed );
    REQUIRE( run.verification.ok );
    CHECK( presentCalls == 1 );
    CHECK( run.executionPayload["status"].asString() == "success" );
  }

  SECTION( "unverified output never reports success and never presents" )
  {
    AgentRunCoordinator coordinator;
    int presentCalls = 0;
    coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    coordinator.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
      return makeSuccessPayload( outPath );
    } );
    coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( false ); } );
    coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );
    coordinator.setPresentFunction( [&presentCalls]( const AgentRun & ) { ++presentCalls; } );

    const AgentRun run = coordinator.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Failed );
    REQUIRE( !run.verification.ok );
    CHECK( presentCalls == 0 );
    CHECK_FALSE( run.stage == AgentRunStage::Completed );
  }

  SECTION( "final map state: verified run is Completed, unverified is Failed" )
  {
    for ( bool verified : { true, false } )
    {
      AgentRunCoordinator coordinator;
      bool presented = false;
      coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
      coordinator.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
        return makeSuccessPayload( outPath );
      } );
      coordinator.setVerifyFunction( [verified]( const QString &, const QString & ) { return makeVerification( verified ); } );
      coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );
      coordinator.setPresentFunction( [&presented]( const AgentRun & ) { presented = true; } );

      const AgentRun run = coordinator.runSynchronously( simpleRequest() );
      if ( verified )
      {
        REQUIRE( run.stage == AgentRunStage::Completed );
        REQUIRE( presented );
      }
      else
      {
        REQUIRE( run.stage == AgentRunStage::Failed );
        REQUIRE_FALSE( presented );
      }
    }
  }
}

TEST_CASE( "AgentRunCoordinator verification failure rolls back committed asset", "[agent][coordinator][insulator]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/rollback_test.tif" );

  sicnu::data::DataManager manager;
  GDALAllRegister();
  {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, outPath.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
  }
  sicnu::data::SourceDescriptor src;
  src.canonicalSource = outPath;
  src.providerKey = QStringLiteral( "gdal" );
  sicnu::data::RegisterRequest req{ src };
  req.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
  req.additionalCapabilities = sicnu::data::AssetCapability::DeletableSource;
  const auto reg = manager.registerSource( req );
  REQUIRE( !reg.assetId.isNull() );
  const sicnu::data::AssetId committedId = reg.assetId;
  REQUIRE( manager.asset( committedId ).has_value() );

  AgentRunCoordinator coordinator( &manager );
  coordinator.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  coordinator.setExecuteFunction( [outPath, committedId]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    Json::Value v( Json::objectValue );
    v["status"] = "success";
    v["output"] = outPath.toStdString();
    v["assetId"] = committedId.toString().toStdString();
    return v;
  } );
  coordinator.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( false ); } );
  coordinator.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  const AgentRun run = coordinator.runSynchronously( simpleRequest() );
  REQUIRE( run.stage == AgentRunStage::Failed );
  CHECK_FALSE( manager.asset( committedId ).has_value() );
}
