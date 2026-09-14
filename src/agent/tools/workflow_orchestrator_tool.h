// src/agent/tools/workflow_orchestrator_tool.h — deterministic NL->DAG + self-healing (D17, ADR 0162)
#pragma once

//
// The agent-facing seam of the D17 designer stack. Pure, offline, closed:
//
//   - getToolJsonSchema(): the Draft-07 tool description (goal, sensor,
//     region, inputs) for the agent tool catalog.
//   - compileGoalToWorkflow(): production rules over (goal keywords ×
//     sensor) produce a WorkflowDefinition 2.0 — the classic intents
//     (calibrate+index, water extraction, change detection, fusion,
//     classification) map to fixed operator chains with contract-correct
//     port facts. Deterministic: same request -> identical document.
//   - healWorkflow(): pattern-matches an execution error log (PROJ/GDAL CRS
//     text, resolution text) onto the contract repair engine and returns
//     the healed workflow plus the applied rule ids. Unknown errors yield
//     isSuccess=false — never a fake heal.
//

#include <QJsonObject>
#include <QVector>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::agent::tools {

struct AutonomousCompileRequest
{
    QString userNaturalLanguageGoal;
    QString sensorType;      // "GF-1" | "Landsat8" | "Sentinel2" | ...
    QString targetRegionBbox;
    QVector<QString> inputFiles;
};

struct AutonomousCompileResult
{
    bool isSuccess = false;
    sicnu::workflow::WorkflowDefinition workflow;
    QVector<QString> injectedRepairRules; // from healWorkflow; empty on compile
    QString textualExplanation;
};

class WorkflowOrchestratorTool
{
  public:
    WorkflowOrchestratorTool() = delete;

    static QJsonObject getToolJsonSchema();

    static AutonomousCompileResult compileGoalToWorkflow( const AutonomousCompileRequest &request );

    static AutonomousCompileResult healWorkflow( const sicnu::workflow::WorkflowDefinition &brokenWorkflow,
                                                 const QString &executionErrorLog );
};

} // namespace sicnu::agent::tools
