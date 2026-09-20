// src/workflow/workflow_provenance.cpp — queryable run lineage graph
#include "workflow/workflow_provenance.h"

#include <QJsonArray>
#include <QSet>

namespace sicnu::workflow {
namespace {

constexpr const char *kProvenanceKind = "d17_provenance";
constexpr const char *kProvenanceVersion = "1.0";

QString runNodeId( const QString &runId ) { return QStringLiteral( "run:%1" ).arg( runId ); }
QString execNodeId( const QString &nodeId ) { return QStringLiteral( "node:%1" ).arg( nodeId ); }
QString artifactNodeId( const QString &path ) { return QStringLiteral( "artifact:%1" ).arg( path ); }

QStringList collectEdges( const QVector<ProvenanceEdge> &edges,
                          const QString &kind, const QString &fromId )
{
    QStringList out;
    for ( const ProvenanceEdge &edge : edges )
        if ( edge.kind == kind && edge.fromId == fromId )
            out.append( edge.toId );
    out.sort();
    return out;
}

} // namespace

void ProvenanceGraph::addNode( ProvenanceNode node )
{
    m_nodes.append( node );
}

void ProvenanceGraph::addEdge( ProvenanceEdge edge )
{
    m_edges.append( edge );
}

QString ProvenanceGraph::producerOf( const QString &artifactNodeId ) const
{
    for ( const ProvenanceEdge &edge : m_edges )
        if ( edge.kind == QLatin1String( "produced" ) && edge.toId == artifactNodeId )
            return edge.fromId;
    return {};
}

QStringList ProvenanceGraph::consumedBy( const QString &nodeExecId ) const
{
    return collectEdges( m_edges, QStringLiteral( "consumed" ), nodeExecId );
}

QStringList ProvenanceGraph::producedBy( const QString &nodeExecId ) const
{
    return collectEdges( m_edges, QStringLiteral( "produced" ), nodeExecId );
}

QStringList ProvenanceGraph::reusedBy( const QString &nodeExecId ) const
{
    return collectEdges( m_edges, QStringLiteral( "reusedFrom" ), nodeExecId );
}

QJsonObject ProvenanceGraph::toJson() const
{
    QVector<ProvenanceNode> sortedNodes = m_nodes;
    std::sort( sortedNodes.begin(), sortedNodes.end(),
               []( const ProvenanceNode &a, const ProvenanceNode &b ) { return a.id < b.id; } );
    QVector<ProvenanceEdge> sortedEdges = m_edges;
    std::sort( sortedEdges.begin(), sortedEdges.end(),
               []( const ProvenanceEdge &a, const ProvenanceEdge &b ) {
                   if ( a.fromId != b.fromId )
                       return a.fromId < b.fromId;
                    if ( a.toId != b.toId )
                       return a.toId < b.toId;
                    return a.kind < b.kind;
               } );

    QJsonArray nodes;
    for ( const ProvenanceNode &node : sortedNodes )
    {
        QJsonObject entry;
        entry.insert( QLatin1String( "id" ), node.id );
        entry.insert( QLatin1String( "kind" ), node.kind );
        entry.insert( QLatin1String( "attributes" ), node.attributes );
        nodes.append( entry );
    }
    QJsonArray edges;
    for ( const ProvenanceEdge &edge : sortedEdges )
    {
        QJsonObject entry;
        entry.insert( QLatin1String( "from" ), edge.fromId );
        entry.insert( QLatin1String( "to" ), edge.toId );
        entry.insert( QLatin1String( "kind" ), edge.kind );
        edges.append( entry );
    }

    QJsonObject doc;
    doc.insert( QLatin1String( "kind" ), QLatin1String( kProvenanceKind ) );
    doc.insert( QLatin1String( "version" ), QLatin1String( kProvenanceVersion ) );
    doc.insert( QLatin1String( "nodes" ), nodes );
    doc.insert( QLatin1String( "edges" ), edges );
    return doc;
}

Result<ProvenanceGraph> ProvenanceGraph::fromJson( const QJsonObject &doc )
{
    // Same envelope discipline as the checkpoint loader: refuse records this
    // build did not write instead of silently reinterpreting them.
    if ( doc.value( QLatin1String( "kind" ) ).toString() != QLatin1String( kProvenanceKind ) )
        return Result<ProvenanceGraph>::error(
            QStringLiteral( "unsupported provenance kind '%1'" )
                .arg( doc.value( QLatin1String( "kind" ) ).toString() ) );
    if ( doc.value( QLatin1String( "version" ) ).toString() != QLatin1String( kProvenanceVersion ) )
        return Result<ProvenanceGraph>::error(
            QStringLiteral( "unsupported provenance version '%1'" )
                .arg( doc.value( QLatin1String( "version" ) ).toString() ) );

    ProvenanceGraph graph;
    QSet<QString> nodeIds;
    const QJsonArray nodes = doc.value( QLatin1String( "nodes" ) ).toArray();
    for ( const QJsonValue &value : nodes )
    {
        const QJsonObject entry = value.toObject();
        ProvenanceNode node;
        node.id = entry.value( QLatin1String( "id" ) ).toString();
        node.kind = entry.value( QLatin1String( "kind" ) ).toString();
        node.attributes = entry.value( QLatin1String( "attributes" ) ).toObject();
        if ( node.id.isEmpty() || node.kind.isEmpty() || nodeIds.contains( node.id ) )
            return Result<ProvenanceGraph>::error(
                QStringLiteral( "provenance node id missing, kindless or duplicated: '%1'" ).arg( node.id ) );
        nodeIds.insert( node.id );
        graph.addNode( node );
    }
    const QJsonArray edges = doc.value( QLatin1String( "edges" ) ).toArray();
    for ( const QJsonValue &value : edges )
    {
        const QJsonObject entry = value.toObject();
        ProvenanceEdge edge;
        edge.fromId = entry.value( QLatin1String( "from" ) ).toString();
        edge.toId = entry.value( QLatin1String( "to" ) ).toString();
        edge.kind = entry.value( QLatin1String( "kind" ) ).toString();
        if ( edge.fromId.isEmpty() || edge.toId.isEmpty() || edge.kind.isEmpty()
             || !nodeIds.contains( edge.fromId ) || !nodeIds.contains( edge.toId ) )
            return Result<ProvenanceGraph>::error(
                QStringLiteral( "provenance edge dangling or malformed: '%1'->'%2'" )
                    .arg( edge.fromId, edge.toId ) );
        graph.addEdge( edge );
    }
    return Result<ProvenanceGraph>::ok( graph );
}

ProvenanceGraph ProvenanceGraph::fromRunState( const QString &runId,
                                               const WorkflowDocument &def,
                                               const QHash<QString, NodeStatusSnapshot> &statuses,
                                               const QString &planSignature )
{
    ProvenanceGraph graph;

    ProvenanceNode run;
    run.id = runNodeId( runId );
    run.kind = QStringLiteral( "run" );
    run.attributes = QJsonObject{
        { QStringLiteral( "workflowId" ), def.workflowId },
        { QStringLiteral( "workflowName" ), def.name },
        { QStringLiteral( "schemaVersion" ), def.version },
        { QStringLiteral( "planSignature" ), planSignature },
    };
    graph.addNode( run );

    // Artifact nodes keyed by path so producer/consumer share one vertex.
    QSet<QString> artifactIds;
    for ( const NodeFact &node : def.nodes )
    {
        const NodeStatusSnapshot snapshot = statuses.value( node.nodeId );

        ProvenanceNode exec;
        exec.id = execNodeId( node.nodeId );
        exec.kind = QStringLiteral( "nodeExec" );
        exec.attributes = QJsonObject{
            { QStringLiteral( "nodeId" ), node.nodeId },
            { QStringLiteral( "operatorId" ), node.operatorId },
            { QStringLiteral( "state" ), executionStateString( snapshot.state ) },
            { QStringLiteral( "lineageSignature" ), snapshot.lineageSignature },
            { QStringLiteral( "elapsedMs" ), static_cast<double>( snapshot.elapsedMs ) },
            { QStringLiteral( "isCacheHit" ), snapshot.isCacheHit },
        };
        if ( !node.originNodeId.isEmpty() )
            exec.attributes.insert( QStringLiteral( "originNodeId" ), node.originNodeId );
        if ( !snapshot.errorMessage.isEmpty() )
            exec.attributes.insert( QStringLiteral( "errorMessage" ), snapshot.errorMessage );
        graph.addNode( exec );

        const QString execId = exec.id;
        if ( !snapshot.outputArtifactPath.isEmpty() )
        {
            const QString artifactId = artifactNodeId( snapshot.outputArtifactPath );
            if ( !artifactIds.contains( artifactId ) )
            {
                artifactIds.insert( artifactId );
                ProvenanceNode artifact;
                artifact.id = artifactId;
                artifact.kind = QStringLiteral( "artifact" );
                artifact.attributes = QJsonObject{
                    { QStringLiteral( "path" ), snapshot.outputArtifactPath },
                    { QStringLiteral( "fingerprint" ), snapshot.artifactFingerprint },
                    { QStringLiteral( "sizeBytes" ), static_cast<double>( snapshot.artifactSizeBytes ) },
                };
                graph.addNode( artifact );
            }
            // A cache hit reused a verified artifact; only a fresh success
            // produced it this run.
            graph.addEdge( ProvenanceEdge{ execId, artifactId,
                                           snapshot.isCacheHit ? QStringLiteral( "reusedFrom" )
                                                               : QStringLiteral( "produced" ) } );
        }

        // Consumed edges: every incoming wire fed this exec the parent's
        // artifact. Absent artifact (parent failed/skipped) -> no edge, the
        // exec's own state already records why.
        for ( const EdgeFact &edge : def.edges )
        {
            if ( edge.targetNodeId != node.nodeId )
                continue;
            const QString parentArtifact = statuses.value( edge.sourceNodeId ).outputArtifactPath;
            if ( parentArtifact.isEmpty() )
                continue;
            const QString artifactId = artifactNodeId( parentArtifact );
            if ( !artifactIds.contains( artifactId ) )
            {
                artifactIds.insert( artifactId );
                ProvenanceNode artifact;
                artifact.id = artifactId;
                artifact.kind = QStringLiteral( "artifact" );
                const NodeStatusSnapshot &parent = statuses.value( edge.sourceNodeId );
                artifact.attributes = QJsonObject{
                    { QStringLiteral( "path" ), parentArtifact },
                    { QStringLiteral( "fingerprint" ), parent.artifactFingerprint },
                    { QStringLiteral( "sizeBytes" ), static_cast<double>( parent.artifactSizeBytes ) },
                };
                graph.addNode( artifact );
            }
            graph.addEdge( ProvenanceEdge{ execId, artifactId, QStringLiteral( "consumed" ) } );
        }
    }
    return graph;
}

} // namespace sicnu::workflow
