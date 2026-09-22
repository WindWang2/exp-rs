// src/verify/verify_types.cpp — value objects, validation and versioned
// canonical serde for the Unified Scientific Verifier.
#include "verify_types.h"

#include "verify_error_codes.h"
#include "verify_sha256.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace sicnu::verify
{

const std::vector<std::string> kCheckKinds = {
    "state.invariant",        "artifact.exists",  "artifact.type",
    "artifact.grid",          "artifact.schema",  "metric.range",
    "relational.consistency", "provenance.complete", "reproducibility.digest",
    "cross.output.consistency",
};

const std::vector<std::string> kSpecScopes = { "node", "task" };

namespace
{

bool contains( const std::vector<std::string> &vocabulary, const std::string &value )
{
    return std::find( vocabulary.begin(), vocabulary.end(), value ) != vocabulary.end();
}

/// Closed operator vocabularies for state invariants and relations.
const std::vector<std::string> kStateOps = { "eq", "ne", "present", "absent", "gt", "ge", "lt", "le" };
const std::vector<std::string> kRelationOps = { "eq", "ne", "lt", "le", "gt", "ge", "approx", "sum_is" };
const std::vector<std::string> kArtifactKinds = { "raster", "vector", "table", "json", "sidecar" };

bool isString( const Json::Value &value )
{
    return value.isString();
}

bool isReal( const Json::Value &value )
{
    return value.isDouble() || value.isIntegral();
}

bool isCount( const Json::Value &value )
{
    // A count is a non-negative integer; JSON bool is not a number in jsoncpp.
    // Range-limited to what asInt64() can read back without throwing: a
    // uint64 above 2^63-1 passes isIntegral() but makes the later numeric
    // reads escape as Json::LogicError.
    if ( !value.isIntegral() || value.isBool() )
        return false;
    return value.isInt64() && value.asInt64() >= 0;
}

bool isHex64( const std::string &text )
{
    if ( text.size() != 64 )
        return false;
    for ( const char c : text )
    {
        const bool hex = ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
        if ( !hex )
            return false;
    }
    return true;
}

std::string at( const std::string &where, const std::string &reason )
{
    return where + ": " + reason;
}

// ---------------------------------------------------------------------------
// Per-kind params validation. Every check must be ABLE to fail: a check
// whose params pin no constraint is rejected as vacuous.
// ---------------------------------------------------------------------------

void validateStateInvariant( const Json::Value &params, const std::string &where,
                             std::vector<std::string> &errors )
{
    const Json::Value &expectations = params["expectations"];
    if ( !expectations.isArray() || expectations.empty() )
    {
        errors.push_back( at( where, "'expectations' must be a non-empty array" ) );
        return;
    }
    if ( expectations.size() > kMaxExpectationsPerCheck )
    {
        errors.push_back( at( where, "too many expectations (budget " +
                                        std::to_string( kMaxExpectationsPerCheck ) + ")" ) );
        return;
    }
    for ( const Json::Value &expectation : expectations )
    {
        if ( !expectation.isObject() )
        {
            errors.push_back( at( where, "each expectation must be an object" ) );
            continue;
        }
        if ( !isString( expectation["key"] ) || expectation["key"].asString().empty() )
            errors.push_back( at( where, "expectation 'key' must be a non-empty string" ) );
        if ( !isString( expectation["op"] ) )
        {
            errors.push_back( at( where, "expectation 'op' must be a string" ) );
            continue;
        }
        const std::string op = expectation["op"].asString();
        if ( !contains( kStateOps, op ) )
            errors.push_back( at( where, "expectation 'op' '" + op + "' is outside the closed vocabulary" ) );
        const bool needsValue = op == "eq" || op == "ne" || op == "gt" || op == "ge" || op == "lt" || op == "le";
        const bool forbidsValue = op == "present" || op == "absent";
        if ( needsValue && expectation["value"].isNull() )
            errors.push_back( at( where, "op '" + op + "' requires a 'value'" ) );
        if ( forbidsValue && expectation.isMember( "value" ) )
            errors.push_back( at( where, "op '" + op + "' must not carry a 'value'" ) );
    }
}

void validateArtifactExists( const Json::Value &params, const std::string &where,
                             std::vector<std::string> &errors )
{
    if ( !isString( params["path"] ) || params["path"].asString().empty() )
        errors.push_back( at( where, "'path' must be a non-empty string" ) );
    if ( params.isMember( "minBytes" ) && !isCount( params["minBytes"] ) )
        errors.push_back( at( where, "'minBytes' must be a non-negative integer" ) );
}

void validateArtifactType( const Json::Value &params, const std::string &where,
                           std::vector<std::string> &errors )
{
    if ( !isString( params["path"] ) || params["path"].asString().empty() )
        errors.push_back( at( where, "'path' must be a non-empty string" ) );
    if ( !isString( params["kind"] ) ||
         !contains( kArtifactKinds, params["kind"].asString() ) )
        errors.push_back( at( where, "'kind' must be one of raster|vector|table|json|sidecar" ) );
}

void validateArtifactGrid( const Json::Value &params, const std::string &where,
                           std::vector<std::string> &errors )
{
    if ( !isString( params["path"] ) || params["path"].asString().empty() )
        errors.push_back( at( where, "'path' must be a non-empty string" ) );
    std::size_t constraints = 0;
    const auto count = [&]() { ++constraints; };
    for ( const char *field : { "width", "height", "bandCount" } )
    {
        if ( params.isMember( field ) )
        {
            if ( !isCount( params[field] ) || params[field].asInt64() <= 0 )
                errors.push_back( at( where, std::string( "'" ) + field + "' must be a positive integer" ) );
            else
                count();
        }
    }
    if ( params.isMember( "crs" ) )
    {
        if ( !isString( params["crs"] ) || params["crs"].asString().empty() )
            errors.push_back( at( where, "'crs' must be a non-empty string" ) );
        else
            count();
    }
    for ( const char *field : { "maxNodataFraction", "minFiniteFraction" } )
    {
        if ( params.isMember( field ) )
        {
            if ( !isReal( params[field] ) )
                errors.push_back( at( where, std::string( "'" ) + field + "' must be a number in [0,1]" ) );
            else
            {
                const double value = params[field].asDouble();
                if ( !std::isfinite( value ) || value < 0.0 || value > 1.0 )
                    errors.push_back( at( where, std::string( "'" ) + field + "' must be a number in [0,1]" ) );
                else
                    count();
            }
        }
    }
    if ( constraints == 0 )
        errors.push_back( at( where, "vacuous check: pin at least one grid constraint" ) );
}

void validateArtifactSchema( const Json::Value &params, const std::string &where,
                             std::vector<std::string> &errors )
{
    if ( !isString( params["path"] ) || params["path"].asString().empty() )
        errors.push_back( at( where, "'path' must be a non-empty string" ) );
    std::size_t constraints = 0;
    if ( params.isMember( "requiredKeys" ) )
    {
        const Json::Value &keys = params["requiredKeys"];
        if ( !keys.isArray() || keys.empty() || keys.size() > kMaxRequiredKeysPerCheck )
            errors.push_back( at( where, "'requiredKeys' must be a non-empty array (budget " +
                                            std::to_string( kMaxRequiredKeysPerCheck ) + ")" ) );
        else
        {
            bool allStrings = true;
            for ( const Json::Value &key : keys )
                allStrings = allStrings && isString( key ) && !key.asString().empty();
            if ( allStrings )
                ++constraints;
            else
                errors.push_back( at( where, "'requiredKeys' entries must be non-empty strings" ) );
        }
    }
    if ( params.isMember( "schemaId" ) )
    {
        if ( isString( params["schemaId"] ) && !params["schemaId"].asString().empty() )
            ++constraints;
        else
            errors.push_back( at( where, "'schemaId' must be a non-empty string" ) );
    }
    if ( constraints == 0 )
        errors.push_back( at( where, "vacuous check: pin 'requiredKeys' and/or 'schemaId'" ) );
}

void validateMetricRange( const Json::Value &params, const std::string &where,
                          std::vector<std::string> &errors )
{
    if ( !isString( params["metric"] ) || params["metric"].asString().empty() )
        errors.push_back( at( where, "'metric' must be a non-empty string" ) );
    const bool hasMin = params.isMember( "min" );
    const bool hasMax = params.isMember( "max" );
    if ( !hasMin && !hasMax )
        errors.push_back( at( where, "vacuous check: pin 'min' and/or 'max'" ) );
    if ( hasMin && ( !isReal( params["min"] ) || !std::isfinite( params["min"].asDouble() ) ) )
        errors.push_back( at( where, "'min' must be a finite number" ) );
    if ( hasMax && ( !isReal( params["max"] ) || !std::isfinite( params["max"].asDouble() ) ) )
        errors.push_back( at( where, "'max' must be a finite number" ) );
    if ( hasMin && hasMax && isReal( params["min"] ) && isReal( params["max"] ) &&
         params["min"].asDouble() > params["max"].asDouble() )
        errors.push_back( at( where, "'min' must not exceed 'max'" ) );
    if ( params.isMember( "tolerance" ) &&
         ( !isReal( params["tolerance"] ) || !std::isfinite( params["tolerance"].asDouble() ) ||
           params["tolerance"].asDouble() < 0.0 ) )
        errors.push_back( at( where, "'tolerance' must be a non-negative finite number" ) );
}

/// A relation operand: a non-empty metric key (string) or a numeric literal.
bool isOperand( const Json::Value &value )
{
    return ( isString( value ) && !value.asString().empty() ) || isReal( value );
}

void validateRelational( const Json::Value &params, const std::string &where,
                         std::vector<std::string> &errors )
{
    const Json::Value &relations = params["relations"];
    if ( !relations.isArray() || relations.empty() )
    {
        errors.push_back( at( where, "'relations' must be a non-empty array" ) );
        return;
    }
    if ( relations.size() > kMaxRelationsPerCheck )
    {
        errors.push_back( at( where, "too many relations (budget " +
                                        std::to_string( kMaxRelationsPerCheck ) + ")" ) );
        return;
    }
    for ( const Json::Value &relation : relations )
    {
        if ( !relation.isObject() )
        {
            errors.push_back( at( where, "each relation must be an object" ) );
            continue;
        }
        if ( !isString( relation["op"] ) )
        {
            errors.push_back( at( where, "relation 'op' must be a string" ) );
            continue;
        }
        const std::string op = relation["op"].asString();
        if ( !contains( kRelationOps, op ) )
        {
            errors.push_back( at( where, "relation 'op' '" + op + "' is outside the closed vocabulary" ) );
            continue;
        }
        if ( !isOperand( relation["left"] ) )
        {
            errors.push_back( at( where, "relation 'left' must be a metric key or numeric literal" ) );
            continue;
        }
        if ( op == "sum_is" )
        {
            const Json::Value &operands = relation["right"];
            if ( !operands.isArray() || operands.empty() || operands.size() > kMaxSumOperands )
            {
                errors.push_back( at( where, "sum_is 'right' must be an operand array (1.." +
                                                    std::to_string( kMaxSumOperands ) + ")" ) );
                continue;
            }
            bool operandsOk = true;
            for ( const Json::Value &operand : operands )
                operandsOk = operandsOk && isOperand( operand );
            if ( !operandsOk )
                errors.push_back( at( where, "sum_is operands must be metric keys or numeric literals" ) );
        }
        else if ( !isOperand( relation["right"] ) )
        {
            errors.push_back( at( where, "relation 'right' must be a metric key or numeric literal" ) );
        }
        if ( relation.isMember( "tolerance" ) &&
             ( !isReal( relation["tolerance"] ) || !std::isfinite( relation["tolerance"].asDouble() ) ||
               relation["tolerance"].asDouble() < 0.0 ) )
            errors.push_back( at( where, "'tolerance' must be a non-negative finite number" ) );
    }
}

void validateProvenance( const Json::Value &params, const std::string &where,
                         std::vector<std::string> &errors )
{
    const bool hasPath = isString( params["path"] ) && !params["path"].asString().empty();
    const bool hasRunId = isString( params["runId"] ) && !params["runId"].asString().empty();
    if ( !hasPath && !hasRunId )
        errors.push_back( at( where, "pin 'path' and/or 'runId'" ) );
    std::size_t constraints = 0;
    for ( const char *field : { "requiredFields", "requiredDimensions" } )
    {
        if ( !params.isMember( field ) )
            continue;
        const Json::Value &list = params[field];
        if ( !list.isArray() || list.empty() || list.size() > kMaxRequiredKeysPerCheck )
        {
            errors.push_back( at( where, std::string( "'" ) + field + "' must be a non-empty array (budget " +
                                            std::to_string( kMaxRequiredKeysPerCheck ) + ")" ) );
            continue;
        }
        bool allStrings = true;
        for ( const Json::Value &entry : list )
            allStrings = allStrings && isString( entry ) && !entry.asString().empty();
        if ( allStrings )
            ++constraints;
        else
            errors.push_back( at( where, std::string( "'" ) + field + "' entries must be non-empty strings" ) );
    }
    if ( constraints == 0 )
        errors.push_back( at( where, "vacuous check: pin 'requiredFields' and/or 'requiredDimensions'" ) );
}

void validateReproducibility( const Json::Value &params, const std::string &where,
                              std::vector<std::string> &errors )
{
    const bool hasPath = isString( params["path"] ) && !params["path"].asString().empty();
    const bool hasExpected = params.isMember( "expectedDigest" );
    const bool hasLeft = isString( params["leftPath"] ) && !params["leftPath"].asString().empty();
    const bool hasRight = isString( params["rightPath"] ) && !params["rightPath"].asString().empty();

    if ( params.isMember( "fingerprintAlgorithm" ) &&
         ( !isString( params["fingerprintAlgorithm"] ) ||
           params["fingerprintAlgorithm"].asString() != "sha256" ) )
        errors.push_back( at( where, "'fingerprintAlgorithm' supports only 'sha256'" ) );

    if ( hasLeft || hasRight )
    {
        if ( hasPath || hasExpected )
            errors.push_back( at( where, "mixes the expected-digest and pair forms" ) );
        if ( !hasLeft || !hasRight )
            errors.push_back( at( where, "pair form needs both 'leftPath' and 'rightPath'" ) );
        return;
    }
    if ( !hasPath )
    {
        errors.push_back( at( where, "pin ('path' + 'expectedDigest') or ('leftPath' + 'rightPath')" ) );
        return;
    }
    if ( !hasExpected || !isString( params["expectedDigest"] ) ||
         !isHex64( params["expectedDigest"].asString() ) )
        errors.push_back( at( where, "'expectedDigest' must be a 64-char hex sha256" ) );
}

void validateCrossOutput( const Json::Value &params, const std::string &where,
                          std::vector<std::string> &errors )
{
    const Json::Value &outputs = params["outputs"];
    if ( !outputs.isArray() || outputs.size() < 2 || outputs.size() > kMaxOutputsPerCrossCheck )
    {
        errors.push_back( at( where, "'outputs' must be an array of 2.." +
                                        std::to_string( kMaxOutputsPerCrossCheck ) + " entries" ) );
        if ( outputs.isArray() )
        {
            for ( const Json::Value &output : outputs )
                if ( !output.isObject() || !isString( output["path"] ) || output["path"].asString().empty() )
                    errors.push_back( at( where, "each output needs a non-empty 'path'" ) );
        }
        return;
    }
    for ( const Json::Value &output : outputs )
        if ( !output.isObject() || !isString( output["path"] ) || output["path"].asString().empty() )
            errors.push_back( at( where, "each output needs a non-empty 'path'" ) );

    std::size_t constraints = 0;
    for ( const char *field : { "sameGrid", "sameCrs" } )
    {
        if ( params.isMember( field ) )
        {
            if ( params[field].isBool() )
                ++constraints;
            else
                errors.push_back( at( where, std::string( "'" ) + field + "' must be a boolean" ) );
        }
    }
    if ( params.isMember( "bandCount" ) )
    {
        if ( isCount( params["bandCount"] ) && params["bandCount"].asInt64() > 0 )
            ++constraints;
        else
            errors.push_back( at( where, "'bandCount' must be a positive integer" ) );
    }
    if ( params.isMember( "tolerance" ) &&
         ( !isReal( params["tolerance"] ) || !std::isfinite( params["tolerance"].asDouble() ) ||
           params["tolerance"].asDouble() < 0.0 ) )
        errors.push_back( at( where, "'tolerance' must be a non-negative finite number" ) );
    if ( constraints == 0 )
        errors.push_back( at( where, "vacuous check: pin sameGrid/sameCrs/bandCount" ) );
}

void validateCheckParams( const VerificationCheckSpec &check, const std::string &where,
                          std::vector<std::string> &errors )
{
    const Json::Value &params = check.params;
    if ( check.kind == "state.invariant" )
        validateStateInvariant( params, where, errors );
    else if ( check.kind == "artifact.exists" )
        validateArtifactExists( params, where, errors );
    else if ( check.kind == "artifact.type" )
        validateArtifactType( params, where, errors );
    else if ( check.kind == "artifact.grid" )
        validateArtifactGrid( params, where, errors );
    else if ( check.kind == "artifact.schema" )
        validateArtifactSchema( params, where, errors );
    else if ( check.kind == "metric.range" )
        validateMetricRange( params, where, errors );
    else if ( check.kind == "relational.consistency" )
        validateRelational( params, where, errors );
    else if ( check.kind == "provenance.complete" )
        validateProvenance( params, where, errors );
    else if ( check.kind == "reproducibility.digest" )
        validateReproducibility( params, where, errors );
    else if ( check.kind == "cross.output.consistency" )
        validateCrossOutput( params, where, errors );
}

// ---------------------------------------------------------------------------
// Strict field sets for serde
// ---------------------------------------------------------------------------

const std::set<std::string> &specTopFields()
{
    static const std::set<std::string> fields = { "schema", "specId", "scope", "checks" };
    return fields;
}

const std::set<std::string> &specCheckFields()
{
    static const std::set<std::string> fields = { "checkId", "kind", "description", "params" };
    return fields;
}

const std::set<std::string> &evidenceFields()
{
    static const std::set<std::string> fields = { "source", "observed", "expected", "hasSampling",
                                                  "sampledPoints" };
    return fields;
}

const std::set<std::string> &reportFields()
{
    static const std::set<std::string> fields = { "schema", "specId", "scope", "specDigest", "overall",
                                                  "counts", "checks" };
    return fields;
}

const std::set<std::string> &reportCheckFields()
{
    static const std::set<std::string> fields = { "checkId", "kind", "status", "code", "message",
                                                  "evidence" };
    return fields;
}

bool hasUnknownField( const Json::Value &object, const std::set<std::string> &allowed,
                      std::string &unknown )
{
    for ( const std::string &member : object.getMemberNames() )
    {
        if ( !allowed.count( member ) )
        {
            unknown = member;
            return true;
        }
    }
    return false;
}

bool parseStatusWire( const std::string &wire, VerificationStatus &out )
{
    if ( wire == "pass" )
        out = VerificationStatus::Pass;
    else if ( wire == "fail" )
        out = VerificationStatus::Fail;
    else if ( wire == "indeterminate" )
        out = VerificationStatus::Indeterminate;
    else
        return false;
    return true;
}

/// The canonical seal refuses bodies carrying non-finite numbers: jsoncpp's
/// writer would emit NaN as `null` (indistinguishable from a real null under
/// the digest) and infinities as `1e+9999` (a token other parsers may read as
/// 0 or reject). A body that cannot be sealed honestly must not exist.
bool containsNonFiniteNumber( const Json::Value &value )
{
    switch ( value.type() )
    {
    case Json::realValue:
        return !std::isfinite( value.asDouble() );
    case Json::arrayValue:
        for ( const Json::Value &element : value )
            if ( containsNonFiniteNumber( element ) )
                return true;
        return false;
    case Json::objectValue:
        for ( const Json::Value &member : value )
            if ( containsNonFiniteNumber( member ) )
                return true;
        return false;
    default:
        return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Spec validation
// ---------------------------------------------------------------------------

std::vector<std::string> validateSpec( const VerificationSpec &spec )
{
    std::vector<std::string> errors;
    if ( spec.specId.empty() )
        errors.push_back( "specId must be non-empty" );
    if ( !contains( kSpecScopes, spec.scope ) )
        errors.push_back( "scope '" + spec.scope + "' is outside the closed vocabulary (node|task)" );
    if ( spec.checks.empty() )
        errors.push_back( "spec has no checks: an empty verification must not exist" );
    if ( spec.checks.size() > kMaxChecksPerSpec )
        errors.push_back( "too many checks (budget " + std::to_string( kMaxChecksPerSpec ) + ")" );

    std::set<std::string> ids;
    for ( std::size_t index = 0; index < spec.checks.size() && index <= kMaxChecksPerSpec; ++index )
    {
        const VerificationCheckSpec &check = spec.checks[index];
        const std::string where = "checks[" + std::to_string( index ) + "] '" + check.checkId + "'";
        if ( check.checkId.empty() )
        {
            errors.push_back( at( where, "checkId must be non-empty" ) );
            continue;
        }
        if ( !ids.insert( check.checkId ).second )
        {
            errors.push_back( at( where, "duplicate checkId" ) );
            continue;
        }
        if ( !contains( kCheckKinds, check.kind ) )
        {
            errors.push_back( at( where, "kind '" + check.kind + "' is outside the closed vocabulary" ) );
            continue;
        }
        if ( !check.params.isObject() )
        {
            errors.push_back( at( where, "params must be an object" ) );
            continue;
        }
        validateCheckParams( check, where, errors );
    }
    return errors;
}

// ---------------------------------------------------------------------------
// Canonical JSON
// ---------------------------------------------------------------------------

std::string canonicalJsonText( const Json::Value &value )
{
    // Refusal sentinel: a body with non-finite numbers gets no canonical
    // text at all (digest() mirrors this with an empty digest) instead of
    // sealing NaN/null ambiguity or a parser-divergent `1e+9999`.
    if ( containsNonFiniteNumber( value ) )
        return {};
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["enableYAMLCompatibility"] = false;
    builder["dropNullPlaceholders"] = false;
    // Doubles at 12 significant digits: 0.1+0.2 and 0.3 serialize
    // identically, so digests do not depend on evaluation order (the same
    // discipline as the lab grade body and DAG provenance).
    builder["precision"] = 12;
    builder["precisionType"] = "significant";
    return Json::writeString( builder, value );
}

// ---------------------------------------------------------------------------
// Spec serde
// ---------------------------------------------------------------------------

Json::Value specToJson( const VerificationSpec &spec )
{
    Json::Value doc( Json::objectValue );
    doc["schema"] = kSpecSchema;
    doc["specId"] = spec.specId;
    doc["scope"] = spec.scope;
    Json::Value checks( Json::arrayValue );
    for ( const VerificationCheckSpec &check : spec.checks )
    {
        Json::Value checkJson( Json::objectValue );
        checkJson["checkId"] = check.checkId;
        checkJson["kind"] = check.kind;
        if ( !check.description.empty() )
            checkJson["description"] = check.description;
        checkJson["params"] = check.params;
        checks.append( checkJson );
    }
    doc["checks"] = checks;
    return doc;
}

bool specFromJson( const Json::Value &json, VerificationSpec &out, std::string &error )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !json.isObject() )
        return fail( "spec document must be a JSON object" );
    std::string unknown;
    if ( hasUnknownField( json, specTopFields(), unknown ) )
        return fail( "unknown spec field: '" + unknown + "'" );
    if ( !isString( json["schema"] ) || json["schema"].asString() != kSpecSchema )
        return fail( std::string( "schema marker must be " ) + kSpecSchema );
    if ( !isString( json["specId"] ) || json["specId"].asString().empty() )
        return fail( "'specId' must be a non-empty string" );
    if ( !isString( json["scope"] ) || json["scope"].asString().empty() )
        return fail( "'scope' must be a non-empty string" );
    if ( !contains( kSpecScopes, json["scope"].asString() ) )
        return fail( "scope '" + json["scope"].asString() + "' is outside the closed vocabulary" );
    if ( !json["checks"].isArray() )
        return fail( "'checks' must be an array" );

    VerificationSpec parsed;
    parsed.schemaVersion = 1;
    parsed.specId = json["specId"].asString();
    parsed.scope = json["scope"].asString();
    std::set<std::string> ids;
    for ( const Json::Value &checkJson : json["checks"] )
    {
        if ( !checkJson.isObject() )
            return fail( "each check must be an object" );
        if ( hasUnknownField( checkJson, specCheckFields(), unknown ) )
            return fail( "unknown check field: '" + unknown + "'" );
        if ( !isString( checkJson["checkId"] ) || checkJson["checkId"].asString().empty() )
            return fail( "'checkId' must be a non-empty string" );
        if ( !isString( checkJson["kind"] ) || checkJson["kind"].asString().empty() )
            return fail( "'kind' must be a non-empty string" );
        const std::string checkId = checkJson["checkId"].asString();
        if ( !contains( kCheckKinds, checkJson["kind"].asString() ) )
            return fail( "check '" + checkId + "': kind '" + checkJson["kind"].asString() +
                         "' is outside the closed vocabulary" );
        if ( !ids.insert( checkId ).second )
            return fail( "duplicate checkId: '" + checkId + "'" );
        VerificationCheckSpec check;
        check.checkId = checkId;
        check.kind = checkJson["kind"].asString();
        if ( checkJson.isMember( "description" ) )
        {
            if ( !isString( checkJson["description"] ) )
                return fail( "'description' must be a string" );
            check.description = checkJson["description"].asString();
        }
        if ( checkJson.isMember( "params" ) )
        {
            if ( !checkJson["params"].isObject() )
                return fail( "'params' must be an object" );
            check.params = checkJson["params"];
        }
        parsed.checks.push_back( std::move( check ) );
    }

    out = std::move( parsed );
    error.clear();
    return true;
}

bool parseSpec( const std::string &text, VerificationSpec &out, std::string &error,
                std::vector<std::string> &validationErrors )
{
    Json::CharReaderBuilder builder;
    builder[ "allowComments" ] = false;
    // A spec is caller-supplied content: bound parser recursion explicitly
    // (the jsoncpp default reader has no depth bound — see the plugin
    // manifest precedent, plugin_manifest.cpp).
    builder[ "stackLimit" ] = 128;
    Json::Value root;
    std::string parseError;
    try
    {
        const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        if ( !reader->parse( text.data(), text.data() + text.size(), &root, &parseError ) )
        {
            error = "invalid JSON: " + parseError;
            return false;
        }
    }
    catch ( const Json::Exception &exception )
    {
        error = std::string( "invalid JSON: " ) + exception.what();
        return false;
    }
    if ( !specFromJson( root, out, error ) )
        return false;
    validationErrors = validateSpec( out );
    if ( !validationErrors.empty() )
    {
        out = VerificationSpec{};
        error = "spec rejected by structural validation";
        return false;
    }
    error.clear();
    return true;
}

std::string specDigest( const VerificationSpec &spec )
{
    const std::string text = canonicalJsonText( specToJson( spec ) );
    return text.empty() ? std::string{} : sha256Hex( text );
}

// ---------------------------------------------------------------------------
// Evidence / report
// ---------------------------------------------------------------------------

Json::Value VerificationEvidence::toCanonicalJson() const
{
    Json::Value json( Json::objectValue );
    json["source"] = source;
    json["observed"] = observed;
    json["expected"] = expected;
    if ( hasSampling )
    {
        json["hasSampling"] = true;
        json["sampledPoints"] = static_cast<Json::UInt64>( sampledPoints );
    }
    return json;
}

bool VerificationEvidence::fromCanonicalJson( const Json::Value &json, std::string &error )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !json.isObject() )
        return fail( "evidence must be a JSON object" );
    std::string unknown;
    if ( hasUnknownField( json, evidenceFields(), unknown ) )
        return fail( "unknown evidence field: '" + unknown + "'" );
    if ( !isString( json["source"] ) )
        return fail( "'source' must be a string" );
    if ( !json["observed"].isObject() || !json["expected"].isObject() )
        return fail( "'observed'/'expected' must be objects" );

    VerificationEvidence parsed;
    parsed.source = json["source"].asString();
    parsed.observed = json["observed"];
    parsed.expected = json["expected"];
    if ( json.isMember( "hasSampling" ) && !json["hasSampling"].isBool() )
        return fail( "'hasSampling' must be a boolean" );
    const bool hasSampling = json.isMember( "hasSampling" ) && json["hasSampling"].asBool();
    const bool hasPoints = json.isMember( "sampledPoints" );
    if ( hasSampling != hasPoints )
        return fail( "'hasSampling' and 'sampledPoints' must appear together" );
    if ( hasSampling )
    {
        const Json::Value &points = json["sampledPoints"];
        if ( !points.isUInt64() && ( !points.isIntegral() || points.asInt64() < 0 ) )
            return fail( "'sampledPoints' must be a non-negative integer" );
        parsed.hasSampling = true;
        parsed.sampledPoints = static_cast<std::size_t>( points.asUInt64() );
    }
    *this = std::move( parsed );
    error.clear();
    return true;
}

std::size_t VerificationReport::passCount() const
{
    std::size_t count = 0;
    for ( const VerificationCheckResult &check : checks )
        if ( check.status == VerificationStatus::Pass )
            ++count;
    return count;
}

std::size_t VerificationReport::failCount() const
{
    std::size_t count = 0;
    for ( const VerificationCheckResult &check : checks )
        if ( check.status == VerificationStatus::Fail )
            ++count;
    return count;
}

std::size_t VerificationReport::indeterminateCount() const
{
    return checks.size() - passCount() - failCount();
}

Json::Value VerificationReport::toCanonicalJson() const
{
    Json::Value doc( Json::objectValue );
    doc["schema"] = kReportSchema;
    doc["specId"] = specId;
    doc["scope"] = scope;
    doc["specDigest"] = specDigest;
    doc["overall"] = statusToWire( overall );
    Json::Value counts( Json::objectValue );
    counts["pass"] = static_cast<Json::UInt64>( passCount() );
    counts["fail"] = static_cast<Json::UInt64>( failCount() );
    counts["indeterminate"] = static_cast<Json::UInt64>( indeterminateCount() );
    doc["counts"] = counts;
    Json::Value checksJson( Json::arrayValue );
    for ( const VerificationCheckResult &check : checks )
    {
        Json::Value checkJson( Json::objectValue );
        checkJson["checkId"] = check.checkId;
        checkJson["kind"] = check.kind;
        checkJson["status"] = statusToWire( check.status );
        if ( !check.code.empty() )
            checkJson["code"] = check.code;
        if ( !check.message.empty() )
            checkJson["message"] = check.message;
        if ( check.evidence )
            checkJson["evidence"] = check.evidence->toCanonicalJson();
        checksJson.append( checkJson );
    }
    doc["checks"] = checksJson;
    return doc;
}

std::string VerificationReport::digest() const
{
    const std::string text = canonicalJsonText( toCanonicalJson() );
    // An empty canonical text is the non-finite refusal sentinel: an
    // unsealable body must not masquerade as sha256("")-sealed.
    return text.empty() ? std::string{} : sha256Hex( text );
}

bool VerificationReport::fromCanonicalJson( const Json::Value &json, VerificationReport &out,
                                            std::string &error, const std::string &expectedDigest )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !json.isObject() )
        return fail( "report document must be a JSON object" );
    std::string unknown;
    if ( hasUnknownField( json, reportFields(), unknown ) )
        return fail( "unknown report field: '" + unknown + "'" );
    if ( !isString( json["schema"] ) || json["schema"].asString() != kReportSchema )
        return fail( std::string( "schema marker must be " ) + kReportSchema );
    if ( !isString( json["specId"] ) || !isString( json["scope"] ) || !isString( json["specDigest"] ) )
        return fail( "'specId'/'scope'/'specDigest' must be strings" );
    if ( !isHex64( json["specDigest"].asString() ) )
        return fail( "'specDigest' must be a 64-char hex sha256" );

    VerificationStatus overall;
    if ( !isString( json["overall"] ) || !parseStatusWire( json["overall"].asString(), overall ) )
        return fail( "'overall' must be a status wire string" );

    const Json::Value &counts = json["counts"];
    if ( !counts.isObject() || counts.size() != 3 || !counts.isMember( "pass" ) ||
         !counts.isMember( "fail" ) || !counts.isMember( "indeterminate" ) )
        return fail( "'counts' must carry pass/fail/indeterminate" );
    // Numbers only: a count of any other JSON type (or a negative one) must be
    // refused before the derived-count comparison reads it.
    for ( const char *member : { "pass", "fail", "indeterminate" } )
    {
        const Json::Value &count = counts[member];
        if ( !count.isUInt64() && ( !count.isIntegral() || count.asInt64() < 0 ) )
            return fail( std::string( "'counts." ) + member + "' must be a non-negative integer" );
    }

    if ( !json["checks"].isArray() )
        return fail( "'checks' must be an array" );

    VerificationReport parsed;
    parsed.schemaVersion = 1;
    parsed.specId = json["specId"].asString();
    parsed.scope = json["scope"].asString();
    parsed.specDigest = json["specDigest"].asString();
    parsed.overall = overall;

    std::vector<VerificationStatus> statuses;
    for ( const Json::Value &checkJson : json["checks"] )
    {
        if ( !checkJson.isObject() )
            return fail( "each check result must be an object" );
        if ( hasUnknownField( checkJson, reportCheckFields(), unknown ) )
            return fail( "unknown check result field: '" + unknown + "'" );
        if ( !isString( checkJson["checkId"] ) || checkJson["checkId"].asString().empty() )
            return fail( "'checkId' must be a non-empty string" );
        if ( !isString( checkJson["kind"] ) || checkJson["kind"].asString().empty() )
            return fail( "'kind' must be a non-empty string" );
        // Note: kind vocabulary is NOT re-checked here on purpose — reports
        // may carry the engine-reserved synthetic kind ("spec.valid") which
        // callers cannot declare in a spec, and future engines may project
        // richer kinds without breaking report readers.
        VerificationCheckResult check;
        check.checkId = checkJson["checkId"].asString();
        check.kind = checkJson["kind"].asString();
        if ( !isString( checkJson["status"] ) ||
             !parseStatusWire( checkJson["status"].asString(), check.status ) )
            return fail( "check '" + check.checkId + "': bad status wire string" );
        if ( checkJson.isMember( "code" ) )
        {
            if ( !isString( checkJson["code"] ) )
                return fail( "'code' must be a string" );
            check.code = checkJson["code"].asString();
        }
        if ( checkJson.isMember( "message" ) )
        {
            if ( !isString( checkJson["message"] ) )
                return fail( "'message' must be a string" );
            check.message = checkJson["message"].asString();
        }
        // A pass carries no code; a non-pass carries a closed-vocabulary one
        // (indeterminate keeps the i_ class visible on the wire).
        if ( check.status == VerificationStatus::Pass && !check.code.empty() )
            return fail( "check '" + check.checkId + "': a pass must not carry a code" );
        if ( check.status != VerificationStatus::Pass )
        {
            if ( check.code.empty() )
                return fail( "check '" + check.checkId + "': non-pass result needs a typed code" );
            if ( !isVerifierCode( check.code ) )
                return fail( "check '" + check.checkId + "': code '" + check.code +
                             "' is outside the verifier vocabulary" );
            if ( ( check.status == VerificationStatus::Indeterminate ) != isIndeterminateCode( check.code ) )
                return fail( "check '" + check.checkId + "': code class contradicts the status" );
        }
        if ( checkJson.isMember( "evidence" ) )
        {
            VerificationEvidence evidence;
            std::string evidenceError;
            if ( !evidence.fromCanonicalJson( checkJson["evidence"], evidenceError ) )
                return fail( "check '" + check.checkId + "': " + evidenceError );
            check.evidence = std::move( evidence );
        }
        statuses.push_back( check.status );
        parsed.checks.push_back( std::move( check ) );
    }

    // Counts are derived state: a persisted report whose counts disagree
    // with its checks is rejected before the digest check so tampering is
    // reported precisely.
    if ( counts["pass"].asUInt64() != parsed.passCount() ||
         counts["fail"].asUInt64() != parsed.failCount() ||
         counts["indeterminate"].asUInt64() != parsed.indeterminateCount() )
        return fail( "'counts' disagree with the check list" );
    if ( aggregateStatus( statuses ) != parsed.overall )
        return fail( "'overall' disagrees with the check list" );

    if ( !expectedDigest.empty() && parsed.digest() != expectedDigest )
        return fail( "report digest mismatch: body does not hash to the expected digest" );

    out = std::move( parsed );
    error.clear();
    return true;
}

VerificationReport buildReport( const std::string &specId, const std::string &scope,
                                const std::string &specDigest,
                                std::vector<VerificationCheckResult> checks )
{
    VerificationReport report;
    report.specId = specId;
    report.scope = scope;
    report.specDigest = specDigest;
    report.checks = std::move( checks );
    std::vector<VerificationStatus> statuses;
    statuses.reserve( report.checks.size() );
    for ( const VerificationCheckResult &check : report.checks )
        statuses.push_back( check.status );
    report.overall = aggregateStatus( statuses );
    return report;
}

} // namespace sicnu::verify
