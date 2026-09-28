// src/python/isolated/python_worker_process.h
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include <QByteArray>

namespace sicnu::python::isolated
{

class PythonWorkerProcess : public QObject
{
  Q_OBJECT

  public:
    explicit PythonWorkerProcess( QObject *parent = nullptr );
    ~PythonWorkerProcess() override;

    bool startWorker( const QString &socketName, const QString &pythonPath = QString(), const QString &scriptPath = QString() );
    void stopWorker();
    /// Re-establish process signal connections after stopWorker()'s blanket
    /// disconnect, so a reused instance keeps crash detection (#523).
    void ensureSignalsConnected();

    bool isRunning() const;
    qint64 processId() const;
    QProcess::ProcessState state() const;

    /// Full worker stderr captured so far (bounded tail; diagnostics for
    /// crash classification). Never truncated within the cap.
    QByteArray capturedStderr() const;
    /// Classification axis of the last finished exit (worker_protocol error
    /// contract family): the raw exit code and whether Qt classified the
    /// death as a crash (CrashExit). Valid after workerFinished.
    int lastExitCode() const { return m_lastExitCode; }
    bool lastExitWasCrash() const { return m_lastExitWasCrash; }

  signals:
    void workerStarted();
    void workerFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void workerCrashed();

  private slots:
    void onProcessFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void onProcessError( QProcess::ProcessError error );

  private:
    /// Best-effort kill of the worker's WHOLE process group (POSIX: the
    /// worker is spawned as its own group leader). TERM-ignoring workers
    /// that spawn grandchildren would otherwise leak them.
    void killProcessTree();
    /// Appends fresh stderr into the bounded tail buffer. A member slot (not
    /// a constructor lambda) so ensureSignalsConnected() can re-arm it after
    /// stopWorker()'s blanket disconnect — a reused instance would otherwise
    /// lose crash diagnostics permanently (#523 family, R5 residual of the
    /// #1353 "stderr re-arm" fix that shipped comment-only).
    void onReadyReadStderr();

    QProcess *m_process = nullptr;
    QByteArray m_stderrBuffer;
    int m_lastExitCode = -1;
    bool m_lastExitWasCrash = false;
};

} // namespace sicnu::python::isolated
