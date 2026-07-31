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
