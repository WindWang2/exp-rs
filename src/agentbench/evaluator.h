// src/agentbench/evaluator.h
#pragma once

//
// Case evaluator: case + trace → CaseEvaluation.
//
// Computes the eight benchmark metrics (each null-with-reason when not
// computable — never zero-filled), grades the hidden invariants, applies the
// closed failure taxonomy with a deterministic priority, and emits a
// versioned evaluation document (sicnu.agentbench.evaluation/v1) whose digest
// is stable across identical evaluations. Evaluation is a pure function of
// its inputs; it never executes anything.
//
// Metric semantics (v1):
//   task_completion            passed error-severity invariants / total
//   scientific_validity        passed scientific-dimension invariants / total
//   unnecessary_transformations  count of repeated successful (tool,input)
//                              pairs plus declared redundant-tool uses
//   plan_efficiency            minimal_steps / steps, clamped to [0,1]
//   verifier_pass_rate         expected evidence delivered with all required
//                              fields (verdict quality is scored by the
//                              scientific invariants, not here)
//   recovery_quality           handled / observable injected faults; null
//                              when the case injects no faults; a fault with
//                              no observable trace marker makes the metric
//                              null ("faults_not_observable_in_trace")
//   reproducibility            1.0 iff double evaluation is digest-identical
//   explanation_completeness   non-empty explanation + evidence explicitly
//                              required in the explanation + honest outcome
//                              claim, over that requirement total
//
// Classification priority (first match wins): scope_violation, not_started,
// silent_failure, budget_exhausted, then (on FAIL) verification_failed /
// invalid_science / claim_mismatch / incomplete, then recovery_failed, else
// none.
//

#include "case_schema.h"
#include "invariants.h"
#include "trace.h"
#include "version.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agentbench
{

inline constexpr const char *kAgentEvaluationSchemaTag = "sicnu.agentbench.evaluation/v1";

/// Outcome verdict; wire strings align with the platform harness tri-state.
enum class BenchVerdict
{
	Pass,
	PassWithWarnings,
	Fail,
};
std::string benchVerdictToString( BenchVerdict verdict ); ///< "PASS" | "PASS_WITH_WARNINGS" | "FAIL"

struct MetricResult
{
	std::string name;
	Json::Value value{Json::nullValue}; ///< number when computable, else null
	std::string reason;                 ///< why the value is unavailable ("" when computable)
	std::string notes;
};

struct CaseEvaluation
{
	std::string caseId;
	std::string traceId;
	std::string caseDigest;
	BenchVerdict verdict = BenchVerdict::Fail;
	std::string failureClass; ///< closed taxonomy value
	std::vector<InvariantResult> invariants;
	std::vector<MetricResult> metrics; ///< exactly the 8 metric names, fixed order
	ResourceUsage usage;
	std::string digest; ///< digest of the canonical evaluation document

	/// Lookup helper; nullptr when the name is unknown.
	const MetricResult *metric( const std::string &name ) const;
};

/// Evaluates a trace against a case (runs replay validation and the
/// invariant oracle internally).
CaseEvaluation evaluateCase( const AgentCase &caseValue, const AgentTrace &trace );

/// Canonical versioned document for an evaluation (includes the digest).
Json::Value evaluationToJson( const CaseEvaluation &evaluation );

} // namespace sicnu::agentbench
