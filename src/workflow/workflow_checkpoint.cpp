#include "workflow_checkpoint.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

#include <atomic>
#include <filesystem>
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

  const int fd = file.handle();
  if ( fd >= 0 )
    ::fsync( fd );
  file.close();

  // Atomic replace: std::filesystem::rename maps to rename(2) on POSIX and
  // MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows, both of which replace
  // an existing destination in one step - no remove/rename window in which
  // the previous checkpoint could be lost.
  std::error_code renameError;
  std::filesystem::rename( std::filesystem::path( tmpPath.toStdWString() ),
                           std::filesystem::path( finalPath.toStdWString() ),
                           renameError );
  if ( renameError )
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
      run->forceSetState( WorkflowRunState::Interrupted );
      run->setErrorMessage( "Execution interrupted by system shutdown/restart." );

      // Reconcile step plans: a crash may have left steps marked as actively
      // executing; a resumed run must treat them as not yet run.
      const std::vector<StepPlan> plans = run->stepPlans();
      for ( const StepPlan &plan : plans )
      {
        if ( plan.status == "Running" || plan.status == "Cancelling" )
          run->setStepStatus( plan.stepId, "Pending" );
      }

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
    }
  }

  return recovered;
}

} // namespace sicnu::workflow
