// checks_relational.cpp — see checks_relational.h for why neither side may be
// assumed when a relation is evaluated.

#include "verification/checks_relational.h"

#include "verification/availability.h"
#include "verification/canonical_json.h"
#include "verification/checks_numeric.h"
#include "verification/failure_codes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sicnu::verification
{

std::vector<std::string> allRelations()
{
    return { "equals", "product_equals", "sum_equals" };
}

namespace
{

enum class RelationKind
{
    Equals,
    ProductEquals,
    SumEquals,
};

bool relationFromWire( const std::string &wire, RelationKind &out )
{
    if ( wire == "equals" )
    {
        out = RelationKind::Equals;
        return true;
    }
    if ( wire == "product_equals" )
    {
        out = RelationKind::ProductEquals;
        return true;
    }
    if ( wire == "sum_equals" )
    {
        out = RelationKind::SumEquals;
        return true;
    }
    return false;
}

struct Operand
{
    bool isArtifact = true;
    std::string reference;   ///< artifact ref or metric name
    std::string factPath;    ///< dotted path into the artifact description
};

/// One operand after it has been looked up. `availability` is why the side has
/// no value; nothing but a Found answer makes `value` meaningful.
struct ResolvedOperand
{
    Availability availability = Availability::Missing;
    bool resolved = false;
    double value = 0.0;
    std::string description;
    std::string reason;
};

std::string stringMember( const Json::Value &json, const char *name, const std::string &fallback )
{
    if ( json.isMember( name ) && json[name].isString() )
    {
        return json[name].asString();
    }
    return fallback;
}

/// Splits a dotted path and walks @p facts. Missing anywhere along the way is
/// "no such fact", which is NOT "the fact is zero".
bool walkFactPath( const Json::Value &facts, const std::string &path, double &out )
{
    Json::Value current = facts;
    std::size_t start = 0;
    while ( start <= path.size() )
    {
        const std::size_t dot = path.find( '.', start );
        const std::string segment =
            dot == std::string::npos ? path.substr( start ) : path.substr( start, dot - start );
        if ( !current.isObject() || !current.isMember( segment ) )
        {
            return false;
        }
        current = current[segment];
        if ( dot == std::string::npos )
        {
            break;
        }
        start = dot + 1;
    }
    if ( !current.isNumeric() )
    {
        return false;
    }
    out = current.asDouble();
    return true;
}

ResolvedOperand resolveOperand( const Operand &operand, const VerificationInputs &inputs,
                                const std::string &fallbackReference )
{
    ResolvedOperand resolved;
    resolved.description = ( operand.isArtifact ? "artifact '" : "metric '" ) +
                           ( operand.reference.empty() ? fallbackReference : operand.reference ) +
                           ( operand.factPath.empty() ? "'" : "' fact '" + operand.factPath + "'" );

    const std::string reference =
        operand.reference.empty() ? fallbackReference : operand.reference;

    if ( operand.isArtifact )
    {
        if ( inputs.artifact == nullptr )
        {
            resolved.reason = "no artifact provider is wired";
            return resolved;
        }
        Json::Value facts{ Json::objectValue };
        std::string reason;
        const Availability answer = inputs.artifact->describe( reference, facts, reason );
        resolved.availability = answer;
        resolved.reason = reason;
        if ( answer != Availability::Found )
        {
            return resolved;
        }
        double value = 0.0;
        if ( operand.factPath.empty() || !walkFactPath( facts, operand.factPath, value ) )
        {
            // Found the artifact, not the fact. Still nothing to compute with.
            resolved.availability = Availability::Missing;
            if ( resolved.reason.empty() )
            {
                resolved.reason = "the description carries no numeric '" + operand.factPath + "'";
            }
            return resolved;
        }
        resolved.resolved = true;
        resolved.value = value;
        resolved.availability = Availability::Found;
        return resolved;
    }

    if ( inputs.metric == nullptr )
    {
        resolved.reason = "no metric provider is wired";
        return resolved;
    }
    MetricValue value;
    std::string reason;
    const Availability answer = inputs.metric->metric( reference, value, reason );
    resolved.availability = answer;
    resolved.reason = reason;
    if ( answer != Availability::Found )
    {
        return resolved;
    }
    resolved.resolved = true;
    resolved.value = value.value;
    return resolved;
}

CheckResult unevaluatedResult( CheckResult result, const VerificationCheck &check,
                               const char *side, const ResolvedOperand &operand )
{
    result.evidence.details["unevaluated_side"] = side;
    result.evidence.details["unevaluated_operand"] = operand.description;
    result.evidence.details["availability"] = availabilityToWire( operand.availability );
    if ( !operand.reason.empty() )
    {
        result.evidence.details["reason"] = operand.reason;
    }

    if ( operand.availability == Availability::Refused )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceRefused,
                      titleScopedMessage( check,
                                          std::string( "the " ) + side + " operand (" +
                                              operand.description +
                                              ") was refused: " + operand.reason +
                                              "; a refusal is an operational problem, not a value" ) );
        return result;
    }
    finishResult( result, CheckStatus::Indeterminate, failure_codes::kEvidenceUnavailable,
                  titleScopedMessage( check,
                                      std::string( "the " ) + side + " operand (" +
                                          operand.description +
                                          ") has no evidence, so the relation is unevaluated; "
                                          "assuming a value for the absent side would let this "
                                          "check agree with itself instead of with reality" ) );
    return result;
}

/// Reads one operand descriptor. Anything but the two documented sources is a
/// caller defect rather than something to guess at.
bool readOperand( const Json::Value &json, const std::string &defaultReference, Operand &out,
                  std::string &problem )
{
    const Json::Value *sourceMember = json.isMember( "source" ) ? &json["source"] : nullptr;
    if ( sourceMember == nullptr || !sourceMember->isString() )
    {
        problem = "each operand must declare a string 'source' of artifact|metric";
        return false;
    }
    const std::string source = sourceMember->asString();
    if ( source == "artifact" )
    {
        out.isArtifact = true;
        const Json::Value *factMember = json.isMember( "fact" ) ? &json["fact"] : nullptr;
        if ( factMember == nullptr || !factMember->isString() || factMember->asString().empty() )
        {
            problem = "an artifact operand must name a 'fact' path, e.g. size.width";
            return false;
        }
        out.factPath = factMember->asString();
        const Json::Value *refMember = json.isMember( "ref" ) ? &json["ref"] : nullptr;
        out.reference =
            refMember != nullptr && refMember->isString() ? refMember->asString() : defaultReference;
        return true;
    }
    if ( source == "metric" )
    {
        out.isArtifact = false;
        const Json::Value *nameMember = json.isMember( "name" ) ? &json["name"] : nullptr;
        out.reference =
            nameMember != nullptr && nameMember->isString() ? nameMember->asString()
                                                            : defaultReference;
        return true;
    }
    problem = "operand source '" + source + "' is not one of artifact|metric";
    return false;
}

} // namespace

CheckResult runRelationalCheck( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );
    result.evidence.details["relation"] =
        stringMember( check.params, "relation", std::string{} );

    const Json::Value *relationMember = check.params.isMember( "relation" ) ? &check.params["relation"]
                                                                           : nullptr;
    const Json::Value *operandsMember = check.params.isMember( "operands" ) ? &check.params["operands"]
                                                                            : nullptr;

    RelationKind kind = RelationKind::Equals;
    {
        std::string problem;
        if ( relationMember == nullptr || !relationMember->isString() )
        {
            problem = "a relational check must declare a string 'relation'";
        }
        else if ( !relationFromWire( relationMember->asString(), kind ) )
        {
            problem = "relation '" + relationMember->asString() +
                      "' is not one this build can evaluate";
        }
        if ( !problem.empty() )
        {
            result.evidence.details["spec_problem"] = problem;
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check, problem ) );
            return result;
        }
    }

    if ( operandsMember == nullptr || !operandsMember->isArray() || operandsMember->size() != 2 )
    {
        result.evidence.details["spec_problem"] = "a relational check needs exactly two operands";
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, "a relational check needs exactly two operands" ) );
        return result;
    }

    Operand leftOperand;
    Operand rightOperand;
    {
        std::string problem;
        if ( !readOperand( ( *operandsMember )[0], check.subject.id, leftOperand, problem ) )
        {
            result.evidence.details["spec_problem"] = problem;
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check, problem ) );
            return result;
        }
        if ( !readOperand( ( *operandsMember )[1], check.subject.id, rightOperand, problem ) )
        {
            result.evidence.details["spec_problem"] = problem;
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check, problem ) );
            return result;
        }
    }

    Json::Value expect{ Json::objectValue };
    const Json::Value *expectMember = check.params.isMember( "expect" ) ? &check.params["expect"]
                                                                        : nullptr;
    if ( expectMember != nullptr )
    {
        if ( !expectMember->isObject() )
        {
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check, "member 'expect' must be an object" ) );
            return result;
        }
        expect = *expectMember;
        for ( const std::string &key : expect.getMemberNames() )
        {
            if ( key != "value" && key != "tolerance" )
            {
                result.evidence.details["unexpected_key"] = key;
                finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                              titleScopedMessage( check,
                                                  "this family cannot evaluate expectation member '" +
                                                      key + "'" ) );
                return result;
            }
        }
    }

    const Json::Value *pinMember = expect.isMember( "value" ) ? &expect["value"] : nullptr;
    const Json::Value *toleranceMember = expect.isMember( "tolerance" ) ? &expect["tolerance"]
                                                                        : nullptr;
    double tolerance = -1.0;
    if ( pinMember != nullptr && ( !pinMember->isNumeric() || !numericFinite( pinMember->asDouble() ) ) )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check, "expect.value must be a finite number" ) );
        return result;
    }
    if ( toleranceMember != nullptr )
    {
        if ( !toleranceMember->isNumeric() || !numericFinite( toleranceMember->asDouble() ) ||
             toleranceMember->asDouble() < 0.0 )
        {
            finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                          titleScopedMessage( check,
                                              "expect.tolerance must be a finite number >= 0" ) );
            return result;
        }
        tolerance = toleranceMember->asDouble();
    }

    const bool needsPin = kind != RelationKind::Equals;
    if ( needsPin && pinMember == nullptr )
    {
        finishResult( result, CheckStatus::Indeterminate, failure_codes::kSpecInvalid,
                      titleScopedMessage( check,
                                          std::string( "this relation needs expectation member "
                                                       "'value' to say what the combination must "
                                                       "equal" ) ) );
        return result;
    }

    const double pin = pinMember == nullptr ? 0.0 : pinMember->asDouble();
    if ( pinMember != nullptr )
    {
        result.evidence.expected["value"] = canonicalNumber( pin );
    }
    if ( toleranceMember != nullptr )
    {
        result.evidence.expected["tolerance"] = canonicalNumber( tolerance );
    }
    result.evidence.details["left_operand"] = leftOperand.isArtifact
                                                  ? leftOperand.reference + "." + leftOperand.factPath
                                                  : leftOperand.reference;
    result.evidence.details["right_operand"] =
        rightOperand.isArtifact ? rightOperand.reference + "." + rightOperand.factPath
                                : rightOperand.reference;

    const ResolvedOperand left = resolveOperand( leftOperand, inputs, check.subject.id );
    if ( !left.resolved )
    {
        return unevaluatedResult( std::move( result ), check, "left", left );
    }
    const ResolvedOperand right = resolveOperand( rightOperand, inputs, check.subject.id );
    if ( !right.resolved )
    {
        return unevaluatedResult( std::move( result ), check, "right", right );
    }

    result.evidence.coverage = EvidenceCoverage::Full;
    result.evidence.observed["left"] = canonicalNumber( left.value );
    result.evidence.observed["right"] = canonicalNumber( right.value );

    // A NaN operand can be consistent with nothing, and comparing one would let
    // `numericSame`'s "never silently equal" be the only thing standing between
    // this check and a fabricated agreement.
    const char *brokenSide = !numericFinite( left.value ) ? "left"
                                                          : ( !numericFinite( right.value ) ? "right"
                                                                                            : nullptr );
    if ( brokenSide != nullptr )
    {
        result.evidence.details["non_finite_side"] = brokenSide;
        finishResult( result, CheckStatus::Fail, failure_codes::kNumericNotFinite,
                      titleScopedMessage( check,
                                          std::string( "the " ) + brokenSide +
                                              " operand is not finite, so the relation has no "
                                              "truth value; every derived quantity inherits the "
                                              "corruption" ) );
        return result;
    }

    const std::string leftText = roundSignificant( left.value );
    const std::string rightText = roundSignificant( right.value );

    double combination = 0.0;
    std::string combinationText;
    switch ( kind )
    {
        case RelationKind::Equals:
            combinationText = leftText;
            break;
        case RelationKind::ProductEquals:
            combination = left.value * right.value;
            combinationText = roundSignificant( combination );
            result.evidence.observed["combination"] = canonicalNumber( combination );
            break;
        case RelationKind::SumEquals:
            combination = left.value + right.value;
            combinationText = roundSignificant( combination );
            result.evidence.observed["combination"] = canonicalNumber( combination );
            break;
    }

    double observedSide = combination;
    double expectedSide = pin;
    switch ( kind )
    {
        case RelationKind::Equals:
            observedSide = left.value;
            expectedSide = right.value;
            break;
        case RelationKind::ProductEquals:
        case RelationKind::SumEquals:
            observedSide = combination;
            expectedSide = pin;
            break;
    }

    // `equals` may additionally be pinned: then both must agree with each other
    // AND with the declared quantity.
    if ( kind == RelationKind::Equals )
    {
        if ( !numericSame( observedSide, expectedSide, tolerance ) )
        {
            // Magnitude, not signed difference. The evidence has to answer
            // "how far apart are these?" in a way a reader can compare against a
            // tolerance directly; a sign would encode an arbitrary choice of
            // which side is the reference, and two equally-wrong results would
            // then look different. The sides themselves are already recorded in
            // observed.left / observed.right, so no information is lost.
            result.evidence.details["delta"] = canonicalNumber( std::fabs( observedSide - expectedSide ) );
            finishResult( result, CheckStatus::Fail, failure_codes::kRelationInconsistent,
                          titleScopedMessage( check,
                                              leftText + " must agree with " + rightText +
                                                  " but differs by " +
                                                  roundSignificant( std::fabs( observedSide - expectedSide ) ) +
                                                  "; these two facts are supposed to describe one "
                                                  "artifact, so at least one of them is wrong and "
                                                  "nothing computed from either is trustworthy" ) );
            return result;
        }
        if ( pinMember != nullptr && !numericSame( left.value, pin, tolerance ) )
        {
            result.evidence.details["delta"] = canonicalNumber( std::fabs( left.value - pin ) );
            finishResult( result, CheckStatus::Fail, failure_codes::kRelationInconsistent,
                          titleScopedMessage( check,
                                              leftText + " and " + rightText +
                                                  " agree with each other but the pair does not "
                                                  "equal the contracted " +
                                                  roundSignificant( pin ) ) );
            return result;
        }
    }
    else if ( !numericSame( combination, pin, tolerance ) )
    {
        result.evidence.details["delta"] = canonicalNumber( std::fabs( combination - pin ) );
        finishResult( result, CheckStatus::Fail, failure_codes::kRelationInconsistent,
                      titleScopedMessage( check,
                                          leftText + " and " + rightText + " combine to " +
                                              combinationText + " instead of the contracted " +
                                              roundSignificant( pin ) + " (delta " +
                                              roundSignificant( std::fabs( combination - pin ) ) +
                                              "); these facts cannot describe the same artifact, "
                                              "so any composite built from them misaddresses its "
                                              "own pixels" ) );
        return result;
    }

    finishResult( result, CheckStatus::Pass, {},
                  titleScopedMessage( check,
                                      leftText + " and " + rightText +
                                          ( kind == RelationKind::Equals
                                                ? " agree, as they must when they describe one artifact"
                                                : " combine to the contracted " +
                                                      roundSignificant( pin ) ) ) );
    return result;
}

} // namespace sicnu::verification
