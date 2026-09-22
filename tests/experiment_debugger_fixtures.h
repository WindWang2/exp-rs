// experiment_debugger_fixtures.h — synthetic evidence builders for the
// RS14-06 Experiment Debugger suites.
//
// Fixtures are synthesized at runtime (repo convention) and shaped after the
// REAL recorded contracts: d17_provenance JSON (workflow_provenance.cpp),
// checkpoint step plans (workflow_run.h StepPlan), bridge step summaries
// (run_bridge.cpp workflowEvidence). Building the wire shapes by hand is
// deliberate: the fixtures pin the recorded contract, not the producers.
#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>

#include "experiment/experiment_types.h"

namespace sicnu::experiment::debugger::fixtures
{

/// Minimal but honest run record fixture: identity pins populated, status
/// Completed with deterministic timestamps (terminal runs require a finish
/// stamp), no artifacts unless the caller adds them.
inline sicnu::experiment::ExperimentRun makeRunRecord( const QString &runId )
{
    sicnu::experiment::ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( QStringLiteral( "exp-lab-1" ) );
    run.setAlgorithmId( QStringLiteral( "workflow:ndvi-threshold-area" ) );
    run.setAlgorithmVersion( QStringLiteral( "1" ) );
    run.setDatasetVersionId( QStringLiteral( "dsv-1" ) );
    run.setDatasetFingerprint( QStringLiteral( "fp-dataset-1" ) );
    run.setSplitManifestId( QStringLiteral( "split-1" ) );
    run.setSplitFingerprint( QStringLiteral( "fp-split-1" ) );
    run.setSeed( 7 );
    run.setSoftwareRevision( QStringLiteral( "rev-test" ) );
    static const QDateTime start( QDate( 2026, 9, 22 ), QTime( 8, 0, 0 ), QTimeZone::utc() );
    run.setCreatedAtUtc( start );
    run.setStartedAtUtc( start );
    run.setFinishedAtUtc( start.addSecs( 90 ) );
    return run;
}

/// One nodeExec entry of a d17_provenance document.
struct ProvExec
{
    QString nodeId;        // plain node id (document id becomes "node:<id>")
    QString operatorId;
    QString state = QStringLiteral( "Succeeded" );
    QString lineageSignature;
    bool isCacheHit = false;
    QString outputArtifactPath;   // empty = no produced artifact
    QString outputFingerprint;    // as recorded, e.g. "sha256fl:abcd"
    qint64 outputSizeBytes = -1;
    QString errorMessage;
};

/// One consumed wire: consumer node id -> produced artifact path.
struct ProvConsumed
{
    QString consumerNodeId;
    QString artifactPath;
    QString artifactFingerprint;
    qint64 artifactSizeBytes = -1;
};

inline QJsonObject makeProvenanceDoc( const QString &runId,
                                      const QString &planSignature,
                                      const QVector<ProvExec> &execs,
                                      const QVector<ProvConsumed> &consumed )
{
    QJsonObject doc;
    doc.insert( QLatin1String( "kind" ), QLatin1String( "d17_provenance" ) );
    doc.insert( QLatin1String( "version" ), QLatin1String( "1.0" ) );

    QJsonObject runNode;
    runNode.insert( QLatin1String( "id" ), QStringLiteral( "run:%1" ).arg( runId ) );
    runNode.insert( QLatin1String( "kind" ), QLatin1String( "run" ) );
    runNode.insert( QLatin1String( "attributes" ), QJsonObject{
        { QLatin1String( "planSignature" ), planSignature },
        { QLatin1String( "workflowId" ), QStringLiteral( "workflow:ndvi-threshold-area" ) },
    } );

    QJsonArray nodes;
    nodes.append( runNode );
    for ( const ProvExec &exec : execs )
    {
        QJsonObject node;
        node.insert( QLatin1String( "id" ), QStringLiteral( "node:%1" ).arg( exec.nodeId ) );
        node.insert( QLatin1String( "kind" ), QLatin1String( "nodeExec" ) );
        QJsonObject attributes;
        attributes.insert( QLatin1String( "nodeId" ), exec.nodeId );
        attributes.insert( QLatin1String( "operatorId" ), exec.operatorId );
        attributes.insert( QLatin1String( "state" ), exec.state );
        attributes.insert( QLatin1String( "lineageSignature" ), exec.lineageSignature );
        attributes.insert( QLatin1String( "isCacheHit" ), exec.isCacheHit );
        if ( !exec.errorMessage.isEmpty() )
            attributes.insert( QLatin1String( "errorMessage" ), exec.errorMessage );
        node.insert( QLatin1String( "attributes" ), attributes );
        nodes.append( node );
    }
    // NOTE: QJsonObject stores inserted values by copy — "nodes" must be
    // (re-)inserted only AFTER the artifact nodes are appended below.

    QJsonArray edges;
    // produced edges (from outputArtifactPath) + consumed edges.
    for ( const ProvExec &exec : execs )
    {
        if ( exec.outputArtifactPath.isEmpty() )
            continue;
        QJsonObject edge;
        edge.insert( QLatin1String( "from" ), QStringLiteral( "node:%1" ).arg( exec.nodeId ) );
        edge.insert( QLatin1String( "to" ), QStringLiteral( "artifact:%1" ).arg( exec.outputArtifactPath ) );
        edge.insert( QLatin1String( "kind" ),
                     exec.isCacheHit ? QLatin1String( "reusedFrom" ) : QLatin1String( "produced" ) );
        edges.append( edge );
    }
    for ( const ProvConsumed &wire : consumed )
    {
        QJsonObject edge;
        edge.insert( QLatin1String( "from" ), QStringLiteral( "node:%1" ).arg( wire.consumerNodeId ) );
        edge.insert( QLatin1String( "to" ), QStringLiteral( "artifact:%1" ).arg( wire.artifactPath ) );
        edge.insert( QLatin1String( "kind" ), QLatin1String( "consumed" ) );
        edges.append( edge );
        // ensure the artifact node exists even when nothing in-run produced it
        // (root input); the real writer adds these from parent snapshots.
    }
    doc.insert( QLatin1String( "edges" ), edges );
    // artifact nodes (deduped by path)
    QSet<QString> seen;
    QJsonArray artifactNodes;
    auto appendArtifact = [&]( const QString &path, const QString &fingerprint, qint64 size ) {
        if ( seen.contains( path ) )
            return;
        seen.insert( path );
        QJsonObject node;
        node.insert( QLatin1String( "id" ), QStringLiteral( "artifact:%1" ).arg( path ) );
        node.insert( QLatin1String( "kind" ), QLatin1String( "artifact" ) );
        node.insert( QLatin1String( "attributes" ), QJsonObject{
            { QLatin1String( "path" ), path },
            { QLatin1String( "fingerprint" ), fingerprint },
            { QLatin1String( "sizeBytes" ), static_cast<double>( size ) },
        } );
        artifactNodes.append( node );
    };
    for ( const ProvExec &exec : execs )
        if ( !exec.outputArtifactPath.isEmpty() )
            appendArtifact( exec.outputArtifactPath, exec.outputFingerprint, exec.outputSizeBytes );
    for ( const ProvConsumed &wire : consumed )
        appendArtifact( wire.artifactPath, wire.artifactFingerprint, wire.artifactSizeBytes );
    for ( const QJsonValue &value : artifactNodes )
        nodes.append( value );
    doc.insert( QLatin1String( "nodes" ), nodes );

    return doc;
}

} // namespace sicnu::experiment::debugger::fixtures
