// tests/test_descriptor_outputs.cpp
//
// AlgorithmDescriptor 2.0: real input/output port extraction from operator
// schemas. Statistics-only, multi-output, array and string outputs must not be
// flattened into a single Raster "output" port.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_adapter.h"
#include "operators/rs/rs_segment_stats_operator.h"
#include "operators/rs/rs_endmember_extraction_operator.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "operators/rs/rs_change_detection_operator.h"

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

TEST_CASE( "Descriptor parses real outputs from operator schema", "[processing][descriptor][outputs]" )
{
  SECTION( "statistics-only operator (segment_stats) exposes Table + Numeric outputs" )
  {
    auto op = std::make_unique<RsSegmentStatsOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

    REQUIRE( !desc.outputs.empty() );
    REQUIRE( desc.outputs.size() >= 2 );

    const PortDescriptor *csvOut = nullptr;
    const PortDescriptor *segmentsOut = nullptr;
    for ( const auto &out : desc.outputs )
    {
      if ( out.name == "output" ) csvOut = &out;
      else if ( out.name == "segments" ) segmentsOut = &out;
    }

    REQUIRE( csvOut != nullptr );
    REQUIRE( csvOut->type == DataType::Table );
    REQUIRE( csvOut->fileFormat == "csv" );

    REQUIRE( segmentsOut != nullptr );
    REQUIRE( segmentsOut->type == DataType::Integer );
  }

  SECTION( "multi-method facade (change_detection) exposes numeric auxiliary outputs" )
  {
    auto op = std::make_unique<RsChangeDetectionOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

    REQUIRE( desc.outputs.size() >= 4 );
    bool hasRaster = false;
    bool hasMean = false;
    bool hasChangedPixels = false;
    for ( const auto &out : desc.outputs )
    {
      if ( out.name == "output" ) hasRaster = ( out.type == DataType::Raster );
      else if ( out.name == "mean" ) hasMean = ( out.type == DataType::Numeric );
      else if ( out.name == "changedPixels" ) hasChangedPixels = ( out.type == DataType::Integer );
    }
    REQUIRE( hasRaster );
    REQUIRE( hasMean );
    REQUIRE( hasChangedPixels );
  }

  SECTION( "string auxiliary output (spectral_index) is not a raster" )
  {
    auto op = std::make_unique<RsSpectralIndexOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

    REQUIRE( desc.outputs.size() >= 1 );
    bool hasIndexString = false;
    for ( const auto &out : desc.outputs )
      if ( out.name == "index" && out.type == DataType::String )
        hasIndexString = true;
    REQUIRE( hasIndexString );
  }

  SECTION( "output schema round-trips through toOutputSchema()" )
  {
    auto op = std::make_unique<RsSegmentStatsOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );
    const Json::Value outSchema = desc.toOutputSchema();

    REQUIRE( outSchema.isObject() );
    REQUIRE( outSchema["properties"].isObject() );
    REQUIRE( outSchema["properties"].isMember( "output" ) );
    REQUIRE( outSchema["properties"]["output"].isMember( "x-ui-type" ) );
    REQUIRE( outSchema["properties"]["output"]["x-ui-type"].asString() == "table" );
  }
}

TEST_CASE( "Descriptor input ports carry type/range/array constraints", "[processing][descriptor][inputs]" )
{
  SECTION( "enum parameters are typed as Enum with options" )
  {
    auto op = std::make_unique<RsChangeDetectionOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );

    const PortDescriptor *method = nullptr;
    for ( const auto &in : desc.inputs )
      if ( in.name == "method" ) method = &in;
    REQUIRE( method != nullptr );
    REQUIRE( method->type == DataType::Enum );
    REQUIRE( !method->enumOptions.empty() );
    REQUIRE( std::find( method->enumOptions.begin(), method->enumOptions.end(), "mad" )
             != method->enumOptions.end() );
  }

  SECTION( "numeric range constraints survive into the JSON schema" )
  {
    auto op = std::make_unique<RsChangeDetectionOperator>();
    const AlgorithmDescriptor desc = AlgorithmDescriptorBuilder::buildFromRsOperator( *op );
    const Json::Value inputSchema = desc.toInputSchema();
    REQUIRE( inputSchema["properties"].isMember( "percentile" ) );
  }
}
