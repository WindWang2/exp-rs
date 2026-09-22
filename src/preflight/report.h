// report.h — PreflightReport, the versioned deterministic artifact of the
// Scientific Preflight Engine (RS14-02).
//
// schema sicnu.preflight.report/1:
// {
//   "schema_version": "1",
//   "kind": "preflight_report",
//   "request_digest": "<sha256-16 of canonical request>",
//   "rules_revision": "<sha256-16 of sorted rule_id@revision>",
//   "operator_id": "...",
//   "mode": "teaching" | "agent",
//   "verdict": "ok" | "requires_ack" | "blocked",
//   "findings": [ PreflightFinding... ],   // stable order (severity, code, subject)
//   "evaluated": [ { rule_id, revision, outcome, detail }... ],
//   "budgets": { max_rules, max_inputs, max_findings }
// }
//
// Determinism contract: no wall-clock fields; canonical JSON (sorted keys,
// compact, 12-significant-digit doubles); the same inputs always produce
// byte-identical text and the same sha256. That is what makes the report
// storable as provenance/experiment evidence.

#pragma once

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

#include "preflight/finding.h"

namespace sicnu::preflight {

struct PreflightBudgets
{
  int maxRules = 512;
  int maxInputs = 64;
  int maxFindings = 256;

  Json::Value toJson() const;
  static std::optional<PreflightBudgets> fromJson( const Json::Value &json );
};

/// Per-rule evaluation trace — makes the safe/unsafe/unknown decision matrix
/// inspectable: a rule that could not decide says so ("insufficient_facts")
/// instead of silently passing.
struct PreflightEvaluatedRule
{
  std::string ruleId;
  int revision = 1;
  std::string outcome; ///< pass | finding | insufficient_facts | skipped
  std::string detail;  ///< Short reason, e.g. "missing facts: radiometric_state".

  Json::Value toJson() const;
  static std::optional<PreflightEvaluatedRule> fromJson( const Json::Value &json );
};

bool isValidEvaluatedOutcome( const std::string &outcome );

struct PreflightReport
{
  std::string schema = "sicnu.preflight.report/1"; ///< Full schema id.
  std::string schemaVersion = "1";
  std::string kind = "preflight_report";
  std::string requestDigest; ///< sha256-16 of the canonical request document.
  std::string rulesRevision; ///< sha256-16 of the sorted rule id@revision set.
  std::string operatorId;
  std::string mode; ///< "teaching" | "agent"
  std::string verdict = "ok";
  std::vector<PreflightFinding> findings;
  std::vector<PreflightEvaluatedRule> evaluated;
  PreflightBudgets budgets;

  static PreflightReport makeEmpty( const std::string &operatorId, const std::string &mode );

  Json::Value toJson() const;

  /// Fail-closed reader: checks schema_version/kind/mode/verdict and every
  /// finding. Returns nullopt on any mismatch — a report that cannot be
  /// trusted must not silently degrade.
  static std::optional<PreflightReport> fromJson( const Json::Value &json );
};

/// Byte-deterministic serialization: sorted keys, compact separators,
/// 12-significant-digit doubles, no comments. Two evaluations of the same
/// request under the same rule set produce identical bytes.
std::string canonicalReportJson( const PreflightReport &report );

/// sha256 over canonicalReportJson — tamper evidence for stored reports.
std::string reportDigest( const PreflightReport &report );

} // namespace sicnu::preflight
