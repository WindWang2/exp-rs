// src/workflow/workflow_repair_engine.h — closed-form adapter injection (D17, ADR 0162)
#pragma once

//
// Deterministic repair over contract violations. The rule table is CLOSED:
//
//   CrsMismatch              -> gdal:reproject {dstCrs, resampling:bilinear}
//   ResolutionMismatch       -> rs:resample {resolution, resampling:bilinear}
//                               (square cells only; a non-square expected
//                               resolution has no closed-form adapter)
//   RadiometricStateMismatch -> DN -> Radiance: rs:radiometric_calibration
//                               DN -> TOA|BOA: rs:radiometric_calibration
//                                 then rs:atmospheric_correction
//                               Radiance -> TOA|BOA: rs:atmospheric_correction
//                               (TOA -> BOA is same-rank, never a violation)
//   DataTypeMismatch         -> no closed rule: no registered operator
//                               performs dtype conversion, so the violation
//                               is reported without an auto-action
//   DimensionMismatch        -> no closed rule (not emitted today)
//
// Every insertOperatorId resolves in the live RSOperatorRegistry.
//
// Repair invariant (test-pinned): for violations WITH a closed rule,
// inspectContracts(applyRepairPlan(W, inferRepairs(W))) is empty. Same
// workflow -> byte-identical repaired workflow (violations enumerate
// deterministically; adapter node ids are derived from the violated edge id
// + operator, with collision suffixes).
//

#include <QJsonObject>
#include <QVector>

#include "workflow/contract_checker.h"
#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

struct RepairAction
{
    QString ruleId;           ///< stable rule key, e.g. "rule_crs_auto_reproject"
    QString insertOperatorId; ///< e.g. "gdal:reproject"
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
    WorkflowDocument repairedWorkflow; // populated by applyRepairPlan
};

class WorkflowRepairEngine
{
  public:
    WorkflowRepairEngine() = delete;

    /// Same as the free function (single implementation, two spellings).
    static QVector<ContractViolation> inspectContracts( const WorkflowDocument &def );

    /// Maps every violation onto its closed-form repair actions, in
    /// application order (calibration before atmospheric correction, etc.).
    static RepairPlan inferRepairs( const WorkflowDocument &def );

    /// Rewires the workflow: each action chain replaces its violated edge
    /// with adapter nodes source -> a1 -> ... -> target. Deterministic ids.
    static WorkflowDocument applyRepairPlan( const WorkflowDocument &def, const RepairPlan &plan );
};

} // namespace sicnu::workflow
