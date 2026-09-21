// tests/test_agentbench_core.cpp
//
// RS14 agent-benchmark harness: lane smoke test + deterministic serializer
// contract. Everything in this lane is pure C++ (no Qt, no GDAL, no engine).

#include <catch2/catch_test_macros.hpp>

#include "agentbench/json_writer.h"
#include "agentbench/version.h"

#include <json/json.h>

#include <cmath>
#include <limits>
#include <string>

using sicnu::agentbench::deterministicSerialize;
using sicnu::agentbench::kAgentBenchFrameworkVersion;

TEST_CASE( "agentbench lane links and exposes framework version", "[agentbench]" )
{
	REQUIRE( std::string( kAgentBenchFrameworkVersion ).size() > 0 );
}

TEST_CASE( "deterministicSerialize sorts object keys and drops whitespace", "[agentbench]" )
{
	Json::Value object{Json::objectValue};
	object["zeta"] = 1;
	object["alpha"] = "a";
	object["mid"]["nested"] = true;

	const std::string bytes = deterministicSerialize( object );
	// Keys sorted at every level, no spaces.
	REQUIRE( bytes == R"({"alpha":"a","mid":{"nested":true},"zeta":1})" );
}

TEST_CASE( "deterministicSerialize is stable across rebuilds of equal values", "[agentbench]" )
{
	Json::Value first{Json::objectValue};
	first["b"] = Json::arrayValue;
	first["b"].append( 1.5 );
	first["b"].append( "x" );
	first["a"] = false;

	Json::Value second{Json::objectValue};
	second["a"] = false;
	second["b"] = Json::arrayValue;
	second["b"].append( 1.5 );
	second["b"].append( "x" );

	REQUIRE( deterministicSerialize( first ) == deterministicSerialize( second ) );
	REQUIRE( deterministicSerialize( first ) == R"({"a":false,"b":[1.5,"x"]})" );
}

TEST_CASE( "deterministicSerialize preserves array order and escapes strings", "[agentbench]" )
{
	Json::Value array{Json::arrayValue};
	array.append( "b" );
	array.append( "a" );
	array.append( Json::Value( "quote\"newline\n" ) );

	const std::string bytes = deterministicSerialize( array );
	REQUIRE( bytes == R"(["b","a","quote\"newline\n"])" );
}

TEST_CASE( "deterministicSerialize maps non-finite doubles to null", "[agentbench]" )
{
	Json::Value object{Json::objectValue};
	object["nan"] = std::sqrt( -1.0 );
	object["inf"] = std::numeric_limits<double>::infinity();

	REQUIRE( deterministicSerialize( object ) == R"({"inf":null,"nan":null})" );
}
