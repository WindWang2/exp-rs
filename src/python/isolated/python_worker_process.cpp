// src/python/isolated/python_worker_process.cpp
#include "python_worker_process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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
    pythonExec = QStringLiteral( "python3" );
  }

  QString workerScript = scriptPath;
  if ( workerScript.isEmpty() )
  {
    QString appDir = QCoreApplication::applicationDirPath();
    QString srcScript = QDir( appDir ).filePath( QStringLiteral( "../src/python/scripts/worker_daemon.py" ) );
    if ( QFileInfo::exists( srcScript ) )
    {
      workerScript = QFileInfo( srcScript ).canonicalFilePath();
    }
    else
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
