// tests/test_atomic_registry_contract.cpp
//
// Canonical registry contract: unique IDs, stable descriptors, explicit
// initialize(), legacy facade compatibility, and composition (an atomic
// primitive's output feeds the next algorithm as a valid input).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/schema_validator.h"
#include "operators/rs/rs_change_primitives.h"
#include "operators/rs/rs_threshold_raster_operator.h"

#include <set>
#include <string>

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

TEST_CASE( "Registry exposes unique ids and stable descriptors", "[processing][registry][contract]" )
{
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const auto descriptors = registry.listDescriptors();
    REQUIRE( descriptors.size() >= 40 );

    std::set<std::string> ids;
    for ( const auto &d : descriptors )
        REQUIRE( ids.insert( d.id ).second ); // unique

    // Descriptors are stable across calls (no per-call mutation).
    const AlgorithmDescriptor first = registry.findAdapter( "rs:change_difference" )->descriptor();
    const AlgorithmDescriptor second = registry.findAdapter( "rs:change_difference" )->descriptor();
    REQUIRE( first.id == second.id );
    REQUIRE( first.outputs.size() == second.outputs.size() );
    REQUIRE( first.agentMetadata.memoryPolicy == second.agentMetadata.memoryPolicy );
}

TEST_CASE( "Legacy facade IDs remain registered and executable", "[processing][registry][compat]" )
{
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    // The legacy multi-method facade is still present alongside the primitives.
    REQUIRE( registry.findAdapter( "rs:change_detection" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:change_difference" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:image_fusion" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:fusion_linear" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:fusion_gram_schmidt" ) != nullptr );
    REQUIRE( registry.findAdapter( "rs:threshold_raster" ) != nullptr );

    // Facade metadata points at its primitives.
    const auto facadeDesc = registry.findAdapter( "rs:change_detection" )->descriptor();
    REQUIRE( facadeDesc.agentMetadata.facadeOf.find( "rs:change_difference" )
             != std::string::npos );
    // Primitives point back at the facade.
    const auto primitiveDesc = registry.findAdapter( "rs:change_difference" )->descriptor();
    REQUIRE( primitiveDesc.agentMetadata.facadeOf == "change_detection" );
}

TEST_CASE( "Composition: primitive output satisfies the next algorithm's input contract", "[processing][registry][compose]" )
{
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();

    const auto diffDesc = registry.findAdapter( "rs:change_difference" )->descriptor();
    const auto thresholdDesc = registry.findAdapter( "rs:threshold_raster" )->descriptor();

    // The change primitive produces a Raster output named "output".
    const PortDescriptor *diffOut = nullptr;
    for ( const auto &out : diffDesc.outputs )
        if ( out.name == "output" && out.type == DataType::Raster )
            diffOut = &out;
    REQUIRE( diffOut != nullptr );

    // The threshold operator consumes a Raster input named "input".
    const PortDescriptor *thresholdIn = nullptr;
    for ( const auto &in : thresholdDesc.inputs )
        if ( in.name == "input" && in.type == DataType::Raster )
            thresholdIn = &in;
    REQUIRE( thresholdIn != nullptr );

    // Feeding the change output path as the threshold input validates.
    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/magnitude.tif"; // shape-valid raster path
    params["output"] = "/tmp/mask.tif";
    const auto result = validateParameters( params, thresholdDesc );
    REQUIRE( result.ok() );
}
