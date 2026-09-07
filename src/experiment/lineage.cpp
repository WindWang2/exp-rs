// lineage.cpp — graph assembly + bounded traversal.
#include "lineage.h"

#include <QJsonArray>

#include <algorithm>
#include <functional>

namespace sicnu::experiment
{

using sicnu::dataset::DatasetId;
using sicnu::dataset::DatasetStore;

namespace
{

void appendEdges( QVector<LineageEdgeRecord> &edges, QHash<QString, QVector<int>> &outgoing,
                  QHash<QString, QVector<int>> &incoming, QSet<QString> &referenced,
                  const LineageEdgeRecord &edge )
{
    const int index = edges.size();
    edges.append( edge );
    outgoing[edge.from.key()].append( index );
    incoming[edge.to.key()].append( index );
    referenced.insert( edge.from.key() );
    referenced.insert( edge.to.key() );
}

void appendEdgesFrom( const QVector<DatasetStore::LineageEdge> &raw,
                      QVector<LineageEdgeRecord> &edges,
                      QHash<QString, QVector<int>> &outgoing,
                      QHash<QString, QVector<int>> &incoming, QSet<QString> &referenced )
{
    for ( const auto &edge : raw )
    {
        LineageEdgeRecord record;
        record.from = { edge.fromKind, edge.fromId };
        record.to = { edge.toKind, edge.toId };
        record.edgeKind = edge.edgeKind;
        appendEdges( edges, outgoing, incoming, referenced, record );
    }
}

void appendEdgesFrom( const QVector<ExperimentStore::LineageEdge> &raw,
                      QVector<LineageEdgeRecord> &edges,
                      QHash<QString, QVector<int>> &outgoing,
                      QHash<QString, QVector<int>> &incoming, QSet<QString> &referenced )
{
    for ( const auto &edge : raw )
    {
        LineageEdgeRecord record;
        record.from = { edge.fromKind, edge.fromId };
        record.to = { edge.toKind, edge.toId };
        record.edgeKind = edge.edgeKind;
        appendEdges( edges, outgoing, incoming, referenced, record );
    }
}

LineageQueryResult traverse( const LineageNodeId &start, bool upstream,
                             const QVector<LineageEdgeRecord> &edges,
                             const QHash<QString, QVector<int>> &outgoing,
                             const QHash<QString, QVector<int>> &incoming,
                             const QSet<QString> &referenced,
                             const std::function<bool( const LineageNodeId & )> &resolver,
                             int maxDepth, qint64 maxNodes )
{
    LineageQueryResult result;
    QHash<QString, int> depthOf;
    QVector<LineageNodeId> frontier{ start };
    depthOf.insert( start.key(), 0 );
    int depth = 0;
    while ( !frontier.isEmpty() && depth < maxDepth )
    {
        QVector<LineageNodeId> next;
        for ( const LineageNodeId &node : frontier )
        {
            // Edge direction: from = upstream/producer, to = downstream.
            // Ancestors therefore walk INCOMING edges.
            const QVector<int> &edgeIndexes =
                upstream ? incoming.value( node.key() ) : outgoing.value( node.key() );
            for ( const int edgeIndex : edgeIndexes )
            {
                const LineageEdgeRecord &edge = edges.at( edgeIndex );
                const LineageNodeId neighbor = upstream ? edge.from : edge.to;
                result.edges.append( edge );
                if ( depthOf.contains( neighbor.key() ) )
                    continue; // cycle / already visited
                if ( qint64( result.nodes.size() ) >= maxNodes )
                {
                    result.budgetExhausted = true;
                    return result;
                }
                LineageNode visited;
                visited.id = neighbor;
                visited.depth = depth + 1;
                visited.viaEdgeKind = edge.edgeKind;
                visited.viaFrom = upstream ? edge.to : edge.from;
                visited.dangling = resolver && !resolver( neighbor );
                result.nodes.append( visited );
                depthOf.insert( neighbor.key(), depth + 1 );
                next.append( neighbor );
            }
        }
        frontier = next;
        ++depth;
    }
    std::sort( result.nodes.begin(), result.nodes.end(),
               []( const LineageNode &a, const LineageNode &b ) {
                   if ( a.depth != b.depth )
                       return a.depth < b.depth;
                   return a.id.key() < b.id.key();
               } );
    std::sort( result.edges.begin(), result.edges.end(),
               []( const LineageEdgeRecord &a, const LineageEdgeRecord &b ) {
                   return a.from.key() + a.edgeKind + a.to.key() <
                          b.from.key() + b.edgeKind + b.to.key();
               } );
    return result;
}

} // namespace

LineageGraph::LineageGraph( const DatasetStore &datasetStore, const ExperimentStore &experimentStore )
{
    // Bulk scan: edges referencing phantom nodes (a deleted run, an external
    // asset) MUST enter the graph so traversal can surface them as
    // tombstones — anchor-based pulls would silently drop exactly the
    // dangling lineage this layer exists to expose.
    appendEdgesFrom( datasetStore.allLineageEdges(), m_edges, m_outgoing, m_incoming,
                     m_referencedNodes );
    appendEdgesFrom( experimentStore.allLineageEdges(), m_edges, m_outgoing, m_incoming,
                     m_referencedNodes );
}

void LineageGraph::addEdge( const LineageNodeId &from, const QString &edgeKind,
                            const LineageNodeId &to )
{
    if ( !from.isValid() || !to.isValid() || edgeKind.isEmpty() )
        return;
    LineageEdgeRecord edge;
    edge.from = from;
    edge.to = to;
    edge.edgeKind = edgeKind;
    appendEdges( m_edges, m_outgoing, m_incoming, m_referencedNodes, edge );
}

LineageQueryResult LineageGraph::ancestors( const LineageNodeId &start, int maxDepth,
                                            qint64 maxNodes ) const
{
    return traverse( start, true, m_edges, m_outgoing, m_incoming, m_referencedNodes, m_resolver,
                     maxDepth, maxNodes );
}

LineageQueryResult LineageGraph::descendants( const LineageNodeId &start, int maxDepth,
                                              qint64 maxNodes ) const
{
    return traverse( start, false, m_edges, m_outgoing, m_incoming, m_referencedNodes, m_resolver,
                     maxDepth, maxNodes );
}

QVector<LineageEdgeRecord> LineageGraph::edgesOf( const LineageNodeId &node ) const
{
    QVector<LineageEdgeRecord> result;
    for ( const int index : m_outgoing.value( node.key() ) )
        result.append( m_edges.at( index ) );
    for ( const int index : m_incoming.value( node.key() ) )
        result.append( m_edges.at( index ) );
    return result;
}

QJsonObject LineageQueryResult::toJson() const
{
    QJsonObject json;
    QJsonArray nodeArray;
    for ( const LineageNode &node : nodes )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "kind" ), node.id.kind );
        item.insert( QStringLiteral( "id" ), node.id.id );
        item.insert( QStringLiteral( "dangling" ), node.dangling );
        item.insert( QStringLiteral( "depth" ), node.depth );
        if ( !node.viaEdgeKind.isEmpty() )
            item.insert( QStringLiteral( "via_edge" ), node.viaEdgeKind );
        nodeArray.append( item );
    }
    json.insert( QStringLiteral( "nodes" ), nodeArray );
    QJsonArray edgeArray;
    for ( const LineageEdgeRecord &edge : edges )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "from_kind" ), edge.from.kind );
        item.insert( QStringLiteral( "from_id" ), edge.from.id );
        item.insert( QStringLiteral( "edge" ), edge.edgeKind );
        item.insert( QStringLiteral( "to_kind" ), edge.to.kind );
        item.insert( QStringLiteral( "to_id" ), edge.to.id );
        edgeArray.append( item );
    }
    json.insert( QStringLiteral( "edges" ), edgeArray );
    json.insert( QStringLiteral( "budget_exhausted" ), budgetExhausted );
    return json;
}

} // namespace sicnu::experiment
