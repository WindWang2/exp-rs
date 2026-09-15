// src/workflow/workflow_dag_analyzer.h — Kahn concurrency tiers + DFS 3-color cycle diagnosis (D17, ADR 0162)
#pragma once

//
// Static analysis of a WorkflowDefinition 2.0 as a directed graph.
//
//   - analyzeDag: one entry point — acyclicity verdict, Kahn in-degree
//     concurrency tiers, a linear schedule (tiers in order, ids sorted
//     within a tier for determinism), and, for cyclic input, the closed
//     DFS back-edge cycle path [v, ..., v].
//   - detectCycleDFS: 3-color DFS; a Gray hit is a back-edge; the explicit
//     ancestor stack yields the closed cycle sequence.
//   - computeConcurrencyTiers: the tier partition alone.
//   - calculateMaxParallelism: C_max = max_k |T_k|.
//
// Determinism: all orderings are derived from sorted node/edge ids — the
// same document always yields the same analysis. O(V + E) throughout.
//

#include <QString>
#include <QVector>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

struct ConcurrencyTier
{
    int tierIndex = 0;
    QVector<QString> nodeIds; // mutually independent nodes; schedulable in parallel

    bool operator==( const ConcurrencyTier & ) const = default;
};

struct DagAnalysisResult
{
    bool isAcyclic = true;
    QVector<ConcurrencyTier> executionTiers;   // empty when cyclic
    QVector<QString> linearSchedule;           // tier order, sorted within tier
    QVector<QString> cyclePath;                // closed loop [v, ..., v] when cyclic
    QString errorMessage;                      // non-empty when cyclic
};

class WorkflowDagAnalyzer
{
  public:
    WorkflowDagAnalyzer() = delete;

    static DagAnalysisResult analyzeDag( const WorkflowDefinition &def );

    /// True when acyclic; when false, @p outCyclePath receives the closed
    /// loop [v, w1, ..., v] (empty when acyclic).
    static bool detectCycleDFS( const WorkflowDefinition &def, QVector<QString> &outCyclePath );

    /// Kahn in-degree tier partition; empty when the graph is cyclic.
    static QVector<ConcurrencyTier> computeConcurrencyTiers( const WorkflowDefinition &def );

    static int calculateMaxParallelism( const QVector<ConcurrencyTier> &tiers );
};

} // namespace sicnu::workflow
