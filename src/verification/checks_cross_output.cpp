// checks_cross_output.cpp — do two outputs agree about the same thing?
//
// Two ways this check can lie, both closed below:
//
//   1. Comparing one observed value against nothing. `|a - b| <= tolerance`
//      with `b` unknown is not satisfiable, and returning Pass there would
//      launder "we only looked at one output" into "the two outputs agree".
//   2. Comparing a non-finite value. `fabs(NaN - x) > tolerance` is FALSE, so
//      a naive comparison reports two outputs as consistent precisely when the
//      statistic is meaningless. Non-finite is therefore its own answer.
//
// Every number that reaches the record goes through `roundSignificant`: the
// record is what gets hashed and diffed between runs, and a raw double is how
// two logically equal runs get different digests.

#include "verification/checks_cross_output.h"

#include "verification/availability.h"
#include "verification/canonical_json.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/status_lattice.h"

#include <json/json.h>

#include <cmath>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

CheckResult beginResult( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result;
    result.checkId = check.id;
    result.kind = check.kind;
    result.title = check.title;
    result.hints = check.hints.isObject() ? check.hints : Json::Value{ Json::objectValue };
    result.evidence.kind = check.kind;
    result.evidence.sourceId = inputs.sourceId;
    result.evidence.coverage = EvidenceCoverage::Unavailable;
    return result;
}

bool readStringArray( const Json::Value &params, const char *name,
                      std::vector<std::string> &out )
{
    if ( !params.isObject() || !params.isMember( name ) || !params[name].isArray() )
    {
        return false;
    }
    const Json::Value &array = params[name];
    if ( array.empty() )
    {
        return false;
    }
    for ( Json::Value::ArrayIndex i = 0; i < array.size(); ++i )
    {
        if ( !array[i].isString() || array[i].asString().empty() )
        {
            return false;
        }
        out.push_back( array[i].asString() );
    }
    return true;
}

bool readString( const Json::Value &params, const char *name, std::string &out )
{
    if ( !params.isObject() || !params.isMember( name ) || !params[name].isString() ||
         params[name].asString().empty() )
    {
        return false;
    }
    out = params[name].asString();
    return true;
}

bool readNumber( const Json::Value &params, const char *name, double &out )
{
    if ( !params.isObject() || !params.isMember( name ) || !params[name].isNumeric() )
    {
        return false;
    }
    out = params[name].asDouble();
    if ( !std::isfinite( out ) || out < 0.0 )
    {
        return false;
    }
    return true;
}

/// A half-observed comparison: coverage stays Unavailable and `observed` stays
/// empty so no reader can mistake one value for an agreement.
CheckResult unobserved( CheckResult result, const VerificationCheck &check, const char *code,
                        const std::string &why )
{
    result.status = CheckStatus::Indeterminate;
    result.failureCode = code;
    result.message =
        "cross-output consistency of '" + check.subject.id + "' is undetermined: " + why +
        ". Two outputs were supposed to corroborate each other and only one was obtained; "
        "a single value corroborates nothing, and reporting agreement here would be a "
        "claim about an output nobody looked at.";
    result.evidence.details["subject_id"] = check.subject.id;
    return result;
}

} // namespace

CheckResult runCrossOutputConsistencyCheck( const VerificationCheck &check,
                                            const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );

    std::vector<std::string> references;
    std::string statistic;
    double tolerance = 0.0;
    if ( !readStringArray( check.params, "artifacts", references ) || references.size() != 2 ||
         !readString( check.params, "statistic", statistic ) ||
         !readNumber( check.params, "tolerance", tolerance ) )
    {
        // An unpinned tolerance does not mean "exact": it means unspecified.
        result.status = CheckStatus::Indeterminate;
        result.failureCode = failure_codes::kSpecInvalid;
        result.message =
            "cross-output consistency of '" + check.subject.id +
            "' was never pinned: params must name exactly two artifacts in 'artifacts', the "
            "'statistic' to compare and a non-negative 'tolerance'. Without a declared "
            "tolerance there is no threshold to disagree beyond, so the question was never "
            "actually asked.";
        result.evidence.details["subject_id"] = check.subject.id;
        result.evidence.details["spec_problem"] =
            "params.artifacts/statistic/tolerance is missing or malformed";
        return result;
    }

    if ( inputs.artifact == nullptr )
    {
        return unobserved( result, check, failure_codes::kEvidenceUnavailable,
                           "no artifact provider is wired" );
    }

    static const char *const kSides[2] = { "left", "right" };
    Json::Value facts[2];
    for ( std::size_t side = 0; side < 2; ++side )
    {
        std::string reason;
        const Availability answer = inputs.artifact->describe( references[side], facts[side],
                                                              reason );
        if ( !unavailable( answer ) )
        {
            continue;
        }
        result.evidence.details["missing_side"] = kSides[side];
        result.evidence.details["artifact"] = references[side];
        if ( !reason.empty() )
        {
            result.evidence.details["provider_reason"] = reason;
        }
        return unobserved( result, check,
                           answer == Availability::Refused ? failure_codes::kEvidenceRefused
                                                           : failure_codes::kEvidenceUnavailable,
                           "'" + references[side] + "' could not be described" );
    }

    double values[2] = { 0.0, 0.0 };
    for ( std::size_t side = 0; side < 2; ++side )
    {
        if ( !facts[side].isMember( statistic ) || !facts[side][statistic].isNumeric() )
        {
            result.evidence.details["missing_side"] = kSides[side];
            result.evidence.details["artifact"] = references[side];
            result.evidence.details["statistic"] = statistic;
            return unobserved( result, check, failure_codes::kEvidenceUnavailable,
                               "'" + references[side] + "' carries no numeric '" + statistic +
                                   "'" );
        }
        values[side] = facts[side][statistic].asDouble();
        if ( !std::isfinite( values[side] ) )
        {
            result.evidence.details["non_finite_side"] = kSides[side];
            result.evidence.details["artifact"] = references[side];
            result.evidence.details["statistic"] = statistic;
            return unobserved( result, check, failure_codes::kNumericNotFinite,
                               "'" + references[side] + "' reports a non-finite '" +
                                   statistic + "', which is unordered" );
        }
    }

    const double delta = std::fabs( values[0] - values[1] );
    if ( !std::isfinite( delta ) )
    {
        return unobserved( result, check, failure_codes::kNumericNotFinite,
                           "the difference between the two values is not finite" );
    }

    result.evidence.coverage = EvidenceCoverage::Full;
    result.evidence.details["subject_id"] = check.subject.id;
    result.evidence.details["delta"] = roundSignificant( delta );
    result.evidence.details["tolerance"] = roundSignificant( tolerance );

    result.evidence.expected["statistic"] = statistic;
    result.evidence.expected["tolerance"] = roundSignificant( tolerance );

    result.evidence.observed["subject_id"] = check.subject.id;
    result.evidence.observed["statistic"] = statistic;
    result.evidence.observed["left"]["artifact"] = references[0];
    result.evidence.observed["left"]["value"] = roundSignificant( values[0] );
    result.evidence.observed["right"]["artifact"] = references[1];
    result.evidence.observed["right"]["value"] = roundSignificant( values[1] );

    if ( delta > tolerance )
    {
        result.status = CheckStatus::Fail;
        result.failureCode = failure_codes::kCrossOutputInconsistent;
        result.message =
            "cross-output inconsistency on '" + check.subject.id + "': " + statistic +
            " differs by " + roundSignificant( delta ) + " between '" + references[0] +
            "' and '" + references[1] + "', beyond the declared tolerance of " +
            roundSignificant( tolerance ) +
            ". Two routes to the same tile cannot both be right, and until we know which "
            "one is wrong neither may be trusted.";
        return result;
    }

    result.status = CheckStatus::Pass;
    result.message =
        "cross-output consistency on '" + check.subject.id + "': " + statistic +
        " agrees between '" + references[0] + "' and '" + references[1] + "' (difference " +
        roundSignificant( delta ) + " within tolerance " + roundSignificant( tolerance ) +
        "), so the two routes corroborate each other.";
    return result;
}

} // namespace sicnu::verification
