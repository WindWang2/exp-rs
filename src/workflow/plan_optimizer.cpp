// src/workflow/plan_optimizer.cpp — SHA-256 lineage, DNE, CSE (D17)
#include "workflow/plan_optimizer.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace sicnu::workflow {
namespace {

QString canonicalJson( const QJsonObject &object )
{
    // QJsonObject iterates keys in sorted order; compact document == canonical.
    return QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
}

QVector<QString> parentsOf( const WorkflowDocument &def, const QString &nodeId )
{
    QVector<QString> parents;
    for ( const EdgeFact &edge : def.edges )
        if ( edge.targetNodeId == nodeId && !parents.contains( edge.sourceNodeId ) )
            parents.append( edge.sourceNodeId );
    std::sort( parents.begin(), parents.end() );
    return parents;
}

QVector<QString> childrenOf( const WorkflowDocument &def, const QString &nodeId )
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
                                                     const QMap<QString, QString> &parentSignatures,
                                                     const QVector<QPair<QString, QString>> &incomingPorts )
{
    QString lineage = node.operatorId;
    lineage += QLatin1Char( '\x1f' );
    lineage += canonicalJson( node.parameters );
    if ( !incomingPorts.isEmpty() )
    {
        // Port-sensitive lineage (#1077): each incoming edge contributes
        // (targetPortName, sourceNodeId, parentSignature). Caller sorts.
        for ( const auto &port : incomingPorts )
        {
            lineage += QLatin1Char( '\x1f' );
            lineage += port.first;   // targetPortName
            lineage += QLatin1Char( '\x1f' );
            lineage += port.second;  // sourceNodeId
            lineage += QLatin1Char( '\x1f' );
            lineage += parentSignatures.value( port.second );
        }
    }
    else
    {
        // Legacy / parentless path: parents concatenated in sorted node-id
        // order — keeps pinned digests for root nodes and unit tests.
        QStringList parentIds;
        for ( auto it = parentSignatures.cbegin(); it != parentSignatures.cend(); ++it )
            parentIds << it.key();
        for ( const QString &parentId : parentIds )
        {
            lineage += QLatin1Char( '\x1f' );
            lineage += parentSignatures.value( parentId );
        }
    }

    const QByteArray digest = QCryptographicHash::hash( lineage.toUtf8(), QCryptographicHash::Sha256 );
    return QString::fromLatin1( digest.toHex() );
}

QMap<QString, QString> WorkflowPlanOptimizer::computeLineageSignatures( const WorkflowDocument &def )
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
            // Fold every incoming edge's (targetPort, sourceNodeId) so a
            // swapped-port rewire cannot share a cache/CSE signature (#1077).
            QVector<QPair<QString, QString>> incomingPorts;
            for ( const EdgeFact &edge : def.edges )
            {
                if ( edge.targetNodeId == node.nodeId )
                    incomingPorts.append( qMakePair( edge.targetPortName, edge.sourceNodeId ) );
            }
            std::sort( incomingPorts.begin(), incomingPorts.end() );
            signatures.insert( node.nodeId, computeNodeSignature( node, parentSigs, incomingPorts ) );
            progressed = true;
        }
        if ( !progressed )
            break; // unreachable on acyclic input; fail safe rather than spin
    }
    return signatures;
}

QString WorkflowPlanOptimizer::computePlanSignature( const WorkflowDocument &def )
{
    const QMap<QString, QString> nodeSigs = computeLineageSignatures( def );

    // Canonical multiset: "n:<id>=<sig>" per node plus "e:<src>.<sport>->
    // <dst>.<dport>" per edge. Edge ids are deliberately excluded — they are
    // bookkeeping labels, not topology. Sorting makes the digest invariant
    // to serialization order.
    QStringList parts;
    parts.reserve( nodeSigs.size() + def.edges.size() );
    for ( auto it = nodeSigs.cbegin(); it != nodeSigs.cend(); ++it )
        parts.append( QStringLiteral( "n:%1=%2" ).arg( it.key(), it.value() ) );
    for ( const EdgeFact &edge : def.edges )
        parts.append( QStringLiteral( "e:%1.%2->%3.%4" )
                          .arg( edge.sourceNodeId, edge.sourcePortName,
                                edge.targetNodeId, edge.targetPortName ) );
    parts.sort();

    const QByteArray digest = QCryptographicHash::hash(
        parts.join( QLatin1Char( '\n' ) ).toUtf8(), QCryptographicHash::Sha256 );
    return QString::fromLatin1( digest.toHex() );
}

WorkflowDocument WorkflowPlanOptimizer::optimizePlan( const WorkflowDocument &def,
                                                        const QSet<QString> &targetSinkNodeIds,
                                                        OptimizationReport *outReport,
                                                        const QSet<QString> &cachedSignatures )
{
    OptimizationReport report;

    // ---- Dead Node Elimination: ancestors of the sinks, sinks included.
    QHash<QString, QVector<QString>> children;
    for ( const EdgeFact &edge : def.edges )
        children[edge.sourceNodeId].append( edge.targetNodeId );

    // Iterative ancestor marking (explicit worklist — no recursion depth
    // limit on hostile-deep chains).
    QSet<QString> active;
    QVector<QString> worklist;
    for ( const QString &sink : targetSinkNodeIds )
        if ( def.findNode( sink ) && !active.contains( sink ) )
        {
            active.insert( sink );
            worklist.append( sink );
        }
    while ( !worklist.isEmpty() )
    {
        const QString current = worklist.takeLast();
        for ( const EdgeFact &edge : def.edges )
            if ( edge.targetNodeId == current && !active.contains( edge.sourceNodeId ) )
            {
                active.insert( edge.sourceNodeId );
                worklist.append( edge.sourceNodeId );
            }
    }

    QVector<NodeFact> keptNodes;
    for ( const NodeFact &node : def.nodes )
        if ( active.contains( node.nodeId ) )
            keptNodes.append( node );
    report.deadNodesPruned = def.nodes.size() - keptNodes.size();

    // ---- Common Subexpression Elimination over lineage signatures.
    WorkflowDocument working = def;
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
        if ( signature.isEmpty() && !signatures.contains( node.nodeId ) )
        {
            // Cyclic input violates the documented precondition — fail safe
            // by keeping the node unmerged instead of collapsing arbitrary
            // signature-less nodes into the first.
            uniqueNodes.append( node );
            continue;
        }
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
    // edges that became degenerate self-loops. The parallel-edge collapse
    // applies ONLY where a merge redirected the endpoints: untouched edges
    // between surviving nodes are preserved verbatim (two legal edges
    // A->B.in and A->B.aux must both survive). Among REDIRECTED edges,
    // duplicates landing on the same (source, target, target port) collapse
    // to the first.
    QVector<EdgeFact> finalEdges;
    QSet<QString> edgeIds;
    auto resolve = [&mergedInto]( QString id ) {
        while ( mergedInto.contains( id ) )
            id = mergedInto.value( id );
        return id;
    };
    QSet<QPair<QPair<QString, QString>, QString>> parallelSeen;
    for ( const EdgeFact &edge : working.edges )
    {
        EdgeFact redirected = edge;
        redirected.sourceNodeId = resolve( edge.sourceNodeId );
        redirected.targetNodeId = resolve( edge.targetNodeId );
        if ( redirected.sourceNodeId == redirected.targetNodeId )
            continue; // degenerate self-loop after merging
        const auto parallelKey = qMakePair( qMakePair( redirected.sourceNodeId, redirected.targetNodeId ),
                                            redirected.targetPortName );
        const bool wasRedirected = redirected.sourceNodeId != edge.sourceNodeId
            || redirected.targetNodeId != edge.targetNodeId;
        // Untouched edges are ALWAYS kept, but their key is recorded so a
        // redirected twin landing on the same (source, target, port) after a
        // merge collapses into them instead of double-feeding the port.
        if ( wasRedirected && parallelSeen.contains( parallelKey ) )
            continue;
        parallelSeen.insert( parallelKey );
        if ( edgeIds.contains( redirected.edgeId ) )
            continue;
        edgeIds.insert( redirected.edgeId );
        finalEdges.append( redirected );
    }

    WorkflowDocument optimized = def;
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
