// checks_state.cpp — see checks_state.h for why undeclared differs from wrong.

#include "verification/checks_state.h"

#include "verification/availability.h"
#include "verification/checks_numeric.h"
#include "verification/failure_codes.h"

#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

const Json::Value *optionalMember( const Json::Value &json, const char *name )
{
    return json.isMember( name ) ? &json[name] : nullptr;
}

/// One declared expectation. Empty means "not declared for this check".
struct DeclaredState
{
    bool hasDomain = false;
    std::string numericDomain;
    bool hasRadiometry = false;
    std::string radiometricState;
};

bool readDeclaredState( const Json::Value &params, DeclaredState &out, std::string &problem,
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
        if ( key != "numeric_domain" && key != "radiometric_state" )
        {
            unexpectedKey = key;
            problem = "this family cannot evaluate expectation member '" + key + "'";
            return false;
        }
    }

    const Json::Value *domainMember = optionalMember( expect, "numeric_domain" );
    if ( domainMember != nullptr )
    {
        if ( !domainMember->isString() || domainMember->asString().empty() )
        {
            problem = "expectation member 'numeric_domain' must be a non-empty string";
            return false;
        }
        out.hasDomain = true;
        out.numericDomain = domainMember->asString();
    }

    const Json::Value *radiometryMember = optionalMember( expect, "radiometric_state" );
    if ( radiometryMember != nullptr )
    {
        if ( !radiometryMember->isString() || radiometryMember->asString().empty() )
        {
            problem = "expectation member 'radiometric_state' must be a non-empty string";
            return false;
        }
        out.hasRadiometry = true;
        out.radiometricState = radiometryMember->asString();
    }

    if ( !out.hasDomain && !out.hasRadiometry )
    {
        problem = "no expectation was declared; a state invariant needs a numeric_domain "
                  "or a radiometric_state";
        return false;
    }

    return true;
}

CheckResult unavailableResult( CheckResult result, Availability availability,
                              const std::string &reason, const VerificationCheck &check,
                              bool isRefusal )
{
    result.evidence.details["availability"] = availabilityToWire( availability );
    if ( !reason.empty() )
    {
        result.evidence.details["reason"] = reason;
    }
    if ( isRefusal )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceRefused,
                      titleScopedMessage( check,
                                          std::string( "the state source refused to describe " ) +
                                              check.subject.id + ": " + reason +
                                              "; a refusal is an operational problem, not "
                                              "evidence that the invariant is violated" ) );
        return result;
    }
    finishResult( result, CheckStatus::Indeterminate, failure_codes::kStateTokenMissing,
                  titleScopedMessage( check,
                                      std::string( "no state token could be obtained for " ) +
                                          check.subject.id +
                                          ", so nothing is known about the numeric domain or "
                                          "the radiometric state here; undeclared is not the "
                                          "same as wrong, and this stays unverified" ) );
    return result;
}

/// The consequence of feeding one domain's numbers to another domain's
/// operator. Saying "mismatch" here would leave the reader with no way to judge
/// whether the output is salvageable.
std::string domainConsequence( const std::string &expected, const std::string &observed )
{
    return "the input carries the '" + observed + "' numeric domain where '" + expected +
           "' was contracted: dB magnitudes are logarithmic rather than linear power, so any "
           "operator calibrated for '" + expected +
           "' mis-scales radiometry by roughly 4x with a slope-dependent bias; every ratio, "
           "index and radiometric comparison computed from this step inherits that bias and "
           "cannot be compared against calibrated products";
}

std::string radiometryConsequence( const std::string &expected, const std::string &observed )
{
    return "the input is declared '" + observed + "' where '" + expected +
           "' was contracted: these normalisations differ by the local incidence angle "
           "(gamma0 additionally carries terrain-flattening), so mixing them folds that "
           "cosine factor in twice and biases slope-facing and slope-away pixels in opposite "
           "directions — a terrain-related difference that is entirely an artefact of the "
           "state, not of the surface";
}

CheckResult undeclaredResult( CheckResult result, const VerificationCheck &check,
                              const char *missingToken )
{
    result.evidence.details["undeclared_token"] = missingToken;
    // The snapshot itself was obtained, so the record is not "unavailable" —
    // but nothing in it answers the question that was asked, so the coverage is
    // Sampled-with-nothing rather than Full. Reporting Full here would let a
    // reader conclude that the whole state was examined and found unremarkable.
    if ( result.evidence.coverage == EvidenceCoverage::Full )
    {
        result.evidence.coverage = EvidenceCoverage::Unavailable;
    }
    finishResult( result, CheckStatus::Indeterminate, failure_codes::kStateTokenMissing,
                  titleScopedMessage( check,
                                      std::string( "the source described " ) +
                                          check.subject.id + " but declared no '" +
                                          missingToken + "'; absent metadata is absence of "
                                                         "evidence, so this check cannot call "
                                                         "the invariant violated" ) );
    return result;
}

} // namespace

CheckResult runStateInvariantCheck( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );
    result.evidence.details["subject"] = check.subject.id;

    if ( check.subject.id.empty() )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, "no subject was named, so there is no state "
                                                 "token to obtain" ) );
        return result;
    }

    DeclaredState declared;
    std::string problem;
    std::string unexpectedKey;
    if ( !readDeclaredState( check.params, declared, problem, unexpectedKey ) )
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

    if ( declared.hasDomain )
    {
        result.evidence.expected["numeric_domain"] = declared.numericDomain;
    }
    if ( declared.hasRadiometry )
    {
        result.evidence.expected["radiometric_state"] = declared.radiometricState;
    }

    if ( inputs.state == nullptr )
    {
        return unavailableResult( std::move( result ), Availability::Missing,
                                  "no state provider is wired", check, false );
    }

    StateSnapshot snapshot;
    std::string reason;
    const Availability answer = inputs.state->tryGet( check.subject.id, snapshot, reason );
    if ( answer == Availability::Refused )
    {
        return unavailableResult( std::move( result ), answer, reason, check, true );
    }
    if ( answer != Availability::Found )
    {
        return unavailableResult( std::move( result ), answer, reason, check, false );
    }

    result.evidence.coverage = EvidenceCoverage::Full;
    result.evidence.observed = snapshot.toJson();

    // Both are empty: nothing was declared about this artifact at all. That is
    // the ordinary case for intermediate products, and it is not a violation.
    if ( !snapshot.hasNumericDomain() && !snapshot.hasRadiometricState() )
    {
        return undeclaredResult( std::move( result ), check, declared.hasDomain
                                                                 ? "numeric_domain"
                                                                 : "radiometric_state" );
    }

    if ( declared.hasDomain )
    {
        if ( !snapshot.hasNumericDomain() )
        {
            return undeclaredResult( std::move( result ), check, "numeric_domain" );
        }
        if ( snapshot.numericDomain != declared.numericDomain )
        {
            finishResult( result, CheckStatus::Fail, failure_codes::kStateInvariantViolation,
                          titleScopedMessage(
                              check, domainConsequence( declared.numericDomain,
                                                        snapshot.numericDomain ) ) );
            return result;
        }
    }

    if ( declared.hasRadiometry )
    {
        if ( !snapshot.hasRadiometricState() )
        {
            return undeclaredResult( std::move( result ), check, "radiometric_state" );
        }
        if ( snapshot.radiometricState != declared.radiometricState )
        {
            finishResult( result, CheckStatus::Fail, failure_codes::kStateInvariantViolation,
                          titleScopedMessage(
                              check, radiometryConsequence( declared.radiometricState,
                                                            snapshot.radiometricState ) ) );
            return result;
        }
    }

    finishResult( result, CheckStatus::Pass, {},
                  titleScopedMessage( check,
                                      std::string( "the declared state of " ) +
                                          check.subject.id +
                                          " satisfies every expectation this step contracted" ) );
    return result;
}

} // namespace sicnu::verification
