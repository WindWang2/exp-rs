// checks_provenance.cpp — is this result citable, or only plausible?
//
// Three rules drive every branch below:
//
//   1. A dimension reported present is never listed as missing, and a
//      dimension reported absent is never listed as present. The lists are
//      partitioned from what the provider actually said, never from what would
//      be convenient.
//   2. A required dimension the provider never mentioned is UNKNOWN. Writing
//      it into either list would invent a fact (providers.h), so it becomes
//      Indeterminate instead of nudging the verdict either way.
//   3. When nothing could be observed the record carries no `expected` block.
//      evidence.cpp treats "unavailable yet declares an expectation" as an
//      internally inconsistent record, and it is right: a pin we could not
//      compare anything against is not a finding, it is a wish. It is kept in
//      `details` so the reader still sees what was asked for.

#include "verification/checks_provenance.h"

#include "verification/availability.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/status_lattice.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

/// Every result starts from the same skeleton so no path can forget the
/// identity fields. The status stays Indeterminate until a branch earns
/// something better.
CheckResult beginResult( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckResult result;
    result.checkId = check.id;
    result.kind = check.kind;          // wire spelling, unknown kinds included
    result.title = check.title;
    result.hints = check.hints.isObject() ? check.hints : Json::Value{ Json::objectValue };
    result.evidence.kind = check.kind;
    result.evidence.sourceId = inputs.sourceId;
    result.evidence.coverage = EvidenceCoverage::Unavailable;
    return result;
}

Json::Value toJsonArray( const std::vector<std::string> &names )
{
    Json::Value array{ Json::arrayValue };
    for ( const std::string &name : names )
    {
        array.append( name );
    }
    return array;
}

std::string joinNames( const std::vector<std::string> &names )
{
    std::string out;
    for ( std::size_t i = 0; i < names.size(); ++i )
    {
        if ( i != 0 )
        {
            out += ", ";
        }
        out += names[i];
    }
    return out;
}

/// Refuses anything that is not a non-empty array of non-empty strings, so
/// "no requirement declared" can never be read as "requirement satisfied".
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

} // namespace

CheckResult runProvenanceCompletenessCheck( const VerificationCheck &check,
                                            const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );

    std::vector<std::string> required;
    if ( !readStringArray( check.params, "required_dimensions", required ) )
    {
        result.status = CheckStatus::Indeterminate;
        result.failureCode = failure_codes::kSpecInvalid;
        result.message =
            "provenance for '" + check.subject.id +
            "' was never pinned: params.required_dimensions must be a non-empty array of "
            "dimension names. Without knowing what 'complete' means here, a complete "
            "provenance cannot be claimed, and an uncitable result cannot be reproduced.";
        result.evidence.details["subject_id"] = check.subject.id;
        result.evidence.details["spec_problem"] =
            "params.required_dimensions is missing or malformed";
        return result;
    }

    // An unwired provider is not an empty answer: it is no answer at all.
    if ( inputs.provenance == nullptr )
    {
        result.status = CheckStatus::Indeterminate;
        result.failureCode = failure_codes::kEvidenceUnavailable;
        result.message =
            "provenance for '" + check.subject.id +
            "' could not be established: no provenance provider is wired, so the "
            "completeness set was never obtained. That is unknown, not incomplete, and "
            "not complete.";
        result.evidence.details["subject_id"] = check.subject.id;
        result.evidence.details["required_dimensions"] = toJsonArray( required );
        result.evidence.details["provider_reason"] = "provenance provider is not wired";
        return result;
    }

    std::vector<ProvenanceDimension> dimensions;
    std::string reason;
    const Availability answer = inputs.provenance->completeness( check.subject.id, dimensions,
                                                                 reason );
    if ( unavailable( answer ) )
    {
        result.status = CheckStatus::Indeterminate;
        result.failureCode = answer == Availability::Refused
                                 ? failure_codes::kEvidenceRefused
                                 : failure_codes::kEvidenceUnavailable;
        result.message =
            "provenance for '" + check.subject.id +
            "' could not be established: the completeness set was " +
            ( answer == Availability::Refused ? "refused" : "not found" ) +
            ". Incomplete provenance is a defect of the result, but an unreadable "
            "provenance store is a defect of the wiring; neither may be reported as "
            "'the science is wrong'.";
        result.evidence.details["subject_id"] = check.subject.id;
        result.evidence.details["required_dimensions"] = toJsonArray( required );
        if ( !reason.empty() )
        {
            result.evidence.details["provider_reason"] = reason;
        }
        return result;
    }

    // Partition exactly what the provider said. A name it never mentioned lands
    // in `unknown` and nowhere else.
    std::vector<std::string> present;
    std::vector<std::string> missing;
    std::vector<std::string> unknown;
    for ( const std::string &name : required )
    {
        const ProvenanceDimension *reported = nullptr;
        for ( const ProvenanceDimension &dimension : dimensions )
        {
            if ( dimension.name == name )
            {
                reported = &dimension;
                break;
            }
        }
        if ( reported == nullptr )
        {
            unknown.push_back( name );
        }
        else if ( reported->present )
        {
            present.push_back( name );
        }
        else
        {
            missing.push_back( name );
        }
    }

    result.evidence.coverage = EvidenceCoverage::Full;
    result.evidence.details["subject_id"] = check.subject.id;
    result.evidence.details["present_dimensions"] = toJsonArray( present );
    result.evidence.details["missing_dimensions"] = toJsonArray( missing );
    result.evidence.details["unknown_dimensions"] = toJsonArray( unknown );
    if ( !reason.empty() )
    {
        result.evidence.details["provider_reason"] = reason;
    }

    result.evidence.expected["required_dimensions"] = toJsonArray( required );

    Json::Value observedDimensions{ Json::arrayValue };
    for ( const ProvenanceDimension &dimension : dimensions )
    {
        observedDimensions.append( dimension.toJson() );
    }
    result.evidence.observed["dimensions"] = observedDimensions;

    if ( !missing.empty() )
    {
        // Fail dominates in the lattice: a dimension declared absent is a real
        // defect even while some other dimension is still unknown.
        result.status = CheckStatus::Fail;
        result.failureCode = failure_codes::kProvenanceIncomplete;
        result.message =
            "provenance for '" + check.subject.id + "' is incomplete: " + joinNames( missing ) +
            " " + ( missing.size() == 1 ? "is" : "are" ) +
            " declared but absent. Without them nobody else can cite, reproduce or refute "
            "this result, so it remains a plausible number rather than a finding.";
        if ( !unknown.empty() )
        {
            result.evidence.details["also_unknown"] = toJsonArray( unknown );
        }
        return result;
    }

    if ( !unknown.empty() )
    {
        result.status = CheckStatus::Indeterminate;
        result.failureCode = failure_codes::kEvidenceUnavailable;
        result.message =
            "provenance for '" + check.subject.id + "' is undetermined: " +
            joinNames( unknown ) + " " + ( unknown.size() == 1 ? "was" : "were" ) +
            " never reported by the provider, so " +
            ( unknown.size() == 1 ? "it is" : "they are" ) +
            " unknown rather than absent. Reporting them either way would fabricate "
            "evidence, and a result whose provenance is unknown is not citable.";
        return result;
    }

    result.status = CheckStatus::Pass;
    result.message =
        "provenance for '" + check.subject.id + "' declares every required dimension (" +
        joinNames( present ) + "), so the result can be cited and reproduced by someone "
        "who was not in the room.";
    return result;
}

} // namespace sicnu::verification
