/***************************************************************************
 * job_engine_qt_bridge.h  —  Qt signal bridge over Qt-free JobEngine
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

/**
 * Singleton QObject that installs a JobEngine listener once and re-emits
 * updates on the GUI thread via Qt::QueuedConnection.
 *
 * UI code must call snapshot()/list() on JobEngine for full JobRecord data.
 */
class JobEngineQtBridge : public QObject
{
    Q_OBJECT
  public:
    /** Parent to qApp (or nullptr); installs the engine listener once. */
    static JobEngineQtBridge *instance();

  signals:
    void jobUpdated( const QString &jobId );
    void jobFinished( const QString &jobId );

  private:
    explicit JobEngineQtBridge( QObject *parent = nullptr );
};
