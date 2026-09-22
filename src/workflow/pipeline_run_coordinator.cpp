// src/workflow/pipeline_run_coordinator.cpp — checkpointed frontier scheduler (D17)
#include "workflow/pipeline_run_coordinator.h"

#include "workflow/plan_optimizer.h"
#include "workflow/workflow_composer.h"
#include "workflow/workflow_dag_analyzer.h"
#include "workflow/workflow_limits.h"
#include "workflow/workflow_provenance.h"

#include "runtime/observability/fault_point.h"

#include <QCoreApplication>
#include <QEventLoop>
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

#include <QPointer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <type_traits>

#ifdef Q_OS_WIN
// _O_WRONLY/_O_BINARY for fsyncFile's Win32 branch live in fcntl.h (MSVC);
// the POSIX branch below needs the same header for its own flags.
// Build-unblock for master breakage (see open PR #1009's identical fix).
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
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
QString computeArtifactFingerprintFast( const QString &path, qint64 size )
{
    QFile file( path );
    if ( size < 0 || !file.open( QIODevice::ReadOnly ) )
        return {};
    constexpr qint64 kWindow = 1024 * 1024;
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    QByteArray sizeLE( 8, '\0' );
    qToLittleEndian<qint64>( size, sizeLE.data() );
    hash.addData( sizeLE );
    // QFile::read may return a short buffer — loop until the window is
    // filled so the fingerprint is deterministic for a fixed file.
    auto readExactly = [&file]( qint64 count ) -> QByteArray {
        QByteArray out;
        while ( out.size() < count )
        {
            const QByteArray chunk = file.read( count - out.size() );
            if ( chunk.isEmpty() )
                break;
            out.append( chunk );
        }
        return out;
    };
    const QByteArray head = readExactly( qMin( kWindow, size ) );
    if ( head.size() != qMin( kWindow, size ) )
        return {};
    hash.addData( head );
    if ( size > kWindow )
    {
        if ( !file.seek( size - kWindow ) )
            return {};
        const QByteArray tail = readExactly( kWindow );
        if ( tail.size() != kWindow )
            return {};
        hash.addData( tail );
    }
    return QStringLiteral( "sha256fl:%1" ).arg( QString::fromLatin1( hash.result().toHex() ) );
}

/// Chunk granularity for the whole-file hash — also the cancel latency: the
/// loop polls @p cancel once per chunk, so a cancel observed mid-hash aborts
/// within one chunk (≤ 1 MiB of reading) instead of after the whole file.
constexpr qint64 kFullHashChunkBytes = 1024 * 1024;

/// sha256full: SHA-256 over [8-byte LE size][whole file streamed in
/// kFullHashChunkBytes chunks]. Bounded memory for arbitrarily large
/// artifacts; @p cancel (may be null) aborts the computation. Returns
/// cancelled=true when the caller aborted, an empty digest with
/// cancelled=false when the file cannot be read.
struct FingerprintResult
{
    QString digest;
    bool cancelled = false;
};

FingerprintResult computeArtifactFingerprintFull( const QString &path, qint64 size,
                                                  const std::atomic<bool> *cancel )
{
    const auto aborted = [cancel] {
        return cancel && cancel->load( std::memory_order_relaxed );
    };
    if ( aborted() )
        return { {}, true };
    QFile file( path );
    if ( size < 0 || !file.open( QIODevice::ReadOnly ) )
        return {};
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    QByteArray sizeLE( 8, '\0' );
    qToLittleEndian<qint64>( size, sizeLE.data() );
    hash.addData( sizeLE );
    qint64 remaining = size;
    // #1186 / C-F6: when the affinity thread is the GUI thread, a multi-GB
    // hash freezes the event loop so the user cannot click Cancel (phase-1
    // atomic is only set after the click is delivered). Pump events once
    // per chunk so input + the cancel hop can land; the atomic abort check
    // above still bounds cancel latency to one chunk.
    const bool pumpGui = QCoreApplication::instance()
                         && QThread::currentThread() == QCoreApplication::instance()->thread();
    while ( remaining > 0 )
    {
        if ( aborted() )
            return { {}, true };
        if ( pumpGui )
            QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        const QByteArray chunk = file.read( qMin( kFullHashChunkBytes, remaining ) );
        if ( chunk.isEmpty() )
            return {}; // short read: the file shrank under us — unverifiable
        hash.addData( chunk );
        remaining -= chunk.size();
    }
    return { QStringLiteral( "sha256full:%1" ).arg( QString::fromLatin1( hash.result().toHex() ) ),
             false };
}

/// Effective scheme for one artifact under @p mode: Full exactly when the
/// artifact is larger than the fast window (where sha256fl stops covering
/// the middle of the file), Fast otherwise.
bool useFullIdentity( ArtifactIdentityMode mode, qint64 size )
{
    switch ( mode )
    {
        case ArtifactIdentityMode::Fast:
            return false;
        case ArtifactIdentityMode::Full:
            return true;
        case ArtifactIdentityMode::Auto:
            return size > 2 * kFullHashChunkBytes;
    }
    return false;
}

FingerprintResult computeArtifactFingerprint( const QString &path, qint64 size,
                                              ArtifactIdentityMode mode,
                                              const std::atomic<bool> *cancel )
{
    if ( useFullIdentity( mode, size ) )
        return computeArtifactFingerprintFull( path, size, cancel );
    const QString digest = computeArtifactFingerprintFast( path, size );
    return { digest, false };
}

/// Re-verifies a RECORDED fingerprint against the live file. The recorded
/// tag selects the algorithm — a legacy "sha256fl:" record verifies with the
/// fast scheme, a "sha256full:" record with the whole-file hash — so mixed
/// and pre-1.1 checkpoints never need migration. An unknown scheme, an
/// unreadable file, or a caller abort fails closed (not verified).
bool verifyRecordedFingerprint( const QString &recordedTag, const QString &path, qint64 size,
                                const std::atomic<bool> *cancel, bool *cancelled = nullptr )
{
    if ( cancelled )
        *cancelled = false;
    if ( recordedTag.startsWith( QLatin1String( "sha256fl:" ) ) )
        return computeArtifactFingerprintFast( path, size ) == recordedTag;
    if ( recordedTag.startsWith( QLatin1String( "sha256full:" ) ) )
    {
        const FingerprintResult full = computeArtifactFingerprintFull( path, size, cancel );
        if ( cancelled )
            *cancelled = full.cancelled;
        return !full.cancelled && full.digest == recordedTag;
    }
    return false; // unknown scheme: never served
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
    // #1186: macOS default APFS/HFS+ is case-insensitive — CaseSensitive
    // rejects case-variant spellings of the same run dir (ir2.artifact_outside_run).
    return canonicalArtifact.startsWith( canonicalRunDir + QLatin1Char( '/' ),
#if defined( Q_OS_WIN ) || defined( Q_OS_MACOS )
                                         Qt::CaseInsensitive
#else
                                         Qt::CaseSensitive
#endif
    );
}

/// Returns false when the flush itself failed — atomicWriteJson must not
/// promote a file whose bytes never reached stable storage.
bool fsyncFile( const QString &path )
{
#ifdef Q_OS_WIN
    int fd = -1;
    if ( _wsopen_s( &fd, reinterpret_cast<const wchar_t *>( path.utf16() ),
                    _O_WRONLY | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE ) != 0 )
        return false;
    const int rc = _commit( fd );
    _close( fd );
    return rc == 0;
#else
    const int fd = ::open( QFile::encodeName( path ).constData(), O_RDONLY );
    if ( fd < 0 )
        return false;
    const int rc = ::fsync( fd );
    ::close( fd );
    return rc == 0;
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
    // A false return means the marshal itself failed (e.g. dying object).
    // Callers must NOT run the body on the foreign thread (#1186 / D7).
    return QMetaObject::invokeMethod( self, body, Qt::BlockingQueuedConnection );
}

/// Value-returning form of runOnCoordinatorThread — the ONE marshalling
/// pattern every public entry point uses (Track 13, DECISIONS D7). The
/// affinity thread never blocks on a foreign thread, so a blocking marshal
/// cannot deadlock; a foreign caller simply waits for the owner's current
/// unit of work to finish. A marshal that fails (dying object) yields a
/// default-constructed result, never a torn read.
template <typename F>
auto invokeOnCoordinatorThread( const PipelineRunCoordinator *self, F &&body ) -> decltype( body() )
{
    using Result = decltype( body() );
    auto *owner = const_cast<PipelineRunCoordinator *>( self );
    if ( QThread::currentThread() == self->thread() )
    {
        if constexpr ( std::is_void_v<Result> )
        {
            body();
            return;
        }
        else
        {
            return body();
        }
    }
    if constexpr ( std::is_void_v<Result> )
    {
        // #1186: marshal failure (dying object) drops the call — never run
        // affinity-thread bookkeeping inline on a foreign thread (D7).
        (void) runOnCoordinatorThread( owner, body );
        return;
    }
    else
    {
        Result result{};
        // BlockingQueuedConnection keeps the caller's stack (and @p body's
        // captured references) alive until the functor has run on the owner.
        QMetaObject::invokeMethod( owner, [&result, &body] { result = body(); },
                                   Qt::BlockingQueuedConnection );
        return result;
    }
}

/// Two-phase atomic write (DECISIONS D7): tmp -> flush -> fsync -> rename ->
/// dir fsync. Returns the final path, empty on failure. The tmp name is
/// unique per writer (pid + counter) so concurrent writes to the same target
/// — e.g. two processes resuming one checkpoint path — cannot interleave
/// their payloads on a shared tmp file. @p faultPoint (optional) arms the
/// deterministic crash-between-write-and-rename injection: it routes through
/// the REAL rename-failure branch (tmp removed, nothing promoted) exactly
/// like a locked target or a cross-device rename error.
QString atomicWriteJson( const QString &path, const QJsonObject &document, const char *faultPoint = nullptr )
{
    static std::atomic<quint64> s_tmpCounter{ 0 };
    const QString tmp = path + QStringLiteral( ".tmp.%1.%2" )
                            .arg( QCoreApplication::applicationPid() )
                            .arg( s_tmpCounter.fetch_add( 1 ) );
    QFile file( tmp );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return {};
    const QByteArray bytes = QJsonDocument( document ).toJson( QJsonDocument::Indented );
    // Writer honours the reader's cap: a document this build cannot re-read
    // is refused rather than promoted into a poison checkpoint.
    if ( bytes.size() > kMaxCheckpointDocumentBytes
         || file.write( bytes ) != bytes.size() || !file.flush() )
    {
        file.close();
        QFile::remove( tmp );
        return {};
    }
    file.close();
    if ( !fsyncFile( tmp ) )
    {
        QFile::remove( tmp );
        return {};
    }
    if ( faultPoint && SICNU_FAULT_POINT( faultPoint ) )
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
               const QString &runDirectory,
               const std::atomic<bool> *cancelRequested ) -> NodeExecutionResult {
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
    ArtifactIdentityMode identityMode = ArtifactIdentityMode::Auto;
    QString provenancePath;

    QHash<QString, NodeStatusSnapshot> statuses;
    QHash<QString, int> remainingParents; // nodeId -> unfinished parent count
    std::atomic<bool> cancelRequested{ false };
    // Set by the destructor's drain (on the affinity thread): stops dispatch
    // and completion handling so a foreign-thread destruction cannot race
    // onNodeFinished touching this state.
    std::atomic<bool> shuttingDown{ false };
    bool finished = false;
    bool success = false;
    // Resume attempt counter: persisted in the checkpoint so each attempt's
    // provenance file gets a distinct location — a resumed run must never
    // overwrite the record of the attempt it reuses artifacts from.
    int attempt = 0;

    ~RunState() { pool.waitForDone(); }
};

PipelineRunCoordinator::PipelineRunCoordinator( QObject *parent )
    : QObject( parent )
    , m_state( std::make_unique<RunState>() )
{
    qRegisterMetaType<sicnu::workflow::ExecutionState>( "sicnu::workflow::ExecutionState" );
    m_state->pool.setMaxThreadCount( m_state->maxParallelism );
}

PipelineRunCoordinator::~PipelineRunCoordinator()
{
    // Trip the cancel flag from ANY thread first: an artifact hash in flight
    // on the affinity thread then aborts within one chunk, so the drain below
    // never waits for a whole-file hash. Destroying on the affinity thread
    // (the documented fast path) runs the drain inline.
    m_state->cancelRequested.store( true, std::memory_order_relaxed );
    invokeOnCoordinatorThread( this, [this] {
        m_state->shuttingDown.store( true, std::memory_order_relaxed );
        m_state->pool.clear();    // drop queued, not-yet-started node work
        m_state->pool.waitForDone(); // drain the running workers
        // Completions already posted to this thread must not be delivered to
        // a dying object; the worker lambdas additionally hold a QPointer.
        QCoreApplication::removePostedEvents( this );
    } );
}

void PipelineRunCoordinator::setExecutor( NodeExecutor executor )
{
    invokeOnCoordinatorThread( this, [this, executor = std::move( executor )]() mutable {
        m_state->executor = std::move( executor );
    } );
}

void PipelineRunCoordinator::setMaxParallelism( int workers )
{
    invokeOnCoordinatorThread( this, [this, workers] {
        m_state->maxParallelism = std::clamp( workers, 1, kMaxPoolWorkers );
        m_state->pool.setMaxThreadCount( m_state->maxParallelism );
    } );
}

void PipelineRunCoordinator::setArtifactIdentityMode( ArtifactIdentityMode mode )
{
    invokeOnCoordinatorThread( this, [this, mode] { m_state->identityMode = mode; } );
}

ArtifactIdentityMode PipelineRunCoordinator::artifactIdentityMode() const
{
    return invokeOnCoordinatorThread( this, [this] { return m_state->identityMode; } );
}

QString PipelineRunCoordinator::checkpointPath() const
{
    return invokeOnCoordinatorThread( this, [this] { return m_state->checkpointPath; } );
}

QString PipelineRunCoordinator::provenancePath() const
{
    // Written on the affinity thread during finalizeIfDone — an unmarshal'd
    // read from a UI thread races the QString assignment.
    return invokeOnCoordinatorThread( this, [this] { return m_state->provenancePath; } );
}

bool PipelineRunCoordinator::isRunning() const
{
    // Marshal the read onto the affinity thread: onNodeFinished flips
    // m_state->finished there and an unsynchronised cross-thread read races.
    return invokeOnCoordinatorThread(
        this, [this] { return !m_state->finished && !m_state->def.nodes.isEmpty(); } );
}

bool PipelineRunCoordinator::hasCompleted() const
{
    return invokeOnCoordinatorThread( this, [this] { return m_state->finished; } );
}

QMap<QString, NodeStatusSnapshot> PipelineRunCoordinator::getAllStatuses() const
{
    return invokeOnCoordinatorThread( this, [this] {
        QMap<QString, NodeStatusSnapshot> map;
        for ( auto it = m_state->statuses.cbegin(); it != m_state->statuses.cend(); ++it )
            map.insert( it.key(), it.value() );
        return map;
    } );
}

bool PipelineRunCoordinator::startRun( const WorkflowDocument &def, const QString &runDirectory, QString *outError )
{
    // Marshalled like every other public entry point (DECISIONS D7): the run
    // state this body installs is also mutated by onNodeFinished on the
    // affinity thread.
    return invokeOnCoordinatorThread(
        this, [&] { return startRunOnAffinity( def, runDirectory, outError ); } );
}

bool PipelineRunCoordinator::startRunOnAffinity( const WorkflowDocument &def, const QString &runDirectory, QString *outError )
{
    auto fail = [outError]( const QString &message ) {
        if ( outError )
            *outError = message;
        return false;
    };

    if ( !m_state->finished && !m_state->def.nodes.isEmpty() )
        return fail( QStringLiteral( "a run is already active on this coordinator" ) );
    QString semanticError;
    if ( !WorkflowIR::validateSemantics( def, &semanticError ) )
        return fail( QStringLiteral( "workflow document is not semantically valid: %1" )
                         .arg( semanticError ) );

    // Composition seam: fragment instances flatten to plain nodes before
    // planning. The executor, checkpoint and lineage signatures only ever
    // see the expanded document — "workflow:subflow" never reaches them.
    WorkflowDocument runDef = def;
    if ( WorkflowComposer::hasSubflowNodes( def ) )
    {
        Result<WorkflowDocument> expanded = WorkflowComposer::expandSubflows( def );
        if ( !expanded.isSuccess() )
            return fail( expanded.error() );
        runDef = expanded.value();
        QString expandedError;
        if ( !WorkflowIR::validateSemantics( runDef, &expandedError ) )
            return fail( QStringLiteral( "expanded workflow document is not semantically valid: %1" )
                             .arg( expandedError ) );
    }

    const DagAnalysisResult dag = WorkflowDagAnalyzer::analyzeDag( runDef );
    if ( !dag.isAcyclic )
        return fail( dag.errorMessage );

    // Fresh state.
    m_state->def = runDef;
    m_state->runDirectory = runDirectory;
    m_state->runId = QUuid::createUuid().toString( QUuid::WithoutBraces );
    m_state->checkpointPath = QDir( runDirectory ).filePath( QStringLiteral( "checkpoint_%1.json" ).arg( m_state->runId ) );
    m_state->finished = false;
    m_state->success = false;
    // #1158: do NOT clear the cancel flag here. The previous run is
    // terminal (its finalization cleared the flag), so a set flag can only
    // be a requestCancel whose phase-2 hop is queued behind this start —
    // wiping it would swallow that cancel (nodes completing before the hop
    // would be recorded Succeeded with full artifact identity).
    m_state->statuses.clear();
    m_state->remainingParents.clear();
    m_state->provenancePath.clear();
    m_state->attempt = 1;

    const QMap<QString, QString> signatures = WorkflowPlanOptimizer::computeLineageSignatures( runDef );
    for ( const NodeFact &node : runDef.nodes )
    {
        NodeStatusSnapshot snapshot;
        snapshot.nodeId = node.nodeId;
        snapshot.lineageSignature = signatures.value( node.nodeId );
        m_state->statuses.insert( node.nodeId, snapshot );

        int parents = 0;
        for ( const EdgeFact &edge : runDef.edges )
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
    // Phase 1 — lock-free, callable from ANY thread: trip the atomic so a
    // whole-file artifact hash in flight on the affinity thread aborts within
    // one chunk. Phase 2 (status mutation) must never wait on that hash, so
    // the flag is set BEFORE the blocking marshal, not inside it (Track 13).
    m_state->cancelRequested.store( true, std::memory_order_relaxed );
    // Phase 2 — affinity thread: mutate statuses/bookkeeping. Callers already
    // on the affinity thread run inline; a failed marshal (dying object)
    // drops phase-2 — the atomic above still aborts in-flight hashes (#1186).
    if ( QThread::currentThread() == thread() )
    {
        requestCancelOnAffinity();
        return;
    }
    (void) runOnCoordinatorThread( this, [this] { requestCancelOnAffinity(); } );
}

void PipelineRunCoordinator::requestCancelOnAffinity()
{
    // Cancelling an idle coordinator must not emit a phantom completion:
    // with no document loaded there is nothing to cancel and finalizeIfDone
    // would emit "0 succeeded" for a run that never existed. The phase-1
    // store this hop followed has already tripped the atomic — un-trip it
    // HERE (#1158): clearing a lock-free foreign-writer flag anywhere on
    // the start/resume paths swallowed cancels that raced the clear.
    if ( m_state->def.nodes.isEmpty() || m_state->finished )
    {
        // Nothing to cancel (no document, or the loaded run is already
        // terminal — finalizeIfDone's early return skips its own clear).
        m_state->cancelRequested.store( false, std::memory_order_relaxed );
        return;
    }
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
    if ( m_state->finished || m_state->shuttingDown.load( std::memory_order_relaxed ) )
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
        // #1152: hand the run's cooperative cancel flag to the executor so
        // requestCancel() aborts a long-running registry operator mid-run
        // instead of freezing the GUI thread for the node's full duration.
        // RunState's destructor drains the pool before destruction, so the
        // pointer outlives every worker that can read it.
        const std::atomic<bool> *cancelFlag = &m_state->cancelRequested;
        const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
        m_state->pool.start( [self, node, inputArtifacts, runDirectory, startedAt,
                              cancelFlag, executor = std::move( executor )]() {
            NodeExecutionResult result;
            if ( executor )
                result = executor( node, inputArtifacts, runDirectory, cancelFlag );
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
    // The destructor's drain stops completion handling: no state mutation may
    // outlive the drain that precedes m_state's destruction.
    if ( m_state->shuttingDown.load( std::memory_order_relaxed ) )
        return;

    NodeStatusSnapshot &snapshot = m_state->statuses[nodeId];
    snapshot.elapsedMs = elapsedMs;

    if ( m_state->cancelRequested.load( std::memory_order_relaxed ) )
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
        else
        {
            // Track 13: the identity is computed here, on the affinity
            // thread. A whole-file hash streams in bounded chunks and polls
            // the cancel flag, so it never blocks unbounded and a cancel
            // landing mid-hash marks the node Cancelled (the run did not
            // produce it) instead of failing it.
            const FingerprintResult fingerprint =
                computeArtifactFingerprint( artifactInfo.canonicalFilePath(), artifactInfo.size(),
                                            m_state->identityMode, &m_state->cancelRequested );
            if ( fingerprint.cancelled )
            {
                snapshot.state = ExecutionState::Cancelled;
            }
            else if ( fingerprint.digest.isEmpty() )
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
                snapshot.artifactFingerprint = fingerprint.digest;
                snapshot.progress = 1.0f;
            }
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

    // Provenance (WP5/D9): one queryable lineage record per terminal ATTEMPT,
    // emitted for successes AND failures — audits need the failure paths
    // most. The attempt suffix keeps a resumed run from overwriting the
    // record whose artifacts it marked reusedFrom. A write failure never
    // fails the run it describes.
    if ( !m_state->def.nodes.isEmpty() )
    {
        const ProvenanceGraph graph = ProvenanceGraph::fromRunState(
            m_state->runId, m_state->def, m_state->statuses,
            WorkflowPlanOptimizer::computePlanSignature( m_state->def ) );
        // Attempt 1 keeps the canonical provenance_<runId>.json beside the
        // checkpoint; later attempts live in attempt-<N>/ — the SYSTEM
        // lineage in a path segment and the USER identity in the filename.
        // A valid runId cannot contain '/', so no user-named runId can
        // collide with the attempt encoding (DECISIONS D6); a single-segment
        // suffix could not promise that.
        QString provenanceTarget = QDir( m_state->runDirectory ).filePath(
            QStringLiteral( "provenance_%1.json" ).arg( m_state->runId ) );
        if ( m_state->attempt > 1 )
        {
            const QString attemptDir = QDir( m_state->runDirectory ).filePath(
                QStringLiteral( "attempt-%1" ).arg( m_state->attempt ) );
            QDir().mkpath( attemptDir );
            provenanceTarget =
                QDir( attemptDir ).filePath( QStringLiteral( "provenance_%1.json" ).arg( m_state->runId ) );
        }
        const QString path = atomicWriteJson( provenanceTarget, graph.toJson(), "d17_provenance.publish" );
        if ( !path.isEmpty() )
            m_state->provenancePath = path;
    }

    emit pipelineCompleted(
        success,
        QStringLiteral( "%1 succeeded, %2 skipped, %3 failed, %4 cancelled" )
            .arg( succeeded )
            .arg( skipped )
            .arg( failed )
            .arg( cancelled ) );

    // The run is terminal: the cancel flag describes THIS run only, so
    // clear it now — never earlier (#1158). A foreign requestCancel that
    // lands after this point takes the idle path, which clears its own
    // phase-1 store.
    m_state->cancelRequested.store( false, std::memory_order_relaxed );
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
    document.insert( QLatin1String( "attempt" ), m_state->attempt );
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

    const QString path = atomicWriteJson( m_state->checkpointPath, document, "d17_checkpoint.publish" );
    if ( !path.isEmpty() )
        emit checkpointPersisted( path );
}

bool PipelineRunCoordinator::resumeFromCheckpoint( const QString &checkpointFilePath, QString *outError )
{
    // Symmetric with startRun: a live run's queued completions must never
    // mutate a resumed run's state, so the whole body runs on the owner.
    return invokeOnCoordinatorThread(
        this, [&] { return resumeOnAffinity( checkpointFilePath, outError ); } );
}

bool PipelineRunCoordinator::resumeOnAffinity( const QString &checkpointFilePath, QString *outError )
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

    // #1158: no stale-clear before the verification loop. Stale flags can
    // no longer exist (finalizeIfDone clears at terminal state; the idle
    // requestCancel hop clears its own phase-1 store), so a set flag here
    // is a LIVE cancel — the loop's checks abort on it and the commit
    // below refuses when it is set.

    QFile file( checkpointFilePath );
    if ( !file.open( QIODevice::ReadOnly ) )
        return fail( QStringLiteral( "cannot open checkpoint '%1'" ).arg( checkpointFilePath ) );
    // Bounded read (DECISIONS D5): a checkpoint is a small JSON sidecar —
    // anything past the shared cap is planted or corrupt and must not be
    // buffered unbounded. Read cap+1 rather than size-then-readAll so a file
    // growing between the two calls cannot bypass the bound.
    const QByteArray raw = file.read( kMaxCheckpointDocumentBytes + 1 );
    if ( raw.size() > kMaxCheckpointDocumentBytes )
        return fail( QStringLiteral( "checkpoint '%1' exceeds the %2-byte size cap" )
                         .arg( checkpointFilePath )
                         .arg( kMaxCheckpointDocumentBytes ) );
    const QJsonDocument doc = QJsonDocument::fromJson( raw );
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
    // safely name a file. Mirrors isValidRunId in workflow_run.cpp: bounded
    // length, no leading dot, [A-Za-z0-9._-] only (a leading '.' plus '..' is
    // the traversal vector — interior dots are legitimate filename chars).
    const QString runId = document.value( QLatin1String( "runId" ) ).toString();
    if ( runId.isEmpty() || runId.size() > 128 || runId.startsWith( QLatin1Char( '.' ) )
         || !runId.contains( QRegularExpression( QStringLiteral( "\\A[A-Za-z0-9._-]+\\z" ) ) ) )
        return fail( QStringLiteral( "checkpoint '%1' carries an unsafe runId '%2'" )
                         .arg( checkpointFilePath, runId ) );
    auto parsed = WorkflowIR::fromJson( document.value( QLatin1String( "workflow" ) ).toObject() );
    if ( !parsed.isSuccess() )
        return fail( QStringLiteral( "checkpoint workflow does not parse: %1" ).arg( parsed.error() ) );

    // The checkpoint's embedded workflow gets the SAME gate a fresh document
    // gets in startRun: semantic validity (dangling endpoints, in-degree)
    // plus acyclicity. Without this a mutated checkpoint whose edges form a
    // cycle or reference ghost nodes parses fine, returns true here, and then
    // deadlocks the resumed run — every cycle node keeps remainingParents > 0
    // forever, no completion signal ever fires.
    const WorkflowDocument resumedDef = parsed.value();
    QString semanticError;
    if ( !WorkflowIR::validateSemantics( resumedDef, &semanticError ) )
        return fail( QStringLiteral( "checkpoint workflow is not semantically valid: %1" )
                         .arg( semanticError ) );
    const DagAnalysisResult resumedDag = WorkflowDagAnalyzer::analyzeDag( resumedDef );
    if ( !resumedDag.isAcyclic )
        return fail( QStringLiteral( "checkpoint workflow is not acyclic: %1" )
                         .arg( resumedDag.errorMessage ) );

    // Validate node set + count BEFORE mutating m_state so a corrupt
    // checkpoint cannot wedge the coordinator as "already active" (#1078c).
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

    // Replay statuses into a LOCAL map — every remaining fail path (state
    // vocabulary) runs before m_state is touched, so a rejected checkpoint
    // leaves the coordinator reusable instead of half-loaded.
    const QMap<QString, QString> recomputed = WorkflowPlanOptimizer::computeLineageSignatures( resumedDef );
    const QString runDirectory = document.value( QLatin1String( "runDirectory" ) ).toString();
    const QString canonicalRunDir = QDir( runDirectory ).canonicalPath();
    QHash<QString, NodeStatusSnapshot> restored;
    restored.reserve( nodes.size() );
    for ( const QJsonValue &value : nodes )
    {
        const QJsonObject entry = value.toObject();
        const QString nodeId = entry.value( QLatin1String( "nodeId" ) ).toString();

        // A cancel requested while this (possibly whole-file) verification
        // pass runs aborts the resume before anything is dispatched — no
        // partial publish, no phantom run.
        if ( m_state->cancelRequested.load( std::memory_order_relaxed ) )
            return fail( QStringLiteral( "checkpoint '%1' verification cancelled" )
                             .arg( checkpointFilePath ) );

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

        // A node becomes CacheHit iff ALL of: recorded state Succeeded,
        // lineage signature still matches, and the recorded artifact
        // re-verifies — exists, resolves inside the recorded run directory,
        // and matches size / mtime / content fingerprint. Anything less
        // reverts to Pending and recomputes: a tampered, moved, or foreign
        // artifact is never served (DECISIONS D3). The recorded fingerprint
        // TAG selects the verification algorithm, so legacy sha256fl records
        // keep the fast scheme and sha256full records get the whole-file
        // hash — mixed checkpoints need no migration. Checkpoints written
        // before format 1.1 carry no identity fields and degrade to a full
        // recompute — correct but cold.
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
            bool hashCancelled = false;
            bool identityMatches = false;
            if ( artifactInfo.isFile() )
            {
                identityMatches =
                    verifyRecordedFingerprint( recordedFingerprint, snapshot.outputArtifactPath,
                                               artifactInfo.size(), &m_state->cancelRequested,
                                               &hashCancelled );
                if ( hashCancelled )
                    return fail( QStringLiteral( "checkpoint '%1' verification cancelled at node '%2'" )
                                     .arg( checkpointFilePath, nodeId ) );
            }
            artifactVerified =
                identityMatches
                && isContainedInDirectory( snapshot.outputArtifactPath, canonicalRunDir )
                && recordedSize >= 0 && recordedSize == artifactInfo.size() && recordedMtime >= 0
                && recordedMtime == liveMtime && !recordedFingerprint.isEmpty();
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
        else
        {
            // Recompute — Skipped re-derives at the frontier; Failed /
            // Cancelled / Running and unverifiable Succeeded all re-run.
            // Stale artifact path AND stale diagnostics are dropped: a
            // Pending node must not carry a previous attempt's error text
            // or progress into this run.
            snapshot.state = ExecutionState::Pending;
            snapshot.outputArtifactPath.clear();
            snapshot.errorMessage.clear();
            snapshot.progress = 0.0f;
            snapshot.elapsedMs = 0;
        }
        snapshot.lineageSignature = recomputed.value( nodeId );
        restored.insert( nodeId, snapshot );
    }
    if ( restored.size() != resumedDef.nodes.size() )
        return fail( QStringLiteral( "checkpoint is missing node statuses" ) );

    // #1158: a cancel that landed during verification must not be wiped by
    // the commit (the pre-fix clear swallowed it — already-dispatched nodes
    // then recorded Succeeded with full artifact identity and poisoned the
    // resume cache). Refuse the resume instead: nothing has run yet.
    if ( m_state->cancelRequested.load( std::memory_order_relaxed ) )
    {
        // The cancel is honored by this refusal — nothing started, so
        // consume it here rather than leaving it to poison the next resume
        // (finalizeIfDone early-returns on the already-terminal state and
        // would never clear it).
        m_state->cancelRequested.store( false, std::memory_order_relaxed );
        return fail( QStringLiteral( "resume cancelled before it started" ) );
    }

    // All validation passed — commit to run state in one step. The attempt
    // counter continues from the checkpoint so this resume's provenance file
    // does not overwrite the record of the attempt it reuses artifacts from.
    m_state->def = resumedDef;
    m_state->runDirectory = runDirectory;
    m_state->runId = runId;
    // #1186: attempt-less v1.1 checkpoints must resume as attempt 2 (default
    // 1 + 1), not attempt 1 — otherwise provenance overwrites the original
    // attempt's canonical record under the same path.
    m_state->attempt = document.value( QLatin1String( "attempt" ) ).toInteger( 1 ) + 1;
    m_state->checkpointPath = checkpointFilePath;
    m_state->finished = false;
    m_state->success = false;
    m_state->statuses = restored;
    m_state->remainingParents.clear();
    m_state->provenancePath.clear();

    // Second pass over the FULL status map (never the partially filled one):
    // count only parents that still need to RUN this round — CacheHit
    // (Succeeded) parents release their children immediately, otherwise a
    // fully-cached prefix would stall the resumed frontier. Document order
    // of the checkpoint array is irrelevant.
    // Iterate the stored document directly (m_state->def was assigned the
    // parsed checkpoint above): the msbuild/clang builds of this file also
    // reject the same-scope redefinition of `resumedDef` (C2373).
    for ( const NodeFact &node : m_state->def.nodes )
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
