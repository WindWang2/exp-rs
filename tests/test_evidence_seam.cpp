// test_evidence_seam.cpp — cross-module evidence-semantics seam oracle
// (hardening/verifier-grader-explain-evidence).
//
// The track invariant: the grader may CONSUME verifier evidence (the
// verifier_verdict taxonomy kind exists for exactly that) but never re-runs
// verification, and each module keeps its own digest authority. Composition
// happens HERE, above the leaves — sicnu_grader itself does not link
// sicnu_verifier (CMake layer guards in src/grader + src/explain enforce
// that; this binary is the only place both leaves meet).
//
// Light lane: Catch2 + sicnu_grader + sicnu_verifier, no Qt/QGIS.
#include "grader/grader_types.h"

#include "verify/verify_types.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE( "grader evidence consumes verifier verdicts as recorded facts",
           "[grader][verify][cross-module]" )
{
    using sicnu::verify::VerificationCheckResult;
    using sicnu::verify::VerificationReport;
    using sicnu::verify::VerificationStatus;

    // 1. A real verifier report, built with the verifier's own API.
    std::vector<VerificationCheckResult> checks( 1 );
    checks[0].checkId = "ndvi-range";
    checks[0].kind = "metric.range";
    checks[0].status = VerificationStatus::Fail;
    checks[0].code = "verify:e_metric_out_of_range";
    const VerificationReport verifierReport =
        sicnu::verify::buildReport( "spec.preprocess.optical", "node", std::string( 64, 'a' ), checks );
    const Json::Value verifierJson = verifierReport.toCanonicalJson();

    // 2. The grader side projects it into evidence: verdict + digest travel
    //    as recorded facts, to be judged later against a rubric.
    sicnu::grader::GraderError err;
    sicnu::grader::GradeEvidenceItem item;
    item.evidenceId = "ev-verify-1";
    item.kind = sicnu::grader::EvidenceKind::VerifierVerdict;
    item.key = verifierJson["specId"].asString();
    item.state = verifierJson["overall"].asString();
    item.facts["reportDigest"] = verifierReport.digest();
    item.facts["failCount"] = verifierJson["counts"]["fail"];
    sicnu::grader::GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    evidence.items.push_back( item );

    Json::Value doc = evidence.toJson();
    auto roundTrip = sicnu::grader::GradeEvidence::fromJson( doc, err );
    REQUIRE( roundTrip.has_value() );
    REQUIRE( roundTrip->validate( err ) );
    CHECK( roundTrip->items[0].state == "fail" );
    CHECK( roundTrip->items[0].facts["reportDigest"].asString() == verifierReport.digest() );

    // 3. Each authority keeps its own digest discipline: the grader evidence
    //    stays VALID as a recorded fact (it does not recompute verification),
    //    while the verifier's own surface refuses a body/digest mismatch.
    VerificationReport parsedBack;
    std::string verifyError;
    CHECK_FALSE( VerificationReport::fromCanonicalJson( verifierJson, parsedBack, verifyError,
                                                        "wrong-digest" ) );
    CHECK_FALSE( verifyError.empty() );
}
