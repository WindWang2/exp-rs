// src/workflow/pipeline_run_coordinator.cpp — checkpointed frontier scheduler (D17)
#include "workflow/pipeline_run_coordinator.h"

#include "workflow/plan_optimizer.h"
#include "workflow/workflow_dag_analyzer.h"
#include "workflow/workflow_limits.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QSet>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QtEndian>
#include <QThread>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>

#include <fcntl.h>
#ifdef Q_OS_WIN
#include <fcntl.h> // _O_WRONLY/_O_BINARY (io.h alone does not define them)
// _O_WRONLY/_O_BINARY for fsyncFile's Win32 branch live in fcntl.h (MSVC);
// the POSIX branch below needs the same header for its own flags.
// Build-unblock for master breakage (see open PR #1009's identical fix).
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sicnu::workflow {
namespace {

// Each D17 node writes one artifact; the run keeps at most
// (frontier width x artifact size) live plus retained products — bounded by
// the coordinator, never the caller.
constexpr int kMaxPoolWorkers = 8;

QString stateKey( ExecutionState state )
{
    return executionStateString( state );
}

/// Fail-closed inverse of stateKey: the checkpoint state vocabulary is part
/// of the format contract — an unknown key means the document is corrupt (or
/// written by a build with a wider vocabulary, which must bump `version`).
std::optional<ExecutionState> stateFromKey( const QString &key )
{
    for ( ExecutionState s :
          { ExecutionState::Pending, ExecutionState::Ready, ExecutionState::Running, ExecutionState::Succeeded,
            ExecutionState::Failed, ExecutionState::Cancelled, ExecutionState::Skipped } )
        if ( executionStateString( s ) == key )
            return s;
    return std::nullopt;
}

/// Checkpoint envelope this build writes; the reader accepts the closed set
/// below. 1.1 adds per-node artifact identity (size / mtime / fingerprint).
const QString kCheckpointKind = QStringLiteral( "d17_pipeline_checkpoint" );
const QString kCheckpointVersionCurrent = QStringLiteral( "1.1" );
const QString kCheckpointVersionLegacy = QStringLiteral( "1.0" );

/// sha256fl: SHA-256 over [8-byte LE size][first <=1MiB][last <=1MiB] — an
/// O(2 MiB) deterministic identity for arbitrarily large artifacts (whole
/// file when <= 2 MiB). Empty string when the file cannot be read.
QString computeArtifactFingerprint( const QString &path, qint64 size )
{
    QFile file( path );
    if ( size < 0 || !file.open( QIODevice::ReadOnly ) )
        return {};
    constexpr qint64 kWindow = 1024 * 1024;
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    QByteArray sizeLE( 8, '\0' );
    qToLittleEndian<qint64>( size, sizeLE.data() );
    hash.addData( sizeLE );
    hash.addData( file.read( qMin( kWindow, size ) ) );
    if ( size > kWindow )
    {
        if ( !file.seek( size - kWindow ) )
            return {};
        hash.addData( file.read( kWindow ) );
    }
    return QStringLiteral( "sha256fl:%1" ).arg( QString::fromLatin1( hash.result().toHex() ) );
}

/// Canonical-path containment: the artifact must resolve (symlinks included)
/// to a path strictly inside the canonical run directory. Unresolvable or
/// empty paths fail closed; the run directory itself does not count.
bool isContainedInDirectory( const QString &artifactPath, const QString &canonicalRunDir )
{
    if ( artifactPath.isEmpty() || canonicalRunDir.isEmpty() )
        return false;
    const QString canonicalArtifact = QFileInfo( artifactPath ).canonicalFilePath();
    if ( canonicalArtifact.isEmpty() )
        return false;
    return canonicalArtifact.startsWith( canonicalRunDir + QLatin1Char( '/' ),
#ifdef Q_OS_WIN
                                         Qt::CaseInsensitive
#else
                                         Qt::CaseSensitive
#endif
    );
}

bool fsyncFile( const QString &path )
{
#ifdef Q_OS_WIN
    const int fd = _wopen( reinterpret_cast<const wchar_t *>( path.utf16() ), _O_WRONLY | _O_BINARY );
    if ( fd < 0 )
        return false;
    _commit( fd );
    _close( fd );
    return true;
#else
    const int fd = ::open( QFile::encodeName( path ).constData(), O_RDONLY );
    if ( fd < 0 )
        return false;
    ::fsync( fd );
    ::close( fd );
    return true;
#endif
}

void fsyncDirectory( const QString &path )
{
#ifdef Q_OS_WIN
    Q_UNUSED( path ); // Windows metadata durability is handled by the OS volume logic.
#else
    const int fd = ::open( QFile::encodeName( path ).constData(), O_RDONLY | O_DIRECTORY );
    if ( fd >= 0 )
    {
        ::fsync( fd );
        ::close( fd );
    }
#endif
}

/// Runs @p body on the coordinator's affinity thread when the caller lives on
/// another one. onNodeFinished (and therefore every run-state mutation it
/// performs) is delivered to that thread, so a public accessor reading the
/// same fields from a worker/UI thread would race (#1056). Returns true when
/// the body was executed on the caller's behalf; false when the caller is
/// already on the affinity thread and must run the read inline.
bool runOnCoordinatorThread( PipelineRunCoordinator *self, const std::function<void()> &body )
{
    Q_ASSERT( self );
    if ( QThread::currentThread() == self->thread() )
        return false;
    QMetaObject::invokeMethod( self, body, Qt::BlockingQueuedConnection );
    return true;
}

/// Two-phase atomic write (DECISIONS D7): tmp -> flush -> fsync -> rename ->
/// dir fsync. Returns the final path, empty on failure.
QString atomicWriteJson( const QString &path, const QJsonObject &document )
{
    const QString tmp = path + QStringLiteral( ".tmp" );
    QFile file( tmp );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return {};
    const QByteArray bytes = QJsonDocument( document ).toJson( QJsonDocument::Indented );
    if ( file.write( bytes ) != bytes.size() )
    {
        file.close();
        QFile::remove( tmp );
        return {};
    }
    file.flush();
    file.close();
    if ( !fsyncFile( tmp ) )
    {
        QFile::remove( tmp );
        return {};
    }
    // POSIX rename(2) atomically REPLACES the target (QFile::rename refuses
    // when the destination exists) — exactly the two-phase commit semantics
    // this checkpoint writer needs.
#ifdef Q_OS_WIN
    if ( !MoveFileExW( reinterpret_cast<const wchar_t *>( tmp.utf16() ),
                       reinterpret_cast<const wchar_t *>( path.utf16() ),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
    {
        QFile::remove( tmp );
        return {};
    }
#else
    if ( ::rename( QFile::encodeName( tmp ).constData(), QFile::encodeName( path ).constData() ) != 0 )
    {
        QFile::remove( tmp );
        return {};
    }
#endif
    fsyncDirectory( QFileInfo( path ).absolutePath() );
    return path;
}

} // namespace

NodeExecutor makeSyntheticNodeExecutor()
{
    return []( const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
               const QString &runDirectory ) -> NodeExecutionResult {
        NodeExecutionResult result;
        QDir().mkpath( runDirectory );
        const QString artifact =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.artifact" ).arg( node.nodeId ) );
        QFile file( artifact );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        {
            result.errorMessage = QStringLiteral( "cannot write artifact for '%1'" ).arg( node.nodeId );
            return result;
        }
        QByteArray payload;
        payload += QStringLiteral( "d17-node %1 op %2\n" ).arg( node.nodeId, node.operatorId ).toUtf8();
        // Keys are IR2 target port names (D-W6); sorted for determinism.
        QStringList ports = inputArtifacts.keys();
        std::sort( ports.begin(), ports.end() );
        for ( const QString &port : ports )
            payload += QStringLiteral( "in %1=%2\n" )
                           .arg( port, inputArtifacts.value( port ) )
                           .toUtf8();
        file.write( payload );
        file.close();
        result.success = true;
        result.artifactPath = artifact;
        return result;
    };
}

QString executionStateString( ExecutionState state )
{
    switch ( state )
    {
        case ExecutionState::Pending:
            return QStringLiteral( "Pending" );
        case ExecutionState::Ready:
            return QStringLiteral( "Ready" );
        case ExecutionState::Running:
            return QStringLiteral( "Running" );
        case ExecutionState::Succeeded:
            return QStringLiteral( "Succeeded" );
        case ExecutionState::Failed:
            return QStringLiteral( "Failed" );
        case ExecutionState::Cancelled:
            return QStringLiteral( "Cancelled" );
        case ExecutionState::Skipped:
            return QStringLiteral( "Skipped" );
    }
    return QStringLiteral( "Unknown" );
}

struct PipelineRunCoordinator::RunState
{
    WorkflowDocument def;
    QString runDirectory;
    QString runId;
    QString checkpointPath;
    NodeExecutor executor;
    QThreadPool pool;
    int maxParallelism = 2;

    QHash<QString, NodeStatusSnapshot> statuses;
    QHash<QString, int> remainingParents; // nodeId -> unfinished parent count
    std::atomic<bool> cancelRequested{ false };
    bool finished = false;
    bool success = false;

    ~RunState() { pool.waitForDone(); }
};

PipelineRunCoordinator::PipelineRunCoordinator( QObject *parent )
    : QObject( parent )
    , m_state( std::make_unique<RunState>() )
{
    qRegisterMetaType<sicnu::workflow::ExecutionState>( "sicnu::workflow::ExecutionState" );
    m_state->pool.setMaxThreadCount( m_state->maxParallelism );
}

PipelineRunCoordinator::~PipelineRunCoordinator() = default;

void PipelineRunCoordinator::setExecutor( NodeExecutor executor )
{
    m_state->executor = std::move( executor );
}

void PipelineRunCoordinator::setMaxParallelism( int workers )
{
    m_state->maxParallelism = std::clamp( workers, 1, kMaxPoolWorkers );
    m_state->pool.setMaxThreadCount( m_state->maxParallelism );
}

QString PipelineRunCoordinator::checkpointPath() const
{
    return m_state->checkpointPath;
}

bool PipelineRunCoordinator::isRunning() const
{
    // Marshal the read onto the affinity thread: onNodeFinished flips
    // m_state->finished there and an unsynchronised cross-thread read races.
    bool value = false;
    if ( runOnCoordinatorThread( const_cast<PipelineRunCoordinator *>( this ),
                                 [this, &value] {
                                     value = !m_state->finished && !m_state->def.nodes.isEmpty();
                                 } ) )
        return value;
    return !m_state->finished && !m_state->def.nodes.isEmpty();
}

bool PipelineRunCoordinator::hasCompleted() const
{
    bool value = false;
    if ( runOnCoordinatorThread( const_cast<PipelineRunCoordinator *>( this ),
                                 [this, &value] { value = m_state->finished; } ) )
        return value;
    return m_state->finished;
}

QMap<QString, NodeStatusSnapshot> PipelineRunCoordinator::getAllStatuses() const
{
    QMap<QString, NodeStatusSnapshot> map;
    const auto fill = [this]( QMap<QString, NodeStatusSnapshot> &out ) {
        for ( auto it = m_state->statuses.cbegin(); it != m_state->statuses.cend(); ++it )
            out.insert( it.key(), it.value() );
    };
    if ( runOnCoordinatorThread( const_cast<PipelineRunCoordinator *>( this ),
                                 [&fill, &map] { fill( map ); } ) )
        return map;
    fill( map );
    return map;
}

bool PipelineRunCoordinator::startRun( const WorkflowDocument &def, const QString &runDirectory, QString *outError )
{
    auto fail = [outError]( const QString &message ) {
        if ( outError )
            *outError = message;
        return false;
    };

    if ( !m_state->finished && !m_state->def.nodes.isEmpty() )
        return fail( QStringLiteral( "a run is already active on this coordinator" ) );
    if ( !WorkflowIR::validateSemantics( def ) )
        return fail( QStringLiteral( "workflow document is not semantically valid" ) );

    const DagAnalysisResult dag = WorkflowDagAnalyzer::analyzeDag( def );
    if ( !dag.isAcyclic )
        return fail( dag.errorMessage );

    // Fresh state.
    m_state->def = def;
    m_state->runDirectory = runDirectory;
    m_state->runId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    m_state->checkpointPath = QDir( runDirectory ).filePath( QStringLiteral( "checkpoint_%1.json" ).arg( m_state->runId ) );
    m_state->finished = false;
    m_state->success = false;
    m_state->cancelRequested = false;
    m_state->statuses.clear();
    m_state->remainingParents.clear();

    const QMap<QString, QString> signatures = WorkflowPlanOptimizer::computeLineageSignatures( def );
    for ( const NodeFact &node : def.nodes )
    {
        NodeStatusSnapshot snapshot;
        snapshot.nodeId = node.nodeId;
        snapshot.lineageSignature = signatures.value( node.nodeId );
        m_state->statuses.insert( node.nodeId, snapshot );

        int parents = 0;
        for ( const EdgeFact &edge : def.edges )
            if ( edge.targetNodeId == node.nodeId )
                ++parents;
        m_state->remainingParents.insert( node.nodeId, parents );
    }

    QDir().mkpath( runDirectory );
    persistCheckpoint();
    dispatchReadyNodes();
    finalizeIfDone(); // empty documents complete synchronously
    return true;
}

void PipelineRunCoordinator::requestCancel()
{
    // Cancel mutates the run state (statuses, bookkeeping) that onNodeFinished
    // also touches on the affinity thread: marshal the request there so the
    // two can never interleave (#1056). Callers already on the affinity thread
    // run inline, which keeps the synchronous pipelineCompleted behaviour.
    if ( runOnCoordinatorThread( this, [this] { requestCancel(); } ) )
        return;
    m_state->cancelRequested = true;
    markRemaining( ExecutionState::Cancelled );
    dispatchReadyNodes(); // nothing will be dispatched; drive finalization
    finalizeIfDone();
}

void PipelineRunCoordinator::markRemaining( ExecutionState state )
{
    for ( auto it = m_state->statuses.begin(); it != m_state->statuses.end(); ++it )
    {
        if ( it.value().state == ExecutionState::Pending || it.value().state == ExecutionState::Ready )
        {
            it.value().state = state;
            emit nodeStatusChanged( it.key(), state, 0.0f );
        }
    }
}

void PipelineRunCoordinator::dispatchReadyNodes()
{
    if ( m_state->finished )
        return;

    bool anySkipped = false;
    QVector<QString> toDispatch;

    // Fixed-point frontier drain: skipping a node releases ITS children, so
    // the cascade propagates transitively. Mutations are collected per pass
    // and applied after the pass — never inside the QHash iteration.
    bool changed = true;
    while ( changed )
    {
        changed = false;
        toDispatch.clear();
        QVector<QString> newlySkipped;

        for ( auto it = m_state->remainingParents.cbegin(); it != m_state->remainingParents.cend(); ++it )
        {
            if ( it.value() != 0 )
                continue;
            const NodeStatusSnapshot &snapshot = m_state->statuses[it.key()];
            if ( snapshot.state != ExecutionState::Pending && snapshot.state != ExecutionState::Ready )
                continue;
            // Skip cascade: any non-Succeeded parent stops this node.
            bool blocked = false;
            for ( const EdgeFact &edge : m_state->def.edges )
            {
                if ( edge.targetNodeId != it.key() )
                    continue;
                const ExecutionState parentState = m_state->statuses.value( edge.sourceNodeId ).state;
                if ( parentState != ExecutionState::Succeeded )
                {
                    blocked = true;
                    break;
                }
            }
            if ( blocked )
                newlySkipped.append( it.key() );
            else
                toDispatch.append( it.key() );
        }

        if ( !newlySkipped.isEmpty() )
        {
            for ( const QString &nodeId : newlySkipped )
            {
                NodeStatusSnapshot &snapshot = m_state->statuses[nodeId];
                snapshot.state = ExecutionState::Skipped;
                emit nodeStatusChanged( nodeId, ExecutionState::Skipped, 0.0f );
                m_state->remainingParents[nodeId] = -1; // terminal, do not revisit
                // Release the skipped node's children so the cascade drains.
                for ( const EdgeFact &edge : m_state->def.edges )
                {
                    if ( edge.sourceNodeId != nodeId )
                        continue;
                    const auto child = m_state->remainingParents.find( edge.targetNodeId );
                    if ( child != m_state->remainingParents.end() && child.value() > 0 )
                        child.value() -= 1;
                }
            }
            anySkipped = true;
            changed = true;
        }
    }

    if ( anySkipped )
        persistCheckpoint();

    // Deterministic dispatch order (QHash iteration order is not).
    std::sort( toDispatch.begin(), toDispatch.end() );
    for ( const QString &nodeId : toDispatch )
    {
        NodeStatusSnapshot &snapshot = m_state->statuses[nodeId];
        snapshot.state = ExecutionState::Running;
        emit nodeStatusChanged( nodeId, ExecutionState::Running, 0.0f );
        m_state->remainingParents[nodeId] = -1; // claimed

        // Capture for the worker lambda (values, no coordinator access).
        const NodeFact node = *m_state->def.findNode( nodeId );
        // D-W6: key by target port name (explicit IR2 port→param mapping).
        // Single-source invariant guarantees one edge per input port.
        QHash<QString, QString> inputArtifacts;
        for ( const EdgeFact &edge : m_state->def.edges )
            if ( edge.targetNodeId == nodeId )
                inputArtifacts.insert( edge.targetPortName,
                                       m_state->statuses.value( edge.sourceNodeId ).outputArtifactPath );
        const QString runDirectory = m_state->runDirectory;
        QPointer<PipelineRunCoordinator> self( this );
        // The executor is copied into the worker: no shared mutable state
        // crosses the thread boundary, and no worker blocks on a peer node.
        NodeExecutor executor = m_state->executor;
        const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
        m_state->pool.start( [self, node, inputArtifacts, runDirectory, startedAt,
                              executor = std::move( executor )]() {
            NodeExecutionResult result;
            if ( executor )
                result = executor( node, inputArtifacts, runDirectory );
            else
            {
                // #1006 fail-closed: no bound executor means no silent
                // synthetic success — production binds
                // makeRegistryNodeExecutor(), tests bind makeSyntheticNodeExecutor().
                result.errorMessage =
                    QStringLiteral( "ir2.executor_missing: no NodeExecutor bound (node '%1')" )
                        .arg( node.nodeId );
            }
            const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startedAt;
            if ( self )
                QMetaObject::invokeMethod( self, [self, node, result, elapsed]() {
                    self->onNodeFinished( node.nodeId, result, elapsed );
                },
                                           Qt::QueuedConnection );
        } );
    }

    // A terminal skip cascade can finish the run without any node event.
    finalizeIfDone();
}

void PipelineRunCoordinator::onNodeFinished( const QString &nodeId, NodeExecutionResult result, qint64 elapsedMs )
{
    if ( !m_state->statuses.contains( nodeId ) )
        return;

    NodeStatusSnapshot &snapshot = m_state->statuses[nodeId];
    snapshot.elapsedMs = elapsedMs;

    if ( m_state->cancelRequested )
    {
        // A draining worker after a cancel request is Cancelled regardless
        // of its outcome — the run as a whole did not produce it.
        snapshot.state = ExecutionState::Cancelled;
    }
    else if ( result.success && !result.artifactPath.isEmpty() )
    {
        // Artifact contract (DECISIONS D3): a reported product must be a real
        // file that resolves inside the run directory, and it must be
        // fingerprintable — a success we cannot re-verify on resume is a
        // contract violation, not a cacheable result.
        const QFileInfo artifactInfo( result.artifactPath );
        const QString canonicalRunDir = QDir( m_state->runDirectory ).canonicalPath();
        if ( !artifactInfo.isFile()
             || !isContainedInDirectory( result.artifactPath, canonicalRunDir ) )
        {
            snapshot.state = ExecutionState::Failed;
            snapshot.errorMessage =
                QStringLiteral( "ir2.artifact_outside_run: artifact '%1' is missing or resolves outside run directory '%2' (node '%3')" )
                    .arg( result.artifactPath, m_state->runDirectory, nodeId );
        }
        else if ( ( snapshot.artifactFingerprint =
                        computeArtifactFingerprint( artifactInfo.canonicalFilePath(), artifactInfo.size() ) )
                      .isEmpty() )
        {
            snapshot.state = ExecutionState::Failed;
            snapshot.errorMessage =
                QStringLiteral( "ir2.artifact_unverifiable: cannot fingerprint artifact '%1' (node '%2')" )
                    .arg( result.artifactPath, nodeId );
        }
        else
        {
            snapshot.state = ExecutionState::Succeeded;
            snapshot.outputArtifactPath = result.artifactPath;
            snapshot.artifactSizeBytes = artifactInfo.size();
            snapshot.artifactLastModifiedMs =
                artifactInfo.fileTime( QFileDevice::FileModificationTime ).toMSecsSinceEpoch();
            snapshot.progress = 1.0f;
        }
    }
    else if ( result.success )
    {
        // A sink node may legitimately produce no file; it Succeeds but can
        // never be a cache hit (no artifact to verify on resume).
        snapshot.state = ExecutionState::Succeeded;
        snapshot.progress = 1.0f;
    }
    else
    {
        snapshot.state = ExecutionState::Failed;
        snapshot.errorMessage = result.errorMessage;
    }

    // Release the node's children (frontier bookkeeping). Non-Succeeded
    // terminations still decrement: the skip cascade is decided from the
    // recorded parent states inside dispatchReadyNodes.
    for ( const EdgeFact &edge : m_state->def.edges )
    {
        if ( edge.sourceNodeId != nodeId )
            continue;
        const auto child = m_state->remainingParents.find( edge.targetNodeId );
        if ( child != m_state->remainingParents.end() && child.value() > 0 )
            child.value() -= 1;
    }

    emit nodeStatusChanged( nodeId, snapshot.state, snapshot.progress );
    emit nodeFinished( nodeId, snapshot.state == ExecutionState::Succeeded, snapshot.outputArtifactPath );
    persistCheckpoint();
    finalizeIfDone();
    dispatchReadyNodes();
}

void PipelineRunCoordinator::finalizeIfDone()
{
    if ( m_state->finished )
        return;
    for ( const NodeStatusSnapshot &snapshot : std::as_const( m_state->statuses ) )
        if ( snapshot.state == ExecutionState::Pending || snapshot.state == ExecutionState::Ready
             || snapshot.state == ExecutionState::Running )
            return;

    m_state->finished = true;
    bool success = true;
    int succeeded = 0, skipped = 0, failed = 0, cancelled = 0;
    for ( const NodeStatusSnapshot &snapshot : std::as_const( m_state->statuses ) )
    {
        switch ( snapshot.state )
        {
            case ExecutionState::Succeeded:
                ++succeeded;
                break;
            case ExecutionState::Skipped:
                ++skipped;
                success = false;
                break;
            case ExecutionState::Failed:
                ++failed;
                success = false;
                break;
            case ExecutionState::Cancelled:
                ++cancelled;
                success = false;
                break;
            default:
                break;
        }
    }
    m_state->success = success;
    persistCheckpoint();
    emit pipelineCompleted(
        success,
        QStringLiteral( "%1 succeeded, %2 skipped, %3 failed, %4 cancelled" )
            .arg( succeeded )
            .arg( skipped )
            .arg( failed )
            .arg( cancelled ) );
}

void PipelineRunCoordinator::persistCheckpoint()
{
    if ( m_state->def.nodes.isEmpty() )
        return;

    QJsonObject document;
    document.insert( QLatin1String( "kind" ), kCheckpointKind );
    document.insert( QLatin1String( "version" ), kCheckpointVersionCurrent );
    document.insert( QLatin1String( "runId" ), m_state->runId );
    document.insert( QLatin1String( "runDirectory" ), m_state->runDirectory );
    document.insert( QLatin1String( "workflow" ), WorkflowIR::toJson( m_state->def ) );
    document.insert( QLatin1String( "finished" ), m_state->finished );
    document.insert( QLatin1String( "success" ), m_state->success );
    document.insert( QLatin1String( "updatedAt" ),
                     QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );

    QJsonArray nodes;
    for ( const NodeFact &node : m_state->def.nodes ) // document order, deterministic
    {
        const NodeStatusSnapshot &snapshot = m_state->statuses.value( node.nodeId );
        QJsonObject entry;
        entry.insert( QLatin1String( "nodeId" ), snapshot.nodeId );
        entry.insert( QLatin1String( "state" ), stateKey( snapshot.state ) );
        entry.insert( QLatin1String( "progress" ), static_cast<double>( snapshot.progress ) );
        entry.insert( QLatin1String( "errorMessage" ), snapshot.errorMessage );
        entry.insert( QLatin1String( "elapsedMs" ), snapshot.elapsedMs );
        entry.insert( QLatin1String( "artifact" ), snapshot.outputArtifactPath );
        entry.insert( QLatin1String( "artifactSize" ), snapshot.artifactSizeBytes );
        entry.insert( QLatin1String( "artifactMtimeMs" ), snapshot.artifactLastModifiedMs );
        entry.insert( QLatin1String( "artifactFingerprint" ), snapshot.artifactFingerprint );
        entry.insert( QLatin1String( "isCacheHit" ), snapshot.isCacheHit );
        entry.insert( QLatin1String( "signature" ), snapshot.lineageSignature );
        nodes.append( entry );
    }
    document.insert( QLatin1String( "nodes" ), nodes );

    const QString path = atomicWriteJson( m_state->checkpointPath, document );
    if ( !path.isEmpty() )
        emit checkpointPersisted( path );
}

bool PipelineRunCoordinator::resumeFromCheckpoint( const QString &checkpointFilePath, QString *outError )
{
    auto fail = [outError]( const QString &message ) {
        if ( outError )
            *outError = message;
        return false;
    };

    // Symmetric with startRun: a live run's queued completions must never
    // mutate a resumed run's state.
    if ( !m_state->finished && !m_state->def.nodes.isEmpty() )
        return fail( QStringLiteral( "a run is already active on this coordinator" ) );

    QFile file( checkpointFilePath );
    if ( !file.open( QIODevice::ReadOnly ) )
        return fail( QStringLiteral( "cannot open checkpoint '%1'" ).arg( checkpointFilePath ) );
    // Bounded read (DECISIONS D5): a checkpoint is a small JSON sidecar —
    // anything past the shared cap is planted or corrupt and must not be
    // buffered unbounded.
    if ( file.size() > kMaxCheckpointDocumentBytes )
        return fail( QStringLiteral( "checkpoint '%1' exceeds the %2-byte size cap" )
                         .arg( checkpointFilePath )
                         .arg( kMaxCheckpointDocumentBytes ) );
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    if ( doc.isNull() || !doc.object().value( QLatin1String( "workflow" ) ).isObject() )
        return fail( QStringLiteral( "checkpoint '%1' is not a valid document" ).arg( checkpointFilePath ) );

    const QJsonObject document = doc.object();
    // Envelope gate (DECISIONS D2): unknown kind or a version outside the
    // closed supported set is refused outright — a checkpoint this build did
    // not write must never be silently reinterpreted.
    if ( document.value( QLatin1String( "kind" ) ).toString() != kCheckpointKind )
        return fail( QStringLiteral( "checkpoint '%1' has unsupported kind '%2'" )
                         .arg( checkpointFilePath,
                               document.value( QLatin1String( "kind" ) ).toString() ) );
    const QString checkpointVersion = document.value( QLatin1String( "version" ) ).toString();
    if ( checkpointVersion != kCheckpointVersionLegacy
         && checkpointVersion != kCheckpointVersionCurrent )
        return fail( QStringLiteral( "checkpoint '%1' has unsupported version '%2'" )
                         .arg( checkpointFilePath, checkpointVersion ) );
    // runId is persisted into provenance artifacts; refuse ids that could not
    // safely name a file (mirrors the Engine-2.0 isValidRunId policy).
    const QString runId = document.value( QLatin1String( "runId" ) ).toString();
    if ( runId.isEmpty() || runId.size() > 128 || runId.contains( QLatin1String( ".." ) )
         || !runId.contains( QRegularExpression( QStringLiteral( "\\A[A-Za-z0-9._-]+\\z" ) ) ) )
        return fail( QStringLiteral( "checkpoint '%1' carries an unsafe runId '%2'" )
                         .arg( checkpointFilePath, runId ) );
    auto parsed = WorkflowIR::fromJson( document.value( QLatin1String( "workflow" ) ).toObject() );
    if ( !parsed.isSuccess() )
        return fail( QStringLiteral( "checkpoint workflow does not parse: %1" ).arg( parsed.error() ) );

    // Validate node set + count BEFORE mutating m_state so a corrupt
    // checkpoint cannot wedge the coordinator as "already active" (#1078c).
    const WorkflowDocument resumedDef = parsed.value();
    const QJsonArray nodes = document.value( QLatin1String( "nodes" ) ).toArray();
    QSet<QString> seenNodeIds;
    for ( const QJsonValue &value : nodes )
    {
        const QString nodeId = value.toObject().value( QLatin1String( "nodeId" ) ).toString();
        if ( !resumedDef.findNode( nodeId ) )
            return fail( QStringLiteral( "checkpoint references unknown node '%1'" ).arg( nodeId ) );
        seenNodeIds.insert( nodeId );
    }
    if ( seenNodeIds.size() != resumedDef.nodes.size() )
        return fail( QStringLiteral( "checkpoint is missing node statuses" ) );

    // Fresh scheduling state over the resumed document.
    m_state->def = resumedDef;
    m_state->runDirectory = document.value( QLatin1String( "runDirectory" ) ).toString();
    m_state->runId = document.value( QLatin1String( "runId" ) ).toString();
    m_state->checkpointPath = checkpointFilePath;
    m_state->finished = false;
    m_state->success = false;
    m_state->cancelRequested = false;
    m_state->statuses.clear();
    m_state->remainingParents.clear();

    // Replay statuses. A node becomes CacheHit iff ALL of:
    //   recorded state Succeeded, lineage signature still matches, and the
    //   recorded artifact re-verifies — exists, resolves inside the recorded
    //   run directory, and matches size / mtime / content fingerprint.
    // Anything less reverts to Pending and recomputes: a tampered, moved, or
    // foreign artifact is never served (DECISIONS D3). Checkpoints written
    // before format 1.1 carry no identity fields and degrade to a full
    // recompute — correct but cold.
    const QMap<QString, QString> recomputed = WorkflowPlanOptimizer::computeLineageSignatures( m_state->def );
    const QString canonicalRunDir = QDir( m_state->runDirectory ).canonicalPath();
    for ( const QJsonValue &value : nodes )
    {
        const QJsonObject entry = value.toObject();
        const QString nodeId = entry.value( QLatin1String( "nodeId" ) ).toString();
        const NodeFact *node = m_state->def.findNode( nodeId );
        if ( !node )
            return fail( QStringLiteral( "checkpoint references unknown node '%1'" ).arg( nodeId ) );

        NodeStatusSnapshot snapshot;
        snapshot.nodeId = nodeId;
        snapshot.errorMessage = entry.value( QLatin1String( "errorMessage" ) ).toString();
        snapshot.elapsedMs = entry.value( QLatin1String( "elapsedMs" ) ).toInteger();
        snapshot.outputArtifactPath = entry.value( QLatin1String( "artifact" ) ).toString();
        snapshot.progress = static_cast<float>( entry.value( QLatin1String( "progress" ) ).toDouble() );

        const QString recordedStateKey = entry.value( QLatin1String( "state" ) ).toString();
        const std::optional<ExecutionState> recorded = stateFromKey( recordedStateKey );
        if ( !recorded.has_value() )
            return fail( QStringLiteral( "checkpoint '%1' records unknown state '%2' for node '%3'" )
                             .arg( checkpointFilePath, recordedStateKey, nodeId ) );
        const QString recordedSignature = entry.value( QLatin1String( "signature" ) ).toString();
        const bool signatureMatches = recordedSignature == recomputed.value( nodeId );

        bool artifactVerified = false;
        if ( *recorded == ExecutionState::Succeeded && !snapshot.outputArtifactPath.isEmpty() )
        {
            const QFileInfo artifactInfo( snapshot.outputArtifactPath );
            const qint64 recordedSize = entry.value( QLatin1String( "artifactSize" ) ).toInteger( -1 );
            const qint64 recordedMtime = entry.value( QLatin1String( "artifactMtimeMs" ) ).toInteger( -1 );
            const QString recordedFingerprint =
                entry.value( QLatin1String( "artifactFingerprint" ) ).toString();
            const qint64 liveMtime =
                artifactInfo.fileTime( QFileDevice::FileModificationTime ).toMSecsSinceEpoch();
            artifactVerified =
                artifactInfo.isFile()
                && isContainedInDirectory( snapshot.outputArtifactPath, canonicalRunDir )
                && recordedSize >= 0 && recordedSize == artifactInfo.size() && recordedMtime >= 0
                && recordedMtime == liveMtime && !recordedFingerprint.isEmpty()
                && recordedFingerprint
                       == computeArtifactFingerprint( snapshot.outputArtifactPath, artifactInfo.size() );
            if ( artifactVerified )
            {
                snapshot.artifactSizeBytes = artifactInfo.size();
                snapshot.artifactLastModifiedMs = liveMtime;
                snapshot.artifactFingerprint = recordedFingerprint;
            }
        }

        if ( *recorded == ExecutionState::Succeeded && artifactVerified && signatureMatches )
        {
            snapshot.state = ExecutionState::Succeeded;
            snapshot.isCacheHit = true;
        }
        else if ( *recorded == ExecutionState::Skipped )
        {
            // Skip decisions are re-derived by the frontier; treat as fresh.
            snapshot.state = ExecutionState::Pending;
            snapshot.outputArtifactPath.clear();
        }
        else
        {
            snapshot.state = ExecutionState::Pending; // recompute (incl. Failed/Cancelled/Running)
            snapshot.outputArtifactPath.clear(); // stale path must not leak into this run
        }
        snapshot.lineageSignature = recomputed.value( nodeId );
        m_state->statuses.insert( nodeId, snapshot );
    }
    if ( m_state->statuses.size() != m_state->def.nodes.size() )
        return fail( QStringLiteral( "checkpoint is missing node statuses" ) );

    // Second pass over the FULL status map (never the partially filled one):
    // count only parents that still need to RUN this round — CacheHit
    // (Succeeded) parents release their children immediately, otherwise a
    // fully-cached prefix would stall the resumed frontier. Document order
    // of the checkpoint array is irrelevant.
    for ( const NodeFact &node : resumedDef.nodes )
    {
        int parents = 0;
        for ( const EdgeFact &edge : m_state->def.edges )
        {
            if ( edge.targetNodeId != node.nodeId )
                continue;
            const NodeStatusSnapshot &parentSnapshot = m_state->statuses.value( edge.sourceNodeId );
            if ( parentSnapshot.state != ExecutionState::Succeeded )
                ++parents;
        }
        m_state->remainingParents.insert( node.nodeId, parents );
    }

    dispatchReadyNodes();
    finalizeIfDone();
    return true;
}

} // namespace sicnu::workflow
