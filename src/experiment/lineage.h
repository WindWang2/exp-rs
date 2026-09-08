// lineage.h — queryable lineage over dataset + experiment stores
// (goal §28, ADR 0138).
//
// DerivationRecord/ArtifactStore stay the provenance AUTHORITIES for assets
// and runs; this layer joins their edges with the dataset- and
// experiment-side edges into one traversable graph. Nodes are (kind, id)
// pairs; missing endpoints become typed TOMBSTONES (never silently
// dropped), cycles are cut by a visited set, and traversal is bounded by
// depth and node budget (goal §37).
#pragma once

#include "../dataset/dataset_store.h"
#include "experiment_store.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace sicnu::experiment
{

/// Node kinds of the unified lineage vocabulary (ADR 0138). Loose string
/// refs by design — the stores stay authoritative and independent.
struct LineageNodeId
{
    QString kind; // asset|dataset|dataset_version|sample|annotation|split|experiment|run|artifact|metric
    QString id;

    bool isValid() const { return !kind.isEmpty() && !id.isEmpty(); }
    QString key() const { return kind + QLatin1Char( ':' ) + id; }
    friend bool operator==( const LineageNodeId &, const LineageNodeId & ) = default;
};

struct LineageEdgeRecord
{
    LineageNodeId from;
    LineageNodeId to;
    QString edgeKind; // derived_from|snapshot_of|evaluated_on|produced|consumed|…
};

/// A traversed node: its identity, whether any store still knows it
/// (dangling/tombstone otherwise), and the edge it was reached by.
struct LineageNode
{
    LineageNodeId id;
    bool dangling = false;
    int depth = 0;
    QString viaEdgeKind;
    LineageNodeId viaFrom;
};

struct LineageQueryResult
{
    QVector<LineageNode> nodes;       // visited nodes, start node excluded
    QVector<LineageEdgeRecord> edges; // traversed edges
    bool budgetExhausted = false;     ///< node budget hit (result is a prefix)
    QJsonObject toJson() const;
};

class LineageGraph
{
  public:
    /// Assembles the graph over the two stores. @p knownNodes optionally
    /// carries ids the caller vouches for (e.g. DataManager assets); anything
    /// referenced but absent from every store stays reachable as a tombstone.
    LineageGraph( const sicnu::dataset::DatasetStore &datasetStore,
                  const ExperimentStore &experimentStore );

    void addEdge( const LineageNodeId &from, const QString &edgeKind, const LineageNodeId &to );

    /// Existence resolver: true when the store layer currently knows the
    /// node (row present). Wired by the integration layer; when unset,
    /// dangling flags stay false and reports say "existence not checked" —
    /// the graph never fabricates tombstones it cannot verify.
    void setExistenceResolver( const std::function<bool( const LineageNodeId & )> &resolver )
    {
        m_resolver = resolver;
    }

    /// Upstream closure (ancestors/inputs). Cycle-safe, bounded.
    LineageQueryResult ancestors( const LineageNodeId &start, int maxDepth = 16,
                                  qint64 maxNodes = 1000 ) const;
    /// Downstream closure (descendants/consumers). Cycle-safe, bounded.
    LineageQueryResult descendants( const LineageNodeId &start, int maxDepth = 16,
                                    qint64 maxNodes = 1000 ) const;
    /// Direct neighbors only.
    QVector<LineageEdgeRecord> edgesOf( const LineageNodeId &node ) const;

    qint64 edgeCount() const { return m_edges.size(); }

  private:
    QVector<LineageEdgeRecord> m_edges;
    QHash<QString, QVector<int>> m_outgoing; // from-key → edge indexes
    QHash<QString, QVector<int>> m_incoming; // to-key → edge indexes
    std::function<bool( const LineageNodeId & )> m_resolver;
    // Nodes with at least one edge (existence witness): any id NOT here and
    // not resolvable is a tombstone when reached.
    QSet<QString> m_referencedNodes;
};

} // namespace sicnu::experiment
