// tests/test_provider_algorithm_adapter.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/provider_algorithm_adapter.h"
#include "qgsprocessingalgorithm.h"
#include "qgsprocessingparameters.h"
#include "qgsprocessingoutputs.h"
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

  // Test execution
  Json::Value params( Json::objectValue );
  params["INPUT_STR"] = "hello";

  Json::Value result = adapter.execute( params );
  REQUIRE_FALSE( result.isMember( "error" ) );
  REQUIRE( result.isMember( "OUTPUT_STR" ) );
  REQUIRE( result["OUTPUT_STR"].asString() == "processed_hello" );
}

TEST_CASE( "AtomicAlgorithmRegistry provider algorithms mirroring callback and duplicate handling", "[processing][registry]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  size_t initialCount = registry.adapterCount();

  DummyTestAlgorithm alg;
  alg.initAlgorithm();

  bool providerCalled = false;
  AtomicAlgorithmRegistry::setProviderAlgorithmProvider( [&]( AtomicAlgorithmRegistry &reg ) {
    providerCalled = true;
    if ( !reg.findAdapter( alg.id().toStdString() ) )
    {
      reg.registerAdapter( std::make_shared<ProviderAlgorithmAdapter>( alg ) );
    }
  } );

  REQUIRE( providerCalled );
  REQUIRE( registry.adapterCount() == initialCount + 1 );
  REQUIRE( registry.findAdapter( "dummyalg" ) != nullptr );

  // Duplicate call to registerProviderAlgorithms should not add duplicate entries or crash
  registry.registerProviderAlgorithms();
  REQUIRE( registry.adapterCount() == initialCount + 1 );

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

  // Reset preserves provider registration
  registry.reset();
  REQUIRE( registry.findAdapter( "dummyalg" ) != nullptr );
}
