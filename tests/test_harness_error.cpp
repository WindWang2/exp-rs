// tests/test_harness_error.cpp
//
// Harness 4.0 Phase 12: stable error taxonomy. Pins the closed code
// vocabulary, category/retry-class mapping, envelope shape, and the legacy
// normalization contract.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <string>

#include "agent/harness/harness_error.h"

using namespace sicnu::agent::harness;

namespace {
std::string asString( const Json::Value &v ) { return v.asString(); }
} // namespace

TEST_CASE( "Harness error codes carry mission-mandated vocabulary", "[harness][errors]" )
{
  REQUIRE( isKnownErrorCode( "DATASET_NOT_FOUND" ) );
  REQUIRE( isKnownErrorCode( "BAND_ROLE_UNRESOLVED" ) );
  REQUIRE( isKnownErrorCode( "CRS_MISMATCH" ) );
  REQUIRE( isKnownErrorCode( "GRID_MISMATCH" ) );
  REQUIRE( isKnownErrorCode( "INVALID_RADIOMETRY" ) );
  REQUIRE( isKnownErrorCode( "INSUFFICIENT_MEMORY" ) );
  REQUIRE( isKnownErrorCode( "MODEL_INCOMPATIBLE" ) );
  REQUIRE( isKnownErrorCode( "EXECUTION_FAILED" ) );
  REQUIRE( isKnownErrorCode( "CANCELLED" ) );
  REQUIRE( isKnownErrorCode( "OUTPUT_INVALID" ) );
  REQUIRE( isKnownErrorCode( "MAP_PREFLIGHT_FAILED" ) );
  // Harness-internal codes.
  REQUIRE( isKnownErrorCode( "ENTITY_AMBIGUOUS" ) );
  REQUIRE( isKnownErrorCode( "PREFLIGHT_BLOCKED" ) );
  REQUIRE( !isKnownErrorCode( "TOTALLY_MADE_UP" ) );
}

TEST_CASE( "Error categories and retry classes are stable", "[harness][errors]" )
{
  // Validation failures never retry automatically.
  CHECK( errorCategoryForCode( "DATASET_NOT_FOUND" ) == "validation" );
  CHECK( std::string( retryClassToString( retryClassForCode( "DATASET_NOT_FOUND" ) ) ) == "none" );
  CHECK( errorCategoryForCode( "CRS_MISMATCH" ) == "validation" );
  CHECK( errorCategoryForCode( "INSUFFICIENT_MEMORY" ) == "environment" );
  CHECK( std::string( retryClassToString( retryClassForCode( "INSUFFICIENT_MEMORY" ) ) ) == "manual" );
  // Only transient-class codes auto-retry (PlanRunner policy, Phase 13).
  CHECK( std::string( retryClassToString( retryClassForCode( "TRANSIENT_FAILURE" ) ) ) == "transient" );
  CHECK( std::string( retryClassToString( retryClassForCode( "IO_ERROR" ) ) ) == "transient" );
  CHECK( std::string( retryClassToString( retryClassForCode( "EXECUTION_FAILED" ) ) ) == "manual" );
  CHECK( std::string( retryClassToString( retryClassForCode( "NO_SUCH_CODE" ) ) ) == "manual" );
}

TEST_CASE( "HarnessError JSON envelope carries all Phase-12 fields", "[harness][errors]" )
{
  HarnessError err = HarnessError::makeWithAction(
    "CRS_MISMATCH", "Inputs use different CRS",
    "reproject", Json::Value( "EPSG:32650" ) );
  const Json::Value json = err.toJson();
  CHECK( asString( json["code"] ) == "CRS_MISMATCH" );
  CHECK( asString( json["summary"] ) == "Inputs use different CRS" );
  CHECK( json["recoverable"].asBool() );
  CHECK( json["suggested_actions"].isArray() );
  CHECK( json["suggested_actions"].size() == 1 );
  CHECK( asString( json["suggested_actions"][0]["action"] ) == "reproject" );
  CHECK( json.isMember( "details" ) );
  CHECK( asString( json["category"] ) == "validation" );
  CHECK( asString( json["retry_class"] ) == "none" );

  const Json::Value envelope = errorEnvelope( err );
  CHECK( !envelope["success"].asBool() );
  CHECK( asString( envelope["error"]["code"] ) == "CRS_MISMATCH" );
  CHECK( !envelope["error"]["retryable"].asBool() );
}

TEST_CASE( "Legacy error codes normalize into the stable taxonomy", "[harness][errors]" )
{
  CHECK( normalizeLegacyError( "DATASET_NOT_FOUND", "m" ).code == "DATASET_NOT_FOUND" );
  CHECK( normalizeLegacyError( "NOT_FOUND", "m" ).code == "DATASET_NOT_FOUND" );
  CHECK( normalizeLegacyError( "MODEL_NOT_FOUND", "m" ).code == "MODEL_NOT_READY" );
  CHECK( normalizeLegacyError( "DATA_IO", "m" ).code == "TRANSIENT_FAILURE" );
  CHECK( normalizeLegacyError( "CANCELED", "m" ).code == "CANCELLED" );
  // Unmapped codes still surface with a stable outer shape.
  HarnessError weird = normalizeLegacyError( "WIBBLE", "something broke" );
  CHECK( weird.code == "EXECUTION_FAILED" );
  CHECK( weird.details["legacy_code"].asString() == "WIBBLE" );
}
