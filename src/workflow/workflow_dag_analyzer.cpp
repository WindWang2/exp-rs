// src/workflow/workflow_dag_analyzer.cpp — Kahn tiers + DFS 3-color cycle path (D17)
#include "workflow/workflow_dag_analyzer.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>

namespace sicnu::workflow {
namespace {

QHash<QString, QVector<QString>> adjacency( const WorkflowDefinition &def )
{
    QHash<QString, QVector<QString>> successors;
    for ( const NodeFact &node : def.nodes )
        successors.insert( node.nodeId, {} );
    for ( const EdgeFact &edge : def.edges )
    {
        // analyzeDag is a pure graph pass: it never invents nodes, and an
        // edge naming a ghost node would be a semantics error already
        // reported by WorkflowIR::validateSemantics.
        if ( successors.contains( edge.sourceNodeId ) && successors.contains( edge.targetNodeId ) )
            successors[edge.sourceNodeId].append( edge.targetNodeId );
    }
    // Deterministic traversal regardless of document edge order.
    for ( auto it = successors.begin(); it != successors.end(); ++it )
        std::sort( it->begin(), it->end() );
    return successors;
}

} // namespace

bool WorkflowDagAnalyzer::detectCycleDFS( const WorkflowDefinition &def, QVector<QString> &outCyclePath )
{
    outCyclePath.clear();
    const QHash<QString, QVector<QString>> successors = adjacency( def );

    enum class Color : unsigned char
    {
        White,
        Gray,
        Black
    };
    QHash<QString, Color> color;
    for ( auto it = successors.cbegin(); it != successors.cend(); ++it )
        color.insert( it.key(), Color::White );

    // Iterative DFS with an explicit ancestor chain (recursion depth on
    // hostile graphs is a caller problem, not a stack-overflow one).
    QHash<QString, QString> parent;
    QVector<QString> stack;

    for ( const NodeFact &node : def.nodes )
    {
        if ( color[node.nodeId] != Color::White )
            continue;
        stack = { node.nodeId };
        QSet<QString> onStack{ node.nodeId };

        while ( !stack.isEmpty() )
        {
            const QString current = stack.back();
            if ( color[current] == Color::White )
            {
                color[current] = Color::Gray;
                for ( const QString &next : successors[current] )
                {
                    if ( color[next] == Color::Gray )
                    {
                        // Back edge current -> next: extract the ancestor
                        // chain next..current from the explicit stack, then
                        // close the loop.
                        const int start = stack.indexOf( next );
                        outCyclePath.clear();
                        for ( int i = start; i < stack.size(); ++i )
                            outCyclePath.append( stack[i] );
                        outCyclePath.append( next );
                        return false;
                    }
                    if ( color[next] == Color::White && !onStack.contains( next ) )
                    {
                        // Skip on-stack duplicates: a node pushed twice would
                        // break the ancestor-chain invariant and could yield
                        // a cycle path that is not a walk of the graph.
                        parent[next] = current;
                        stack.append( next );
                        onStack.insert( next );
                    }
                }
            }
            else
            {
                // All children pushed; finished this branch.
                color[current] = Color::Black;
                stack.removeLast();
                onStack.remove( current );
            }
            // A Gray node whose children were all Black stays Gray on the
            // stack until the pass above blackens it — loop continues.
        }
    }
    return true;
}

QVector<ConcurrencyTier> WorkflowDagAnalyzer::computeConcurrencyTiers( const WorkflowDefinition &def )
{
    const QHash<QString, QVector<QString>> successors = adjacency( def );

    QHash<QString, int> inDegree;
    QVector<QString> frontier;
    for ( auto it = successors.cbegin(); it != successors.cend(); ++it )
        inDegree.insert( it.key(), 0 );
    for ( const EdgeFact &edge : def.edges )
        if ( inDegree.contains( edge.targetNodeId ) && inDegree.contains( edge.sourceNodeId ) )
            inDegree[edge.targetNodeId]++;
    for ( auto it = inDegree.cbegin(); it != inDegree.cend(); ++it )
        if ( it.value() == 0 )
            frontier.append( it.key() );
    std::sort( frontier.begin(), frontier.end() );

    QVector<ConcurrencyTier> tiers;
    int processed = 0;
    while ( !frontier.isEmpty() )
    {
        ConcurrencyTier tier;
        tier.tierIndex = tiers.size();
        tier.nodeIds = frontier;
        std::sort( tier.nodeIds.begin(), tier.nodeIds.end() );
        processed += tier.nodeIds.size();

        QVector<QString> next;
        for ( const QString &node : frontier )
            for ( const QString &succ : successors[node] )
                if ( --inDegree[succ] == 0 )
                    next.append( succ );
        std::sort( next.begin(), next.end() );
        // Duplicates cannot occur: a node reaches zero once.
        frontier = next;
        tiers.append( tier );
    }

    // Cyclic residue: tiers only valid for DAGs.
    if ( processed != def.nodes.size() )
        return {};
    return tiers;
}

int WorkflowDagAnalyzer::calculateMaxParallelism( const QVector<ConcurrencyTier> &tiers )
{
    qsizetype widest = 0;
    for ( const ConcurrencyTier &tier : tiers )
        widest = std::max( widest, tier.nodeIds.size() );
    return static_cast<int>( widest );
}

DagAnalysisResult WorkflowDagAnalyzer::analyzeDag( const WorkflowDefinition &def )
{
    DagAnalysisResult result;

    QVector<QString> cyclePath;
    result.isAcyclic = detectCycleDFS( def, cyclePath );
    if ( !result.isAcyclic )
    {
        result.cyclePath = cyclePath;
        QStringList loop = QStringList( cyclePath.cbegin(), cyclePath.cend() );
        result.errorMessage = QStringLiteral( "workflow graph is not a DAG: cycle %1" ).arg( loop.join( QStringLiteral( " -> " ) ) );
        return result;
    }

    result.executionTiers = computeConcurrencyTiers( def );
    for ( const ConcurrencyTier &tier : result.executionTiers )
        result.linearSchedule += tier.nodeIds;
    return result;
}

} // namespace sicnu::workflow
