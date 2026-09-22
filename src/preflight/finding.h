// finding.h — typed findings for the Scientific Preflight Engine (RS14-02).
//
// A finding is the atom of a PreflightReport: one rule, one violation (or
// one unverifiable fact), one evidence payload. The four-level severity
// ladder is the point of this layer:
//
//   block       — scientifically wrong as stated; running it produces a
//                 silently-wrong product. NOT acknowledgable: the engine
//                 never lets an ack flip a block into a pass.
//   require_ack — defensible only if a human/agent explicitly accepts the
//                 named risk; acknowledgements are recorded in the report.
//   warn        — worth knowing, does not gate the run.
//   info        — honest boundary note (e.g. "metadata alone cannot prove
//                 spatial independence").
//
// Vocabulary reuse (no second truth source):
//   basis  — workflow_ir fact provenance words (observed|declared|derived|
//            assumed|unknown)
//   domain — free short tag (radiometric/grid/spectral/quality/temporal/ml/
//            model/budget)
//   code   — "SPF_*", a namespace distinct from harness codes (PREFLIGHT_*),
//            CAPSTATE_* and verify:*.

#pragma once

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::preflight {

enum class PreflightSeverity
{
  Block,
  RequireAck,
  Warn,
  Info,
};

std::string severityToString( PreflightSeverity severity );
std::optional<PreflightSeverity> severityFromString( const std::string &text );

/// Wire vocabulary shared with src/agent/harness/workflow_ir.h fact status.
bool isValidFactBasis( const std::string &basis );

/// Report-level verdicts. Distinct from harness "ok|fixable|blocked" and
/// planner "feasible*" — mapping documented in docs/adr/0174.
bool isValidVerdict( const std::string &verdict );

struct PreflightFinding
{
  std::string code;                 ///< Stable "SPF_*" code.
  PreflightSeverity severity = PreflightSeverity::Info;
  std::string ruleId;               ///< e.g. "preflight.radiometric_state_policy"
  int ruleRevision = 1;
  std::string domain;               ///< radiometric | grid | spectral | ...
  std::string subject;              ///< Slot or asset the finding is about.
  std::vector<std::string> affectedInputs; ///< Asset references.
  Json::Value evidence{Json::objectValue}; ///< Concrete values only.
  std::string basis = "declared";   ///< Fact provenance of the deciding facts.
  bool acknowledged = false;        ///< Set by the engine from the request.
  std::string humanExplanation;     ///< Teaching-mode "why this is wrong now".
  Json::Value machineExplanation{Json::objectValue}; ///< expectation/actual/remediation[]

  Json::Value toJson() const;

  /// Fail-closed reader: requires code, a valid severity, a valid basis and
  /// an object-shaped machine_explanation. Returns nullopt on any mismatch.
  static std::optional<PreflightFinding> fromJson( const Json::Value &json );
};

/// Ordered by severity rank (block first), then code, then subject — the
/// stable presentation order used by reports regardless of rule registration
/// order.
bool findingLess( const PreflightFinding &a, const PreflightFinding &b );

} // namespace sicnu::preflight
