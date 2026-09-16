// tests/test_quality_mosaic_registration.cpp — F15: registry-path wiring
// test for rs:quality_mosaic (ADR 0163).
//
// test_quality_mosaic_operator instantiates the operator class directly;
// this test exercises the REGISTRATION seam (factory registry) so a broken
// rs_operators_init wiring cannot hide behind direct construction. It also
// stands in for the master-blocked test_rs_operators capability check for
// this operator (see .planning EVIDENCE: pre-existing sicnu_agent breakage).
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

using namespace sicnu::operators;

TEST_CASE( "QualityMosaic: registered through the factory registry seam",
           "[processing][mosaic][operator][registration]" )
{
    rs::initBuiltinRsOperators();
    auto &registry = RSOperatorRegistry::instance();

    REQUIRE( registry.hasOperator( "rs:quality_mosaic" ) );

    auto op = registry.create( "rs:quality_mosaic" );
    REQUIRE( op != nullptr );
    CHECK( op->name() == "rs:quality_mosaic" );
    CHECK( op->group() == "composition" );

    // Schema contract: declares inputs/output and mirrors metadata claims.
    const auto schema = op->schema();
    REQUIRE( schema.isMember( "properties" ) );
    REQUIRE( schema.isMember( "required" ) );
    bool hasInputs = false, hasOutput = false;
    for ( const auto &req : schema["required"] )
    {
        if ( req.asString() == "inputs" )
            hasInputs = true;
        if ( req.asString() == "output" )
            hasOutput = true;
    }
    CHECK( hasInputs );
    CHECK( hasOutput );
    CHECK( schema["properties"].isMember( "method" ) );
    CHECK( schema["properties"].isMember( "reportOutput" ) );
    CHECK( schema["properties"].isMember( "provenance" ) );

    const auto meta = op->metadata();
    CHECK( meta["memoryPolicy"].asString() == "streaming" );
}
