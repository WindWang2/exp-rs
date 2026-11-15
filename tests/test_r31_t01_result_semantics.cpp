#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

#include <QCoreApplication>
#include <QStringList>

namespace
{

QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r31_t01_result_semantics";
  static char *argv[] = { name, nullptr };
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
}

class MixedReportOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "r31:mixed_report"; }

    Json::Value schema() const override
    {
      return sicnu::operators::schema::makeRootSchema(
        "Mixed report", "Value fields only; no declared file artifact", Json::Value(), Json::Value() );
    }

    Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext & ) override
    {
      Json::Value result( Json::objectValue );
      result["driver"] = "GTiff";
      result["kind"] = "raster";
      result["path"] = "/tmp/source-that-must-not-be-published.tif";
      result["digest"] = "0123456789abcdef";
      result["output"] = "verification report, not an artifact";
      return result;
    }
};

struct ExecutionReset
{
    ExecutionReset()
    {
      sicnu::TaskCenter::instance().shutdownForTests();
      sicnu::jobs::JobEngine::instance().clearExecutors();
      sicnu::processing::AtomicAlgorithmRegistry::instance().reset();
    }
    ~ExecutionReset()
    {
      sicnu::TaskCenter::instance().shutdownForTests();
      sicnu::jobs::JobEngine::instance().clearExecutors();
    }
};

} // namespace

TEST_CASE( "R31-T01 WP02: mixed value reports never become output assets",
           "[r31][t01][result_semantics][reports]" )
{
  ensureApp();
  ExecutionReset reset;
  sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<sicnu::processing::RsOperatorAdapter>(
      std::make_unique<MixedReportOperator>() ) );

  auto &center = sicnu::TaskCenter::instance();
  QStringList autoLoadPaths;
  QObject::connect( &center, &sicnu::TaskCenter::layerAutoLoadRequested,
                    [&autoLoadPaths]( const QString &path, long ) {
                      autoLoadPaths.append( path );
                    } );

  const long taskId = center.enqueueTask( QStringLiteral( "r31:mixed_report" ), {},
                                          true, sicnu::TaskPriority::Normal, {}, true );
  REQUIRE( taskId > 0 );
  const auto info = center.waitForTask( taskId, std::chrono::seconds( 30 ) );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  CHECK( info.resultPayload["driver"].asString() == "GTiff" );
  CHECK( info.resultPayload["kind"].asString() == "raster" );
  CHECK( info.resultPayload["path"].asString()
         == "/tmp/source-that-must-not-be-published.tif" );
  CHECK( info.resultPayload["digest"].asString() == "0123456789abcdef" );
  CHECK( info.resultPayload["output"].asString()
         == "verification report, not an artifact" );
  CHECK( info.outputLayerPath.isEmpty() );
  CHECK( autoLoadPaths.isEmpty() );

  int commitCalls = 0;
  const auto committer = [&commitCalls]( const sicnu::AlgorithmTaskInfo &, std::string &path,
                                         std::string &, std::string &assetId ) {
    ++commitCalls;
    path = "/tmp/false-asset.tif";
    assetId = "asset-false";
    return true;
  };
  const Json::Value payload =
    sicnu::processing::ToolCallDispatcher::buildTaskResultPayload( info, committer );
  CHECK( commitCalls == 0 );
  CHECK( payload["status"].asString() == "success" );
  CHECK( payload["output"].asString() == "verification report, not an artifact" );
  CHECK_FALSE( payload.isMember( "assetId" ) );
}
