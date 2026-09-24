// src/verify/projections.cpp — pure shape adapters into the harness and
// teaching-lab evidence document shapes.
#include "projections.h"

namespace sicnu::verify
{

Json::Value harnessVerificationFromReport( const VerificationReport &report )
{
    Json::Value json( Json::objectValue );
    json["verdict"] = report.overall == VerificationStatus::Pass ? std::string( "PASS" )
                                                                 : std::string( "FAIL" );
    Json::Value checks( Json::arrayValue );
    for ( const VerificationCheckResult &check : report.checks )
    {
        Json::Value entry( Json::objectValue );
        entry["check"] = check.checkId;
        entry["passed"] = check.status == VerificationStatus::Pass;
        // "error" for every non-pass — a warning-class indeterminate would
        // re-aggregate to PASS_WITH_WARNINGS through the harness lattice and
        // quietly convert "unknown" into "success".
        entry["severity"] = check.status == VerificationStatus::Pass ? std::string( "info" )
                                                                     : std::string( "error" );
        entry["code"] = check.code;
        Json::Value details( Json::objectValue );
        if ( check.evidence )
        {
            details["observed"] = check.evidence->observed;
            details["expected"] = check.evidence->expected;
        }
        entry["details"] = details;
        checks.append( entry );
    }
    json["checks"] = checks;
    return json;
}

Json::Value labEvidenceFromReport( const VerificationReport &report )
{
    Json::Value json( Json::objectValue );
    Json::Value evidence( Json::arrayValue );
    for ( const VerificationCheckResult &check : report.checks )
    {
        Json::Value entry( Json::objectValue );
        entry["assertion_id"] = check.checkId;
        entry["kind"] = check.kind;
        entry["passed"] = check.status == VerificationStatus::Pass;
        entry["observed"] = check.evidence ? check.evidence->observed : Json::Value( Json::objectValue );
        entry["expected"] = check.evidence ? check.evidence->expected : Json::Value( Json::objectValue );
        evidence.append( entry );
    }
    json["evidence"] = evidence;
    return json;
}

} // namespace sicnu::verify
