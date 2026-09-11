// src/agent/harness/harness_verification.h
#pragma once

//
// Harness 4.0 automatic output verification (mission Phase 9).
//
// Every important processing output is verified before the harness may report
// success. Verdicts are the closed tri-state PASS / PASS_WITH_WARNINGS / FAIL;
// a FAIL verdict forces the run result status to "failed" — there is no code
// path that reports success after a FAIL.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "harness_error.h"

namespace sicnu::agent::harness {

enum class Verdict {
  Pass,
  PassWithWarnings,
  Fail,
};

std::string verdictToString( Verdict verdict );
const char *verdictToStringWire( Verdict verdict ); ///< "PASS" | "PASS_WITH_WARNINGS" | "FAIL"

/// Expectations a caller can pin for one artifact. Everything is optional;
/// undeclared expectations fall back to the structural defaults.
struct VerificationExpectations {
  std::string kind;               ///< "raster"|"vector"|"map"|"any" ("" = infer from extension)
  std::string crs;                ///< expected CRS authority id, empty = any non-empty
  Json::Int width = 0;            ///< 0 = any
  Json::Int height = 0;           ///< 0 = any
  double maxNodataFraction = 1.0; ///< FAIL above this fraction of NoData samples
  double minFiniteFraction = 0.0; ///< FAIL below this fraction of finite samples
  Json::Value classValues;        ///< optional closed class-value domain (array)
  bool requireNonEmpty = true;    ///< vector feature count > 0
  bool requireProvenance = true;  ///< derivation/provenance sidecar must exist

  /// Harness 7.0 (mission Area F): when declared, the raster output extent
  /// ({xmin,ymin,xmax,ymax}) must COVER this region; a shortfall is an error.
  Json::Value expectedExtent{Json::Value()};
  /// When true, an uncertainty sidecar (<path>.uncertainty.json) is expected
  /// where the plan/recipe declares an uncertainty path (warning-class).
  bool requireUncertainty = false;

  /// Harness 8.0 (Area G): raster band count the output must carry (0 = any).
  /// Multimodal/classified chains break silently when a producer drops bands.
  Json::Int expectedBandCount = 0;
};

struct VerificationCheck {
  std::string check;
  bool passed = false;
  std::string severity; ///< "info" | "warning" | "error"
  std::string code;     ///< stable error code when failed ("" on pass)
  Json::Value details{Json::objectValue};
};

struct ArtifactVerification {
  std::string path;
  std::string kind;
  Verdict verdict = Verdict::Fail;
  std::vector<VerificationCheck> checks;

  Json::Value toJson() const;
};

/// Verifies one output artifact against the expectations: existence,
/// openability, CRS presence (and match when pinned), dimensions (raster),
/// band count (when pinned), finite-fraction and NoData-fraction on a
/// bounded sample, class-value domain membership, vector non-emptiness, and
/// provenance sidecar presence.
ArtifactVerification verifyArtifact( const std::string &path,
                                     const VerificationExpectations &expectations );

/// Aggregates artifact verdicts: any FAIL → FAIL; any warning →
/// PASS_WITH_WARNINGS; else PASS.
Verdict aggregateVerdict( const std::vector<ArtifactVerification> &artifacts );

/// Harness 8.0: appends a post-verification check (evidence-write outcomes)
/// to an already-verified artifact and recomputes the verdict from ALL
/// checks — an error-class append flips the verdict to FAIL, keeping
/// FAIL-never-success semantics intact.
void appendCheck( ArtifactVerification &artifact, VerificationCheck check );

} // namespace sicnu::agent::harness
