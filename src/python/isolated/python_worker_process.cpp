// src/python/isolated/python_worker_process.cpp
#include "python_worker_process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

namespace sicnu::python::isolated
{

PythonWorkerProcess::PythonWorkerProcess( QObject *parent )
  : QObject( parent )
{
  m_process = new QProcess( this );
  connect( m_process, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ),
           this, &PythonWorkerProcess::onProcessFinished );
  connect( m_process, &QProcess::errorOccurred, this, &PythonWorkerProcess::onProcessError );
}

PythonWorkerProcess::~PythonWorkerProcess()
{
  stopWorker();
}

bool PythonWorkerProcess::startWorker( const QString &socketName, const QString &pythonPath, const QString &scriptPath )
{
  if ( isRunning() )
  {
    stopWorker();
  }

  QString pythonExec = pythonPath;
  if ( pythonExec.isEmpty() )
  {
    // Search for available python executable (python3, python)
    const QStringList execCandidates = { QStringLiteral( "python3" ), QStringLiteral( "python" ), QStringLiteral( "python.exe" ) };
    for ( const QString &cand : execCandidates )
    {
      if ( !QStandardPaths::findExecutable( cand ).isEmpty() )
      {
        pythonExec = cand;
        break;
      }
    }
    if ( pythonExec.isEmpty() )
      pythonExec = QStringLiteral( "python" );
  }

  QString workerScript = scriptPath;
  if ( workerScript.isEmpty() )
  {
    QString appDir = QCoreApplication::applicationDirPath();
    const QStringList scriptCandidates = {
      QDir( appDir ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) ),
      QDir( appDir ).filePath( QStringLiteral( "../../src/python/scripts/worker_daemon.py" ) ),
      QDir( appDir ).filePath( QStringLiteral( "../share/sicnu_geo_rs/scripts/worker_daemon.py" ) ),
      QDir( appDir ).filePath( QStringLiteral( "share/sicnu_geo_rs/scripts/worker_daemon.py" ) ),
      QDir( appDir ).filePath( QStringLiteral( "scripts/worker_daemon.py" ) ),
      QDir::current().filePath( QStringLiteral( "src/python/scripts/worker_daemon.py" ) )
    };
    for ( const QString &cand : scriptCandidates )
    {
      if ( QFileInfo::exists( cand ) )
      {
        workerScript = QFileInfo( cand ).canonicalFilePath();
        break;
      }
    }
    if ( workerScript.isEmpty() )
    {
      workerScript = QDir( appDir ).filePath( QStringLiteral( "scripts/worker_daemon.py" ) );
    }
  }

  QStringList args;
  args << workerScript << QStringLiteral( "--socket" ) << socketName;

  m_process->start( pythonExec, args );
  if ( !m_process->waitForStarted( 500 ) )
  {
    qWarning() << "PythonWorkerProcess: Failed to start process" << pythonExec << args << m_process->errorString();
    return false;
  }

  emit workerStarted();
  return true;
}

void PythonWorkerProcess::stopWorker()
{
  if ( m_process && m_process->state() != QProcess::NotRunning )
  {
    m_process->disconnect();
    m_process->terminate();
    if ( !m_process->waitForFinished( 500 ) )
    {
      m_process->kill();
      m_process->waitForFinished( 500 );
    }
  }
}

bool PythonWorkerProcess::isRunning() const
{
  return m_process && m_process->state() == QProcess::Running;
}

qint64 PythonWorkerProcess::processId() const
{
  return m_process ? m_process->processId() : 0;
}

QProcess::ProcessState PythonWorkerProcess::state() const
{
  return m_process ? m_process->state() : QProcess::NotRunning;
}

void PythonWorkerProcess::onProcessFinished( int exitCode, QProcess::ExitStatus exitStatus )
{
  if ( exitStatus == QProcess::CrashExit || ( exitCode != 0 && exitCode != 137 && exitCode != 15 && exitCode != 9 && exitCode != 1 ) )
  {
    emit workerCrashed();
  }
  emit workerFinished( exitCode, exitStatus );
}

void PythonWorkerProcess::onProcessError( QProcess::ProcessError error )
{
  // A crash reports BOTH errorOccurred(Crashed) and finished(CrashExit).
  // workerCrashed is emitted only from onProcessFinished so the pool's
  // handleWorkerCrash runs exactly once per process death; re-emitting here
  // would restart the replacement worker a second time and orphan any
  // crash-recovery bookkeeping (ADR 0064 state recovery).
  Q_UNUSED( error )
}

} // namespace sicnu::python::isolated
