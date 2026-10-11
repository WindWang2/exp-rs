// checks_reproducibility.cpp — can this result be produced again?
//
// The comparison has THREE inputs, not two: the pinned algorithm, the pinned
// digest, and the recorded one. Dropping the algorithm is the classic way this
// check lies — a sha256 and an md5 over the same bytes are different strings,
// and calling that a mismatch asserts the outputs differ when the truth is
// that the comparison was never possible. Calling it a match is worse.
//
// So:
//   same algorithm, same digest -> Pass
//   same algorithm, other digest -> Fail (the run is not the run that was pinned)
//   different algorithm         -> Indeterminate (incomparable, not unequal)
//   either side missing         -> Indeterminate (nothing to compare)
//
// The last two are the `ReplayCheckStatus::Unknown` shape: they lower the
// grade, they do not fail the science, and they do not pass it either.

#include "verification/checks_reproducibility.h"

#include "verification/availability.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/status_lattice.h"

#include <json/json.h>

#include <string>

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

void readPin( const Json::Value &params, std::string &algorithm, std::string &digest )
{
    if ( !params.isObject() || !params.isMember( "expected" ) ||
         !params["expected"].isObject() )
    {
        return;
    }
    const Json::Value &expected = params["expected"];
    if ( expected.isMember( "algorithm" ) && expected["algorithm"].isString() )
    {
        algorithm = expected["algorithm"].asString();
    }
    if ( expected.isMember( "digest" ) && expected["digest"].isString() )
    {
        digest = expected["digest"].asString();
    }
}

/// A digest without an algorithm is not comparable, and a record with an empty
/// digest observed nothing. Either way there is no second side to compare.
bool usable( const DigestRecord &record )
{
    return !record.algorithm.empty() && !record.digest.empty();
}

CheckResult oneSided( CheckResult result, const VerificationCheck &check,
                      const std::string &declaredAlgorithm, const std::string &declaredDigest,
                      bool hasPin, const std::string &reason )
{
    result.status = CheckStatus::Indeterminate;
    result.failureCode = failure_codes::kEvidenceUnavailable;
    result.message =
        "reproducibility of '" + check.subject.id + "' is undetermined: " + reason +
        ". One digest is not a comparison — it is neither evidence that the run "
        "reproduces nor evidence that it does not, so the grade is lowered rather than "
        "closed either way.";
    result.evidence.details["subject_id"] = check.subject.id;
    result.evidence.details["comparable"] = false;
    if ( hasPin )
    {
        // Kept in details, not in `expected`: evidence.cpp flags an unavailable
        // record that still declares an expectation, and that flag is honest.
        result.evidence.details["declared_algorithm"] = declaredAlgorithm;
        result.evidence.details["declared_digest"] = declaredDigest;
    }
    return result;
}

} // namespace

CheckResult runReproducibilityDigestCheck( const VerificationCheck &check,
                                           const VerificationInputs &inputs )
{
    CheckResult result = beginResult( check, inputs );

    std::string declaredAlgorithm;
    std::string declaredDigest;
    readPin( check.params, declaredAlgorithm, declaredDigest );
    const bool hasPin = !declaredAlgorithm.empty() && !declaredDigest.empty();

    if ( inputs.digest == nullptr )
    {
        return oneSided( result, check, declaredAlgorithm, declaredDigest, hasPin,
                         "no digest provider is wired, so no digest was recorded" );
    }

    DigestRecord observed;
    std::string reason;
    const Availability answer = inputs.digest->record( check.subject.id, observed, reason );
    if ( unavailable( answer ) )
    {
        result.evidence.details["provider_reason"] = reason.empty()
                                                         ? std::string( "no digest recorded" )
                                                         : reason;
        return oneSided( result, check, declaredAlgorithm, declaredDigest, hasPin,
                         answer == Availability::Refused
                             ? "the digest provider refused to answer"
                             : "the digest provider found no digest for this subject" );
    }

    if ( !usable( observed ) )
    {
        result.evidence.details["provider_reason"] =
            observed.algorithm.empty() ? std::string( "the recorded digest names no algorithm" )
                                       : std::string( "the recorded digest is empty" );
        return oneSided( result, check, declaredAlgorithm, declaredDigest, hasPin,
                         "the run's digest record is not usable" );
    }

    if ( !hasPin )
    {
        result.evidence.coverage = EvidenceCoverage::Full;
        result.evidence.observed["algorithm"] = observed.algorithm;
        result.evidence.observed["digest"] = observed.digest;
        return oneSided( result, check, declaredAlgorithm, declaredDigest, false,
                         "the check pins no digest to compare against" );
    }

    result.evidence.coverage = EvidenceCoverage::Full;
    result.evidence.details["subject_id"] = check.subject.id;
    result.evidence.observed["algorithm"] = observed.algorithm;
    result.evidence.observed["digest"] = observed.digest;
    result.evidence.expected["algorithm"] = declaredAlgorithm;
    result.evidence.expected["digest"] = declaredDigest;
    if ( !reason.empty() )
    {
        result.evidence.details["provider_reason"] = reason;
    }

    if ( observed.algorithm != declaredAlgorithm )
    {
        result.status = CheckStatus::Indeterminate;
        result.failureCode = failure_codes::kEvidenceUnavailable;
        result.evidence.details["comparable"] = false;
        result.evidence.details["reason"] =
            "digests were produced by different algorithms (" + declaredAlgorithm + " vs " +
            observed.algorithm + ")";
        result.message =
            "reproducibility of '" + check.subject.id +
            "' is undetermined: the pinned digest is a " + declaredAlgorithm +
            " digest while the run recorded a " + observed.algorithm +
            " digest. Digests from different algorithms are not comparable, so neither "
            "equality nor difference can be claimed without inventing knowledge.";
        return result;
    }

    result.evidence.details["comparable"] = true;

    if ( observed.digest == declaredDigest )
    {
        result.status = CheckStatus::Pass;
        result.message =
            "reproducibility of '" + check.subject.id + "' is confirmed: the " +
            observed.algorithm +
            " digest recorded for this run matches the pinned one, so the result can be "
            "regenerated from what was declared.";
        return result;
    }

    result.status = CheckStatus::Fail;
    result.failureCode = failure_codes::kReproducibilityDigestMismatch;
    result.message =
        "reproducibility of '" + check.subject.id + "' is broken: the " + observed.algorithm +
        " digest recorded for this run differs from the pinned one. The bytes that came out "
        "are not the bytes that were declared, so this is not the computation the "
        "specification described.";
    return result;
}

} // namespace sicnu::verification
