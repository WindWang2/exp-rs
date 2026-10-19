// tests/test_schema_validator.cpp
//
// Shared JSON schema validation: required/type/enum/range/array/shape and the
// unknown-parameter policy. Structured errors are machine-readable
// ({code, parameter, expected, actual, message}).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/schema_validator.h"

using namespace sicnu::processing;

namespace {

AlgorithmDescriptor makeTestDescriptor()
{
    AlgorithmDescriptor desc;

    PortDescriptor input;
    input.name = "input";
    input.type = DataType::Raster;
    input.required = true;
    desc.inputs.push_back( input );

    PortDescriptor method;
    method.name = "method";
    method.type = DataType::Enum;
    method.enumOptions = { "a", "b" };
    method.required = false;
    desc.inputs.push_back( method );

    PortDescriptor threshold;
    threshold.name = "threshold";
    threshold.type = DataType::Numeric;
    threshold.required = false;
    threshold.hasMinimum = true;
    threshold.minimum = 0.0;
    threshold.hasMaximum = true;
    threshold.maximum = 1.0;
    desc.inputs.push_back( threshold );

    PortDescriptor bands;
    bands.name = "bands";
    bands.isArray = true;
    bands.itemType = DataType::Integer;
    bands.required = false;
    desc.inputs.push_back( bands );

    PortDescriptor label;
    label.name = "label";
    label.type = DataType::String;
    label.required = false;
    desc.inputs.push_back( label );

    PortDescriptor extra;
    extra.name = "extra";
    extra.type = DataType::Any;
    extra.required = false;
    desc.inputs.push_back( extra );

    return desc;
}

} // namespace

TEST_CASE( "validateParameters checks required presence", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    const auto result = validateParameters( params, desc );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.size() == 1 );
    REQUIRE( result.errors.front().code == "missing_required" );
    REQUIRE( result.errors.front().parameter == "input" );
    REQUIRE( result.errors.front().message == "Missing required parameter: input" );
}

TEST_CASE( "validateParameters checks enum membership", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    params["method"] = "z"; // not in {a, b}
    const auto result = validateParameters( params, desc );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.front().code == "enum_mismatch" );
    REQUIRE( result.errors.front().parameter == "method" );
    REQUIRE( result.errors.front().actual == "\"z\"" );
}

TEST_CASE( "validateParameters checks numeric range", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    params["threshold"] = 5.0; // > maximum 1.0
    const auto result = validateParameters( params, desc );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.front().code == "range_violation" );
    REQUIRE( result.errors.front().parameter == "threshold" );
}

TEST_CASE( "validateParameters checks array item types", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    Json::Value bands( Json::arrayValue );
    bands.append( 1 );
    bands.append( "not-an-int" );
    params["bands"] = bands;
    const auto result = validateParameters( params, desc );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.front().code == "array_item_type" );
    REQUIRE( result.errors.front().parameter == "bands[1]" );
}

TEST_CASE( "validateParameters accepts numeric strings for numeric ports", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    params["threshold"] = "0.25"; // LLMs often pass numbers as strings
    const auto result = validateParameters( params, desc );
    REQUIRE( result.ok() );
}

TEST_CASE( "validateParameters rejects null/object/array on String ports", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    // Explicit null is a type violation: it would otherwise be stringified
    // downstream into the literal "null" (#1566).
    Json::Value nullParams( Json::objectValue );
    nullParams["input"] = "/tmp/a.tif";
    nullParams["label"] = Json::nullValue;
    const auto nullResult = validateParameters( nullParams, desc );
    REQUIRE_FALSE( nullResult.ok() );
    REQUIRE( nullResult.errors.front().code == "type_mismatch" );
    REQUIRE( nullResult.errors.front().parameter == "label" );
    REQUIRE( nullResult.errors.front().expected == "string" );

    Json::Value objParams( Json::objectValue );
    objParams["input"] = "/tmp/a.tif";
    Json::Value obj( Json::objectValue );
    obj["path"] = "/tmp/a.tif";
    objParams["label"] = obj;
    const auto objResult = validateParameters( objParams, desc );
    REQUIRE_FALSE( objResult.ok() );
    REQUIRE( objResult.errors.front().code == "type_mismatch" );
    REQUIRE( objResult.errors.front().parameter == "label" );

    Json::Value arrParams( Json::objectValue );
    arrParams["input"] = "/tmp/a.tif";
    Json::Value arr( Json::arrayValue );
    arr.append( "a" );
    arrParams["label"] = arr;
    const auto arrResult = validateParameters( arrParams, desc );
    REQUIRE_FALSE( arrResult.ok() );
    REQUIRE( arrResult.errors.front().code == "type_mismatch" );
    REQUIRE( arrResult.errors.front().parameter == "label" );
}

TEST_CASE( "validateParameters rejects explicit null but keeps structured values on Any ports", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value nullParams( Json::objectValue );
    nullParams["input"] = "/tmp/a.tif";
    nullParams["extra"] = Json::nullValue;
    const auto nullResult = validateParameters( nullParams, desc );
    REQUIRE_FALSE( nullResult.ok() );
    REQUIRE( nullResult.errors.front().code == "type_mismatch" );
    REQUIRE( nullResult.errors.front().parameter == "extra" );
    REQUIRE( nullResult.errors.front().expected == "non-null value" );

    // "Any" keeps structured values — only null has no lossless mapping.
    Json::Value objParams( Json::objectValue );
    objParams["input"] = "/tmp/a.tif";
    Json::Value obj( Json::objectValue );
    obj["key"] = "value";
    objParams["extra"] = obj;
    REQUIRE( validateParameters( objParams, desc ).ok() );

    Json::Value arrParams( Json::objectValue );
    arrParams["input"] = "/tmp/a.tif";
    Json::Value arr( Json::arrayValue );
    arr.append( 1 );
    arrParams["extra"] = arr;
    REQUIRE( validateParameters( arrParams, desc ).ok() );
}

TEST_CASE( "validateParameters accepts scalars on String ports", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    params["label"] = "out.tif";
    REQUIRE( validateParameters( params, desc ).ok() );

    params["label"] = 5; // LLMs often pass numbers where strings are expected
    REQUIRE( validateParameters( params, desc ).ok() );

    params["label"] = "5";
    REQUIRE( validateParameters( params, desc ).ok() );

    params["label"] = true;
    REQUIRE( validateParameters( params, desc ).ok() );
}

TEST_CASE( "unknown parameter policy: Error vs Warn vs Ignore", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/a.tif";
    params["bogusParam"] = 1;

    const auto strict = validateParameters( params, desc, UnknownParameterPolicy::Error );
    REQUIRE_FALSE( strict.ok() );
    REQUIRE( strict.errors.front().code == "unknown_parameter" );

    const auto warn = validateParameters( params, desc, UnknownParameterPolicy::Warn );
    REQUIRE( warn.ok() );
    REQUIRE( warn.warnings.size() == 1 );
    REQUIRE( warn.warnings.front().code == "unknown_parameter" );

    const auto ignore = validateParameters( params, desc, UnknownParameterPolicy::Ignore );
    REQUIRE( ignore.ok() );
    REQUIRE( ignore.warnings.empty() );
}

TEST_CASE( "validateParameters validates raster file shape", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    // Object shape {path: ...} is accepted (agents may pass {path}).
    Json::Value objParams( Json::objectValue );
    Json::Value pathObj( Json::objectValue );
    pathObj["path"] = "/tmp/a.tif";
    objParams["input"] = pathObj;
    REQUIRE( validateParameters( objParams, desc ).ok() );

    // Non-string, non-object is rejected.
    Json::Value badParams( Json::objectValue );
    badParams["input"] = 42;
    const auto result = validateParameters( badParams, desc );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.front().code == "invalid_shape" );
}

TEST_CASE( "structured validation result serializes to JSON", "[processing][schema_validator]" )
{
    const AlgorithmDescriptor desc = makeTestDescriptor();

    Json::Value params( Json::objectValue );
    const auto result = validateParameters( params, desc );
    const Json::Value json = result.toJson();
    REQUIRE( json["valid"].asBool() == false );
    REQUIRE( json["errors"].isArray() );
    REQUIRE( json["errors"][0]["code"].asString() == "missing_required" );
    REQUIRE( json["errors"][0]["message"].asString() == "Missing required parameter: input" );
    REQUIRE( json["warnings"].isArray() );
}
