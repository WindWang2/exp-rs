// checks_numeric.cpp — see checks_numeric.h for why NaN never reaches a
// comparison and why equality is resolved rather than bit-exact.

#include "verification/checks_numeric.h"

#include "verification/availability.h"
#include "verification/canonical_json.h"
#include "verification/failure_codes.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::verification
{

// ---------------------------------------------------------------------------
// Primitives shared with the other check families
// ---------------------------------------------------------------------------

CheckResult beginResult( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result;
    result.checkId = check.id;
    result.kind = check.kind;
    result.title = check.title;
    // Status stays CheckResult's default (Indeterminate): if this shared
    // skeleton is ever extended, forgetting to set a status must fail closed.
    result.evidence.kind = check.kind;
    result.evidence.sourceId = inputs.sourceId;
    result.hints = check.hints.isObject() ? check.hints : Json::Value{ Json::objectValue };
    return result;
}

void finishResult( CheckResult &result, CheckStatus status, const std::string &failureCode,
                   const std::string &message )
{
    result.status = status;
    result.message = message;
    if ( status == CheckStatus::Pass )
    {
        // A passing check has nothing to repair and nothing to report.
        result.failureCode.clear();
        return;
    }
    result.failureCode = isKnownFailureCode( failureCode ) ? failureCode
                                                           : failure_codes::kSpecInvalid;
}

std::string titleScopedMessage( const VerificationCheck &check, const std::string &body )
{
    // A report line must read as a sentence about THIS check. Without the title
    // the same body text is indistinguishable across twenty identical-looking
    // rows and nobody can tell which step produced it.
    return check.title.empty() ? body : check.title + ": " + body;
}

bool numericFinite( double value )
{
    return std::isfinite( value );
}

double numericTolerance( double left, double right )
{
    // Relative to the larger magnitude, floored so comparisons near zero keep a
    // sane absolute resolution instead of collapsing to exact equality.
    constexpr double kRelative = 1e-9;
    constexpr double kFloor = 1e-9;
    const double scale = std::fmax( 1.0, std::fmax( std::fabs( left ), std::fabs( right ) ) );
    return std::fmax( kFloor, kRelative * scale );
}

bool numericSame( double left, double right, double tolerance )
{
    if ( !numericFinite( left ) || !numericFinite( right ) )
    {
        // Never answered by accident: NaN == NaN is false, and treating two
        // unknowns as one quantity is exactly the fail-open this file guards.
        return false;
    }
    if ( roundSignificant( left ) == roundSignificant( right ) )
    {
        return true;
    }
    const double allowed = tolerance >= 0.0 ? tolerance : numericTolerance( left, right );
    return std::fabs( left - right ) <= allowed;
}

Json::Value canonicalNumber( double value )
{
    // Text, not digits: see checks_numeric.h. Every family writes numbers this
    // way so one quantity has exactly one spelling in every report.
    return Json::Value{ roundSignificant( value ) };
}

// ---------------------------------------------------------------------------
// NumericRange
// ---------------------------------------------------------------------------

namespace
{

/// One contracted expectation. Members are optional individually, but at least
/// one of value / lower / upper must be declared.
struct Interval
{
    bool hasPin = false;
    double pin = 0.0;
    bool hasLower = false;
    bool hasUpper = false;
    double lower = 0.0;
    double upper = 0.0;
    bool inclusiveLower = true;
    bool inclusiveUpper = true;
    double tolerance = -1.0;  ///< negative means "let numericSame decide"

    Json::Value toJson() const
    {
        Json::Value json{ Json::objectValue };
        if ( hasPin )
        {
            json["value"] = canonicalNumber( pin );
        }
        if ( hasLower )
        {
            json["lower"] = canonicalNumber( lower );
            json["inclusive_lower"] = inclusiveLower;
        }
        if ( hasUpper )
        {
            json["upper"] = canonicalNumber( upper );
            json["inclusive_upper"] = inclusiveUpper;
        }
        if ( tolerance >= 0.0 )
        {
            json["tolerance"] = canonicalNumber( tolerance );
        }
        return json;
    }
};

const Json::Value *optionalMember( const Json::Value &json, const char *name )
{
    return json.isMember( name ) ? &json[name] : nullptr;
}

double readOrNull( const Json::Value &expect, const char *name, bool &declared, double &value,
                   std::string &problem )
{
    const Json::Value *found = optionalMember( expect, name );
    if ( found == nullptr )
    {
        declared = false;
        return 0.0;
    }
    declared = true;
    if ( !found->isNumeric() )
    {
        problem = std::string( "expectation member '" ) + name + "' must be a number";
        return 0.0;
    }
    value = found->asDouble();
    return value;
}

/// Reads `params.expect` into @p out. Members outside the closed set are
/// REFUSED rather than ignored: an expectation this family cannot evaluate
/// would otherwise be silently dropped and the check would conclude anyway.
bool readInterval( const Json::Value &params, Interval &out, std::string &problem,
                   std::string &unexpectedKey )
{
    Json::Value expect{ Json::objectValue };
    const Json::Value *member = optionalMember( params, "expect" );
    if ( member != nullptr )
    {
        if ( !member->isObject() )
        {
            problem = "member 'expect' must be an object";
            return false;
        }
        expect = *member;
    }

    for ( const std::string &key : expect.getMemberNames() )
    {
        const bool known = key == "value" || key == "lower" || key == "upper" ||
                           key == "inclusive_lower" || key == "inclusive_upper" ||
                           key == "tolerance";
        if ( !known )
        {
            unexpectedKey = key;
            problem = "this family cannot evaluate expectation member '" + key + "'";
            return false;
        }
    }

    readOrNull( expect, "value", out.hasPin, out.pin, problem );
    readOrNull( expect, "lower", out.hasLower, out.lower, problem );
    readOrNull( expect, "upper", out.hasUpper, out.upper, problem );
    if ( !problem.empty() )
    {
        return false;
    }

    const Json::Value *toleranceMember = optionalMember( expect, "tolerance" );
    if ( toleranceMember != nullptr )
    {
        if ( !toleranceMember->isNumeric() )
        {
            problem = "expectation member 'tolerance' must be a number";
            return false;
        }
        out.tolerance = toleranceMember->asDouble();
        if ( !numericFinite( out.tolerance ) || out.tolerance < 0.0 )
        {
            problem = "expectation member 'tolerance' must be a finite number >= 0";
            return false;
        }
    }
    else
    {
        out.tolerance = -1.0;
    }

    const auto readFlag = [ &expect, &problem ]( const char *name, bool &value )
    {
        const Json::Value *found = optionalMember( expect, name );
        if ( found == nullptr )
        {
            return true;
        }
        if ( !found->isBool() )
        {
            problem = std::string( "expectation member '" ) + name + "' must be a boolean";
            return false;
        }
        value = found->asBool();
        return true;
    };

    if ( !readFlag( "inclusive_lower", out.inclusiveLower ) ||
         !readFlag( "inclusive_upper", out.inclusiveUpper ) )
    {
        return false;
    }

    if ( !out.hasPin && !out.hasLower && !out.hasUpper )
    {
        problem = "no expectation was declared; a range check needs a pin, a lower bound "
                  "or an upper bound";
        return false;
    }

    // An expectation nothing can satisfy is not evidence about the
    // observation — it is a caller defect, and it must NOT come out as Fail.
    if ( out.hasPin && !numericFinite( out.pin ) )
    {
        problem = "the pinned expectation is not finite, so nothing can equal it";
        return false;
    }
    if ( out.hasLower && !numericFinite( out.lower ) )
    {
        problem = "the declared lower bound is not finite, so no observation can be judged";
        return false;
    }
    if ( out.hasUpper && !numericFinite( out.upper ) )
    {
        problem = "the declared upper bound is not finite, so no observation can be judged";
        return false;
    }
    if ( out.hasLower && out.hasUpper && out.lower > out.upper )
    {
        problem = "the declared lower bound exceeds the declared upper bound, so no "
                  "observation can satisfy this expectation";
        return false;
    }

    return true;
}

/// True when @p observed is admitted by the lower bound. Exclusive means
/// "beyond it past comparison resolution", not merely "not bit-identical".
bool lowerAdmits( double observed, const Interval &interval, double tolerance )
{
    if ( !interval.hasLower )
    {
        return true;
    }
    if ( interval.inclusiveLower )
    {
        return observed > interval.lower || numericSame( observed, interval.lower, tolerance );
    }
    return observed > interval.lower && !numericSame( observed, interval.lower, tolerance );
}

bool upperAdmits( double observed, const Interval &interval, double tolerance )
{
    if ( !interval.hasUpper )
    {
        return true;
    }
    if ( interval.inclusiveUpper )
    {
        return observed < interval.upper || numericSame( observed, interval.upper, tolerance );
    }
    return observed < interval.upper && !numericSame( observed, interval.upper, tolerance );
}

CheckResult unavailableResult( CheckResult result, Availability availability,
                               const std::string &reason, const VerificationCheck &check,
                               const char *subjectDescription )
{
    result.evidence.details["availability"] = availabilityToWire( availability );
    if ( !reason.empty() )
    {
        result.evidence.details["reason"] = reason;
    }
    if ( availability == Availability::Refused )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceRefused,
                      titleScopedMessage( check,
                                          std::string( "the source refused to supply " ) +
                                              subjectDescription + ": " + reason +
                                              "; that refusal is an operational problem, not "
                                              "scientific evidence in either direction" ) );
        return result;
    }
    finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceUnavailable,
                  titleScopedMessage( check,
                                      std::string( "nothing supplied " ) + subjectDescription +
                                          " through any wired source, so the observation "
                                          "neither confirms nor contradicts the expectation; "
                                          "this stays unverified rather than being redone" ) );
    return result;
}

} // namespace

CheckResult runNumericRangeCheck( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );
    result.evidence.details["metric"] = check.subject.id;

    if ( check.subject.id.empty() )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, "no metric was named, so there is nothing to "
                                                 "compare against this expectation" ) );
        return result;
    }

    Interval interval;
    std::string problem;
    std::string unexpectedKey;
    if ( !readInterval( check.params, interval, problem, unexpectedKey ) )
    {
        if ( !unexpectedKey.empty() )
        {
            result.evidence.details["unexpected_key"] = unexpectedKey;
        }
        else
        {
            result.evidence.details["spec_problem"] = problem;
        }
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, problem ) );
        return result;
    }

    result.evidence.expected = interval.toJson();
    if ( interval.tolerance >= 0.0 )
    {
        result.evidence.details["tolerance"] = canonicalNumber( interval.tolerance );
    }
    else
    {
        result.evidence.details["tolerance"] = "default numericTolerance (1e-9 relative, "
                                               "1e-9 absolute floor)";
    }

    if ( inputs.metric == nullptr )
    {
        return unavailableResult( std::move( result ), Availability::Missing,
                                  "no metric provider is wired", check, "this metric" );
    }

    MetricValue value;
    std::string reason;
    const Availability answer = inputs.metric->metric( check.subject.id, value, reason );
    if ( answer != Availability::Found )
    {
        return unavailableResult( std::move( result ), answer, reason, check, "this metric" );
    }

    const double observed = value.value;
    result.evidence.coverage = EvidenceCoverage::Full;
    // Written as canonical TEXT: this is the measurement the verdict rests on,
    // and it must have one spelling everywhere it appears.
    result.evidence.observed["value"] = canonicalNumber( observed );
    if ( !value.unit.empty() )
    {
        result.evidence.observed["unit"] = value.unit;
    }

    // FINITENESS FIRST. Every comparison below is meaningless for NaN: both
    // `!(x < lower)` and `!(x > upper)` are TRUE for NaN, which is exactly how
    // a range check written the obvious way reports "in range" for garbage.
    if ( !numericFinite( observed ) )
    {
        const bool isNan = std::isnan( observed );
        result.evidence.details["violation"] = isNan ? "not_a_number" : "not_finite";
        finishResult( result, CheckStatus::Fail, failure_codes::kNumericNotFinite,
                      titleScopedMessage( check,
                                          std::string( "the observed value is " ) +
                                              ( isNan ? "NaN" : "not finite" ) +
                                              ": NaN and infinity have no position on a "
                                              "measurement scale, so no statistic computed "
                                              "from this quantity is meaningful and every "
                                              "downstream comparison inherits the corruption" ) );
        return result;
    }

    const std::string observedText = roundSignificant( observed );

    if ( interval.hasPin )
    {
        const double delta = observed - interval.pin;
        result.evidence.details["delta"] = canonicalNumber( delta );
        if ( numericSame( observed, interval.pin, interval.tolerance ) )
        {
            finishResult( result, CheckStatus::Pass, {},
                          titleScopedMessage( check, std::string( "observed " ) + observedText +
                                                         " equals the contracted value "
                                                         "within tolerance" ) );
            return result;
        }
        result.evidence.details["violated_bound"] = "pin";
        result.evidence.details["bound"] = canonicalNumber( interval.pin );
        finishResult( result, CheckStatus::Fail, failure_codes::kNumericOutOfRange,
                      titleScopedMessage( check,
                                          std::string( "observed " ) + observedText +
                                              " differs from the contracted value " +
                                              roundSignificant( interval.pin ) + " by " +
                                              roundSignificant( delta ) +
                                              "; outside its admissible range this quantity "
                                              "cannot be compared against calibrated products" ) );
        return result;
    }

    if ( interval.hasLower && !lowerAdmits( observed, interval, interval.tolerance ) )
    {
        const double delta = observed - interval.lower;
        result.evidence.details["violated_bound"] = "lower";
        result.evidence.details["bound"] = canonicalNumber( interval.lower );
        result.evidence.details["delta"] = canonicalNumber( delta );
        finishResult( result, CheckStatus::Fail, failure_codes::kNumericOutOfRange,
                      titleScopedMessage( check,
                                          std::string( "observed " ) + observedText +
                                              ( interval.inclusiveLower
                                                    ? " falls below the contracted inclusive lower bound "
                                                    : " is rejected by the contracted exclusive lower bound " ) +
                                              roundSignificant( interval.lower ) + " (delta " +
                                              roundSignificant( delta ) +
                                              "); below this bound the quantity has no physical "
                                              "meaning for this step" ) );
        return result;
    }

    if ( interval.hasUpper && !upperAdmits( observed, interval, interval.tolerance ) )
    {
        const double delta = observed - interval.upper;
        result.evidence.details["violated_bound"] = "upper";
        result.evidence.details["bound"] = canonicalNumber( interval.upper );
        result.evidence.details["delta"] = canonicalNumber( delta );
        finishResult( result, CheckStatus::Fail, failure_codes::kNumericOutOfRange,
                      titleScopedMessage( check,
                                          std::string( "observed " ) + observedText +
                                              ( interval.inclusiveUpper
                                                    ? " exceeds the contracted inclusive upper bound "
                                                    : " is rejected by the contracted exclusive upper bound " ) +
                                              roundSignificant( interval.upper ) + " (delta " +
                                              roundSignificant( delta ) +
                                              "); beyond this bound the quantity no longer "
                                              "describes this physical variable, so the result "
                                              "must not be trusted" ) );
        return result;
    }

    finishResult( result, CheckStatus::Pass, {},
                  titleScopedMessage( check, std::string( "observed " ) + observedText +
                                                 " lies inside the contracted interval" ) );
    return result;
}

} // namespace sicnu::verification
