#include "workflow_checkpoint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <unistd.h>

namespace sicnu::workflow {

QString WorkflowCheckpointManager::defaultCheckpointDirectory()
{
  QString base = QDir::homePath() + QStringLiteral( "/.rs_studio/checkpoints" );
  return base;
}

QString WorkflowCheckpointManager::saveCheckpoint( const WorkflowRun &run, const QString &directoryPath )
{
  const QString dir = directoryPath.isEmpty() ? defaultCheckpointDirectory() : directoryPath;
  QDir().mkpath( dir );

  const QString runId = QString::fromStdString( run.getRunId() );
  const QString finalPath = QDir( dir ).filePath( QStringLiteral( "checkpoint_%1.json" ).arg( runId ) );
  const QString tmpPath = finalPath + QStringLiteral( ".tmp" );

  const Json::Value root = run.toJson();
  Json::StreamWriterBuilder writerBuilder;
  writerBuilder["indentation"] = "  ";
  const std::string jsonStr = Json::writeString( writerBuilder, root );

  QFile file( tmpPath );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return QString();

  file.write( jsonStr.data(), static_cast<qint64>( jsonStr.size() ) );
  file.flush();

  const int fd = file.handle();
  if ( fd >= 0 )
  {
    ::fsync( fd );
  }
  file.close();

  if ( QFile::exists( finalPath ) )
    QFile::remove( finalPath );

  if ( !file.rename( finalPath ) )
  {
    // Fallback if rename fails
    if ( QFile::rename( tmpPath, finalPath ) )
      return finalPath;
    return QString();
  }

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
  const QStringList checkpointFiles = listCheckpoints( dir );

  std::vector<std::shared_ptr<WorkflowRun>> recovered;
  for ( const QString &cpFile : checkpointFiles )
  {
    QString err;
    auto run = loadCheckpoint( cpFile, &err );
    if ( !run )
      continue;

    const WorkflowRunState st = run->getState();
    if ( st == WorkflowRunState::Running
         || st == WorkflowRunState::Planning
         || st == WorkflowRunState::WaitingResource
         || st == WorkflowRunState::Cancelling )
    {
      run->forceSetState( WorkflowRunState::Interrupted );
      run->setErrorMessage( "Execution interrupted by system shutdown/restart." );
      saveCheckpoint( *run, dir );
      recovered.push_back( std::shared_ptr<WorkflowRun>( std::move( run ) ) );
    }
  }

  return recovered;
}

} // namespace sicnu::workflow
