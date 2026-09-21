/***************************************************************************
  test_verifier_provenance_14.cpp — provenance / reproducibility / cross-output

  What is actually under test here is NOT "the verdict is right". It is the
  narrower and much easier-to-get-wrong property that a check **refuses to
  invent a verdict from partial information**:

    - absent evidence   -> Indeterminate, never Fail (absence is not error);
    - absent evidence   -> Indeterminate, never Pass (absence is not success);
    - two digests from DIFFERENT algorithms -> Indeterminate, because sha256
      and md5 outputs are not comparable and judging them either way asserts
      knowledge nobody has;
    - a required provenance dimension the provider never mentioned -> Unknown,
      which is neither "present" nor "absent".

  That third value is inherited from `ReplayCheckStatus::Unknown`
  (src/experiment/replay_readiness.h), which lowers the readiness level
  instead of passing. Every case below is written so that collapsing
  Indeterminate into either Pass or Fail turns the lane red.

  Fixtures are in-memory fakes implementing the pure-virtual provider seams:
  no files, no Qt, no clock.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "verification/availability.h"
#include "verification/canonical_json.h"
#include "verification/checks_cross_output.h"
#include "verification/checks_provenance.h"
#include "verification/checks_reproducibility.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"
#include "verification/providers.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <limits>
#include <string>
#include <vector>

namespace
{

using sicnu::verification::Availability;
using sicnu::verification::CheckResult;
using sicnu::verification::CheckStatus;
using sicnu::verification::DigestRecord;
using sicnu::verification::ProvenanceDimension;
using sicnu::verification::VerificationCheck;
using sicnu::verification::VerificationInputs;

namespace fc = sicnu::verification::failure_codes;

const char *kSourceId = "fake.provider.bundle";

// --------------------------------------------------------------------------
// in-memory providers
// --------------------------------------------------------------------------

struct FakeProvenance final : sicnu::verification::ProvenanceProvider
{
    Availability answer = Availability::Found;
    std::vector<ProvenanceDimension> dimensions;
    std::string reason;

    Availability completeness( const std::string &subjectId,
                               std::vector<ProvenanceDimension> &out,
                               std::string &why ) const override
    {
        askedSubjects.push_back( subjectId );
        out = dimensions;
        why = reason;
        return answer;
    }

    mutable std::vector<std::string> askedSubjects;
};

struct FakeDigest final : sicnu::verification::DigestProvider
{
    Availability answer = Availability::Found;
    DigestRecord value;
    std::string reason;

    Availability record( const std::string &subjectId, DigestRecord &out,
                         std::string &why ) const override
    {
        askedSubjects.push_back( subjectId );
        out = value;
        why = reason;
        return answer;
    }

    mutable std::vector<std::string> askedSubjects;
};

struct FakeArtifact final : sicnu::verification::ArtifactProvider
{
    struct Entry
    {
        std::string ref;
        Availability answer = Availability::Found;
        Json::Value facts{ Json::objectValue };
        std::string reason;
    };

    std::vector<Entry> entries;

    Availability describe( const std::string &artifactRef, Json::Value &facts,
                           std::string &reason ) const override
    {
        askedRefs.push_back( artifactRef );
        for ( const Entry &entry : entries )
        {
            if ( entry.ref == artifactRef )
            {
                facts = entry.facts;
                reason = entry.reason;
                return entry.answer;
            }
        }
        reason = "no fact set registered for '" + artifactRef + "'";
        return Availability::Missing;
    }

    mutable std::vector<std::string> askedRefs;
};

// --------------------------------------------------------------------------
// fixture builders
// --------------------------------------------------------------------------

VerificationCheck baseCheck( const std::string &id, const std::string &kind,
                             const std::string &subjectId )
{
    VerificationCheck check;
    check.id = id;
    check.kind = kind;
    check.title = "the result must be citable and reproducible, not merely plausible";
    check.subject.kind = "artifact";
    check.subject.id = subjectId;
    check.hints["replan"] = "supply the missing provenance and re-run";
    return check;
}

Json::Value stringArray( const std::vector<std::string> &values )
{
    Json::Value array{ Json::arrayValue };
    for ( const std::string &value : values )
    {
        array.append( value );
    }
    return array;
}

std::vector<std::string> toStringVector( const Json::Value &array )
{
    std::vector<std::string> out;
    if ( !array.isArray() )
    {
        return out;
    }
    for ( Json::Value::ArrayIndex i = 0; i < array.size(); ++i )
    {
        out.push_back( array[i].asString() );
    }
    return out;
}

/// Deterministic text for a whole result. A report is only replayable if two
/// runs over the same facts produce byte-identical text, so this is the
/// comparison every determinism assertion below uses.
std::string canonicalOf( const CheckResult &result )
{
    std::string error;
    std::string text;
    if ( !sicnu::verification::canonicalJson( result.toJson(), text, error ) )
    {
        return std::string( "<uncanonicalizable:" ) + error + ">";
    }
    return text;
}

/// Invariants that must hold for EVERY result this slice produces, whatever
/// the verdict. Most of them exist to catch a fail-open or a fabricated claim.
void requireResultInvariants( const CheckResult &result, const VerificationCheck &check )
{
    UNSCOPED_INFO( "check id: " << check.id << " status: "
                                << sicnu::verification::statusToWire( result.status ) );

    REQUIRE( result.checkId == check.id );
    REQUIRE( result.kind == check.kind );
    REQUIRE( !result.kind.empty() );
    REQUIRE( !result.title.empty() );

    // The message must explain WHY it matters, not just restate the verdict.
    REQUIRE( result.message.size() > 20 );

    // "who told you that?" is answerable after the fact.
    REQUIRE( result.evidence.sourceId == kSourceId );
    REQUIRE( !result.evidence.sourceId.empty() );
    REQUIRE( result.evidence.kind == check.kind );

    // A record that is internally insufficient is a defect, not a corner case.
    const std::vector<std::string> problems =
        sicnu::verification::evidenceCompleteness( result.evidence );
    REQUIRE( problems.empty() );

    // Pass never carries a failure code; every non-pass must carry a known one.
    if ( result.status == CheckStatus::Pass )
    {
        REQUIRE( result.failureCode.empty() );
    }
    else
    {
        REQUIRE( !result.failureCode.empty() );
        REQUIRE( sicnu::verification::isKnownFailureCode( result.failureCode ) );
    }

    REQUIRE( result.hints == check.hints );
    REQUIRE( !result.promoted );
}

void requireRoundTrip( const CheckResult &result )
{
    const Json::Value json = result.toJson();
    CheckResult loaded;
    std::string error;
    REQUIRE( CheckResult::fromJson( json, loaded, error ) );

    REQUIRE( loaded.checkId == result.checkId );
    REQUIRE( loaded.kind == result.kind );
    REQUIRE( loaded.title == result.title );
    REQUIRE( loaded.status == result.status );
    REQUIRE( loaded.failureCode == result.failureCode );
    REQUIRE( loaded.message == result.message );
    REQUIRE( loaded.evidence.kind == result.evidence.kind );
    REQUIRE( loaded.evidence.sourceId == result.evidence.sourceId );
    REQUIRE( loaded.evidence.coverage == result.evidence.coverage );
    REQUIRE( loaded.evidence.observed == result.evidence.observed );
    REQUIRE( loaded.evidence.expected == result.evidence.expected );
    REQUIRE( loaded.evidence.details == result.evidence.details );

    // The strongest form: the canonical TEXT survives, not just the fields.
    REQUIRE( canonicalOf( loaded ) == canonicalOf( result ) );
}

} // namespace

// ==========================================================================
// 1. provenance: every required dimension present => Pass
// ==========================================================================

TEST_CASE( "verifier14: fully declared provenance passes and names its source",
           "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = {
        { "algorithm", true, "sar_calibrate@v3" },
        { "input_digest", true, "sha256:9f2c..." },
        { "crs", true, "EPSG:4326" },
    };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness",
                                         "artifact.sar_calibrate" );
    check.params["required_dimensions"] = stringArray( { "algorithm", "input_digest", "crs" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Pass );
    REQUIRE( result.failureCode.empty() );

    REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Full );
    REQUIRE( toStringVector( result.evidence.details["missing_dimensions"] ).empty() );
    REQUIRE( toStringVector( result.evidence.details["unknown_dimensions"] ).empty() );

    // The subject the check was asked about must be the subject queried.
    REQUIRE( provider.askedSubjects.size() == 1 );
    REQUIRE( provider.askedSubjects.front() == "artifact.sar_calibrate" );
}

// ==========================================================================
// 2. provenance: an absent required dimension => Fail + kProvenanceIncomplete,
//    and details must list the missing NAMES
// ==========================================================================

TEST_CASE( "verifier14: an absent required provenance dimension fails and is named",
           "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = {
        { "algorithm", true, "sar_calibrate@v3" },
        { "input_digest", false, "no digest was recorded for the input scene" },
        { "crs", true, "EPSG:4326" },
    };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness",
                                         "artifact.sar_calibrate" );
    check.params["required_dimensions"] = stringArray( { "algorithm", "input_digest", "crs" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kProvenanceIncomplete );

    const std::vector<std::string> missing =
        toStringVector( result.evidence.details["missing_dimensions"] );
    REQUIRE( missing.size() == 1 );
    REQUIRE( missing.front() == "input_digest" );
    REQUIRE( result.message.find( "input_digest" ) != std::string::npos );
}

TEST_CASE( "verifier14: provenance failure lists every absent dimension, not just the first",
           "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = {
        { "algorithm", true, "sar_calibrate@v3" },
        { "input_digest", false, "" },
        { "crs", false, "" },
        { "software_version", true, "1.4.2" },
    };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness", "a1" );
    check.params["required_dimensions"] =
        stringArray( { "algorithm", "input_digest", "crs", "software_version" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kProvenanceIncomplete );

    const std::vector<std::string> missing =
        toStringVector( result.evidence.details["missing_dimensions"] );
    REQUIRE( missing.size() == 2 );
    REQUIRE( missing[0] == "input_digest" );
    REQUIRE( missing[1] == "crs" );
}

// ==========================================================================
// 3. provenance: Missing / Refused / unwired => Indeterminate, never Fail
// ==========================================================================

TEST_CASE( "verifier14: unobtainable provenance is Indeterminate, not a failure of the science",
           "[verifier14][provenance]" )
{
    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness", "a1" );
    check.params["required_dimensions"] = stringArray( { "algorithm", "crs" } );

    // 3a. provider found nothing: we know nothing either way.
    {
        FakeProvenance provider;
        provider.answer = Availability::Missing;
        provider.reason = "no provenance sidecar exists for this artifact";

        VerificationInputs inputs;
        inputs.provenance = &provider;
        inputs.sourceId = kSourceId;

        const CheckResult result =
            sicnu::verification::runProvenanceCompletenessCheck( check, inputs );
        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Fail );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
        REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Unavailable );
        REQUIRE( result.evidence.observed.empty() );
    }

    // 3b. provider refused: an operational problem, still not wrong science.
    {
        FakeProvenance provider;
        provider.answer = Availability::Refused;
        provider.reason = "provenance store is locked";

        VerificationInputs inputs;
        inputs.provenance = &provider;
        inputs.sourceId = kSourceId;

        const CheckResult result =
            sicnu::verification::runProvenanceCompletenessCheck( check, inputs );
        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.failureCode == fc::kEvidenceRefused );
    }

    // 3c. provider never wired: asking for a fact through a provider that does
    // not exist must not look like "the answer was empty".
    {
        VerificationInputs inputs;
        inputs.sourceId = kSourceId;

        const CheckResult result =
            sicnu::verification::runProvenanceCompletenessCheck( check, inputs );
        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
    }
}

// ==========================================================================
// 4. no fabrication: present never appears as missing, and vice versa
// ==========================================================================

TEST_CASE( "verifier14: provenance never fabricates presence or absence", "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = {
        { "algorithm", true, "sar_calibrate@v3" },
        { "input_digest", true, "sha256:abc" },
        { "crs", false, "sidecar declares no CRS" },
        { "software_version", true, "1.4.2" },
        { "operator", false, "" },
        { "parameters", true, "{\"looks\":4}" },
    };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness", "a1" );
    check.params["required_dimensions"] =
        stringArray( { "algorithm", "input_digest", "crs", "software_version", "operator",
                       "parameters" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kProvenanceIncomplete );

    const std::vector<std::string> missing =
        toStringVector( result.evidence.details["missing_dimensions"] );
    const std::vector<std::string> present =
        toStringVector( result.evidence.details["present_dimensions"] );

    REQUIRE( missing.size() == 2 );
    REQUIRE( present.size() == 4 );

    // The two directions of "no fabrication":
    for ( const std::string &name : present )
    {
        UNSCOPED_INFO( "present dimension under test: " << name );
        REQUIRE( std::find( missing.begin(), missing.end(), name ) == missing.end() );
    }
    for ( const std::string &name : missing )
    {
        UNSCOPED_INFO( "missing dimension under test: " << name );
        REQUIRE( std::find( present.begin(), present.end(), name ) == present.end() );
    }

    // And the mixed set resolved the right way round.
    REQUIRE( std::find( missing.begin(), missing.end(), "crs" ) != missing.end() );
    REQUIRE( std::find( missing.begin(), missing.end(), "operator" ) != missing.end() );
    REQUIRE( std::find( present.begin(), present.end(), "algorithm" ) != present.end() );
    REQUIRE( std::find( present.begin(), present.end(), "parameters" ) != present.end() );
}

TEST_CASE( "verifier14: a required dimension the provider never mentioned is Unknown, not absent",
           "[verifier14][provenance]" )
{
    // providers.h: "A dimension not mentioned is NOT evidence of absence: it
    // is unknown." Reporting it as absent would invent a fact.
    FakeProvenance provider;
    provider.dimensions = {
        { "algorithm", true, "sar_calibrate@v3" },
    };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "provenance.complete", "provenance_completeness", "a1" );
    check.params["required_dimensions"] = stringArray( { "algorithm", "crs" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.status != CheckStatus::Pass );
    REQUIRE( result.status != CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kEvidenceUnavailable );

    const std::vector<std::string> unknown =
        toStringVector( result.evidence.details["unknown_dimensions"] );
    REQUIRE( unknown.size() == 1 );
    REQUIRE( unknown.front() == "crs" );
    REQUIRE( toStringVector( result.evidence.details["missing_dimensions"] ).empty() );
}

TEST_CASE( "verifier14: provenance with no declared requirement cannot be called complete",
           "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = { { "algorithm", true, "x" } };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    const CheckResult empty =
        sicnu::verification::runProvenanceCompletenessCheck(
            baseCheck( "p", "provenance_completeness", "a1" ), inputs );
    requireResultInvariants( empty, baseCheck( "p", "provenance_completeness", "a1" ) );
    REQUIRE( empty.status == CheckStatus::Indeterminate );
    REQUIRE( empty.status != CheckStatus::Pass );
    REQUIRE( empty.failureCode == fc::kSpecInvalid );

    VerificationCheck emptyArray = baseCheck( "p", "provenance_completeness", "a1" );
    emptyArray.params["required_dimensions"] = Json::Value{ Json::arrayValue };
    const CheckResult alsoEmpty =
        sicnu::verification::runProvenanceCompletenessCheck( emptyArray, inputs );
    requireResultInvariants( alsoEmpty, emptyArray );
    REQUIRE( alsoEmpty.status == CheckStatus::Indeterminate );
    REQUIRE( alsoEmpty.failureCode == fc::kSpecInvalid );

    // A malformed requirement list is a caller defect, not a pass either.
    VerificationCheck malformed = baseCheck( "p", "provenance_completeness", "a1" );
    malformed.params["required_dimensions"] = "algorithm";
    const CheckResult bad =
        sicnu::verification::runProvenanceCompletenessCheck( malformed, inputs );
    requireResultInvariants( bad, malformed );
    REQUIRE( bad.status == CheckStatus::Indeterminate );
    REQUIRE( bad.failureCode == fc::kSpecInvalid );
}

// ==========================================================================
// 5. reproducibility: equal digests => Pass; unequal => Fail
// ==========================================================================

namespace
{

VerificationCheck digestCheck( const std::string &algorithm, const std::string &digest )
{
    VerificationCheck check = baseCheck( "reproducibility.digest", "reproducibility_digest",
                                         "artifact.sar_calibrate" );
    if ( !algorithm.empty() || !digest.empty() )
    {
        check.params["expected"]["algorithm"] = algorithm;
        check.params["expected"]["digest"] = digest;
    }
    return check;
}

} // namespace

TEST_CASE( "verifier14: matching digests under one algorithm pass", "[verifier14][provenance]" )
{
    FakeDigest provider;
    provider.value.algorithm = "sha256";
    provider.value.digest = "9f2c1a";

    VerificationInputs inputs;
    inputs.digest = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = digestCheck( "sha256", "9f2c1a" );
    const CheckResult result = sicnu::verification::runReproducibilityDigestCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Pass );
    REQUIRE( result.failureCode.empty() );
    REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Full );
    REQUIRE( result.evidence.details["comparable"].asBool() );
    REQUIRE( provider.askedSubjects.front() == "artifact.sar_calibrate" );
}

TEST_CASE( "verifier14: differing digests fail and the evidence carries both plus the algorithm",
           "[verifier14][provenance]" )
{
    FakeDigest provider;
    provider.value.algorithm = "sha256";
    provider.value.digest = "9f2c1a";

    VerificationInputs inputs;
    inputs.digest = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = digestCheck( "sha256", "deadbeef" );
    const CheckResult result = sicnu::verification::runReproducibilityDigestCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kReproducibilityDigestMismatch );

    // Both digests, and the algorithm that makes them comparable at all.
    REQUIRE( result.evidence.observed["digest"].asString() == "9f2c1a" );
    REQUIRE( result.evidence.expected["digest"].asString() == "deadbeef" );
    REQUIRE( result.evidence.observed["algorithm"].asString() == "sha256" );
    REQUIRE( result.evidence.expected["algorithm"].asString() == "sha256" );
    REQUIRE( result.evidence.details["comparable"].asBool() );
}

// ==========================================================================
// 6. different algorithms => Indeterminate, NEITHER Pass NOR Fail
// ==========================================================================

TEST_CASE( "verifier14: digests from different algorithms are incomparable, not a verdict",
           "[verifier14][provenance]" )
{
    FakeDigest provider;
    provider.value.algorithm = "sha256";
    provider.value.digest = "9f2c1a";

    VerificationInputs inputs;
    inputs.digest = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = digestCheck( "md5", "9f2c1a" );
    const CheckResult result = sicnu::verification::runReproducibilityDigestCheck( check, inputs );

    // The primary assertion leads, so a mutant that flips this verdict is named
    // by the assertion that means it rather than by an incidental invariant.
    // The whole point: sha256 vs md5 says nothing about equality either way.
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.status != CheckStatus::Pass );
    REQUIRE( result.status != CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kEvidenceUnavailable );

    requireResultInvariants( result, check );
    REQUIRE( result.evidence.details["comparable"].asBool() == false );

    // Both digests are still recorded: the reader must see what we could not
    // compare rather than a bare "we don't know".
    REQUIRE( result.evidence.observed["digest"].asString() == "9f2c1a" );
    REQUIRE( result.evidence.expected["digest"].asString() == "9f2c1a" );
    REQUIRE( result.evidence.observed["algorithm"].asString() == "sha256" );
    REQUIRE( result.evidence.expected["algorithm"].asString() == "md5" );
}

// ==========================================================================
// 7. digest on one side only => Indeterminate (the ReplayCheckStatus::Unknown
//    shape: it lowers the grade, it does not fail the science)
// ==========================================================================

TEST_CASE( "verifier14: a digest on only one side is Indeterminate, not a mismatch",
           "[verifier14][provenance]" )
{
    // 7a. observed but nothing declared to compare against.
    {
        FakeDigest provider;
        provider.value.algorithm = "sha256";
        provider.value.digest = "9f2c1a";

        VerificationInputs inputs;
        inputs.digest = &provider;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = digestCheck( "", "" );
        const CheckResult result =
            sicnu::verification::runReproducibilityDigestCheck( check, inputs );

        // Primary assertion leads: one digest is not a comparison.
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.status != CheckStatus::Fail );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );

        requireResultInvariants( result, check );
        REQUIRE( result.evidence.observed["digest"].asString() == "9f2c1a" );
        REQUIRE( result.evidence.expected["digest"].asString().empty() );
    }

    // 7b. declared but the provider could not produce the run's digest.
    {
        FakeDigest provider;
        provider.answer = Availability::Missing;
        provider.reason = "run ledger was pruned";

        VerificationInputs inputs;
        inputs.digest = &provider;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = digestCheck( "sha256", "deadbeef" );
        const CheckResult result =
            sicnu::verification::runReproducibilityDigestCheck( check, inputs );

        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Fail );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
        REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Unavailable );
        REQUIRE( result.evidence.observed.empty() );
    }

    // 7c. the provider was never wired.
    {
        VerificationInputs inputs;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = digestCheck( "sha256", "deadbeef" );
        const CheckResult result =
            sicnu::verification::runReproducibilityDigestCheck( check, inputs );

        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
    }

    // 7d. a Found record that forgot to name its algorithm is not comparable.
    {
        FakeDigest provider;
        provider.value.digest = "9f2c1a";

        VerificationInputs inputs;
        inputs.digest = &provider;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = digestCheck( "sha256", "9f2c1a" );
        const CheckResult result =
            sicnu::verification::runReproducibilityDigestCheck( check, inputs );

        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
        requireResultInvariants( result, check );
    }
}

// ==========================================================================
// 8. cross-output consistency
// ==========================================================================

namespace
{

VerificationCheck crossCheck( const std::string &left, const std::string &right,
                              const std::string &statistic, double tolerance )
{
    VerificationCheck check = baseCheck( "cross_output.consistency", "cross_output_consistency",
                                         "tile.T042" );
    check.params["artifacts"] = stringArray( { left, right } );
    check.params["statistic"] = statistic;
    check.params["tolerance"] = tolerance;
    return check;
}

FakeArtifact::Entry artifactEntry( const std::string &ref, double mean )
{
    FakeArtifact::Entry entry;
    entry.ref = ref;
    entry.facts["mean"] = mean;
    entry.facts["kind"] = "raster";
    return entry;
}

} // namespace

TEST_CASE( "verifier14: agreeing cross-output statistics pass", "[verifier14][provenance]" )
{
    FakeArtifact provider;
    provider.entries = { artifactEntry( "out/a.tif", 0.42 ),
                         artifactEntry( "out/b.tif", 0.42 + 1e-9 ) };

    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    const CheckResult result = sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Pass );
    REQUIRE( result.failureCode.empty() );
    REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Full );
    REQUIRE( result.evidence.observed["statistic"].asString() == "mean" );

    // Both sides must be visible, otherwise "consistent" is a claim about one.
    REQUIRE( provider.askedRefs.size() == 2 );
    REQUIRE( result.evidence.observed["left"]["artifact"].asString() == "out/a.tif" );
    REQUIRE( result.evidence.observed["right"]["artifact"].asString() == "out/b.tif" );
}

TEST_CASE( "verifier14: disagreeing cross-output statistics fail with both values recorded",
           "[verifier14][provenance]" )
{
    FakeArtifact provider;
    provider.entries = { artifactEntry( "out/a.tif", 0.42 ),
                         artifactEntry( "out/b.tif", 0.55 ) };

    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    const CheckResult result = sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Fail );
    REQUIRE( result.failureCode == fc::kCrossOutputInconsistent );

    // Every number in the record is rounded: no raw double reaches evidence.
    REQUIRE( result.evidence.observed["left"]["value"].asString() ==
             sicnu::verification::roundSignificant( 0.42 ) );
    REQUIRE( result.evidence.observed["right"]["value"].asString() ==
             sicnu::verification::roundSignificant( 0.55 ) );
    REQUIRE( result.evidence.details["delta"].asString() ==
             sicnu::verification::roundSignificant( 0.13 ) );
    REQUIRE( result.evidence.expected["tolerance"].asString() ==
             sicnu::verification::roundSignificant( 1e-6 ) );
}

TEST_CASE( "verifier14: a difference inside tolerance is not an inconsistency",
           "[verifier14][provenance]" )
{
    FakeArtifact provider;
    provider.entries = { artifactEntry( "out/a.tif", 0.42 ),
                         artifactEntry( "out/b.tif", 0.4200001 ) };

    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-3 );
    const CheckResult result = sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Pass );
}

TEST_CASE( "verifier14: one missing side makes cross-output consistency Indeterminate",
           "[verifier14][provenance]" )
{
    // 8a. the counterpart artifact could not be described.
    {
        FakeArtifact provider;
        provider.entries = { artifactEntry( "out/a.tif", 0.42 ) };

        VerificationInputs inputs;
        inputs.artifact = &provider;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
        const CheckResult result =
            sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.status != CheckStatus::Fail );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
        REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Unavailable );
        REQUIRE( result.evidence.observed.empty() );
        REQUIRE( result.evidence.details["missing_side"].asString() == "right" );
    }

    // 8b. one side exists but does not carry the statistic being compared.
    {
        FakeArtifact provider;
        FakeArtifact::Entry incomplete = artifactEntry( "out/b.tif", 0.42 );
        incomplete.facts.removeMember( "mean" );
        provider.entries = { artifactEntry( "out/a.tif", 0.42 ), incomplete };

        VerificationInputs inputs;
        inputs.artifact = &provider;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
        const CheckResult result =
            sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.status != CheckStatus::Pass );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
    }

    // 8c. no artifact provider at all.
    {
        VerificationInputs inputs;
        inputs.sourceId = kSourceId;

        const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
        const CheckResult result =
            sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

        requireResultInvariants( result, check );
        REQUIRE( result.status == CheckStatus::Indeterminate );
        REQUIRE( result.failureCode == fc::kEvidenceUnavailable );
    }
}

TEST_CASE( "verifier14: a non-finite cross-output statistic is Indeterminate, never consistent",
           "[verifier14][provenance]" )
{
    // |nan - x| > tolerance is FALSE, so a naive comparison would report the
    // two outputs as consistent. That is the fail-open this case exists for.
    FakeArtifact provider;
    FakeArtifact::Entry nanEntry = artifactEntry( "out/b.tif", 0.42 );
    nanEntry.facts["mean"] = std::numeric_limits<double>::quiet_NaN();
    provider.entries = { artifactEntry( "out/a.tif", 0.42 ), nanEntry };

    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = kSourceId;

    const VerificationCheck check = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    const CheckResult result = sicnu::verification::runCrossOutputConsistencyCheck( check, inputs );

    requireResultInvariants( result, check );
    REQUIRE( result.status == CheckStatus::Indeterminate );
    REQUIRE( result.status != CheckStatus::Pass );
    REQUIRE( result.failureCode == fc::kNumericNotFinite );
    REQUIRE( result.evidence.coverage == sicnu::verification::EvidenceCoverage::Unavailable );
    REQUIRE( result.evidence.observed.empty() );
}

TEST_CASE( "verifier14: a cross-output check that names no comparable pair cannot conclude",
           "[verifier14][provenance]" )
{
    FakeArtifact provider;
    provider.entries = { artifactEntry( "out/a.tif", 0.42 ),
                         artifactEntry( "out/b.tif", 0.42 ) };

    VerificationInputs inputs;
    inputs.artifact = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck oneSided = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    oneSided.params["artifacts"] = stringArray( { "out/a.tif" } );
    const CheckResult tooFew =
        sicnu::verification::runCrossOutputConsistencyCheck( oneSided, inputs );
    requireResultInvariants( tooFew, oneSided );
    REQUIRE( tooFew.status == CheckStatus::Indeterminate );
    REQUIRE( tooFew.status != CheckStatus::Pass );
    REQUIRE( tooFew.failureCode == fc::kSpecInvalid );

    VerificationCheck noTolerance = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    noTolerance.params.removeMember( "tolerance" );
    const CheckResult unpinned =
        sicnu::verification::runCrossOutputConsistencyCheck( noTolerance, inputs );
    requireResultInvariants( unpinned, noTolerance );
    REQUIRE( unpinned.status == CheckStatus::Indeterminate );
    REQUIRE( unpinned.status != CheckStatus::Pass );
    REQUIRE( unpinned.failureCode == fc::kSpecInvalid );

    VerificationCheck noStatistic = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );
    noStatistic.params.removeMember( "statistic" );
    const CheckResult unnamed =
        sicnu::verification::runCrossOutputConsistencyCheck( noStatistic, inputs );
    requireResultInvariants( unnamed, noStatistic );
    REQUIRE( unnamed.status == CheckStatus::Indeterminate );
    REQUIRE( unnamed.failureCode == fc::kSpecInvalid );
}

// ==========================================================================
// 9. round-trip and determinism
// ==========================================================================

TEST_CASE( "verifier14: every check result round-trips and is byte-identical across runs",
           "[verifier14][provenance]" )
{
    FakeProvenance provenance;
    provenance.dimensions = { { "algorithm", true, "sar_calibrate@v3" },
                              { "crs", false, "" } };

    FakeDigest digest;
    digest.value.algorithm = "sha256";
    digest.value.digest = "9f2c1a";

    FakeArtifact artifact;
    artifact.entries = { artifactEntry( "out/a.tif", 0.42 ),
                         artifactEntry( "out/b.tif", 0.55 ) };

    VerificationInputs inputs;
    inputs.provenance = &provenance;
    inputs.digest = &digest;
    inputs.artifact = &artifact;
    inputs.sourceId = kSourceId;

    VerificationCheck provenanceCheck =
        baseCheck( "provenance.complete", "provenance_completeness", "artifact.sar" );
    provenanceCheck.params["required_dimensions"] = stringArray( { "algorithm", "crs" } );

    const VerificationCheck reproducibilityCheck = digestCheck( "sha256", "deadbeef" );
    const VerificationCheck crossOutputCheck = crossCheck( "out/a.tif", "out/b.tif", "mean", 1e-6 );

    struct Lane
    {
        const char *name;
        VerificationCheck check;
        CheckResult ( *run )( const VerificationCheck &, const VerificationInputs & );
    };

    const Lane lanes[] = {
        { "provenance", provenanceCheck,
          &sicnu::verification::runProvenanceCompletenessCheck },
        { "reproducibility", reproducibilityCheck,
          &sicnu::verification::runReproducibilityDigestCheck },
        { "cross_output", crossOutputCheck,
          &sicnu::verification::runCrossOutputConsistencyCheck },
    };

    for ( const Lane &lane : lanes )
    {
        UNSCOPED_INFO( "lane under test: " << lane.name );

        const CheckResult first = lane.run( lane.check, inputs );
        const CheckResult second = lane.run( lane.check, inputs );

        requireResultInvariants( first, lane.check );
        requireRoundTrip( first );

        // Determinism: identical inputs, identical canonical text.
        REQUIRE( canonicalOf( first ) == canonicalOf( second ) );
        REQUIRE( first.toJson() == second.toJson() );
    }
}

TEST_CASE( "verifier14: an unsupported kind still produces a record that round-trips",
           "[verifier14][provenance]" )
{
    FakeProvenance provider;
    provider.dimensions = { { "algorithm", true, "x" } };

    VerificationInputs inputs;
    inputs.provenance = &provider;
    inputs.sourceId = kSourceId;

    VerificationCheck check = baseCheck( "p.unknown", "totally_new_kind", "a1" );
    check.params["required_dimensions"] = stringArray( { "algorithm" } );

    const CheckResult result = sicnu::verification::runProvenanceCompletenessCheck( check, inputs );
    requireResultInvariants( result, check );
    REQUIRE( result.kind == "totally_new_kind" );
    REQUIRE( result.evidence.kind == "totally_new_kind" );
    requireRoundTrip( result );
}
