// src/workflow/workflow_repair_engine.h — closed-form adapter injection (D17, ADR 0162)
#pragma once

//
// Deterministic repair over contract violations. The rule table is CLOSED:
//
//   CrsMismatch              -> rs:reproject {target_crs, resampling:bilinear}
//   ResolutionMismatch       -> rs:resample {target_resolution_x/y, resampling:bilinear}
//   RadiometricStateMismatch -> DN -> Radiance: rs:radiometric_calibration
//                               DN/TOA -> TOA|BOA: rs:radiometric_calibration
//                                 then rs:atmospheric_correction
//                               Radiance -> TOA|BOA: rs:atmospheric_correction
//   DataTypeMismatch         -> rs:convert_dtype {target_data_type}
//
// Repair invariant (test-pinned): inspectContracts(applyRepairPlan(W,
// inferRepairs(W))) is empty. Same workflow -> byte-identical repaired
// workflow (violations enumerate deterministically; adapter node ids are
// derived from the violated edge id + operator, with collision suffixes).
//

#include <QJsonObject>
#include <QVector>

#include "workflow/contract_checker.h"
#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

struct RepairAction
{
    QString ruleId;           ///< stable rule key, e.g. "rule_crs_auto_reproject"
    QString insertOperatorId; ///< e.g. "rs:reproject"
    QJsonObject adapterParameters;
    QString targetEdgeId;     ///< the violated edge this action rewrites
    QString newSourcePort;    ///< final new edge's source port (post chain)
    QString newTargetPort;    ///< final new edge's target port (post chain)
};

struct RepairPlan
{
    bool requiresRepair = false;
    QVector<ContractViolation> violations;
    QVector<RepairAction> suggestedActions;
    WorkflowDefinition repairedWorkflow; // populated by applyRepairPlan
};

class WorkflowRepairEngine
{
  public:
    WorkflowRepairEngine() = delete;

    /// Same as the free function (single implementation, two spellings).
    static QVector<ContractViolation> inspectContracts( const WorkflowDefinition &def );

    /// Maps every violation onto its closed-form repair actions, in
    /// application order (calibration before atmospheric correction, etc.).
    static RepairPlan inferRepairs( const WorkflowDefinition &def );

    /// Rewires the workflow: each action chain replaces its violated edge
    /// with adapter nodes source -> a1 -> ... -> target. Deterministic ids.
    static WorkflowDefinition applyRepairPlan( const WorkflowDefinition &def, const RepairPlan &plan );
};

} // namespace sicnu::workflow
