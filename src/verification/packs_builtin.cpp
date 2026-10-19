// packs_builtin.cpp — the shipped packs and the contract projection.
// See packs_builtin.h for the two rules that shape the derivation:
// it reads the registry, and it never answers an unknown operator with an
// empty, ok pack.
//
// The third rule is about spelling. Every family reads its expectation out of
// a CLOSED vocabulary and REFUSES a member it cannot evaluate with
// VERIFY.SPEC_INVALID instead of ignoring it, so a key of this file's own
// invention is a key no evaluator ever sees — and a check whose declaration
// cannot be read cannot conclude anything. Each literal below is therefore
// written where its family actually looks for it.

#include "verification/packs_builtin.h"

#include "contracts/scientific_contract.h"
#include "verification/failure_codes.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <initializer_list>
#include <string>
#include <utility>

namespace sicnu::verification
{

namespace
{

/// A wire string deliberately OUTSIDE the CheckKind vocabulary. A check
/// carrying it can never be evaluated, so the runner can only report it as
/// VERIFY.UNSUPPORTED_CHECK_KIND — which is exactly the honest answer for an
/// operator we hold no contract for. Inventing a known kind here would let
/// some checker eventually read it as Pass.
constexpr const char *kUnsupportedOperatorKind = "unsupported_operator";

/// The digest algorithm this module computes (digest.h): the one half of a
/// reproducibility pin a shipped pack can declare without naming a run.
constexpr const char *kDigestAlgorithm = "sha256";

/// The kind the structural pack expects of the result surface. The pack is the
/// one for "the raster surface the operator must produce" (packs_builtin.h),
/// and "raster" is the token the artifact fact vocabulary reserves for a
/// grid-describing surface (checks_artifact.cpp's grid guard, the plan IR's
/// output ports).
constexpr const char *kStructuralKind = "raster";

VerificationCheck makeCheck( std::string id, const char *kind, std::string title,
                             SubjectRef subject, Json::Value params, const char *failureCode )
{
    VerificationCheck check;
    check.id = std::move( id );
    check.kind = kind;
    check.title = std::move( title );
    check.subject = std::move( subject );
    check.params = std::move( params );
    check.failureCode = failureCode;
    return check;
}

Json::Value objectParam()
{
    return Json::Value{ Json::objectValue };
}

/// A closed-list member (`required_keys`, `required_dimensions`). The families
/// that read one refuse an empty entry and refuse a non-string entry, so that
/// "no requirement declared" can never be read as "requirement satisfied".
Json::Value keyArray( std::initializer_list<const char *> keys )
{
    Json::Value array{ Json::arrayValue };
    for ( const char *key : keys )
    {
        array.append( key );
    }
    return array;
}

VerifierPack baseBuiltin( const std::string &id )
{
    VerifierPack pack;
    pack.id = id;
    pack.version = "1";
    pack.schema = kVerifierPackSchema;
    return pack;
}

Json::Value contractParams( const sicnu::contracts::ScientificContract &contract )
{
    Json::Value params = objectParam();
    params["operator_id"] = contract.operatorId;
    return params;
}

/// The unsupported-operator placeholder. It exists so the absence of a
/// contract is CARRIED by the pack instead of vanishing into an empty check
/// list that rolls up to "no checks".
VerificationCheck unsupportedOperatorCheck( const std::string &operatorId )
{
    Json::Value params = objectParam();
    params["operator_id"] = operatorId;
    return makeCheck( "contract.unsupported_operator", kUnsupportedOperatorKind,
                      "operator '" + operatorId + "' has no registered scientific contract, "
                      "so no expectation about it can be evaluated",
                      SubjectRef{ "task", operatorId }, std::move( params ),
                      failure_codes::kUnsupportedCheckKind );
}

} // namespace

VerifierPack structuralPack()
{
    VerifierPack pack = baseBuiltin( "structural" );

    // ArtifactShape reads `expect.{kind,size,crs_authid}` plus `required_keys`
    // drawn from the closed fact list, and refuses a member outside that set
    // (checks_artifact.cpp readExpectation). Presence, kind and grid are
    // therefore declared in THAT vocabulary: `expect_present`, `expect_kind`
    // and `expect_grid` were spellings of this file's own, so all three checks
    // died on VERIFY.SPEC_INVALID before a provider was ever consulted.

    Json::Value present = objectParam();
    present["required_keys"] = keyArray( { "kind" } );
    pack.checks.push_back( makeCheck( "structural.artifact_present",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the declared result artifact exists and is readable",
                                      SubjectRef{ "artifact", "" }, std::move( present ),
                                      failure_codes::kArtifactMissing ) );

    Json::Value kind = objectParam();
    kind["expect"] = objectParam();
    kind["expect"]["kind"] = kStructuralKind;
    pack.checks.push_back( makeCheck( "structural.artifact_kind",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the result surface is a raster, the kind the gridded "
                                      "operators read",
                                      SubjectRef{ "artifact", "" }, std::move( kind ),
                                      failure_codes::kArtifactKindMismatch ) );

    Json::Value grid = objectParam();
    grid["required_keys"] = keyArray( { "size" } );
    pack.checks.push_back( makeCheck( "structural.artifact_grid",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the result surface describes the grid it was produced on",
                                      SubjectRef{ "artifact", "" }, std::move( grid ),
                                      failure_codes::kArtifactGridMismatch ) );

    return pack;
}

VerifierPack provenancePack()
{
    VerifierPack pack = baseBuiltin( "provenance" );

    // ProvenanceCompleteness reads `required_dimensions`: a non-empty array of
    // dimension names the provider must report present (checks_provenance.cpp
    // readStringArray). `required_fields` was a spelling no evaluator reads, so
    // both checks below used to be refused before the completeness set was
    // ever obtained. The names are the ones a provenance record is made of —
    // which operator, with which parameters, from which inputs — split across
    // the two checks the way their titles already promised.

    Json::Value required = objectParam();
    required["required_dimensions"] = keyArray( { "operator", "parameters" } );
    pack.checks.push_back( makeCheck( "provenance.complete",
                                      checkKindToWire( CheckKind::ProvenanceCompleteness ),
                                      "the result records which operator and parameters produced it",
                                      SubjectRef{ "task", "" }, std::move( required ),
                                      failure_codes::kProvenanceIncomplete ) );

    Json::Value resolved = objectParam();
    resolved["required_dimensions"] = keyArray( { "inputs" } );
    pack.checks.push_back( makeCheck( "provenance.inputs_resolved",
                                      checkKindToWire( CheckKind::ProvenanceCompleteness ),
                                      "every input the result was derived from is identified",
                                      SubjectRef{ "task", "" }, std::move( resolved ),
                                      failure_codes::kProvenanceIncomplete ) );

    return pack;
}

VerifierPack reproducibilityPack()
{
    VerifierPack pack = baseBuiltin( "reproducibility" );

    // ReproducibilityDigest reads the pin out of `params.expected.algorithm`
    // and `params.expected.digest`, and compares nothing until BOTH halves are
    // there: a sha256 and an md5 over the same bytes are different strings, so
    // a pin missing either side is not a comparison at all
    // (checks_reproducibility.cpp readPin). The algorithm is this module's own,
    // so the pack can declare that half honestly. The digest half belongs to
    // whoever declares the expected output digest of a run — a shipped pack has
    // no run to name, and inventing one would turn every real result into a
    // mismatch. Both checks share the pin because the family has no repeats
    // mode; what separates them is which subject the digest provider is asked
    // about.

    Json::Value digest = objectParam();
    digest["expected"] = objectParam();
    digest["expected"]["algorithm"] = kDigestAlgorithm;
    pack.checks.push_back( makeCheck( "reproducibility.digest",
                                      checkKindToWire( CheckKind::ReproducibilityDigest ),
                                      "re-running the same inputs reproduces the same output digest",
                                      SubjectRef{ "metric", "reproducibility.digest" },
                                      std::move( digest ),
                                      failure_codes::kReproducibilityDigestMismatch ) );

    Json::Value determinism = objectParam();
    determinism["expected"] = objectParam();
    determinism["expected"]["algorithm"] = kDigestAlgorithm;
    pack.checks.push_back( makeCheck( "reproducibility.determinism",
                                      checkKindToWire( CheckKind::ReproducibilityDigest ),
                                      "two runs over identical inputs agree, so the result is replayable",
                                      SubjectRef{ "task", "" }, std::move( determinism ),
                                      failure_codes::kReproducibilityDigestMismatch ) );

    return pack;
}

DerivedPack scientificContractFor( const std::string &operatorId )
{
    DerivedPack result;
    result.pack = baseBuiltin( "contract" );
    result.pack.derivedFrom = operatorId;

    const sicnu::contracts::ScientificContract *contract =
        sicnu::contracts::findScientificContract( operatorId );
    if ( contract == nullptr )
    {
        result.indeterminate = true;
        result.failureCode = failure_codes::kUnsupportedCheckKind;
        result.reason = "no scientific contract is registered for operator '" + operatorId
                        + "'; the pack carries one visibly unevaluable check so the absence is "
                          "reported as "
                        + failure_codes::kUnsupportedCheckKind
                        + " instead of rolling up to 'no checks'";
        result.pack.checks.push_back( unsupportedOperatorCheck( operatorId ) );
        return result;
    }

    // ---- Projection. Every literal below is read from the registry record,
    // never restated here: the registry is the single source of truth, and a
    // copy kept in this file would drift the moment a contract is corrected.
    //
    // The spelling is not free, though. Each family reads its expectation out
    // of a closed vocabulary and refuses a member it cannot evaluate
    // (checks_state.cpp readDeclaredState, checks_numeric.cpp readInterval,
    // checks_provenance.cpp readStringArray), so a literal parked at the top
    // level — where this projection used to leave all of them — is a literal
    // no evaluator ever sees, and the check dies on VERIFY.SPEC_INVALID before
    // a provider is consulted. The contract's own words therefore stay at the
    // top level beside it: they are what the registry declared, and the pack is
    // a projection OF them. Where the family's vocabulary can carry one of
    // those words, the evaluable half is declared inside it too.

    Json::Value domains = contractParams( *contract );
    domains["input_domain"] = contract->inputDomain;
    domains["output_domain"] = contract->outputDomain;
    // The state family compares ONE declared token per check, and its
    // consequence text is written about the input ("the input carries the
    // '<observed>' numeric domain where '<expected>' was contracted"), so the
    // transition is pinned on the surface the operator CONSUMES. What it
    // produces is a second claim, and this family has one slot per check.
    domains["expect"] = objectParam();
    domains["expect"]["numeric_domain"] = contract->inputDomain;
    result.pack.checks.push_back(
        makeCheck( "contract.domain_transition", checkKindToWire( CheckKind::StateInvariant ),
                   "the operator consumes a '" + contract->inputDomain + "' surface and produces a '"
                       + contract->outputDomain + "' surface",
                   SubjectRef{ "task", operatorId }, std::move( domains ),
                   failure_codes::kStateInvariantViolation ) );

    Json::Value scale = contractParams( *contract );
    scale["scale_offset"] = contract->scaleOffset;
    // Deliberately NOT pinned into `expect`. The state family compares a
    // numeric_domain or a radiometric_state token, and a scale/offset POLICY
    // ("identity" | "param_driven" | "product_metadata" | "not_applicable")
    // is neither: it says how stored values become physical ones, not which
    // domain they land in. Writing one of those two tokens here anyway would
    // assert a domain the contract never declared, so the check would fail
    // runs that are correct — a fabricated finding, which is worse than an
    // unevaluable one. The policy stays readable in `params.scale_offset` for
    // whoever wires an evaluator that can compare it.
    result.pack.checks.push_back(
        makeCheck( "contract.scale_offset", checkKindToWire( CheckKind::StateInvariant ),
                   "scale and offset enter the numeric mapping as '" + contract->scaleOffset + "'",
                   SubjectRef{ "task", operatorId }, std::move( scale ),
                   failure_codes::kStateInvariantViolation ) );

    Json::Value noData = contractParams( *contract );
    noData["no_data_policy"] = contract->noDataPolicy;
    noData["output_domain"] = contract->outputDomain;
    // Deliberately NOT pinned into `expect`. This family reads numeric bounds
    // (value / lower / upper / tolerance) out of `params.expect`, and a NoData
    // POLICY ("propagate" | "read_metadata" | "fail_closed" |
    // "internal_sentinel" | "none") is a rule about WHICH samples are invalid,
    // not a bound on any value — the registry holds no number that corresponds
    // to it. Any bound written here would be invented, and an invented bound
    // fails results that are correct.
    result.pack.checks.push_back(
        makeCheck( "contract.no_data", checkKindToWire( CheckKind::NumericRange ),
                   "invalid samples are handled as '" + contract->noDataPolicy
                       + "' rather than as values",
                   SubjectRef{ "task", operatorId }, std::move( noData ),
                   failure_codes::kNumericOutOfRange ) );

    Json::Value provenance = contractParams( *contract );
    provenance["provenance"] = contract->provenance;
    // Deliberately NOT pinned into `required_dimensions`. That member names
    // dimensions a provider must report present, and a provenance LEVEL
    // ("none" | "output_metadata" | "derivation_record") names no dimensions —
    // it says how much provenance the caller may rely on. And the level "none"
    // means nothing is required, while this family refuses an empty list
    // precisely so that "no requirement declared" can never be read as
    // "requirement satisfied": there is no spelling of "nothing is required"
    // available here, so none is written.
    result.pack.checks.push_back(
        makeCheck( "contract.provenance", checkKindToWire( CheckKind::ProvenanceCompleteness ),
                   "the operator records provenance at the level it declares: '"
                       + contract->provenance + "'",
                   SubjectRef{ "task", operatorId }, std::move( provenance ),
                   failure_codes::kProvenanceIncomplete ) );

    return result;
}

} // namespace sicnu::verification
