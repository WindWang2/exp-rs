// src/workflow/plan_optimizer.h — SHA-256 lineage signatures, DNE, CSE (D17, ADR 0162)
#pragma once

//
// Compiles a WorkflowDocument down to the nodes that actually contribute
// to the requested sinks:
//
//   - computeNodeSignature: H(v) = SHA-256( opId ‖ canonical(params) ‖
//     ⊕ (targetPort ‖ sourceNodeId ‖ H(parent)) with incoming edges sorted
//     by (targetPort, sourceNodeId) ). Deterministic and order-insensitive
//     to edge enumeration; parameter-, operator-, and port-sensitive.
//     Signatures drive CSE and cache-hit reuse.
//   - optimizePlan: Dead Node Elimination keeps only the ancestors of the
//     requested sinks; Common Subexpression Elimination merges nodes with
//     equal signatures (first occurrence in document order wins) and
//     redirects every outgoing edge. Edges among surviving nodes are
//     preserved.
//   - The OptimizationReport records pruned/merged counts and every cache
//     hit (signature present in the optional disk-signature map).
//

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

struct CostEstimate
{
    double totalFlops = 0.0;
    qint64 peakRssBytes = 0;
    double estimatedDurationSeconds = 0.0;
    int recommendedMaxParallelism = 2;
};

struct OptimizationReport
{
    int deadNodesPruned = 0;
    int commonSubexpressionsMerged = 0;
    QVector<QString> cachedNodesHit;      // node ids whose signature hit the cache map
    CostEstimate originalCost;            // populated when a cost estimator callback is given
    CostEstimate optimizedCost;
};

class WorkflowPlanOptimizer
{
  public:
    WorkflowPlanOptimizer() = delete;

    /// Content signature of one node given its parents' signatures and the
    /// incoming port wiring. @p incomingPorts is a sorted list of
    /// (targetPortName, sourceNodeId) pairs — port names are hashed so
    /// swapped-edge rewires cannot collide (#1077). 64 lowercase hex
    /// characters (SHA-256).
    static QString computeNodeSignature( const NodeFact &node,
                                         const QMap<QString, QString> &parentSignatures,
                                         const QVector<QPair<QString, QString>> &incomingPorts = {} );

    /// Full lineage signatures for the whole graph: parents-first (the map
    /// value is the node's signature; input is the accumulated map).
    static QMap<QString, QString> computeLineageSignatures( const WorkflowDocument &def );

    /// DNE + CSE. @p targetSinkNodeIds must resolve to existing nodes.
    /// @p cachedSignatures (optional, e.g. from a previous run's checkpoint)
    /// marks nodes whose artifact can be reused; they are kept and annotated
    /// in the report. Deterministic: same inputs -> identical output graph.
    static WorkflowDocument optimizePlan( const WorkflowDocument &def,
                                            const QSet<QString> &targetSinkNodeIds,
                                            OptimizationReport *outReport = nullptr,
                                            const QSet<QString> &cachedSignatures = QSet<QString>() );
};

} // namespace sicnu::workflow
