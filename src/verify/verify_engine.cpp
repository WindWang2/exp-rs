// src/verify/verify_engine.cpp — check dispatch and fail-closed evaluation
// semantics for the Unified Scientific Verifier.
#include "verify_engine.h"

#include "verify_error_codes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::verify
{

namespace
{

// ---------------------------------------------------------------------------
// Safe JSON readers. Params are caller-supplied content: every read is
// type-guarded so a hostile in-memory document can never escape as a
// Json::Exception from an evaluator.
// ---------------------------------------------------------------------------

std::optional<std::string> optString( const Json::Value &object, const char *key )
{
    const Json::Value &value = object[key];
    if ( !value.isString() )
        return std::nullopt;
    return value.asString();
}

std::optional<double> optNumber( const Json::Value &object, const char *key )
{
    const Json::Value &value = object[key];
    if ( value.isBool() || !( value.isDouble() || value.isIntegral() ) )
        return std::nullopt;
    return value.asDouble();
}

// Control characters and line breaks must not smuggle untrusted param text
// into single-line messages (teaching renderers and logs read them).
std::string safeText( const std::string &text )
{
    std::string clean;
    clean.reserve( text.size() );
    for ( const char raw : text )
    {
        const unsigned char c = static_cast<unsigned char>( raw );
        clean.push_back( c < 0x20 || c == 0x7f ? '?' : raw );
    }
    return clean;
}

/// Non-finite doubles would make an otherwise honest report unsealable
/// (the canonical writer refuses non-finite bodies). Evidence mirrors facts,
/// so an unjudgeable number is recorded AS unjudgeable: a typed marker, not
/// a sealed NaN and not a silent omission.
void sanitizeEvidenceNumbers( Json::Value &value )
{
    switch ( value.type() )
    {
    case Json::realValue:
        if ( !std::isfinite( value.asDouble() ) )
            value = "non-finite";
        break;
    case Json::arrayValue:
        for ( Json::ArrayIndex index = 0; index < value.size(); ++index )
            sanitizeEvidenceNumbers( value[index] );
        break;
    case Json::objectValue:
        for ( const std::string &member : value.getMemberNames() )
            sanitizeEvidenceNumbers( value[member] );
        break;
    default:
        break;
    }
}

VerificationEvidence makeEvidence( const std::string &source, Json::Value observed, Json::Value expected )
{
    sanitizeEvidenceNumbers( observed );
    sanitizeEvidenceNumbers( expected );
    VerificationEvidence evidence;
    evidence.source = source;
    evidence.observed = std::move( observed );
    evidence.expected = std::move( expected );
    return evidence;
}

VerificationCheckResult makeResult( const VerificationCheckSpec &check, VerificationStatus status,
                                    const std::string &code, std::string message,
                                    std::optional<VerificationEvidence> evidence = std::nullopt )
{
    VerificationCheckResult result;
    result.checkId = check.checkId;
    result.kind = check.kind;
    result.status = status;
    result.code = status == VerificationStatus::Pass ? std::string{} : std::string( code );
    result.message = std::move( message );
    if ( evidence )
        result.evidence = std::move( *evidence );
    return result;
}

bool isNumeric( const Json::Value &value )
{
    return !value.isBool() && ( value.isDouble() || value.isIntegral() );
}

/// A numeric JSON fact that can honestly enter a comparison.
bool isFiniteNumber( const Json::Value &value )
{
    return isNumeric( value ) && std::isfinite( value.asDouble() );
}

/// Numeric equality BY VALUE: int 0 and real 0.0 are the same fact (the
/// #1191 review ruling). Callers must have excluded non-finite values first.
bool jsonValuesEqual( const Json::Value &observed, const Json::Value &expected )
{
    if ( isNumeric( observed ) && isNumeric( expected ) )
        return observed.asDouble() == expected.asDouble();
    if ( observed.type() != expected.type() )
        return false;
    switch ( observed.type() )
    {
    case Json::nullValue:
        return true;
    case Json::booleanValue:
        return observed.asBool() == expected.asBool();
    case Json::intValue:
    case Json::uintValue:
        // Same-width integers compare exactly; anything the platform widened
        // beyond int64 falls back to the double space (0.0 == -0.0 included).
        if ( observed.isInt64() && expected.isInt64() )
            return observed.asInt64() == expected.asInt64();
        return observed.asDouble() == expected.asDouble();
    case Json::realValue:
        return observed.asDouble() == expected.asDouble();
    case Json::stringValue:
        return observed.asString() == expected.asString();
    case Json::arrayValue:
    {
        if ( observed.size() != expected.size() )
            return false;
        for ( Json::ArrayIndex index = 0; index < observed.size(); ++index )
            if ( !jsonValuesEqual( observed[index], expected[index] ) )
                return false;
        return true;
    }
    case Json::objectValue:
    {
        if ( observed.size() != expected.size() )
            return false;
        for ( const std::string &member : observed.getMemberNames() )
        {
            if ( !expected.isMember( member ) )
                return false;
            if ( !jsonValuesEqual( observed[member], expected[member] ) )
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

/// Fail-closed fold of per-expectation verdicts inside ONE check: any Fail
/// wins, else any Indeterminate, else Pass — the same lattice as the report.
void fold( VerificationStatus &worst, VerificationStatus candidate )
{
    worst = aggregateStatus( { worst, candidate } );
}

// ---------------------------------------------------------------------------
// Per-kind evaluators
// ---------------------------------------------------------------------------

VerificationCheckResult evalStateInvariant( const VerificationCheckSpec &check,
                                            const VerificationContext &context )
{
    if ( context.stateView == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "state provider not attached; state invariant cannot be judged" );

    const Json::Value &expectations = check.params["expectations"];
    if ( !expectations.isArray() )
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "'expectations' must be an array" );

    VerificationStatus worst = VerificationStatus::Pass;
    std::string firstBlocking;
    std::string resultCode;
    std::string failCode;
    std::string indCode;
    Json::Value observed( Json::objectValue );

    for ( const Json::Value &expectation : expectations )
    {
        const std::string key = expectation["key"].asString();
        const std::string op = expectation["op"].asString();
        const std::optional<Json::Value> recorded = context.stateView->state( key );
        const Json::Value &value = expectation["value"];

        observed[key] = Json::Value( Json::objectValue );
        Json::Value entry( Json::objectValue );
        entry["recorded"] = recorded.has_value();
        if ( recorded )
            entry["value"] = *recorded;
        observed[key] = entry;

        VerificationStatus status = VerificationStatus::Pass;
        std::string blocking;
        const char *pendingCode = kCodeStateViolated;
        if ( op == "present" )
        {
            if ( !recorded )
            {
                status = VerificationStatus::Fail;
                blocking = "state key '" + safeText( key ) + "' is not recorded";
            }
        }
        else if ( op == "absent" )
        {
            if ( recorded )
            {
                status = VerificationStatus::Fail;
                blocking = "state key '" + safeText( key ) + "' must be absent";
            }
        }
        else if ( !recorded )
        {
            status = VerificationStatus::Fail;
            blocking = "state key '" + safeText( key ) + "' is not recorded";
        }
        else if ( recorded->isDouble() && !std::isfinite( recorded->asDouble() ) )
        {
            // A non-finite observed fact cannot be judged (never a pass, and
            // never a detected violation — the value simply says nothing).
            status = VerificationStatus::Indeterminate;
            blocking = "state value for '" + safeText( key ) + "' is not finite";
            pendingCode = kCodeMetricNotFinite;
        }
        else if ( op == "eq" || op == "ne" )
        {
            if ( isNumeric( value ) && !std::isfinite( value.asDouble() ) )
            {
                status = VerificationStatus::Indeterminate;
                blocking = "pinned expectation value for '" + safeText( key ) + "' is not finite";
                pendingCode = kCodeMetricNotFinite;
            }
            else
            {
                const bool equal = jsonValuesEqual( *recorded, value );
                const bool holds = op == "eq" ? equal : !equal;
                if ( !holds )
                {
                    status = VerificationStatus::Fail;
                    blocking = "state key '" + safeText( key ) + "' violates '" + op + "'";
                }
            }
        }
        else // gt / ge / lt / le
        {
            if ( !isNumeric( *recorded ) )
            {
                status = VerificationStatus::Fail;
                blocking = "state value for '" + safeText( key ) + "' is not numeric";
                pendingCode = kCodeTypeMismatch;
            }
            else if ( !isFiniteNumber( value ) )
            {
                status = VerificationStatus::Fail;
                blocking = "op '" + op + "' requires a finite numeric pinned value";
                pendingCode = kCodeInvalidSpec;
            }
            else
            {
                const double lhs = recorded->asDouble();
                const double rhs = value.asDouble();
                bool holds = false;
                if ( op == "gt" )
                    holds = lhs > rhs;
                else if ( op == "ge" )
                    holds = lhs >= rhs;
                else if ( op == "lt" )
                    holds = lhs < rhs;
                else
                    holds = lhs <= rhs;
                if ( !holds )
                {
                    status = VerificationStatus::Fail;
                    blocking = "state key '" + safeText( key ) + "' violates '" + op + "'";
                }
            }
        }

        fold( worst, status );
        if ( status != VerificationStatus::Pass && firstBlocking.empty() )
            firstBlocking = blocking;
        // The wire contract ties the code CLASS to the folded status: a Fail
        // verdict must carry the first FAIL's e_ code, never an earlier
        // Indeterminate's i_ code (and vice versa).
        if ( status == VerificationStatus::Fail && failCode.empty() )
            failCode = pendingCode;
        if ( status == VerificationStatus::Indeterminate && indCode.empty() )
            indCode = pendingCode;
    }

    resultCode = worst == VerificationStatus::Fail
                     ? ( failCode.empty() ? std::string( kCodeStateViolated ) : failCode )
                     : ( indCode.empty() ? std::string( kCodeMetricNotFinite ) : indCode );
    if ( worst == VerificationStatus::Pass )
        return makeResult( check, VerificationStatus::Pass, "",
                           "all state invariants hold",
                           makeEvidence( "state", std::move( observed ), Json::Value( Json::objectValue ) ) );
    return makeResult( check, worst, resultCode, firstBlocking,
                       makeEvidence( "state", std::move( observed ), check.params["expectations"] ) );
}

/// Existence + readability resolution shared by the artifact-family checks.
/// nullopt from the probe is a READABILITY gap (Indeterminate); a definitive
/// "does not exist" is a detectable violation (Fail).
enum class ArtifactResolution
{
    Ok,
    Unreadable,
    Missing,
};

ArtifactResolution resolveArtifact( const VerificationCheckSpec &check, const VerificationContext &context,
                                    const std::string &path, ArtifactInfo &info,
                                    VerificationCheckResult &result )
{
    if ( context.artifactProbe == nullptr )
    {
        result = makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                             "artifact provider not attached; '" + safeText( path ) + "' cannot be probed" );
        return ArtifactResolution::Unreadable;
    }
    const std::optional<ArtifactInfo> probed = context.artifactProbe->probe( path );
    if ( !probed )
    {
        result = makeResult( check, VerificationStatus::Indeterminate, kCodeArtifactUnreadable,
                             "artifact probe cannot answer for '" + safeText( path ) + "'" );
        return ArtifactResolution::Unreadable;
    }
    info = *probed;
    if ( !info.exists )
    {
        result = makeResult( check, VerificationStatus::Fail, kCodeArtifactMissing,
                             "artifact '" + safeText( path ) + "' does not exist" );
        return ArtifactResolution::Missing;
    }
    return ArtifactResolution::Ok;
}

VerificationCheckResult evalArtifactExists( const VerificationCheckSpec &check,
                                            const VerificationContext &context )
{
    const std::string path = check.params["path"].asString();
    ArtifactInfo info;
    VerificationCheckResult result;
    const ArtifactResolution resolution = resolveArtifact( check, context, path, info, result );
    if ( resolution != ArtifactResolution::Ok )
        return result;

    const std::optional<std::int64_t> minBytes = [&] {
        const Json::Value &value = check.params["minBytes"];
        if ( !value.isIntegral() || value.isBool() || !value.isInt64() || value.asInt64() < 0 )
            return std::optional<std::int64_t>{};
        return std::optional<std::int64_t>{ value.asInt64() };
    }();
    if ( minBytes && static_cast<std::int64_t>( info.sizeBytes ) < *minBytes )
        return makeResult( check, VerificationStatus::Fail, kCodeArtifactTooSmall,
                           "artifact '" + safeText( path ) + "' is " + std::to_string( info.sizeBytes ) +
                               " bytes, below the pinned minimum",
                           makeEvidence( "artifact:" + path,
                                         [ & ] {
                                             Json::Value observed( Json::objectValue );
                                             observed["exists"] = true;
                                             observed["sizeBytes"] = static_cast<Json::UInt64>( info.sizeBytes );
                                             return observed;
                                         }(),
                                         [ & ] {
                                             Json::Value expected( Json::objectValue );
                                             expected["minBytes"] = static_cast<Json::UInt64>( *minBytes );
                                             return expected;
                                         }() ) );

    Json::Value observed( Json::objectValue );
    observed["exists"] = true;
    observed["kind"] = info.kind;
    observed["sizeBytes"] = static_cast<Json::UInt64>( info.sizeBytes );
    return makeResult( check, VerificationStatus::Pass, "", "artifact exists",
                       makeEvidence( "artifact:" + path, std::move( observed ), Json::Value( Json::objectValue ) ) );
}

VerificationCheckResult evalArtifactType( const VerificationCheckSpec &check,
                                          const VerificationContext &context )
{
    const std::string path = check.params["path"].asString();
    const std::string expectedKind = check.params["kind"].asString();
    ArtifactInfo info;
    VerificationCheckResult result;
    const ArtifactResolution resolution = resolveArtifact( check, context, path, info, result );
    if ( resolution != ArtifactResolution::Ok )
        return result;

    Json::Value observed( Json::objectValue );
    observed["kind"] = info.kind;
    Json::Value expected( Json::objectValue );
    expected["kind"] = expectedKind;
    if ( info.kind != expectedKind )
        return makeResult( check, VerificationStatus::Fail, kCodeTypeMismatch,
                           "artifact '" + safeText( path ) + "' has kind '" + safeText( info.kind ) +
                               "', expected '" + safeText( expectedKind ) + "'",
                           makeEvidence( "artifact:" + path, std::move( observed ), std::move( expected ) ) );
    return makeResult( check, VerificationStatus::Pass, "", "artifact kind matches",
                       makeEvidence( "artifact:" + path, std::move( observed ), std::move( expected ) ) );
}

VerificationCheckResult evalArtifactGrid( const VerificationCheckSpec &check,
                                          const VerificationContext &context )
{
    const std::string path = check.params["path"].asString();
    if ( context.gridProbe == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "grid provider not attached; grid facts for '" + safeText( path ) +
                               "' cannot be probed" );
    const std::optional<GridInfo> grid = context.gridProbe->grid( path );
    if ( !grid )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeArtifactUnreadable,
                           "grid probe cannot answer for '" + safeText( path ) + "'" );

    Json::Value observed( Json::objectValue );
    observed["width"] = grid->width;
    observed["height"] = grid->height;
    observed["bandCount"] = grid->bandCount;
    observed["crs"] = grid->crs;
    observed["nodataFraction"] = grid->nodataFraction;
    observed["finiteFraction"] = grid->finiteFraction;
    Json::Value expected( Json::objectValue );
    for ( const char *field : { "width", "height", "bandCount", "crs", "maxNodataFraction", "minFiniteFraction" } )
        if ( check.params.isMember( field ) )
            expected[field] = check.params[field];

    const auto fail = [ & ]( const std::string &detail ) {
        return makeResult( check, VerificationStatus::Fail, kCodeGridMismatch,
                           "grid of '" + safeText( path ) + "': " + detail,
                           makeEvidence( "grid:" + path, observed, expected ) );
    };
    // A non-finite observed fraction says nothing about the contract: the
    // grid NaN that silently passed review round 1 is now an Indeterminate.
    if ( check.params.isMember( "maxNodataFraction" ) && !std::isfinite( grid->nodataFraction ) )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeMetricNotFinite,
                           "observed nodataFraction for '" + safeText( path ) + "' is not finite",
                           makeEvidence( "grid:" + path, observed, expected ) );
    if ( check.params.isMember( "minFiniteFraction" ) && !std::isfinite( grid->finiteFraction ) )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeMetricNotFinite,
                           "observed finiteFraction for '" + safeText( path ) + "' is not finite",
                           makeEvidence( "grid:" + path, observed, expected ) );

    if ( check.params.isMember( "width" ) && grid->width != check.params["width"].asInt() )
        return fail( "width " + std::to_string( grid->width ) );
    if ( check.params.isMember( "height" ) && grid->height != check.params["height"].asInt() )
        return fail( "height " + std::to_string( grid->height ) );
    if ( check.params.isMember( "bandCount" ) && grid->bandCount != check.params["bandCount"].asInt() )
        return fail( "bandCount " + std::to_string( grid->bandCount ) );
    if ( check.params.isMember( "crs" ) && grid->crs != check.params["crs"].asString() )
        return fail( "crs '" + safeText( grid->crs ) + "'" );
    if ( check.params.isMember( "maxNodataFraction" ) &&
         grid->nodataFraction > check.params["maxNodataFraction"].asDouble() )
        return fail( "nodataFraction above the pinned maximum" );
    if ( check.params.isMember( "minFiniteFraction" ) &&
         grid->finiteFraction < check.params["minFiniteFraction"].asDouble() )
        return fail( "finiteFraction below the pinned minimum" );

    return makeResult( check, VerificationStatus::Pass, "", "grid satisfies every pinned constraint",
                       makeEvidence( "grid:" + path, std::move( observed ), std::move( expected ) ) );
}

VerificationCheckResult evalArtifactSchema( const VerificationCheckSpec &check,
                                            const VerificationContext &context )
{
    const std::string path = check.params["path"].asString();
    ArtifactInfo info;
    VerificationCheckResult result;
    const ArtifactResolution resolution = resolveArtifact( check, context, path, info, result );
    if ( resolution != ArtifactResolution::Ok )
        return result;
    const std::optional<Json::Value> document = context.artifactProbe->readJson( path );
    // The file exists but is not readable JSON: a detectable schema violation,
    // not a capability gap (the probe answered existence).
    if ( !document )
        return makeResult( check, VerificationStatus::Fail, kCodeSchemaMismatch,
                           "artifact '" + safeText( path ) + "' is not readable as JSON" );

    Json::Value observed( Json::objectValue );
    observed["schemaMarker"] = document->isMember( "schema" ) && ( *document )["schema"].isString()
                                   ? ( *document )["schema"].asString()
                                   : std::string{};
    Json::Value missingKeys( Json::arrayValue );
    if ( check.params.isMember( "requiredKeys" ) )
    {
        for ( const Json::Value &key : check.params["requiredKeys"] )
            if ( !document->isMember( key.asString() ) )
                missingKeys.append( key.asString() );
    }
    observed["missingKeys"] = missingKeys;

    const auto fail = [ & ]( const std::string &detail ) {
        return makeResult( check, VerificationStatus::Fail, kCodeSchemaMismatch,
                           "schema of '" + safeText( path ) + "': " + detail,
                           makeEvidence( "json:" + path, observed, check.params ) );
    };
    if ( check.params.isMember( "schemaId" ) )
    {
        const std::string schemaId = check.params["schemaId"].asString();
        if ( observed["schemaMarker"].asString() != schemaId )
            return fail( "schema marker '" + safeText( observed["schemaMarker"].asString() ) +
                         "', expected '" + safeText( schemaId ) + "'" );
    }
    if ( missingKeys.size() > 0 )
        return fail( std::to_string( missingKeys.size() ) + " required key(s) missing" );

    return makeResult( check, VerificationStatus::Pass, "", "document satisfies the pinned schema",
                       makeEvidence( "json:" + path, std::move( observed ), Json::Value( Json::objectValue ) ) );
}

VerificationCheckResult evalMetricRange( const VerificationCheckSpec &check,
                                         const VerificationContext &context )
{
    if ( context.metricView == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "metric provider not attached; metric facts cannot be judged" );
    const std::string name = check.params["metric"].asString();
    const std::optional<double> value = context.metricView->metric( name );
    if ( !value )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeMetricMissing,
                           "metric '" + safeText( name ) + "' is not recorded" );
    if ( !std::isfinite( *value ) )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeMetricNotFinite,
                           "metric '" + safeText( name ) + "' is not finite" );

    const double tolerance = optNumber( check.params, "tolerance" ).value_or( 0.0 );
    Json::Value observed( Json::objectValue );
    observed["value"] = *value;
    Json::Value expected( Json::objectValue );
    if ( check.params.isMember( "min" ) )
        expected["min"] = check.params["min"];
    if ( check.params.isMember( "max" ) )
        expected["max"] = check.params["max"];
    if ( check.params.isMember( "tolerance" ) )
        expected["tolerance"] = check.params["tolerance"];

    const double low = check.params.isMember( "min" ) ? check.params["min"].asDouble() - tolerance : 0.0;
    const double high = check.params.isMember( "max" ) ? check.params["max"].asDouble() + tolerance : 0.0;
    const bool below = check.params.isMember( "min" ) && *value < low;
    const bool above = check.params.isMember( "max" ) && *value > high;
    if ( below || above )
        return makeResult( check, VerificationStatus::Fail, kCodeMetricOutOfRange,
                           "metric '" + safeText( name ) + "' is " +
                               ( below ? "below the pinned minimum" : "above the pinned maximum" ),
                           makeEvidence( "metric:" + name, observed, expected ) );
    return makeResult( check, VerificationStatus::Pass, "", "metric inside the pinned range",
                       makeEvidence( "metric:" + name, observed, expected ) );
}

/// A relation operand resolves to a finite double: a numeric literal, or a
/// metric name looked up through the metric view. Any unresolvable operand
/// fails the whole relation check fail-closed (missing -> Indeterminate).
bool resolveOperand( const VerificationCheckSpec &check, const VerificationContext &context,
                     const Json::Value &operand, double &out, VerificationStatus &status,
                     std::string &blocking, const char *&code )
{
    if ( operand.isString() )
    {
        const std::string name = operand.asString();
        const std::optional<double> value = context.metricView->metric( name );
        if ( !value )
        {
            status = VerificationStatus::Indeterminate;
            blocking = "metric '" + safeText( name ) + "' is not recorded";
            code = kCodeMetricMissing;
            return false;
        }
        if ( !std::isfinite( *value ) )
        {
            status = VerificationStatus::Indeterminate;
            blocking = "metric '" + safeText( name ) + "' is not finite";
            code = kCodeMetricNotFinite;
            return false;
        }
        out = *value;
        return true;
    }
    if ( !isFiniteNumber( operand ) )
    {
        status = VerificationStatus::Indeterminate;
        blocking = "relation operand is not a finite number";
        code = kCodeMetricNotFinite;
        return false;
    }
    out = operand.asDouble();
    return true;
}

VerificationCheckResult evalRelational( const VerificationCheckSpec &check,
                                        const VerificationContext &context )
{
    if ( context.metricView == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "metric provider not attached; relations cannot be judged" );

    const Json::Value &relations = check.params["relations"];
    if ( !relations.isArray() )
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "'relations' must be an array" );

    VerificationStatus worst = VerificationStatus::Pass;
    std::string firstBlocking;
    std::string resultCode;
    std::string failCode;
    std::string indCode;
    Json::Value observed( Json::arrayValue );

    std::size_t index = 0;
    for ( const Json::Value &relation : relations )
    {
        const std::string op = relation["op"].asString();
        const double tolerance = optNumber( relation, "tolerance" ).value_or( 0.0 );

        VerificationStatus status = VerificationStatus::Pass;
        std::string blocking;
        const char *pendingCode = kCodeRelationViolated;
        double lhs = 0.0;
        Json::Value entry( Json::objectValue );
        entry["op"] = op;

        bool resolved = resolveOperand( check, context, relation["left"], lhs, status, blocking, pendingCode );
        double rhs = 0.0;
        double sum = 0.0;
        if ( resolved && op == "sum_is" )
        {
            // Left-to-right over the DECLARED operand order: the sum is part
            // of the deterministic contract, not an accumulation accident.
            for ( const Json::Value &operand : relation["right"] )
            {
                double term = 0.0;
                if ( !resolveOperand( check, context, operand, term, status, blocking, pendingCode ) )
                {
                    resolved = false;
                    break;
                }
                sum += term;
            }
            rhs = sum;
        }
        else if ( resolved )
        {
            resolved = resolveOperand( check, context, relation["right"], rhs, status, blocking, pendingCode );
        }

        if ( resolved )
        {
            entry["left"] = lhs;
            entry["right"] = rhs;
            bool holds = false;
            if ( op == "eq" || op == "approx" )
                holds = std::fabs( lhs - rhs ) <= tolerance;
            else if ( op == "ne" )
                holds = std::fabs( lhs - rhs ) > tolerance;
            else if ( op == "lt" )
                holds = lhs < rhs;
            else if ( op == "le" )
                holds = lhs <= rhs;
            else if ( op == "gt" )
                holds = lhs > rhs;
            else if ( op == "ge" )
                holds = lhs >= rhs;
            else if ( op == "sum_is" )
                holds = std::fabs( lhs - rhs ) <= tolerance;
            if ( !holds )
            {
                status = VerificationStatus::Fail;
                blocking = "relation " + std::to_string( index ) + " ('" + op + "') violated";
            }
        }
        entry["holds"] = status == VerificationStatus::Pass;
        observed.append( entry );

        fold( worst, status );
        if ( status != VerificationStatus::Pass && firstBlocking.empty() )
            firstBlocking = blocking;
        if ( status == VerificationStatus::Fail && failCode.empty() )
            failCode = pendingCode;
        if ( status == VerificationStatus::Indeterminate && indCode.empty() )
            indCode = pendingCode;
        ++index;
    }

    resultCode = worst == VerificationStatus::Fail
                     ? ( failCode.empty() ? std::string( kCodeRelationViolated ) : failCode )
                     : ( indCode.empty() ? std::string( kCodeMetricNotFinite ) : indCode );
    if ( worst == VerificationStatus::Pass )
        return makeResult( check, VerificationStatus::Pass, "", "all relations hold",
                           makeEvidence( "metrics", std::move( observed ), check.params["relations"] ) );
    return makeResult( check, worst, resultCode, firstBlocking,
                       makeEvidence( "metrics", std::move( observed ), check.params["relations"] ) );
}

VerificationCheckResult evalProvenance( const VerificationCheckSpec &check, const VerificationContext &context )
{
    if ( context.provenanceView == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "provenance provider not attached; provenance cannot be judged" );

    VerificationStatus worst = VerificationStatus::Pass;
    std::string firstBlocking;
    Json::Value observed( Json::objectValue );
    Json::Value missingFields( Json::arrayValue );
    Json::Value missingDimensions( Json::arrayValue );
    const auto judgeDocument = [ & ]( const char *what, const std::string &reference,
                                      const std::optional<Json::Value> &document ) {
        if ( !document )
        {
            // No provenance where the contract requires one is a DETECTABLE
            // violation (aligned with the harness requireProvenance ruling),
            // not a capability gap.
            fold( worst, VerificationStatus::Fail );
            if ( firstBlocking.empty() )
                firstBlocking = std::string( "no provenance record for " ) + what + " '" +
                                safeText( reference ) + "'";
            return;
        }
        if ( !document->isObject() )
        {
            fold( worst, VerificationStatus::Indeterminate );
            if ( firstBlocking.empty() )
                firstBlocking = std::string( "provenance document for " ) + what + " '" +
                                safeText( reference ) + "' is unreadable";
            return;
        }
        if ( check.params.isMember( "requiredFields" ) )
        {
            const Json::Value &required = check.params["requiredFields"];
            if ( !required.isArray() )
            {
                fold( worst, VerificationStatus::Fail );
                if ( firstBlocking.empty() )
                    firstBlocking = "'requiredFields' must be an array";
            }
            else
            {
                for ( const Json::Value &field : required )
                {
                    if ( !document->isMember( field.asString() ) )
                    {
                        missingFields.append( field.asString() );
                        fold( worst, VerificationStatus::Fail );
                        if ( firstBlocking.empty() )
                            firstBlocking = "provenance for " + std::string( what ) + " '" +
                                            safeText( reference ) + "' lacks field '" +
                                            safeText( field.asString() ) + "'";
                    }
                }
            }
        }
        if ( check.params.isMember( "requiredDimensions" ) )
        {
            const Json::Value &dimensions = ( *document )["dimensions"];
            const Json::Value &required = check.params["requiredDimensions"];
            if ( !required.isArray() )
            {
                fold( worst, VerificationStatus::Fail );
                if ( firstBlocking.empty() )
                    firstBlocking = "'requiredDimensions' must be an array";
            }
            else
            {
                for ( const Json::Value &dimension : required )
                {
                    const bool present = dimensions.isObject() && dimensions.isMember( dimension.asString() );
                    if ( !present )
                    {
                        missingDimensions.append( dimension.asString() );
                        fold( worst, VerificationStatus::Fail );
                        if ( firstBlocking.empty() )
                            firstBlocking = "provenance for " + std::string( what ) + " '" +
                                            safeText( reference ) + "' lacks dimension '" +
                                            safeText( dimension.asString() ) + "'";
                    }
                }
            }
        }
    };

    const std::optional<std::string> path = optString( check.params, "path" );
    const std::optional<std::string> runId = optString( check.params, "runId" );
    if ( !path && !runId )
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "provenance check pins neither 'path' nor 'runId'" );
    if ( path )
        judgeDocument( "path", *path, context.provenanceView->provenanceForPath( *path ) );
    if ( runId )
        judgeDocument( "run", *runId, context.provenanceView->provenanceForRun( *runId ) );

    observed["missingFields"] = missingFields;
    observed["missingDimensions"] = missingDimensions;
    if ( worst == VerificationStatus::Pass )
        return makeResult( check, VerificationStatus::Pass, "", "provenance satisfies the pinned requirements",
                           makeEvidence( "provenance", std::move( observed ), check.params ) );
    return makeResult( check, worst,
                       worst == VerificationStatus::Fail ? kCodeProvenanceIncomplete : kCodeProvenanceMissing,
                       firstBlocking, makeEvidence( "provenance", std::move( observed ), check.params ) );
}

VerificationCheckResult evalReproducibility( const VerificationCheckSpec &check,
                                             const VerificationContext &context )
{
    if ( context.artifactProbe == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "artifact provider not attached; digests cannot be probed" );

    VerificationStatus worst = VerificationStatus::Pass;
    std::string firstBlocking;
    std::string resultCode;
    std::string failCode;
    std::string indCode;
    Json::Value observed( Json::objectValue );

    // Fold one probe outcome into the check; the code CLASS follows the
    // folded status (Fail -> first fail's e_ code, Indeterminate -> first
    // indeterminate's i_ code), never a masked mixture.
    const auto judge = [ & ]( VerificationStatus status, const char *code, std::string blocking ) {
        fold( worst, status );
        if ( status != VerificationStatus::Pass && firstBlocking.empty() )
            firstBlocking = std::move( blocking );
        if ( status == VerificationStatus::Fail && failCode.empty() )
            failCode = code;
        if ( status == VerificationStatus::Indeterminate && indCode.empty() )
            indCode = code;
        return status == VerificationStatus::Pass;
    };

    const auto look = [ & ]( const std::string &path, std::string &digest ) {
        const std::optional<ArtifactInfo> info = context.artifactProbe->probe( path );
        if ( !info )
            return judge( VerificationStatus::Indeterminate, kCodeArtifactUnreadable,
                          "artifact probe cannot answer for '" + safeText( path ) + "'" );
        if ( !info->exists )
            return judge( VerificationStatus::Fail, kCodeArtifactMissing,
                          "artifact '" + safeText( path ) + "' does not exist" );
        if ( info->digest.empty() )
            return judge( VerificationStatus::Indeterminate, kCodeDigestUnavailable,
                          "digest unavailable for '" + safeText( path ) + "'" );
        digest = info->digest;
        return judge( VerificationStatus::Pass, "", "" );
    };

    const std::optional<std::string> path = optString( check.params, "path" );
    if ( path )
    {
        std::string digest;
        const bool ok = look( *path, digest );
        const std::string expectedDigest = check.params.isMember( "expectedDigest" )
                                               ? check.params["expectedDigest"].asString()
                                               : std::string{};
        if ( ok )
        {
            observed["actualDigest"] = digest;
            if ( digest != expectedDigest )
                judge( VerificationStatus::Fail, kCodeDigestMismatch,
                       "digest of '" + safeText( *path ) + "' does not match the pinned expectation" );
        }
    }

    const std::optional<std::string> leftPath = optString( check.params, "leftPath" );
    const std::optional<std::string> rightPath = optString( check.params, "rightPath" );
    if ( leftPath && rightPath )
    {
        std::string left;
        std::string right;
        const bool leftOk = look( *leftPath, left );
        const bool rightOk = look( *rightPath, right );
        if ( leftOk && rightOk )
        {
            observed["leftDigest"] = left;
            observed["rightDigest"] = right;
            if ( left != right )
                judge( VerificationStatus::Fail, kCodeDigestMismatch,
                       "digests of '" + safeText( *leftPath ) + "' and '" + safeText( *rightPath ) +
                           "' differ" );
        }
    }

    resultCode = worst == VerificationStatus::Fail
                     ? ( failCode.empty() ? std::string( kCodeDigestMismatch ) : failCode )
                     : ( indCode.empty() ? std::string( kCodeDigestUnavailable ) : indCode );
    if ( worst == VerificationStatus::Pass )
        return makeResult( check, VerificationStatus::Pass, "", "digests reproduce as pinned",
                           makeEvidence( "digest", std::move( observed ), check.params ) );
    return makeResult( check, worst, resultCode, firstBlocking,
                       makeEvidence( "digest", std::move( observed ), check.params ) );
}

VerificationCheckResult evalCrossOutput( const VerificationCheckSpec &check, const VerificationContext &context )
{
    if ( context.artifactProbe == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "artifact provider not attached; outputs cannot be probed" );

    const Json::Value &outputs = check.params["outputs"];
    if ( !outputs.isArray() )
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "'outputs' must be an array" );
    const bool sameGrid = check.params.isMember( "sameGrid" ) && check.params["sameGrid"].asBool();
    const bool sameCrs = check.params.isMember( "sameCrs" ) && check.params["sameCrs"].asBool();
    const bool needGrids = sameGrid || sameCrs || check.params.isMember( "bandCount" );
    if ( needGrids && context.gridProbe == nullptr )
        return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                           "grid provider not attached; cross-output grid facts cannot be judged" );

    VerificationStatus worst = VerificationStatus::Pass;
    std::string firstBlocking;
    std::string resultCode;
    std::string failCode;
    std::string indCode;
    Json::Value observed( Json::arrayValue );
    std::vector<GridInfo> grids;

    const auto foldBlocking = [ & ]( VerificationStatus status, const char *code, std::string detail ) {
        fold( worst, status );
        if ( status != VerificationStatus::Pass && firstBlocking.empty() )
            firstBlocking = std::move( detail );
        if ( status == VerificationStatus::Fail && failCode.empty() )
            failCode = code;
        if ( status == VerificationStatus::Indeterminate && indCode.empty() )
            indCode = code;
    };

    for ( const Json::Value &output : outputs )
    {
        const std::string path = output["path"].asString();
        Json::Value entry( Json::objectValue );
        entry["path"] = path;

        const std::optional<ArtifactInfo> info = context.artifactProbe->probe( path );
        if ( !info )
        {
            foldBlocking( VerificationStatus::Indeterminate, kCodeArtifactUnreadable,
                          "artifact probe cannot answer for '" + safeText( path ) + "'" );
            entry["probed"] = false;
            observed.append( entry );
            continue;
        }
        entry["probed"] = true;
        entry["exists"] = info->exists;
        if ( !info->exists )
        {
            foldBlocking( VerificationStatus::Fail, kCodeArtifactMissing,
                          "output '" + safeText( path ) + "' does not exist" );
            observed.append( entry );
            continue;
        }

        if ( needGrids )
        {
            const std::optional<GridInfo> grid = context.gridProbe->grid( path );
            if ( !grid )
            {
                foldBlocking( VerificationStatus::Indeterminate, kCodeArtifactUnreadable,
                              "grid probe cannot answer for '" + safeText( path ) + "'" );
            }
            else
            {
                grids.push_back( *grid );
                entry["width"] = grid->width;
                entry["height"] = grid->height;
                entry["crs"] = grid->crs;
                entry["bandCount"] = grid->bandCount;
            }
        }
        observed.append( entry );
    }

    const auto failInconsistent = [ & ]( const std::string &detail ) {
        foldBlocking( VerificationStatus::Fail, kCodeCrossOutputInconsistent, detail );
    };

    if ( sameGrid && grids.size() == outputs.size() )
    {
        for ( std::size_t index = 1; index < grids.size(); ++index )
            if ( grids[index].width != grids[0].width || grids[index].height != grids[0].height )
                failInconsistent( "outputs disagree on grid dimensions" );
    }
    if ( sameCrs && grids.size() == outputs.size() )
    {
        for ( std::size_t index = 0; index < grids.size(); ++index )
            if ( grids[index].crs.empty() )
                failInconsistent( "output " + std::to_string( index ) + " carries no CRS" );
        for ( std::size_t index = 1; index < grids.size(); ++index )
            if ( grids[index].crs != grids[0].crs )
                failInconsistent( "outputs disagree on CRS" );
    }
    if ( check.params.isMember( "bandCount" ) )
    {
        const int expected = check.params["bandCount"].asInt();
        for ( std::size_t index = 0; index < grids.size(); ++index )
            if ( grids[index].bandCount != expected )
                failInconsistent( "output " + std::to_string( index ) + " has bandCount " +
                                  std::to_string( grids[index].bandCount ) );
    }

    resultCode = worst == VerificationStatus::Fail
                     ? ( failCode.empty() ? std::string( kCodeCrossOutputInconsistent ) : failCode )
                     : ( indCode.empty() ? std::string( kCodeArtifactUnreadable ) : indCode );
    if ( worst == VerificationStatus::Pass )
        return makeResult( check, VerificationStatus::Pass, "", "outputs are mutually consistent",
                           makeEvidence( "outputs", std::move( observed ), check.params ) );
    return makeResult( check, worst, resultCode, firstBlocking,
                       makeEvidence( "outputs", std::move( observed ), check.params ) );
}

using KindEvaluator = VerificationCheckResult ( * )( const VerificationCheckSpec &,
                                                    const VerificationContext & );

const std::map<std::string, KindEvaluator> &dispatchTable()
{
    static const std::map<std::string, KindEvaluator> table = {
        { "state.invariant", evalStateInvariant },
        { "artifact.exists", evalArtifactExists },
        { "artifact.type", evalArtifactType },
        { "artifact.grid", evalArtifactGrid },
        { "artifact.schema", evalArtifactSchema },
        { "metric.range", evalMetricRange },
        { "relational.consistency", evalRelational },
        { "provenance.complete", evalProvenance },
        { "reproducibility.digest", evalReproducibility },
        { "cross.output.consistency", evalCrossOutput },
    };
    return table;
}

} // namespace

bool hasEvaluator( const std::string &kind )
{
    return dispatchTable().count( kind ) > 0;
}

VerificationCheckResult evaluateCheck( const VerificationCheckSpec &check, const VerificationContext &context )
{
    try
    {
        const auto &table = dispatchTable();
        const auto found = table.find( check.kind );
        if ( found == table.end() )
        {
            // The interlock backstop: a kind the vocabulary accepted but the
            // engine does not implement is a capability gap, never a pass.
            return makeResult( check, VerificationStatus::Indeterminate, kCodeProviderMissing,
                               "no evaluator registered for kind '" + safeText( check.kind ) + "'" );
        }
        // Backstop for checks that reach a single-check evaluation without a
        // full-spec validation: an evaluator whose params leave it with
        // nothing to judge (or a truncated/mistyped pin) would otherwise
        // answer an honest-looking Pass. Reuse validateSpec's per-kind rules
        // — one authority, no second validation truth.
        const std::vector<std::string> paramErrors = validateCheckParams( check );
        if ( !paramErrors.empty() )
        {
            return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                               safeText( paramErrors.front() ) );
        }
        return found->second( check, context );
    }
    catch ( const Json::Exception &exception )
    {
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "check params unreadable: " + safeText( exception.what() ) );
    }
    catch ( const std::exception &exception )
    {
        return makeResult( check, VerificationStatus::Fail, kCodeInvalidSpec,
                           "check evaluation failed: " + safeText( exception.what() ) );
    }
}

VerificationReport evaluate( const VerificationSpec &spec, const VerificationContext &context )
{
    const std::vector<std::string> errors = validateSpec( spec );
    const std::string digest = specDigest( spec );

    VerificationCheckResult gate;
    gate.checkId = "spec.valid";
    gate.kind = "spec.valid";
    if ( !errors.empty() )
    {
        gate.status = VerificationStatus::Fail;
        gate.code = kCodeInvalidSpec;
        gate.message = errors.front() + ( errors.size() > 1 ? " (+" + std::to_string( errors.size() - 1 ) +
                                                                  " more validation error(s))"
                                                            : std::string{} );
        return buildReport( spec.specId, spec.scope, digest, { std::move( gate ) } );
    }
    if ( digest.empty() )
    {
        // The spec cannot be sealed (non-finite number smuggled into params):
        // an evaluation whose digest is unrepresentable must not run.
        gate.status = VerificationStatus::Fail;
        gate.code = kCodeInvalidSpec;
        gate.message = "spec cannot be sealed: a non-finite number is present in the params";
        return buildReport( spec.specId, spec.scope, digest, { std::move( gate ) } );
    }

    std::vector<VerificationCheckResult> results;
    results.reserve( spec.checks.size() );
    for ( const VerificationCheckSpec &check : spec.checks )
        results.push_back( evaluateCheck( check, context ) );
    return buildReport( spec.specId, spec.scope, digest, std::move( results ) );
}

} // namespace sicnu::verify
