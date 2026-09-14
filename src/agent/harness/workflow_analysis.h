// src/agent/harness/workflow_analysis.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): static analysis over a
// normalized WorkflowIR.
//
// Compiler-like checks BEFORE anything executes. Every check is a pure
// function over typed facts: the IR document, the resolved
// DatasetUnderstanding documents of its input slots, the capability knowledge
// layer, and (when recorded) model contracts. The analyzer never opens files
// and never executes operators.
//
// Fact discipline (harness 7.0 rule, kept): facts narrow checks, they never
// fake them. A check whose facts are unknown emits no issue — it is reported
// in `checks` with status "skip" — while a check whose facts CONTRADICT the
// capability contract emits a typed issue. Issues backed only by
// assumed/derived facts degrade to warnings; observed/declared contradictions
// are errors.
//
// Issue codes reuse the stable harness taxonomy; the compiler-specific codes
// (WAVELENGTH_INCOMPATIBLE, TEMPORAL_MISALIGNMENT, CATEGORICAL_MISMATCH,
// RESOURCE_OVER_BUDGET, OUTPUT_PATH_COLLISION, NONDETERMINISTIC_CHAIN,
// FACT_CONFLICT) are appended additively there.
//

#include <json/json.h>
#include <map>
#include <string>
#include <vector>

#include "harness_error.h"
#include "workflow_ir.h"

namespace sicnu::agent::harness {

/// The compiler's new error codes are declared in the CANONICAL taxonomy
/// (harness_error.h, error_codes namespace) — one vocabulary, one table.

/// One finding. `severity` is "error" (blocks execution until repaired or
/// refused) or "warning" (an assumption the caller must see). `repairable`
/// marks issues the closed repair table may resolve when the facts suffice.
struct IrIssue
{
    std::string code;
    std::string severity;   ///< "error" | "warning"
    std::string node;       ///< offending node id ("" = document level)
    std::string port;       ///< offending input port ("" = n/a)
    std::string message;
    bool repairable = false;
    Json::Value details{Json::objectValue};

    Json::Value toJson() const;
};

/// Result of one analysis pass. Deterministic: same IR + same facts -> same
/// analysis, byte-identical (issue order: code, then node, then port).
struct IrAnalysis
{
    /// "ok" | "fixable" | "blocked" — mirrors the preflight vocabulary.
    std::string verdict;
    std::vector<IrIssue> issues;
    /// Per-check ledger: [{check, status: pass|fail|warn|skip, code?, details?}]
    Json::Value checks{Json::arrayValue};
    /// Per-slot merged artifact facts + fact_status (the grounding result the
    /// analysis consumed; echoed so callers can audit every verdict).
    Json::Value facts{Json::objectValue};
    Json::Value factStatus{Json::objectValue};
    /// Fingerprint of the analyzed IR (workflowIrFingerprint of the input).
    std::string irFingerprint;

    Json::Value toJson() const;
    bool blocked() const;
    std::vector<IrIssue> errors() const;
    std::vector<IrIssue> warnings() const;
};

/// Inputs for one analysis pass. `inputFacts` maps a document input slot name
/// to its DatasetUnderstanding document (envelope or body; null/missing =
/// unresolved). `modelContracts` optionally maps a model id to its contract
/// document (as recorded by the grounding tools).
struct IrAnalysisInput
{
    std::map<std::string, Json::Value> inputFacts;
    Json::Value modelContracts{Json::objectValue};
};

/// Analyzes `ir` (normalized in place first — analysis input is always the
/// canonical form). Deterministic and pure.
IrAnalysis analyzeWorkflowIr( WorkflowIr &ir, const IrAnalysisInput &input );

/// Effective artifact facts at one node input edge, given the analysis-time
/// fact environment. Exposed for repair planning (the repair table consumes
/// the same facts the analysis saw — no re-derivation, no drift).
/// Returns the merged facts object; empty when the edge resolves to nothing.
Json::Value effectiveEdgeFacts( const WorkflowIr &ir, const IrNode &node,
                                const IrNodeInput &edge,
                                const IrAnalysisInput &input );

/// Derives the numeric domain from understanding-native facts when the typed
/// numeric_domain key is absent: radiometric_state / SAR calibration.
/// Returns "" when nothing justifies a domain (unknown stays unknown).
std::string deriveNumericDomain( const Json::Value &facts );

/// The canonical wavelength window (nm) for a band role, {low, high}, or an
/// empty array when the role has no window in the closed table.
Json::Value wavelengthWindowForRole( const std::string &role );

} // namespace sicnu::agent::harness
