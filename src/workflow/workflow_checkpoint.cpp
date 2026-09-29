#include "workflow_checkpoint.h"

#include "runtime/observability/fault_point.h"
#include "workflow_limits.h"
#include "workflow_run_lock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include "geospatial/util/atomic_fs.h"
#include "platform/portable.h"
#include "platform/durable_sidecar.h"
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>

#if defined( Q_OS_UNIX )
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sicnu::workflow {

namespace {

} // namespace

QString WorkflowCheckpointManager::defaultCheckpointDirectory()
{
  // Session isolation knob (Track 10 R5): two processes sharing $HOME no
  // longer have to share checkpoint state. An MCP/CLI session (or a test)
  // relocates the whole checkpoint family — checkpoints, run locks, history —
  // for its process lifetime; empty/unset keeps the historical default.
  const QString overrideDir = qEnvironmentVariable( "SICNU_CHECKPOINT_DIR" );
  if ( !overrideDir.isEmpty() )
    return overrideDir;
  QString base = QDir::homePath() + QStringLiteral( "/.rs_studio/checkpoints" );
  return base;
}

QString WorkflowCheckpointManager::saveCheckpoint( const WorkflowRun &run, const QString &directoryPath )
{
  const std::string runIdRaw = run.runId();
  if ( !isValidRunId( runIdRaw ) )
    return QString(); // runId is embedded in the filename; refuse unsafe ids

  const QString dir = directoryPath.isEmpty() ? defaultCheckpointDirectory() : directoryPath;
  QDir().mkpath( dir );

  const QString runId = QString::fromStdString( runIdRaw );
  const QString finalPath = QDir( dir ).filePath( QStringLiteral( "checkpoint_%1.json" ).arg( runId ) );

  const Json::Value root = run.toJson();
  Json::StreamWriterBuilder writerBuilder;
  writerBuilder["indentation"] = "  ";
  const std::string jsonStr = Json::writeString( writerBuilder, root );

  // The writer honours the reader's cap (mirrors atomicWriteJson's writer
  // check): a checkpoint past kMaxCheckpointDocumentBytes is one this build's
  // loadCheckpoint always refuses, so promoting it would trade a clear save
  // failure for a silently unrecoverable run (every later load warns and
  // skips).
  if ( static_cast<qint64>( jsonStr.size() ) > kMaxCheckpointDocumentBytes )
  {
    qWarning( "WorkflowCheckpointManager: checkpoint for run %s exceeds the %lld byte size cap "
              "(%lld bytes); refusing to write it",
              runIdRaw.c_str(), static_cast<long long>( kMaxCheckpointDocumentBytes ),
              static_cast<long long>( jsonStr.size() ) );
    return QString();
  }

  // Injected write/publish failures (Verification 7.0 fault matrix) keep
  // their armed names and their externally-visible contract: nothing is
  // promoted, the previous checkpoint stays intact. They fire BEFORE the
  // authority runs so the real publish never races the fault.
  const bool writeFault = SICNU_FAULT_POINT( "workflow_checkpoint.write" );
  const bool publishFault = SICNU_FAULT_POINT( "workflow_checkpoint.publish" );
  if ( writeFault || publishFault )
    return QString();

  // R6: the single sidecar write authority — O_EXCL temp, fsync/FlushFileBuffers
  // gate (the old Windows lane relied on flush() alone: rename-atomic but the
  // bytes were not required to be on the device before the rename), atomic
  // replace, best-effort directory fsync. The old hand-rolled tmp+rename
  // (and its QIODevice::Text CRLF translation on Windows — the reader opens
  // Text mode and never sees the difference) is gone.
  const sicnu::platform::sidecar::WriteResult result = sicnu::platform::sidecar::write(
    { finalPath.toUtf8().constData(), jsonStr, "" } );
  if ( !result )
    return QString();

  return finalPath;
}
std::unique_ptr<WorkflowRun> WorkflowCheckpointManager::loadCheckpoint( const QString &filePath, QString *error )
{
  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    if ( error )
      *error = QStringLiteral( "Failed to open checkpoint file: %1" ).arg( filePath );
    return nullptr;
  }
  if ( file.size() > kMaxCheckpointDocumentBytes )
  {
    file.close();
    if ( error )
      *error = QStringLiteral( "checkpoint file exceeds the %1 byte size cap: %2" )
                   .arg( kMaxCheckpointDocumentBytes )
                   .arg( filePath );
    return nullptr;
  }

  const QByteArray data = file.readAll();
  file.close();

  Json::CharReaderBuilder readerBuilder;
  Json::Value root;
  std::string errs;
  std::istringstream stream( data.toStdString() );
  if ( !Json::parseFromStream( readerBuilder, stream, &root, &errs ) )
  {
    if ( error )
      *error = QStringLiteral( "Failed to parse checkpoint JSON: %1" ).arg( QString::fromStdString( errs ) );
    return nullptr;
  }

  std::string parseErr;
  auto run = WorkflowRun::fromJson( root, parseErr );
  if ( !run )
  {
    if ( error )
      *error = QString::fromStdString( parseErr );
    return nullptr;
  }

  return run;
}

QStringList WorkflowCheckpointManager::listCheckpoints( const QString &directoryPath )
{
  const QString dir = directoryPath.isEmpty() ? defaultCheckpointDirectory() : directoryPath;
  QDir d( dir );
  if ( !d.exists() )
    return QStringList();

  const QStringList entries = d.entryList( QStringList{ QStringLiteral( "*.json" ) }, QDir::Files, QDir::Time );
  QStringList result;
  result.reserve( entries.size() );
  for ( const QString &entry : entries )
  {
    result.append( d.absoluteFilePath( entry ) );
  }
  return result;
}

bool WorkflowCheckpointManager::reconcileToInterrupted( WorkflowRun &run )
{
  const WorkflowRunState st = run.state();
  if ( st != WorkflowRunState::Running
       && st != WorkflowRunState::Planning
       && st != WorkflowRunState::WaitingResource
       && st != WorkflowRunState::Cancelling )
  {
    return false;
  }

  run.forceSetState( WorkflowRunState::Interrupted );
  run.setErrorMessage( "Execution interrupted by system shutdown/restart." );

  // Reconcile step plans: a crash may have left steps marked as actively
  // executing; a resumed run must treat them as not yet run.
  const std::vector<StepPlan> plans = run.stepPlans();
  for ( const StepPlan &plan : plans )
  {
    if ( plan.status == "Running" || plan.status == "Cancelling" )
      run.setStepStatus( plan.stepId, "Pending" );
  }
  return true;
}

bool WorkflowCheckpointManager::archiveCompletedRun( const QString &checkpointPath,
                                                     const QString &directoryPath, int keep )
{
  QFile source( checkpointPath );
  if ( !source.exists() )
    return false;
  QDir dir( directoryPath );
  if ( !dir.mkpath( QStringLiteral( "history" ) ) )
    return false;
  const QString historyDir = dir.filePath( QStringLiteral( "history" ) );
  const QString target = QDir( historyDir ).filePath( QFileInfo( checkpointPath ).fileName() );
  QFile::remove( target );
  if ( !source.rename( target ) )
    return false;

  // Bound the archive: newest first, prune the rest.
  QDir history( historyDir );
  const QStringList entries =
      history.entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files,
                         QDir::Time );
  for ( int i = keep; i < entries.size(); ++i )
    QFile::remove( history.filePath( entries.at( i ) ) );
  return true;
}

namespace
{

/// True for the states recoverInterruptedRuns reconciles and resubmits.
/// Only these states make a checkpoint a duplicate-execution hazard worth
/// electing against its lineage; a terminal checkpoint (e.g. the Canceled
/// ghost a NORMAL resume swap leaves behind) is inert for recovery and must
/// stay standalone so it is never quarantined by its original's election.
bool isRecoveryCandidateState( const std::string &state )
{
  return state == "Running" || state == "Planning" || state == "WaitingResource"
         || state == "Cancelling";
}

/// Election group of one checkpoint file (Track 13, DECISIONS D5). The group
/// decides which files may represent the SAME run lineage and therefore which
/// of them a crash could have left duplicated.
///
/// The lineage is read from the payload's ENVELOPE when the file carries one:
/// a version-2+ checkpoint declaring `resumeOf: X` groups under X (the resume
/// ghost joins its original whatever its filename), and a version-2+
/// checkpoint WITHOUT resumeOf groups under its OWN runId — standalone, so a
/// user-named run like "foo_resume" is never merged into an unrelated run
/// "foo" and quarantined by its election.
///
/// Legacy (version-1) and unparseable payloads carry no envelope, so the
/// historical filename-suffix rule remains their only evidence — that is the
/// migration path that still recognizes legacy `_resume` ghosts.
/// @p isResumeGhost reports whether the file's own envelope declares it a
/// resume submission of another run (the derivative in a crash window).
QString electionGroupFor( const QString &filePath, const QString &legacyGroup, bool *isResumeGhost )
{
  *isResumeGhost = false;
  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return legacyGroup;
  // Read cap+1 rather than size-then-readAll so a file growing between the
  // two calls cannot bypass the bound (same idiom as loadCheckpoint).
  const QByteArray data = file.read( kMaxCheckpointDocumentBytes + 1 );
  file.close();
  if ( data.size() > kMaxCheckpointDocumentBytes )
    return legacyGroup; // oversized: same treatment as the corrupt case

  Json::CharReaderBuilder readerBuilder;
  Json::Value root;
  std::string errs;
  std::istringstream stream( data.toStdString() );
  if ( !Json::parseFromStream( readerBuilder, stream, &root, &errs ) || !root.isObject() )
    return legacyGroup;
  if ( !root.isMember( "version" ) || !root["version"].isInt()
       || root["version"].asInt() < 2 )
    return legacyGroup; // no envelope: the filename suffix is the evidence
  if ( !root.isMember( "runId" ) || !root["runId"].isString() )
    return legacyGroup;
  const std::string runId = root["runId"].asString();
  if ( !isValidRunId( runId ) )
    return legacyGroup;
  const std::string state =
      root.isMember( "state" ) && root["state"].isString() ? root["state"].asString() : "";
  const bool declaredResume = root.isMember( "resumeOf" ) && root["resumeOf"].isString()
                              && !root["resumeOf"].asString().empty();
  // A non-recovery-candidate payload cannot be resurrected, so it never needs
  // an election at all — it groups under its own identity.
  if ( !state.empty() && !isRecoveryCandidateState( state ) )
    return QString::fromStdString( runId );
  if ( declaredResume )
  {
    const std::string resumeOf = root["resumeOf"].asString();
    if ( isValidRunId( resumeOf ) )
    {
      *isResumeGhost = true;
      return QString::fromStdString( resumeOf ); // declared lineage wins
    }
  }
  return QString::fromStdString( runId ); // standalone user identity
}

} // namespace

int WorkflowCheckpointManager::electCheckpoints( const QString &directoryPath )
{
  QDir dir( directoryPath );
  if ( !dir.exists() )
    return 0;
  // Group checkpoint files by lineage; a run appearing both as the original
  // checkpoint and as a post-resume ghost means a crash landed between the
  // fresh submission's persist and the ghost delete. Exactly one may live.
  // Grouping follows the DECLARED lineage (resumeOf) for enveloped
  // checkpoints and the legacy filename suffix only for legacy/corrupt ones —
  // a user-named "*_resume" run can no longer be grouped with an unrelated
  // run and quarantined by its election.
  struct Candidate
  {
    QFileInfo info;
    bool isResumeGhost = false;
  };
  QMultiMap<QString, Candidate> byLineage;
  const QStringList entries = dir.entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) },
                                             QDir::Files );
  for ( const QString &entry : entries )
  {
    // Exact prefix/suffix stripping: QString::remove would strip EVERY
    // occurrence, collapsing distinct runIds (e.g. "checkpoint_a" and "a")
    // into one election group and orphaning a valid resume handle.
    if ( !entry.startsWith( QLatin1String( "checkpoint_" ) )
         || !entry.endsWith( QLatin1String( ".json" ) ) )
      continue;
    QString runId = entry.mid( QStringLiteral( "checkpoint_" ).size() );
    runId.chop( QStringLiteral( ".json" ).size() );
    QString canonical = runId;
    if ( canonical.endsWith( QLatin1String( "_resume" ) )
         && canonical.size() > QStringLiteral( "_resume" ).size() )
      canonical.chop( QStringLiteral( "_resume" ).size() );
    Candidate candidate;
    candidate.info = QFileInfo( dir.filePath( entry ) );
    byLineage.insert( electionGroupFor( candidate.info.absoluteFilePath(), canonical,
                                        &candidate.isResumeGhost ),
                      candidate );
  }
  int quarantined = 0;
  // uniqueKeys(): QMultiMap iteration visits (key, value) PAIRS, so a plain
  // iterator would re-run the election once per file and double-count.
  const QStringList lineageKeys = byLineage.uniqueKeys();
  for ( const QString &lineage : lineageKeys )
  {
    const QList<Candidate> group = byLineage.values( lineage );
    if ( group.size() < 2 )
      continue;
    // Prefer the COMPLETE lineage: a resume ghost is derivable from its
    // original, which also carries every earlier pass's completed plans, so
    // when both survived a crash the original is the one worth keeping.
    // Recency decides only among files of the same kind.
    QList<Candidate> electable = group;
    if ( std::any_of( group.cbegin(), group.cend(),
                      []( const Candidate &c ) { return !c.isResumeGhost; } ) )
    {
      electable.clear();
      for ( const Candidate &candidate : group )
        if ( !candidate.isResumeGhost )
          electable.append( candidate );
    }
    // Keep the newest mtime; quarantine the rest (rename, never delete — the
    // bytes stay available for forensics and are outside checkpoint listing).
    QFileInfo newest;
    for ( const Candidate &candidate : electable )
      if ( newest.filePath().isEmpty() || candidate.info.lastModified() > newest.lastModified() )
        newest = candidate.info;
    for ( const Candidate &candidate : group )
    {
      if ( candidate.info.absoluteFilePath() == newest.absoluteFilePath() )
        continue;
      const QString src = candidate.info.absoluteFilePath();
      const QString dst = src + QLatin1String( ".orphaned" );
      // #1186: replace an existing .orphaned so a prior partial quarantine
      // cannot leave the loser live (QFile::rename refuses existing targets
      // on Windows). Fail closed with a warning when the rename still fails.
      if ( sicnu::geo::atomic_fs::renameReplaceQuiet( src.toStdString(), dst.toStdString() ) )
        ++quarantined;
      else
        qWarning( "WorkflowCheckpointManager: failed to quarantine loser checkpoint %s",
                  qPrintable( src ) );
    }
  }
  return quarantined;
}

std::vector<std::shared_ptr<WorkflowRun>> WorkflowCheckpointManager::recoverInterruptedRuns( const QString &directoryPath )
{
  const QString dir = directoryPath.isEmpty() ? defaultCheckpointDirectory() : directoryPath;

  // Sweep orphaned tmp files from crashed saves, and retired election losers
  // (*.orphaned). Recovery runs at startup, before any new saves (#1186).
  // A tmp file whose run is still LOCKED by a live process is a save in
  // flight, not a crashed one: deleting it would break that writer's final
  // rename so its checkpoint would silently never appear. Liveness follows
  // the lock primitive (#727), never the pid embedded in the tmp name.
  {
    QDir d( dir );
    if ( d.exists() )
    {
      const QStringList tmpOrphans = d.entryList(
        QStringList{ QStringLiteral( "checkpoint_*.json.tmp.*" ) }, QDir::Files );
      for ( const QString &orphan : tmpOrphans )
      {
        // The tmp suffix pid/counter are digits, so the LAST ".json.tmp."
        // marker is always the true suffix separator — a legal runId may
        // itself contain ".json.tmp." (isValidRunId allows '.'), and a
        // first-marker split would probe the wrong run's lock and sweep a
        // live writer's in-flight tmp.
        const QString marker = QStringLiteral( ".json.tmp." );
        const qsizetype markerPos = orphan.lastIndexOf( marker );
        if ( !orphan.startsWith( QLatin1String( "checkpoint_" ) ) || markerPos < 0 )
          continue; // not a name this writer family produces
        const QString runId =
          orphan.mid( QStringLiteral( "checkpoint_" ).size(),
                      markerPos - QStringLiteral( "checkpoint_" ).size() );
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner(
          WorkflowRunLock::lockPathForRun( dir, runId.toStdString() ) );
        if ( probe.state == WorkflowRunLock::OwnerProbe::State::LiveOwner )
          continue;
        QFile::remove( d.absoluteFilePath( orphan ) );
      }
      const QStringList retired = d.entryList(
        QStringList{ QStringLiteral( "checkpoint_*.json.orphaned" ),
                     QStringLiteral( "*.orphaned" ) },
        QDir::Files );
      for ( const QString &orphan : retired )
        QFile::remove( d.absoluteFilePath( orphan ) );

      // R6: the authority-staged temps have the shape
      // "checkpoint_<runId>.<pid>.<ctr>.<rng>.tmp.json". A temp whose owner
      // pid is gone is inert residue from a killed save; sweep it with the
      // same live-owner guard as the old "<name>.json.tmp.*" family.
      const QStringList authorityTemps = d.entryList(
        QStringList{ QStringLiteral( "checkpoint_*.tmp.json" ) }, QDir::Files );
      for ( const QString &orphan : authorityTemps )
      {
        const QString stem = orphan.left( orphan.size() -
                                          QStringLiteral( ".tmp.json" ).size() );
        const QStringList fields = stem.split( QLatin1Char( '.' ) );
        if ( fields.size() < 5 )
          continue; // checkpoint_<runId> + at least pid.ctr.rng
        const QString pidField = fields.at( fields.size() - 3 );
        const QString ctrField = fields.at( fields.size() - 2 );
        const QString rngField = fields.at( fields.size() - 1 );
        bool pidOk = false, ctrOk = false, rngOk = false;
        const qlonglong pidValue = pidField.toLongLong( &pidOk );
        ctrField.toLongLong( &ctrOk );
        rngField.toLongLong( &rngOk );
        if ( !pidOk || !ctrOk || !rngOk || pidValue <= 0 )
          continue; // not a name this writer family produces
        QString runId = fields.mid( 1, fields.size() - 4 ).join( QLatin1Char( '.' ) );
        const WorkflowRunLock::OwnerProbe probe = WorkflowRunLock::probeOwner(
          WorkflowRunLock::lockPathForRun( dir, runId.toStdString() ) );
        if ( probe.state == WorkflowRunLock::OwnerProbe::State::LiveOwner )
          continue;
        QFile::remove( d.absoluteFilePath( orphan ) );
      }
    }
  }

  // Phase J (W3): crash-election between a run checkpoint and its post-resume
  // ghost BEFORE loading — resuming both would double-execute the remaining
  // steps.
  electCheckpoints( dir );

  const QStringList checkpointFiles = listCheckpoints( dir );

  std::vector<std::shared_ptr<WorkflowRun>> recovered;
  for ( const QString &cpFile : checkpointFiles )
  {
    QString err;
    auto run = loadCheckpoint( cpFile, &err );
    if ( !run )
    {
      qWarning( "WorkflowCheckpointManager: skipping corrupt checkpoint %s: %s",
                qPrintable( cpFile ), qPrintable( err ) );
      continue;
    }

    const WorkflowRunState st = run->state();
    if ( st == WorkflowRunState::Running
         || st == WorkflowRunState::Planning
         || st == WorkflowRunState::WaitingResource
         || st == WorkflowRunState::Cancelling )
    {
      // Cross-process ownership (#727): a run held by a LIVE process is left
      // exactly as it is — never reconciled, never rewritten, not reported.
      // Acquiring the run lock proves the previous owner is gone (the kernel
      // released its flock on death), which is what makes reconciliation safe.
      WorkflowRunLock runLock( WorkflowRunLock::lockPathForRun( dir, run->runId() ) );
      QString heldByPid;
      const WorkflowRunLock::TryResult acquired = runLock.tryAcquire( &heldByPid );
      if ( acquired == WorkflowRunLock::TryResult::HeldByLiveOwner )
      {
        qInfo( "WorkflowCheckpointManager: run %s is owned by a live process (pid %s); not recovered",
               run->runId().c_str(), qPrintable( heldByPid ) );
        continue;
      }
      if ( acquired == WorkflowRunLock::TryResult::Error )
      {
        qWarning( "WorkflowCheckpointManager: cannot lock run %s; not recovered",
                  run->runId().c_str() );
        continue;
      }

      ( void )reconcileToInterrupted( *run );

      const QString resavedPath = saveCheckpoint( *run, dir );
      if ( resavedPath.isEmpty() )
      {
        // Disk state is unchanged; do not report this run as recovered so a
        // later recovery pass retries it.
        qWarning( "WorkflowCheckpointManager: failed to persist interrupted state for %s",
                  qPrintable( cpFile ) );
        continue;
      }
      recovered.push_back( std::shared_ptr<WorkflowRun>( std::move( run ) ) );
      runLock.release(); // resumable again by whoever acquires the lock next
    }
  }

  return recovered;
}

} // namespace sicnu::workflow
