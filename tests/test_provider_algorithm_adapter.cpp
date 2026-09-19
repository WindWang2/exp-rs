// tests/test_provider_algorithm_adapter.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "operators/framework/rs_operator_error.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "processing/framework/qgs_processing_provider_adapter.h"
#include "processing/framework/algorithm_engine.h"
#include "qgsprocessingalgorithm.h"
#include "qgsprocessingparameters.h"
#include "qgsprocessingoutputs.h"
#include "qgsprocessingprovider.h"
#include "processing/qgsprocessingregistry.h"
#include "qgsapplication.h"

using namespace sicnu::processing;

class DummyTestAlgorithm : public QgsProcessingAlgorithm
{
public:
  DummyTestAlgorithm() = default;
  ~DummyTestAlgorithm() override = default;

  QString name() const override { return QStringLiteral( "dummyalg" ); }
  QString displayName() const override { return QStringLiteral( "Dummy Test Algorithm" ); }
  QString group() const override { return QStringLiteral( "Test Group" ); }
  QString groupId() const override { return QStringLiteral( "testgroup" ); }
  QString shortDescription() const override { return QStringLiteral( "A dummy algorithm for testing provider adapter." ); }
  QgsProcessingAlgorithm *createInstance() const override { return new DummyTestAlgorithm(); }

  void initAlgorithm( const QVariantMap & = QVariantMap() ) override
  {
    addParameter( new QgsProcessingParameterBoolean( QStringLiteral( "INPUT_BOOL" ), QStringLiteral( "Input Boolean" ), true, true ) ); // optional
    addParameter( new QgsProcessingParameterNumber( QStringLiteral( "INPUT_NUM" ), QStringLiteral( "Input Number" ), Qgis::ProcessingNumberParameterType::Integer, 42, false ) ); // required integer
    addParameter( new QgsProcessingParameterString( QStringLiteral( "INPUT_STR" ), QStringLiteral( "Input String" ), QStringLiteral( "default_val" ), false, true ) ); // optional
    
    addOutput( new QgsProcessingOutputString( QStringLiteral( "OUTPUT_STR" ), QStringLiteral( "Output String" ) ) );
  }

  QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &, QgsProcessingFeedback * ) override
  {
    QVariantMap results;
    results[QStringLiteral( "OUTPUT_STR" )] = QStringLiteral( "processed_" ) + parameters.value( QStringLiteral( "INPUT_STR" ) ).toString();
    return results;
  }
};

/// Minimal provider owning a DummyTestAlgorithm, registered into the real
/// QgsProcessingRegistry so execute() can resolve it by id (#695 flow).
class DummyTestProvider : public QgsProcessingProvider
{
public:
  explicit DummyTestProvider( QString providerId = QStringLiteral( "sicnu_test" ) )
    : m_providerId( std::move( providerId ) ) {}

  QString id() const override { return m_providerId; }
  QString name() const override { return QStringLiteral( "SICNU Test Provider" ); }
  void loadAlgorithms() override
  {
    addAlgorithm( new DummyTestAlgorithm() );
  }

private:
  QString m_providerId;
};

namespace
{
  QCoreApplication *ensureCoreApp()
  {
    static QCoreApplication *sApp = nullptr;
    if ( !sApp )
    {
      static int argc = 1;
      static char name[] = "test_provider_algorithm_adapter";
      static char *argv[] = { name, nullptr };
      sApp = new QCoreApplication( argc, argv );
    }
    return sApp;
  }
}

TEST_CASE( "ProviderAlgorithmAdapter builds descriptor and executes QgsProcessingAlgorithm", "[processing][adapter]" )
{
  DummyTestAlgorithm alg;
  alg.initAlgorithm();

  ProviderAlgorithmAdapter adapter( alg );

  REQUIRE( adapter.algorithmId() == "dummyalg" );
  AlgorithmDescriptor desc = adapter.descriptor();
  REQUIRE( desc.id == "dummyalg" );
  REQUIRE( desc.displayName == "Dummy Test Algorithm" );
  REQUIRE( desc.group == "Test Group" );
  REQUIRE( desc.description == "A dummy algorithm for testing provider adapter." );

  // Verify inputs
  REQUIRE( desc.inputs.size() == 3 );

  // INPUT_BOOL (optional)
  REQUIRE( desc.inputs[0].name == "INPUT_BOOL" );
  REQUIRE( desc.inputs[0].type == DataType::Boolean );
  REQUIRE_FALSE( desc.inputs[0].required );

  // INPUT_NUM (required integer)
  REQUIRE( desc.inputs[1].name == "INPUT_NUM" );
  REQUIRE( desc.inputs[1].type == DataType::Integer );
  REQUIRE( desc.inputs[1].required );
  REQUIRE( desc.inputs[1].defaultValue == "42" );

  // INPUT_STR (optional)
  REQUIRE( desc.inputs[2].name == "INPUT_STR" );
  REQUIRE( desc.inputs[2].type == DataType::String );
  REQUIRE_FALSE( desc.inputs[2].required );
  REQUIRE( desc.inputs[2].defaultValue == "default_val" );

  // Verify outputs
  REQUIRE( desc.outputs.size() == 1 );
  REQUIRE( desc.outputs[0].name == "OUTPUT_STR" );
  REQUIRE( desc.outputs[0].type == DataType::String );

  // Test execution (#695): a stack-local algorithm is NOT registered in the
  // QgsProcessingRegistry, so execute() must fail with a typed error instead
  // of dereferencing an algorithm pointer a provider may have already freed.
  Json::Value params( Json::objectValue );
  params["INPUT_STR"] = "hello";

  REQUIRE_THROWS_AS( adapter.execute( params ), sicnu::operators::RSOperatorError );
}

TEST_CASE("AtomicAlgorithmRegistry integrates ProviderAlgorithmAdapter", "[processing][provider_adapter]")
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  size_t initialCount = registry.adapterCount();

  DummyTestAlgorithm alg;
  alg.initAlgorithm();

  registry.registerAdapter( std::make_shared<ProviderAlgorithmAdapter>( alg ) );

  REQUIRE( registry.adapterCount() == initialCount + 1 );
  REQUIRE( registry.findAdapter( "dummyalg" ) != nullptr );

  // Verify exported tool definitions include the dummy algorithm
  Json::Value toolsJson = registry.exportOpenAiToolDefinitions();
  REQUIRE( toolsJson.isArray() );
  bool foundDummy = false;
  for ( const auto &item : toolsJson )
  {
    if ( item["function"]["name"].asString() == "dummyalg" )
    {
      foundDummy = true;
      break;
    }
  }
  REQUIRE( foundDummy );
}

TEST_CASE( "ProviderAlgorithmAdapter resolves live algorithm via processing registry", "[processing][adapter][registry]" )
{
  ensureCoreApp();
  QgsProcessingRegistry *qgisRegistry = QgsApplication::processingRegistry();
  REQUIRE( qgisRegistry != nullptr );

  if ( !qgisRegistry->providerById( QStringLiteral( "sicnu_test" ) ) )
    REQUIRE( qgisRegistry->addProvider( new DummyTestProvider() ) );

  const QString algId = QStringLiteral( "sicnu_test:dummyalg" );
  const QgsProcessingAlgorithm *live = qgisRegistry->algorithmById( algId );
  REQUIRE( live != nullptr );

  ProviderAlgorithmAdapter adapter( *live );
  REQUIRE( adapter.algorithmId() == algId.toStdString() );

  // Execution re-resolves by id and clones — works while the provider is alive.
  Json::Value params( Json::objectValue );
  params["INPUT_STR"] = "hello";
  Json::Value result = adapter.execute( params );
  REQUIRE( result.isMember( "OUTPUT_STR" ) );
  REQUIRE( result["OUTPUT_STR"].asString() == "processed_hello" );

  // #695: removeProvider() DELETES the provider and its algorithms before
  // emitting providerRemoved(id). The adapter must not touch the freed
  // algorithm — it re-resolves and fails with a typed error instead (this
  // used to be a use-after-free when the raw pointer was cached).
  REQUIRE( qgisRegistry->removeProvider( QStringLiteral( "sicnu_test" ) ) );
  REQUIRE( qgisRegistry->algorithmById( algId ) == nullptr );
  REQUIRE_THROWS_AS( adapter.execute( params ), sicnu::operators::RSOperatorError );
}

TEST_CASE( "providerRemoved hook unregisters adapters of the removed provider", "[processing][adapter][registry]" )
{
  ensureCoreApp();
  QgsProcessingRegistry *qgisRegistry = QgsApplication::processingRegistry();
  REQUIRE( qgisRegistry != nullptr );

  if ( !qgisRegistry->providerById( QStringLiteral( "sicnu_hook" ) ) )
    REQUIRE( qgisRegistry->addProvider( new DummyTestProvider( QStringLiteral( "sicnu_hook" ) ) ) );

  // discoverAlgorithms() both caches the adapters in AtomicAlgorithmRegistry
  // and installs the providerRemoved hook (first call in this process).
  sicnu::QgsProcessingProviderAdapter providerAdapter(
    QStringLiteral( "sicnu_hook" ), QStringLiteral( "SICNU Hook Provider" ),
    sicnu::ProviderResourceProfile::InProcessThread, nullptr );
  providerAdapter.discoverAlgorithms( sicnu::AlgorithmEngine::instance() );

  auto &adapters = AtomicAlgorithmRegistry::instance();
  REQUIRE( adapters.findAdapter( "sicnu_hook:dummyalg" ) != nullptr );

  // Removing the provider must drop the stale catalog entries whose
  // algorithms died with it.
  REQUIRE( qgisRegistry->removeProvider( QStringLiteral( "sicnu_hook" ) ) );
  REQUIRE( adapters.findAdapter( "sicnu_hook:dummyalg" ) == nullptr );
}

namespace
{
// The adapter re-resolves the algorithm from the QgsProcessingRegistry and
// runs a CLONE per execute() — so the clone's own members cannot be observed
// directly. The success-path cleanup hook (postProcessAlgorithm, the one
// virtual QgsProcessingAlgorithm::postProcess invokes) records here (#1043).
std::vector<bool> *g_successPostProcessLog = nullptr;

class CleanupRecordingAlgorithm : public QgsProcessingAlgorithm
{
  public:
    CleanupRecordingAlgorithm() = default;
    ~CleanupRecordingAlgorithm() override = default;

    QString name() const override { return QStringLiteral( "cleanupalg" ); }
    QString displayName() const override { return QStringLiteral( "Cleanup Recording Algorithm" ); }
    QString group() const override { return QStringLiteral( "Test Group" ); }
    QString groupId() const override { return QStringLiteral( "testgroup" ); }
    QString shortDescription() const override { return QStringLiteral( "Records postProcess cleanup calls." ); }
    QgsProcessingAlgorithm *createInstance() const override { return new CleanupRecordingAlgorithm(); }

    void initAlgorithm( const QVariantMap & = QVariantMap() ) override
    {
      addParameter( new QgsProcessingParameterString( QStringLiteral( "MODE" ), QStringLiteral( "Mode" ),
                                                      QStringLiteral( "ok" ), false ) );
      addOutput( new QgsProcessingOutputString( QStringLiteral( "OUTPUT_STR" ), QStringLiteral( "Output String" ) ) );
    }

    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &,
                                  QgsProcessingFeedback *feedback ) override
    {
      const QString mode = parameters.value( QStringLiteral( "MODE" ) ).toString();
      if ( mode == QStringLiteral( "throw_std" ) )
        throw std::runtime_error( "boom from processAlgorithm" );
      if ( mode == QStringLiteral( "cancel" ) )
      {
        feedback->cancel();
        QVariantMap results;
        results[QStringLiteral( "OUTPUT_STR" )] = QStringLiteral( "cancelled" );
        return results;
      }
      if ( mode == QStringLiteral( "cancel_slow" ) )
      {
        // Long enough for the adapter's cancel watcher (50 ms poll) to arm the
        // feedback mid-run, exercising the POST-run cancel branch.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while ( std::chrono::steady_clock::now() < deadline && !feedback->isCanceled() )
          std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        QVariantMap results;
        results[QStringLiteral( "OUTPUT_STR" )] = QStringLiteral( "slow" );
        return results;
      }
      QVariantMap results;
      results[QStringLiteral( "OUTPUT_STR" )] = QStringLiteral( "ok" );
      return results;
    }

    QVariantMap postProcessAlgorithm( QgsProcessingContext &context,
                                      QgsProcessingFeedback *feedback ) override
    {
      // The only virtual success-cleanup hook: invoked by the base
      // postProcess(context, feedback, /*runResult=*/true).
      if ( g_successPostProcessLog )
        g_successPostProcessLog->push_back( feedback != nullptr && feedback->isCanceled() );
      return QgsProcessingAlgorithm::postProcessAlgorithm( context, feedback );
    }
};

class CleanupRecordingProvider : public QgsProcessingProvider
{
  public:
    QString id() const override { return QStringLiteral( "sicnu_cleanup" ); }
    QString name() const override { return QStringLiteral( "SICNU Cleanup Provider" ); }
    void loadAlgorithms() override { addAlgorithm( new CleanupRecordingAlgorithm() ); }
};

} // namespace

// ---------------------------------------------------------------------------
// #1043 regression: the adapter's error paths must survive non-
// QgsProcessingException failures (with cleanup, not a leak) and must cancel
// through the POST-run branch — the typed Cancelled error whose branch runs
// postProcess(false) BEFORE the throw (the historical pre-run checkCancelled()
// fired first and skipped cleanup).
// ---------------------------------------------------------------------------
TEST_CASE( "ProviderAlgorithmAdapter cleans up on std::exception and post-run cancel (#1043)",
           "[processing][adapter][cleanup]" )
{
  ensureCoreApp();
  QgsProcessingRegistry *qgisRegistry = QgsApplication::processingRegistry();
  REQUIRE( qgisRegistry != nullptr );
  if ( !qgisRegistry->providerById( QStringLiteral( "sicnu_cleanup" ) ) )
    REQUIRE( qgisRegistry->addProvider( new CleanupRecordingProvider() ) );

  const QgsProcessingAlgorithm *live = qgisRegistry->algorithmById( QStringLiteral( "sicnu_cleanup:cleanupalg" ) );
  REQUIRE( live != nullptr );
  ProviderAlgorithmAdapter adapter( *live );

  // 1. A non-QgsProcessingException failure must be caught (the new
  //    std::exception arm) and surface as a runtime_error carrying the
  //    original message — never an unhandled escape, never a success.
  {
    std::vector<bool> successLog;
    g_successPostProcessLog = &successLog;
    Json::Value params( Json::objectValue );
    params["MODE"] = "throw_std";
    try
    {
      adapter.execute( params );
      FAIL( "expected the std::exception failure to propagate" );
    }
    catch ( const std::runtime_error &e )
    {
      CHECK( std::string( e.what() ).find( "boom from processAlgorithm" ) != std::string::npos );
    }
    g_successPostProcessLog = nullptr;
    // The failed run must never run the SUCCESS cleanup hook.
    CHECK( successLog.empty() );
  }

  // 2. Feedback-cancelled run: the POST-run branch (postProcess(false) first,
  //    then the typed error) reports the distinguishable "during run" message.
  {
    Json::Value params( Json::objectValue );
    params["MODE"] = "cancel";
    try
    {
      adapter.execute( params );
      FAIL( "expected RSOperatorError for a cancelled run" );
    }
    catch ( const sicnu::operators::RSOperatorError &e )
    {
      REQUIRE( e.code() == sicnu::operators::ErrorCode::Cancelled );
      CHECK( std::string( e.what() ).find( "during run" ) != std::string::npos );
    }
  }

  // 3. External cancellation mid-run (the JobEngine cancel flag): same POST-run
  //    branch — the pre-run gate lets the task start (first predicate call),
  //    the watcher thread arms the cancel while the algorithm is running.
  {
    std::atomic<int> cancelCalls{ 0 };
    Json::Value params( Json::objectValue );
    params["MODE"] = "cancel_slow";
    try
    {
      adapter.execute( params, {}, [&cancelCalls]() {
        return cancelCalls.fetch_add( 1 ) >= 1;
      } );
      FAIL( "expected RSOperatorError for an externally cancelled run" );
    }
    catch ( const sicnu::operators::RSOperatorError &e )
    {
      REQUIRE( e.code() == sicnu::operators::ErrorCode::Cancelled );
      CHECK( std::string( e.what() ).find( "during run" ) != std::string::npos );
    }
  }

  // 4. Success path: exactly one success cleanup hook, no error cleanup.
  {
    std::vector<bool> successLog;
    g_successPostProcessLog = &successLog;
    Json::Value params( Json::objectValue );
    params["MODE"] = "ok";
    const Json::Value result = adapter.execute( params );
    g_successPostProcessLog = nullptr;
    REQUIRE( result["OUTPUT_STR"].asString() == "ok" );
    REQUIRE( successLog.size() == 1 );
    CHECK_FALSE( successLog.back() ); // not canceled at cleanup time
  }

  REQUIRE( qgisRegistry->removeProvider( QStringLiteral( "sicnu_cleanup" ) ) );
}


