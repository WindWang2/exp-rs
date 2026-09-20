// src/workflow/workflow_provenance.h — queryable run lineage graph (D17, flash-workflow-engine-12)
#pragma once

//
// WP5 provenance: every finished run emits one provenance_<runId>.json —
// a small directed bipartite graph of executions and artifacts.
//
//   node kinds   "run"      one per record: carries workflowId + the
//                           deterministic plan signature.
//                "nodeExec" one per graph node: state, lineage signature,
//                           elapsed time, cacheHit flag, originNodeId.
//                "artifact" one per output path: fingerprint + size — the
//                           identity the resume contract re-verifies.
//
//   edge kinds   "consumed"   nodeExec -> artifact (parent output fed in)
//                "produced"   nodeExec -> artifact (this run wrote it)
//                "reusedFrom" nodeExec -> artifact (cache hit: verified and
//                             served, NOT produced by this execution)
//
// Lineage is transitive through artifact nodes: consumer -consumed-> A
// <-produced- producer. The graph serializes with sorted nodes and edges,
// so the same run state always yields a byte-identical record. The file
// envelope mirrors the checkpoint discipline: kind + closed version set,
// bounded reads, edge endpoints must resolve.
//

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "workflow/workflow_ir_v2.h"
#include "workflow/pipeline_run_coordinator.h"

namespace sicnu::workflow {

struct ProvenanceNode
{
    QString id;      // "run:<id>" | "node:<nodeId>" | "artifact:<path>"
    QString kind;    // "run" | "nodeExec" | "artifact"
    QJsonObject attributes;

    bool operator==( const ProvenanceNode & ) const = default;
};

struct ProvenanceEdge
{
    QString fromId;
    QString toId;
    QString kind; // "consumed" | "produced" | "reusedFrom"

    bool operator==( const ProvenanceEdge & ) const = default;
};

class ProvenanceGraph
{
  public:
    const QVector<ProvenanceNode> &nodes() const { return m_nodes; }
    const QVector<ProvenanceEdge> &edges() const { return m_edges; }

    void addNode( ProvenanceNode node );
    void addEdge( ProvenanceEdge edge );

    /// Queries (id-based; return empty when absent):
    /// producer of an artifact node, artifacts a nodeExec consumed or
    /// produced, and artifacts a cache-hit nodeExec reused.
    QString producerOf( const QString &artifactNodeId ) const;
    QStringList consumedBy( const QString &nodeExecId ) const;
    QStringList producedBy( const QString &nodeExecId ) const;
    QStringList reusedBy( const QString &nodeExecId ) const;

    /// Canonical JSON: kind "d17_provenance", version "1.0", nodes/edges
    /// sorted by (id) / (from,to,kind) — byte-stable for identical input.
    QJsonObject toJson() const;

    /// Strict parse: envelope gate, node/edge fields, dangling-edge refusal.
    static Result<ProvenanceGraph> fromJson( const QJsonObject &doc );

    /// Builds the graph for one finished/terminated run. @p statuses maps
    /// nodeId -> terminal snapshot; @p planSignature is the WP6 topology
    /// digest stored on the run node.
    static ProvenanceGraph fromRunState( const QString &runId,
                                         const WorkflowDocument &def,
                                         const QHash<QString, NodeStatusSnapshot> &statuses,
                                         const QString &planSignature );

  private:
    QVector<ProvenanceNode> m_nodes;
    QVector<ProvenanceEdge> m_edges;
};

} // namespace sicnu::workflow
