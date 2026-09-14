// src/agent/harness/workflow_repair.h
#pragma once

//
// Scientific Workflow Compiler 10.0 (ADR 0149): deterministic repair
// insertion over a static-analysis result.
//
// A CLOSED rule table maps analysis issues to repairs. Every rule names the
// EXISTING operator it inserts (the compiler composes capabilities; it never
// ships kernels) and a risk class:
//
//   shape_preserving — geometry/format only (reproject, align). Auto-inserted
//                      with an evidence record whenever the facts suffice.
//   radiometric      — changes pixel semantics in a contract-defined way
//                      (calibration). NEVER auto-inserted under the real
//                      knowledge contracts (DN is warn-class for the optical
//                      families): it becomes a prepared decision refusal that
//                      notes whether observed metadata would back it.
//   science_changing — alters the scientific meaning (QA masking, gap
//                      filling, dataset substitution). NEVER auto-inserted;
//                      becomes a typed decision-required refusal.
//
// Every insertion is recorded (rule, issue, node, facts used) and changes the
// IR fingerprint; refusals carry why + missing_facts. Silent science changes
// are structurally impossible: the only mutations this module performs are
// rule-table insertions, and each one leaves a record.
//
// Determinism: same IR + same analysis + same facts -> byte-identical
// repaired IR (rule application order = the analysis's deterministic issue
// order; inserted node ids are derived deterministically with collision
// suffixes).
//

#include <string>
#include <vector>

#include "harness_error.h"
#include "workflow_analysis.h"
#include "workflow_ir.h"

namespace sicnu::agent::harness {

/// Closed repair risk classes.
namespace repair_risk {
inline constexpr const char *kShapePreserving = "shape_preserving";
inline constexpr const char *kRadiometric = "radiometric";
inline constexpr const char *kScienceChanging = "science_changing";
} // namespace repair_risk

struct IrRepairRuleSpec
{
    std::string ruleId;       ///< stable key, e.g. "align_to_reference"
    std::string issueCode;    ///< analysis issue the rule consumes ("" = opportunity scan)
    std::string riskClass;    ///< repair_risk::* ("" for decision-only rules)
    std::string operatorId;   ///< the operator a fired rule inserts ("" = decision only)
    std::string description;
};

/// The closed rule table, in deterministic order. Drift-pinned by tests.
const std::vector<IrRepairRuleSpec> &repairRuleTable();

bool repairRuleKnown( const std::string &ruleId );

/// Result of one repair pass over the analysis.
struct IrRepairOutcome
{
    WorkflowIr ir;                          ///< repaired copy (equal to input when unchanged)
    std::vector<IrRepairRecord> repairs;    ///< evidence of every insertion
    std::vector<IrRefusal> refusals;        ///< every non-applied (decision-required) repair
    bool changed = false;                   ///< true when the IR was mutated

    Json::Value repairsJson() const;
    Json::Value refusalsJson() const;
};

/// Plans (and, where the risk class allows, applies) repairs for the issues
/// `analysis` reported. Deterministic and pure: no registry writes, no
/// execution, no file I/O. `input` must be the SAME fact environment the
/// analysis consumed (the repair table reasons over what the analysis saw).
IrRepairOutcome planRepairs( const WorkflowIr &ir, const IrAnalysis &analysis,
                             const IrAnalysisInput &input );

/// Re-runs the repair pass on the repaired IR — convenience for callers that
/// want analyze→repair→re-analyze in one step. Returns the repaired IR plus
/// the SECOND analysis (the one that describes the repaired document).
struct IrCompileFixResult
{
    WorkflowIr ir;
    IrAnalysis analysis;      ///< re-analysis of the repaired IR
    std::vector<IrRepairRecord> repairs;
    std::vector<IrRefusal> refusals;
};

/// analyze → repair → re-analyze. The re-analysis is authoritative for the
/// verdict; a repair pass never claims success its own re-analysis contradicts.
IrCompileFixResult analyzeRepairAnalyze( WorkflowIr ir, const IrAnalysisInput &input );

} // namespace sicnu::agent::harness
