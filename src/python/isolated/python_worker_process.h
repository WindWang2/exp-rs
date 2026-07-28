// src/python/isolated/python_worker_process.h
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

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

    bool isRunning() const;
    qint64 processId() const;
    QProcess::ProcessState state() const;

  signals:
    void workerStarted();
    void workerFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void workerCrashed();

  private slots:
    void onProcessFinished( int exitCode, QProcess::ExitStatus exitStatus );
    void onProcessError( QProcess::ProcessError error );

  private:
    QProcess *m_process = nullptr;
};

} // namespace sicnu::python::isolated
