// packs_builtin.cpp — the shipped packs and the contract projection.
// See packs_builtin.h for the two rules that shape the derivation:
// it reads the registry, and it never answers an unknown operator with an
// empty, ok pack.

#include "verification/packs_builtin.h"

#include "contracts/scientific_contract.h"
#include "verification/failure_codes.h"
#include "verification/verification_types.h"

#include <json/json.h>

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

    Json::Value present = objectParam();
    present["expect_present"] = true;
    pack.checks.push_back( makeCheck( "structural.artifact_present",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the declared result artifact exists and is readable",
                                      SubjectRef{ "artifact", "" }, std::move( present ),
                                      failure_codes::kArtifactMissing ) );

    Json::Value kind = objectParam();
    kind["expect_kind"] = "declared";
    pack.checks.push_back( makeCheck( "structural.artifact_kind",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the artifact kind matches the kind the plan declared",
                                      SubjectRef{ "artifact", "" }, std::move( kind ),
                                      failure_codes::kArtifactKindMismatch ) );

    Json::Value grid = objectParam();
    grid["expect_grid"] = "declared";
    pack.checks.push_back( makeCheck( "structural.artifact_grid",
                                      checkKindToWire( CheckKind::ArtifactShape ),
                                      "the artifact grid matches the grid the plan declared",
                                      SubjectRef{ "artifact", "" }, std::move( grid ),
                                      failure_codes::kArtifactGridMismatch ) );

    return pack;
}

VerifierPack provenancePack()
{
    VerifierPack pack = baseBuiltin( "provenance" );

    Json::Value required = objectParam();
    Json::Value fields{ Json::arrayValue };
    fields.append( "operator" );
    fields.append( "parameters" );
    fields.append( "inputs" );
    required["required_fields"] = fields;
    pack.checks.push_back( makeCheck( "provenance.complete",
                                      checkKindToWire( CheckKind::ProvenanceCompleteness ),
                                      "the result records which operator and parameters produced it",
                                      SubjectRef{ "task", "" }, std::move( required ),
                                      failure_codes::kProvenanceIncomplete ) );

    Json::Value resolved = objectParam();
    resolved["require_resolved_inputs"] = true;
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

    Json::Value digest = objectParam();
    digest["algorithm"] = "sha256";
    pack.checks.push_back( makeCheck( "reproducibility.digest",
                                      checkKindToWire( CheckKind::ReproducibilityDigest ),
                                      "re-running the same inputs reproduces the same output digest",
                                      SubjectRef{ "metric", "reproducibility.digest" },
                                      std::move( digest ),
                                      failure_codes::kReproducibilityDigestMismatch ) );

    Json::Value repeats = objectParam();
    repeats["repeats"] = 2;
    pack.checks.push_back( makeCheck( "reproducibility.determinism",
                                      checkKindToWire( CheckKind::ReproducibilityDigest ),
                                      "two runs over identical inputs agree, so the result is replayable",
                                      SubjectRef{ "task", "" }, std::move( repeats ),
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

    Json::Value domains = contractParams( *contract );
    domains["input_domain"] = contract->inputDomain;
    domains["output_domain"] = contract->outputDomain;
    result.pack.checks.push_back(
        makeCheck( "contract.domain_transition", checkKindToWire( CheckKind::StateInvariant ),
                   "the operator consumes a '" + contract->inputDomain + "' surface and produces a '"
                       + contract->outputDomain + "' surface",
                   SubjectRef{ "task", operatorId }, std::move( domains ),
                   failure_codes::kStateInvariantViolation ) );

    Json::Value scale = contractParams( *contract );
    scale["scale_offset"] = contract->scaleOffset;
    result.pack.checks.push_back(
        makeCheck( "contract.scale_offset", checkKindToWire( CheckKind::StateInvariant ),
                   "scale and offset enter the numeric mapping as '" + contract->scaleOffset + "'",
                   SubjectRef{ "task", operatorId }, std::move( scale ),
                   failure_codes::kStateInvariantViolation ) );

    Json::Value noData = contractParams( *contract );
    noData["no_data_policy"] = contract->noDataPolicy;
    noData["output_domain"] = contract->outputDomain;
    result.pack.checks.push_back(
        makeCheck( "contract.no_data", checkKindToWire( CheckKind::NumericRange ),
                   "invalid samples are handled as '" + contract->noDataPolicy
                       + "' rather than as values",
                   SubjectRef{ "task", operatorId }, std::move( noData ),
                   failure_codes::kNumericOutOfRange ) );

    Json::Value provenance = contractParams( *contract );
    provenance["provenance"] = contract->provenance;
    result.pack.checks.push_back(
        makeCheck( "contract.provenance", checkKindToWire( CheckKind::ProvenanceCompleteness ),
                   "the operator records provenance at the level it declares: '"
                       + contract->provenance + "'",
                   SubjectRef{ "task", operatorId }, std::move( provenance ),
                   failure_codes::kProvenanceIncomplete ) );

    return result;
}

} // namespace sicnu::verification
