/***************************************************************************
 * job_engine_qt_bridge.cpp
 ***************************************************************************/
#include "job_engine_qt_bridge.h"

#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QCoreApplication>

using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRecord;
using sicnu::jobs::JobState;

JobEngineQtBridge *JobEngineQtBridge::instance()
{
  static JobEngineQtBridge *s_instance = nullptr;
  if ( !s_instance )
  {
    QObject *parent = QCoreApplication::instance();
    s_instance = new JobEngineQtBridge( parent );
  }
  return s_instance;
}

JobEngineQtBridge::JobEngineQtBridge( QObject *parent )
  : QObject( parent )
{
  JobEngine::instance().setListener( [this]( const JobRecord &rec ) {
    const QString jobId = QString::fromStdString( rec.id );
    const bool finished = ( rec.state == JobState::Succeeded
                            || rec.state == JobState::Failed
                            || rec.state == JobState::Cancelled );

    // Listener may run on a worker thread — marshal to the GUI thread.
    QMetaObject::invokeMethod(
      this,
      [this, jobId, finished]() {
        emit jobUpdated( jobId );
        if ( finished )
          emit jobFinished( jobId );
      },
      Qt::QueuedConnection );
  } );
}
