// src/agent/harness/evidence.h
#pragma once

//
// Harness 8.0 evidence sidecars (mission Area F).
//
// Closes the 7.0 known gap ("no codepath writes these sidecars yet —
// adversarial review F1") with governed, harness-owned writers:
//
//   <output>.provenance.json     — run-level provenance. The workflow engine
//                                  writes this itself on the temp-output path
//                                  (#698); when the file is absent (e.g. the
//                                  governed asset-commit path registered the
//                                  derivation in the catalog instead), the
//                                  harness writes a *run-identity* provenance
//                                  sidecar (plan/run/intent/fingerprints).
//                                  It never overwrites an engine sidecar.
//   <output>.uncertainty.json    — written ONLY from operator-declared
//                                  uncertainty facts in the step result
//                                  payloads (closed key list). The harness
//                                  never fabricates uncertainty: a method
//                                  that produces none yields no sidecar.
//   <output>.verification.json   — the verification evidence: verdict,
//                                  checks, expectations, run identity, and
//                                  the quality summary for the artifact.
//
// All writes follow the engine's own convention: QSaveFile (atomic
// write+rename) beside the artifact, best-effort with a typed error result —
// a failed sidecar write never corrupts the artifact and never flips a
// verdict by itself, but it is reported honestly in the run document.
//
// Ownership: operators own algorithm-specific facts; the engine owns
// derivation lineage on its path; the harness owns run identity, the
// verification record, and the decision to consume or report evidence gaps.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::workflow {
struct StepPlan;
}

namespace sicnu::agent::harness {

struct ArtifactVerification;
struct VerificationExpectations;

namespace evidence {

/// Operator-declared uncertainty facts harvested from completed steps.
struct UncertaintyHarvest {
  bool declared = false;                 ///< any step declared uncertainty?
  Json::Value facts{Json::arrayValue};   ///< [{step_id, operator_id, facts{...}}]
};

/// Sidecar write outcome. `written` false with empty `error` means
/// intentionally skipped (e.g. no uncertainty declared); non-empty `error`
/// means the write was attempted and failed.
struct SidecarResult {
  std::string path;
  bool written = false;
  std::string error;
};

/// Harvests operator-declared uncertainty facts for the artifact produced by
/// `outputPath` from the run's completed steps. Closed result-payload keys:
/// "uncertainty" (free-form operator document), "uncertaintyOutput"
/// (companion product path), "uncertainty_band" (band index), "confidence"
/// (operator confidence document). Steps are matched by output path; facts
/// are bounded per run.
UncertaintyHarvest harvestUncertainty( const std::vector<sicnu::workflow::StepPlan> &steps,
                                       const std::string &outputPath );

/// Writes <outputPath>.uncertainty.json from the harvested facts. Returns
/// {written:false} with no error when nothing was declared — absence of a
/// file is the honest answer for methods that produce no uncertainty.
SidecarResult writeUncertaintySidecar( const std::string &outputPath,
                                       const UncertaintyHarvest &harvest );

/// Writes a run-identity provenance sidecar when no provenance sidecar
/// exists yet (the engine's own writer wins). Reports the plan/run identity,
/// intent, plan fingerprint, and the declared input identities.
SidecarResult writeProvenanceSidecarIfAbsent( const std::string &outputPath,
                                              const Json::Value &runIdentity );

/// Persists the verification evidence: verdict, checks, expectations,
/// run identity, uncertainty pointer, and the quality summary (verdict +
/// failed/warning check counts). Written AFTER verification so the sidecar
/// records the final state.
SidecarResult writeVerificationEvidence( const std::string &outputPath,
                                         const ArtifactVerification &verification,
                                         const VerificationExpectations &expectations,
                                         const Json::Value &runIdentity,
                                         const UncertaintyHarvest &uncertainty );

} // namespace evidence

} // namespace sicnu::agent::harness
