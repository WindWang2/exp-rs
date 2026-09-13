// tests/test_atomic_algorithm_adapter.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_adapter.h"
#include "operators/rs/rs_spectral_index_operator.h"

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

TEST_CASE( "AlgorithmDescriptor builds schema and Tool Call definition from RSOperator", "[processing][adapter]" )
{
  auto op = std::make_unique<RsSpectralIndexOperator>();
  AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

  REQUIRE( desc.id == "rs:spectral_index" );
  REQUIRE( desc.displayName == "Spectral Index" );
  REQUIRE( !desc.inputs.empty() );

  // Check PortDescriptor for 'input' and 'nir'
  bool foundInput = false;
  bool foundNir = false;
  for ( const auto &port : desc.inputs )
  {
    if ( port.name == "input" )
    {
      foundInput = true;
      REQUIRE( port.type == DataType::Raster );
      REQUIRE( port.required == true );
    }
    else if ( port.name == "nir" )
    {
      foundNir = true;
      REQUIRE( port.required == false );
    }
  }
  REQUIRE( foundInput == true );
  REQUIRE( foundNir == true );

  // Check AgentMetadata
  REQUIRE( !desc.agentMetadata.purpose.empty() );
  REQUIRE( !desc.agentMetadata.tags.empty() );

  // Check toInputSchema()
  Json::Value inputSchema = desc.toInputSchema();
  REQUIRE( inputSchema.isObject() );
  REQUIRE( inputSchema["type"].asString() == "object" );
  REQUIRE( inputSchema["properties"].isObject() );
  REQUIRE( inputSchema["properties"].isMember( "input" ) );
  REQUIRE( inputSchema["properties"]["input"]["x-ui-type"].asString() == "raster" );

  // Check toToolCallDefinition() (OpenAI / LLM format)
  Json::Value toolCall = desc.toToolCallDefinition();
  REQUIRE( toolCall.isObject() );
  REQUIRE( toolCall["type"].asString() == "function" );
  REQUIRE( toolCall["function"].isObject() );
  REQUIRE( toolCall["function"]["name"].asString() == "rs_spectral_index" );
  REQUIRE( !toolCall["function"]["description"].asString().empty() );
  REQUIRE( toolCall["function"]["parameters"]["properties"].isMember( "input" ) );
}

TEST_CASE( "RsOperatorAdapter wraps RSOperator execution and progress", "[processing][adapter]" )
{
  auto op = std::make_unique<RsSpectralIndexOperator>();
  RsOperatorAdapter adapter( std::move( op ) );

  REQUIRE( adapter.algorithmId() == "rs:spectral_index" );

  AlgorithmDescriptor desc = adapter.descriptor();
  REQUIRE( desc.id == "rs:spectral_index" );
}

namespace {

/// Minimal stub for capability-projection tests: fixed policy + halo.
class StubHaloOperator : public RSOperator
{
  public:
    explicit StubHaloOperator( RSOperatorMemoryPolicy policy, int halo ) : m_policy( policy ), m_halo( halo ) {}
    std::string name() const override { return "stub:halo_probe"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return m_policy; }
    int streamingHaloPixels() const override { return m_halo; }
    Json::Value run( const Json::Value &, RSOperatorContext & ) override { return {}; }
  private:
    RSOperatorMemoryPolicy m_policy;
    int m_halo;
};

} // namespace

TEST_CASE( "Halo and new memory policies project into agent execution metadata", "[processing][adapter][lsee10]" )
{
  SECTION( "halo > 0 lands in execution.haloPixels" )
  {
    StubHaloOperator op( RSOperatorMemoryPolicy::Streaming, 3 );
    AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( op );
    CHECK( desc.agentMetadata.memoryPolicy == "streaming" );
    CHECK( desc.agentMetadata.largeRasterSafe );
    CHECK( desc.agentMetadata.execution.isObject() );
    CHECK( desc.agentMetadata.execution["haloPixels"].asInt() == 3 );
  }

  SECTION( "halo 0 stays absent (byte-identical metadata for legacy operators)" )
  {
    StubHaloOperator op( RSOperatorMemoryPolicy::Streaming, 0 );
    AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( op );
    CHECK_FALSE( desc.agentMetadata.execution.isObject() );
    CHECK_FALSE( desc.agentMetadata.execution.isMember( "haloPixels" ) );
  }

  SECTION( "global reduction and external memory policies are large-raster-safe" )
  {
    StubHaloOperator reduce( RSOperatorMemoryPolicy::GlobalReductionStreaming, 0 );
    AlgorithmDescriptor descReduce = AlgorithmDescriptorBuilder::buildFromRsOperator( reduce );
    CHECK( descReduce.agentMetadata.memoryPolicy == "global_reduction_streaming" );
    CHECK( descReduce.agentMetadata.largeRasterSafe );

    StubHaloOperator spill( RSOperatorMemoryPolicy::ExternalMemoryStreaming, 2 );
    AlgorithmDescriptor descSpill = AlgorithmDescriptorBuilder::buildFromRsOperator( spill );
    CHECK( descSpill.agentMetadata.memoryPolicy == "external_memory_streaming" );
    CHECK( descSpill.agentMetadata.largeRasterSafe );
    CHECK( descSpill.agentMetadata.execution["haloPixels"].asInt() == 2 );
  }
}
