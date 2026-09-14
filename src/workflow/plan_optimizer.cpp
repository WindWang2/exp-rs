// src/workflow/plan_optimizer.cpp — SHA-256 lineage, DNE, CSE (D17)
#include "workflow/plan_optimizer.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace sicnu::workflow {
namespace {

QString canonicalJson( const QJsonObject &object )
{
    // QJsonObject iterates keys in sorted order; compact document == canonical.
    return QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
}

QVector<QString> parentsOf( const WorkflowDefinition &def, const QString &nodeId )
{
    QVector<QString> parents;
    for ( const EdgeFact &edge : def.edges )
        if ( edge.targetNodeId == nodeId && !parents.contains( edge.sourceNodeId ) )
            parents.append( edge.sourceNodeId );
    std::sort( parents.begin(), parents.end() );
    return parents;
}

QVector<QString> childrenOf( const WorkflowDefinition &def, const QString &nodeId )
{
    QVector<QString> children;
    for ( const EdgeFact &edge : def.edges )
        if ( edge.sourceNodeId == nodeId && !children.contains( edge.targetNodeId ) )
            children.append( edge.targetNodeId );
    std::sort( children.begin(), children.end() );
    return children;
}

} // namespace

QString WorkflowPlanOptimizer::computeNodeSignature( const NodeFact &node,
                                                     const QMap<QString, QString> &parentSignatures )
{
    // Parents concatenated in sorted node-id order — the ⊕^Sorted clause.
    QStringList parentIds;
    for ( auto it = parentSignatures.cbegin(); it != parentSignatures.cend(); ++it )
        parentIds << it.key();

    QString lineage = node.operatorId;
    lineage += QLatin1Char( '\x1f' );
    lineage += canonicalJson( node.parameters );
    for ( const QString &parentId : parentIds )
    {
        lineage += QLatin1Char( '\x1f' );
        lineage += parentSignatures.value( parentId );
    }

    const QByteArray digest = QCryptographicHash::hash( lineage.toUtf8(), QCryptographicHash::Sha256 );
    return QString::fromLatin1( digest.toHex() );
}

QMap<QString, QString> WorkflowPlanOptimizer::computeLineageSignatures( const WorkflowDefinition &def )
{
    QMap<QString, QString> signatures;
    // Parents-first memoized fill: parent ids always sort before children
    // once computed; cycles are rejected upstream by the DAG analyzer, so a
    // simple fixed-point sweep suffices and cannot spin.
    while ( signatures.size() < def.nodes.size() )
    {
        bool progressed = false;
        for ( const NodeFact &node : def.nodes )
        {
            if ( signatures.contains( node.nodeId ) )
                continue;
            QMap<QString, QString> parentSigs;
            bool parentsReady = true;
            for ( const QString &parent : parentsOf( def, node.nodeId ) )
            {
                if ( !signatures.contains( parent ) )
                {
                    parentsReady = false;
                    break;
                }
                parentSigs.insert( parent, signatures.value( parent ) );
            }
            if ( !parentsReady )
                continue;
            signatures.insert( node.nodeId, computeNodeSignature( node, parentSigs ) );
            progressed = true;
        }
        if ( !progressed )
            break; // unreachable on acyclic input; fail safe rather than spin
    }
    return signatures;
}

WorkflowDefinition WorkflowPlanOptimizer::optimizePlan( const WorkflowDefinition &def,
                                                        const QSet<QString> &targetSinkNodeIds,
                                                        OptimizationReport *outReport,
                                                        const QSet<QString> &cachedSignatures )
{
    OptimizationReport report;

    // ---- Dead Node Elimination: ancestors of the sinks, sinks included.
    QHash<QString, QVector<QString>> children;
    for ( const EdgeFact &edge : def.edges )
        children[edge.sourceNodeId].append( edge.targetNodeId );

    QSet<QString> active;
    std::function<void( const QString & )> markActive = [&]( const QString &nodeId ) {
        if ( active.contains( nodeId ) )
            return;
        active.insert( nodeId );
        for ( const EdgeFact &edge : def.edges )
            if ( edge.targetNodeId == nodeId )
                markActive( edge.sourceNodeId );
    };
    for ( const QString &sink : targetSinkNodeIds )
        if ( def.findNode( sink ) )
            markActive( sink );

    QVector<NodeFact> keptNodes;
    for ( const NodeFact &node : def.nodes )
        if ( active.contains( node.nodeId ) )
            keptNodes.append( node );
    report.deadNodesPruned = def.nodes.size() - keptNodes.size();

    // ---- Common Subexpression Elimination over lineage signatures.
    WorkflowDefinition working = def;
    working.nodes = keptNodes;
    working.edges.clear();
    for ( const EdgeFact &edge : def.edges )
        if ( active.contains( edge.sourceNodeId ) && active.contains( edge.targetNodeId ) )
            working.edges.append( edge );

    const QMap<QString, QString> signatures = computeLineageSignatures( working );

    QHash<QString, QString> canonicalOf; // signature -> surviving node id
    QHash<QString, QString> mergedInto;  // merged node -> canonical node
    QVector<NodeFact> uniqueNodes;
    for ( const NodeFact &node : working.nodes ) // document order: first wins
    {
        const QString signature = signatures.value( node.nodeId );
        const auto existing = canonicalOf.find( signature );
        if ( existing != canonicalOf.end() && !cachedSignatures.contains( signature ) )
        {
            mergedInto.insert( node.nodeId, existing.value() );
            ++report.commonSubexpressionsMerged;
            continue;
        }
        if ( existing == canonicalOf.end() )
            canonicalOf.insert( signature, node.nodeId );
        uniqueNodes.append( node );
    }

    // Redirect edges out of merged nodes onto their canonical twin; drop
    // edges that became degenerate self-loops.
    QVector<EdgeFact> finalEdges;
    QSet<QString> edgeIds;
    auto resolve = [&mergedInto]( QString id ) {
        while ( mergedInto.contains( id ) )
            id = mergedInto.value( id );
        return id;
    };
    QSet<QPair<QString, QString>> parallelSeen;
    for ( const EdgeFact &edge : working.edges )
    {
        EdgeFact redirected = edge;
        redirected.sourceNodeId = resolve( edge.sourceNodeId );
        redirected.targetNodeId = resolve( edge.targetNodeId );
        if ( redirected.sourceNodeId == redirected.targetNodeId )
            continue; // degenerate self-loop after merging
        // Two twins feeding the same consumer collapse to ONE edge.
        const auto parallelKey = qMakePair( redirected.sourceNodeId, redirected.targetNodeId );
        if ( parallelSeen.contains( parallelKey ) )
            continue;
        parallelSeen.insert( parallelKey );
        if ( edgeIds.contains( redirected.edgeId ) )
            continue;
        edgeIds.insert( redirected.edgeId );
        finalEdges.append( redirected );
    }

    WorkflowDefinition optimized = def;
    optimized.nodes = uniqueNodes;
    optimized.edges = finalEdges;

    // ---- Cache-hit annotation: signatures present in the reuse set.
    for ( const NodeFact &node : optimized.nodes )
        if ( cachedSignatures.contains( signatures.value( node.nodeId ) ) )
            report.cachedNodesHit.append( node.nodeId );

    if ( outReport )
        *outReport = report;
    return optimized;
}

} // namespace sicnu::workflow
