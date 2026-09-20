#include "workflow_checkpoint.h"

#include "runtime/observability/fault_point.h"
#include "workflow_limits.h"
#include "workflow_run_lock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

void fsyncDirectory( const QString &dirPath )
{
#if defined( Q_OS_UNIX )
  const int dfd = ::open( QDir::toNativeSeparators( dirPath ).toUtf8().constData(),
                          O_RDONLY | O_DIRECTORY );
  if ( dfd >= 0 )
  {
    ::fsync( dfd );
    ::close( dfd );
  }
#else
  Q_UNUSED( dirPath );
#endif
}

} // namespace

QString WorkflowCheckpointManager::defaultCheckpointDirectory()
{
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

  // Unique per-save tmp name: concurrent saves of the same run can never
  // interleave writes on a shared tmp file.
  static std::atomic<uint64_t> s_tmpCounter{ 0 };
  const QString tmpPath = finalPath + QStringLiteral( ".tmp.%1.%2" )
                             .arg( QCoreApplication::applicationPid() )
                             .arg( QString::number( s_tmpCounter.fetch_add( 1 ) ) );

  const Json::Value root = run.toJson();
  Json::StreamWriterBuilder writerBuilder;
  writerBuilder["indentation"] = "  ";
  const std::string jsonStr = Json::writeString( writerBuilder, root );

  QFile file( tmpPath );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return QString();

  const qint64 written = file.write( jsonStr.data(), static_cast<qint64>( jsonStr.size() ) );
  if ( written != static_cast<qint64>( jsonStr.size() ) || !file.flush() )
  {
    file.close();
    QFile::remove( tmpPath );
    return QString(); // a truncated payload must never be promoted
  }
  // Injected write failure (Verification 7.0 fault matrix): same cleanup as a
  // short write — the tmp file is removed and nothing is promoted.
  if ( SICNU_FAULT_POINT( "workflow_checkpoint.write" ) )
  {
    file.close();
    QFile::remove( tmpPath );
    return QString();
  }

  const int fd = file.handle();
#if defined( Q_OS_UNIX )
  if ( fd >= 0 )
    ::fsync( fd );
#endif
  file.close();

  // Atomic replace: std::filesystem::rename maps to rename(2) on POSIX and
  // MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows, both of which replace
  // an existing destination in one step - no remove/rename window in which
  // the previous checkpoint could be lost.
  // Injected rename failure (test-only arming): same cleanup as a real
  // cross-device/locked-target failure.
  const bool renameFault = SICNU_FAULT_POINT( "workflow_checkpoint.publish" );
  std::error_code renameError;
  if ( !renameFault )
  {
    std::filesystem::rename( std::filesystem::path( tmpPath.toStdWString() ),
                             std::filesystem::path( finalPath.toStdWString() ),
                             renameError );
  }
  if ( renameFault || renameError )
  {
    QFile::remove( tmpPath );
    return QString();
  }

  fsyncDirectory( dir );
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
  if ( file.size() > kMaxCheckpointDocumentBytes )
    return legacyGroup; // oversized: same treatment as the corrupt case
  const QByteArray data = file.readAll();
  file.close();

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
      if ( QFile::rename( candidate.info.absoluteFilePath(),
                          candidate.info.absoluteFilePath() + QLatin1String( ".orphaned" ) ) )
        ++quarantined;
    }
  }
  return quarantined;
}

std::vector<std::shared_ptr<WorkflowRun>> WorkflowCheckpointManager::recoverInterruptedRuns( const QString &directoryPath )
{
  const QString dir = directoryPath.isEmpty() ? defaultCheckpointDirectory() : directoryPath;

  // Sweep orphaned tmp files from crashed saves. Recovery runs at startup,
  // before any new saves, so anything matching the tmp pattern is a leftover.
  {
    QDir d( dir );
    if ( d.exists() )
    {
      const QStringList orphans = d.entryList( QStringList{ QStringLiteral( "checkpoint_*.json.tmp.*" ) },
                                               QDir::Files );
      for ( const QString &orphan : orphans )
        QFile::remove( d.absoluteFilePath( orphan ) );
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
