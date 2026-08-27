// tests/test_agent_closed_loop.cpp
//
// Closed-loop gap closure: repairable/unrepairable, retry limit,
// complete→verify, invalid output, registration, layer load/visible,
// final map state, verification gate, GUI close during run,
// inspector transitions. All tests are deterministic, headless where
// possible, with QTemporaryDir isolation and heap-leaked QApplication
// to avoid stack-QCoreApplication / hardcoded /tmp flakes.

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <thread>

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/agent_run_coordinator.h"
#include "agent/output_verifier.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

using namespace sicnu::agent;
using namespace sicnu::data;
using namespace sicnu::processing;

// ---------------------------------------------------------------------------
// Shared helpers (QTemporaryDir-aware, heap QApplication, no stack instances)
// ---------------------------------------------------------------------------
static void ensureQtApp()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char appName[] = "test_agent_closed_loop";
  static char *argv[] = { appName, nullptr };
  // Intentionally leaked: static QApplication dtor races with QGIS singletons.
  new QApplication( argc, argv );
}

static QCoreApplication *ensureCoreApp()
{
  if ( QCoreApplication::instance() )
    return QCoreApplication::instance();
  // Use QApplication even for headless coordinator tests: the suite also
  // contains a GUI dock test, and creating a QCoreApplication first would
  // prevent a later QApplication from being created (Qt forbids mixing).
  ensureQtApp();
  return QCoreApplication::instance();
}

static AgentRunRequest simpleRequest()
{
  AgentRunRequest r;
  r.algorithmId = QStringLiteral( "rs:ndvi" );
  r.userRequest = QStringLiteral( "compute NDVI" );
  return r;
}

static Json::Value makeSuccessPayload( const QString &outPath )
{
  Json::Value v( Json::objectValue );
  v["status"] = "success";
  v["output"] = outPath.toStdString();
  return v;
}
static Json::Value makeErrorPayload( const QString &msg )
{
  Json::Value v( Json::objectValue );
  v["status"] = "error";
  v["errorMessage"] = msg.toStdString();
  return v;
}
static Json::Value makePreflight( bool valid )
{
  Json::Value v( Json::objectValue );
  v["valid"] = valid;
  return v;
}
static OutputVerification makeVerification( bool ok, const QString &kind = QStringLiteral( "raster" ) )
{
  OutputVerification v;
  v.ok = ok;
  v.kind = kind;
  if ( !ok )
    v.issues.append( QStringLiteral( "all pixels are NoData" ) );
  else
  {
    v.summary["width"] = 16;
    v.summary["height"] = 16;
  }
  return v;
}

static bool writeTinyRaster( const QString &path, int w = 16, int h = 16, int bands = 1, bool allNoData = false )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, bands, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
  ds->SetGeoTransform( gt.data() );
  OGRSpatialReference srs;
  srs.importFromEPSG( 4326 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );
  for ( int b = 1; b <= bands; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b );
    if ( allNoData )
      band->SetNoDataValue( -9999.0 );
    std::vector<float> data( w * h, allNoData ? -9999.0f : 1.0f );
    band->RasterIO( GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0 );
  }
  GDALClose( (GDALDatasetH)ds );
  return true;
}

// Stub adapter for ExecutionPlane / dispatcher tests
class ClosedLoopStubAdapter : public AtomicAlgorithmAdapter
{
  public:
    enum class Mode { SleepThenComplete, WriteOutput, Fail };
    ClosedLoopStubAdapter( std::string id, Mode m, int sleepMs = 0, QString out = {} )
      : mId( std::move( id ) ), mMode( m ), mSleepMs( sleepMs ), mOut( std::move( out ) ) {}
    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override { return AlgorithmDescriptor{}; }
    Json::Value execute( const Json::Value &p, ProgressCallback, std::function<bool()> isCancelled ) override
    {
      if ( mMode == Mode::SleepThenComplete )
      {
        const int slice = 20;
        for ( int s = 0; s < mSleepMs; s += slice )
        {
          if ( isCancelled && isCancelled() )
            throw std::runtime_error( "Canceled" );
          std::this_thread::sleep_for( std::chrono::milliseconds( slice ) );
        }
        Json::Value r( Json::objectValue );
        r["status"] = "ok";
        r["echo"] = p;
        return r;
      }
      if ( mMode == Mode::WriteOutput )
      {
        Q_UNUSED( isCancelled )
        Json::Value r( Json::objectValue );
        r["status"] = "ok";
        r["output"] = mOut.toStdString();
        return r;
      }
      throw std::runtime_error( "stub failure" );
    }

  private:
    std::string mId;
    Mode mMode;
    int mSleepMs;
    QString mOut;
};

static void wireRegistryFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      const auto a = AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !a )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      ProgressCallback cb = [&ctx]( int pct, const std::string &msg ) { ctx.reportProgress( pct / 100.0, msg ); };
      return a->execute( req.params, cb, [&ctx]() { return ctx.isCancelled(); } );
    } );
}

static QJsonObject makeToolCallEnvelope( const QString &name )
{
  QJsonObject e;
  e[QStringLiteral( "id" )] = QStringLiteral( "tc_1" );
  e[QStringLiteral( "type" )] = QStringLiteral( "function" );
  QJsonObject f;
  f[QStringLiteral( "name" )] = name;
  f[QStringLiteral( "arguments" )] = QStringLiteral( "{}" );
  e[QStringLiteral( "function" )] = f;
  return e;
}

// ---------------------------------------------------------------------------
// 1. Repairable failure is repaired via RepairFunction and reaches Completed
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop repairable failure is repaired and completes", "[agent][closed_loop][repairable]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );

  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  int attempts = 0;
  c.setExecuteFunction( [&attempts, outPath]( const QString &, const QVariantMap &params, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    if ( params.contains( QStringLiteral( "fixed" ) ) )
      return makeSuccessPayload( outPath );
    return makeErrorPayload( QStringLiteral( "needs fix" ) );
  } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
    QVariantMap m;
    m[QStringLiteral( "fixed" )] = true;
    return m;
  } );
  c.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( run.stage == AgentRunStage::Completed );
  REQUIRE( attempts == 2 );
  REQUIRE( run.repairAttempts == 1 );
}

// ---------------------------------------------------------------------------
// 2. Unrepairable failure is not retried
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop unrepairable failure is not retried", "[agent][closed_loop][unrepairable]" )
{
  ensureCoreApp();
  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  int attempts = 0;
  c.setExecuteFunction( [&attempts]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    return makeErrorPayload( QStringLiteral( "unrepairable" ) );
  } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( attempts == 1 );
  REQUIRE( run.stage == AgentRunStage::Failed );
}

// ---------------------------------------------------------------------------
// 3. Retry limit bounded at 2 (initial + 2 repairs = 3 attempts)
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop retry limit is bounded at two", "[agent][closed_loop][retry_limit]" )
{
  ensureCoreApp();
  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  int attempts = 0;
  c.setExecuteFunction( [&attempts]( const QString &, const QVariantMap &, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    return makeErrorPayload( QStringLiteral( "always fails" ) );
  } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
    return QVariantMap(); // claims repairable
  } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( attempts == 3 );
  REQUIRE( run.repairAttempts == 2 );
  REQUIRE( run.stage == AgentRunStage::Failed );
}

// ---------------------------------------------------------------------------
// 4. Complete→verify: verification gate blocks Completed
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop complete is gated by verification", "[agent][closed_loop][complete_verify]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );

  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  c.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
    return makeSuccessPayload( outPath );
  } );
  c.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( false ); } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  std::vector<AgentRunStage> stages;
  QObject::connect( &c, &AgentRunCoordinator::runStageChanged,
                    [&]( const AgentRun &r ) { stages.push_back( r.stage ); } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( run.stage == AgentRunStage::Failed );
  REQUIRE_FALSE( run.verification.ok );
  // Must have visited Verifying before terminal
  bool sawVerifying = false;
  for ( auto s : stages )
    if ( s == AgentRunStage::Verifying )
      sawVerifying = true;
  REQUIRE( sawVerifying );
  // Must not end with Completed
  REQUIRE( stages.back() == AgentRunStage::Failed );
}

// ---------------------------------------------------------------------------
// 5. Invalid output (all NoData) is rejected by OutputVerifier and blocks present
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop invalid output blocks completion and present", "[agent][closed_loop][invalid_output]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/nodata.tif" );
  REQUIRE( writeTinyRaster( outPath, 16, 16, 1, true ) );

  // Verify the real verifier rejects all-NoData
  const OutputVerification realReport = OutputVerifier().verify( outPath );
  REQUIRE_FALSE( realReport.ok );

  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  c.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
    return makeSuccessPayload( outPath );
  } );
  c.setVerifyFunction( []( const QString &p, const QString &k ) { return OutputVerifier().verify( p, k ); } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );

  int presentCalls = 0;
  c.setPresentFunction( [&presentCalls]( const AgentRun & ) { ++presentCalls; } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( run.stage == AgentRunStage::Failed );
  REQUIRE( presentCalls == 0 );
}

// ---------------------------------------------------------------------------
// 6. Registration: payload enriched with assetId / assetKind / verified
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop registration payload has assetId and verification", "[agent][closed_loop][registration]" )
{
  ensureCoreApp();

  ToolCallDispatcher::OutputCommitterHandler committer = []( const sicnu::AlgorithmTaskInfo &,
                                                            std::string &outPath,
                                                            std::string &,
                                                            std::string &outAssetId ) -> bool {
    outPath = "/committed/reg_test.tif";
    outAssetId = "asset-reg-1";
    return true;
  };
  ToolCallDispatcher::OutputVerificationHandler verifier = []( const QString &, const QString & ) -> Json::Value {
    Json::Value v( Json::objectValue );
    v["ok"] = true;
    v["kind"] = "raster";
    Json::Value summary( Json::objectValue );
    summary["width"] = 32;
    summary["height"] = 32;
    summary["bandCount"] = 1;
    v["summary"] = summary;
    v["issues"] = Json::Value( Json::arrayValue );
    v["warnings"] = Json::Value( Json::arrayValue );
    return v;
  };

  sicnu::AlgorithmTaskInfo info;
  info.taskId = 101;
  info.algorithmId = QStringLiteral( "rs:spectral_index" );
  info.status = sicnu::TaskStatus::Completed;
  info.outputLayerPath = QStringLiteral( "/tmp/raw.tif" );

  const Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info, committer, verifier );
  REQUIRE( payload["status"].asString() == "success" );
  REQUIRE( payload["output"].asString() == "/committed/reg_test.tif" );
  REQUIRE( payload["assetId"].asString() == "asset-reg-1" );
  REQUIRE( payload["assetKind"].asString() == "raster" );
  REQUIRE( payload["verified"].asBool() == true );
  REQUIRE( payload["verification"]["ok"].asBool() == true );
  REQUIRE( payload["verification"]["summary"]["width"].asInt() == 32 );
}

// ---------------------------------------------------------------------------
// 7. Layer load / visible: agent path commits stable path and does not emit temp auto-load
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop layer load uses stable asset without temp auto-load", "[agent][closed_loop][layer]" )
{
  ensureQtApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString tempPath = tmp.path() + QStringLiteral( "/result.tif" );
  REQUIRE( writeTinyRaster( tempPath, 8, 8, 1 ) );

  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<ClosedLoopStubAdapter>( "stub:layer_producer",
                                             ClosedLoopStubAdapter::Mode::WriteOutput, 0, tempPath ) );

  DataManager dm;
  ToolCallDispatcher d;
  d.setSourceTag( QStringLiteral( "agent" ) );
  d.setDataManager( &dm );

  QSignalSpy loadSpy( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::layerAutoLoadRequested );

  const Json::Value envelope = [] {
    Json::Value e( Json::objectValue );
    e["name"] = "stub:layer_producer";
    e["parameters"] = Json::Value( Json::objectValue );
    return e;
  }();

  const Json::Value result = d.dispatchAndAwait( envelope, std::chrono::seconds( 8 ) );
  REQUIRE( result["status"].asString() == "success" );
  const QString committed = QString::fromStdString( result["output"].asString() );
  REQUIRE( committed.endsWith( QStringLiteral( "_committed.tif" ) ) );
  REQUIRE( QFileInfo::exists( committed ) );
  REQUIRE_FALSE( QFileInfo::exists( tempPath ) );
  REQUIRE( loadSpy.count() == 0 );
  REQUIRE( dm.assets().size() == 1 );
  REQUIRE( result.isMember( "assetId" ) );
  REQUIRE( result["assetKind"].asString() == "raster" );
}

// ---------------------------------------------------------------------------
// 8. Final map state: present hook is verification-gated
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop final map present is verification-gated", "[agent][closed_loop][final_map]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString okPath = tmp.path() + QStringLiteral( "/ok.tif" );
  const QString badPath = tmp.path() + QStringLiteral( "/bad.tif" );
  REQUIRE( writeTinyRaster( okPath, 8, 8, 1, false ) );
  REQUIRE( writeTinyRaster( badPath, 8, 8, 1, true ) );

  // Success path: present called
  {
    AgentRunCoordinator c;
    c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    c.setExecuteFunction( [okPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
      return makeSuccessPayload( okPath );
    } );
    c.setVerifyFunction( []( const QString &p, const QString &k ) { return OutputVerifier().verify( p, k ); } );
    int presentCalls = 0;
    c.setPresentFunction( [&presentCalls]( const AgentRun & ) { ++presentCalls; } );
    const AgentRun run = c.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Completed );
    REQUIRE( presentCalls == 1 );
    REQUIRE( run.verification.ok );
  }

  // Failure path: present not called
  {
    AgentRunCoordinator c;
    c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    c.setExecuteFunction( [badPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
      return makeSuccessPayload( badPath );
    } );
    c.setVerifyFunction( []( const QString &p, const QString &k ) { return OutputVerifier().verify( p, k ); } );
    c.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );
    int presentCalls = 0;
    c.setPresentFunction( [&presentCalls]( const AgentRun & ) { ++presentCalls; } );
    const AgentRun run = c.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Failed );
    REQUIRE( presentCalls == 0 );
  }
}

// ---------------------------------------------------------------------------
// 9. Verification gate: no Completed before verify; repair on verify failure
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop verification failure can be repaired once then succeed", "[agent][closed_loop][verify_repair]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString badPath = tmp.path() + QStringLiteral( "/bad.tif" );
  const QString okPath = tmp.path() + QStringLiteral( "/ok.tif" );
  REQUIRE( writeTinyRaster( badPath, 8, 8, 1, true ) );
  REQUIRE( writeTinyRaster( okPath, 8, 8, 1, false ) );

  AgentRunCoordinator c;
  c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
  int attempts = 0;
  c.setExecuteFunction( [&attempts, badPath, okPath]( const QString &, const QVariantMap &params, long &, std::function<void()> & ) -> Json::Value {
    ++attempts;
    if ( params.contains( QStringLiteral( "useOk" ) ) )
      return makeSuccessPayload( okPath );
    return makeSuccessPayload( badPath );
  } );
  c.setVerifyFunction( []( const QString &p, const QString &k ) { return OutputVerifier().verify( p, k ); } );
  c.setRepairFunction( []( const AgentRun &, const Json::Value &payload ) -> std::optional<QVariantMap> {
    if ( payload.isMember( "status" ) && payload["status"].asString() == "verification_failed" )
    {
      QVariantMap m;
      m[QStringLiteral( "useOk" )] = true;
      return m;
    }
    return std::nullopt;
  } );

  const AgentRun run = c.runSynchronously( simpleRequest() );
  REQUIRE( attempts == 2 );
  REQUIRE( run.repairAttempts == 1 );
  REQUIRE( run.stage == AgentRunStage::Completed );
}

// ---------------------------------------------------------------------------
// 10. GUI close during run does not crash (QTemporaryDir + offscreen)
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop GUI close during run is safe", "[agent][closed_loop][gui_close]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<ClosedLoopStubAdapter>( "stub:gui_close",
                                             ClosedLoopStubAdapter::Mode::SleepThenComplete, 600 ) );

  auto dm = std::make_unique<DataManager>();
  auto dock = std::make_unique<AgentCopilotDockWidget>();
  dock->setContext( dm.get(), nullptr );

  REQUIRE( QMetaObject::invokeMethod( dock.get(), "onToolCallParsed", Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:gui_close" ) ) ) ) );

  bool destroyed = false;
  QTimer::singleShot( 150, [&] {
    dock.reset();
    dm.reset();
    destroyed = true;
  } );

  QEventLoop loop;
  QTimer::singleShot( 1400, &loop, &QEventLoop::quit );
  loop.exec();

  REQUIRE( destroyed );
}

// ---------------------------------------------------------------------------
// 11. Inspector transitions reflect full lifecycle including Repairing
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop inspector transitions cover full lifecycle", "[agent][closed_loop][inspector]" )
{
  ensureCoreApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString outPath = tmp.path() + QStringLiteral( "/out.tif" );

  // Happy path: Understanding → Planning → Preflight → Running → Verifying → Presenting → Completed
  {
    AgentRunCoordinator c;
    std::vector<AgentRunStage> stages;
    QObject::connect( &c, &AgentRunCoordinator::runStageChanged,
                      [&]( const AgentRun &r ) { stages.push_back( r.stage ); } );
    c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    c.setExecuteFunction( [outPath]( const QString &, const QVariantMap &, long &, std::function<void()> & ) {
      return makeSuccessPayload( outPath );
    } );
    c.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );
    const AgentRun run = c.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Completed );
    REQUIRE( stages.size() >= 7 );
    REQUIRE( stages[0] == AgentRunStage::Understanding );
    REQUIRE( stages[1] == AgentRunStage::Planning );
    REQUIRE( stages[2] == AgentRunStage::Preflight );
    // Running, Verifying, Presenting must appear in order
    auto find = [&]( AgentRunStage s ) {
      return std::find( stages.begin(), stages.end(), s ) != stages.end();
    };
    REQUIRE( find( AgentRunStage::Running ) );
    REQUIRE( find( AgentRunStage::Verifying ) );
    REQUIRE( find( AgentRunStage::Presenting ) );
    REQUIRE( stages.back() == AgentRunStage::Completed );
  }

  // Repair path must include Repairing
  {
    AgentRunCoordinator c;
    std::vector<AgentRunStage> stages;
    QObject::connect( &c, &AgentRunCoordinator::runStageChanged,
                      [&]( const AgentRun &r ) { stages.push_back( r.stage ); } );
    c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( true ); } );
    int attempts = 0;
    c.setExecuteFunction( [&attempts, outPath]( const QString &, const QVariantMap &params, long &, std::function<void()> & ) -> Json::Value {
      ++attempts;
      if ( params.contains( QStringLiteral( "fixed" ) ) )
        return makeSuccessPayload( outPath );
      return makeErrorPayload( QStringLiteral( "fail" ) );
    } );
    c.setRepairFunction( []( const AgentRun &, const Json::Value & ) -> std::optional<QVariantMap> {
      QVariantMap m;
      m[QStringLiteral( "fixed" )] = true;
      return m;
    } );
    c.setVerifyFunction( []( const QString &, const QString & ) { return makeVerification( true ); } );
    const AgentRun run = c.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Completed );
    bool sawRepairing = false;
    for ( auto s : stages )
      if ( s == AgentRunStage::Repairing )
        sawRepairing = true;
    REQUIRE( sawRepairing );
  }

  // Failed path ends in Failed
  {
    AgentRunCoordinator c;
    c.setPreflightFunction( []( const QString &, const QVariantMap & ) { return makePreflight( false ); } );
    c.setRepairFunction( []( const AgentRun &, const Json::Value & ) { return std::optional<QVariantMap>{}; } );
    std::vector<AgentRunStage> stages;
    QObject::connect( &c, &AgentRunCoordinator::runStageChanged,
                      [&]( const AgentRun &r ) { stages.push_back( r.stage ); } );
    const AgentRun run = c.runSynchronously( simpleRequest() );
    REQUIRE( run.stage == AgentRunStage::Failed );
    REQUIRE( stages.back() == AgentRunStage::Failed );
  }
}

// ---------------------------------------------------------------------------
// 12. Tool-call envelope invalid / layer visible: no sink on invalid, payload has verified
// ---------------------------------------------------------------------------
TEST_CASE( "closed-loop invalid envelope never reaches sink", "[agent][closed_loop][invalid]" )
{
  ensureCoreApp();
  AtomicAlgorithmRegistry::instance().reset();
  // No adapter registered -> any name is invalid
  ToolCallDispatcher d(
    []( const QString &, const QVariantMap & ) -> long {
      FAIL( "sink must not be called for invalid envelope" );
      return -1;
    },
    []( long, ToolCallDispatcher::CompletionCallback ) {
      FAIL( "watcher must not be called for invalid envelope" );
    } );

  Json::Value badEnvelope( Json::objectValue );
  badEnvelope["name"] = "not_registered_anywhere";
  badEnvelope["parameters"] = Json::Value( Json::objectValue );

  REQUIRE( d.classify( badEnvelope ) == ToolCallClassification::Invalid );
  QString err;
  long taskId = -1;
  REQUIRE_FALSE( d.submit( badEnvelope, []( const Json::Value & ) {}, &err, &taskId ) );
  REQUIRE_FALSE( err.isEmpty() );
  REQUIRE( taskId == -1 );
}
