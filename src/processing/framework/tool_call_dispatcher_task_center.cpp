// src/processing/framework/tool_call_dispatcher_task_center.cpp
//
// Production wiring for the ToolCallDispatcher default constructor: the sink,
// the completion watcher and the sync await all ride the ExecutionPlane on top
// of TaskCenter/JobEngine.
//
// Issue #559 root cause, for the record: this file used to spawn a detached
// watcher thread per submission that fetched the terminal task and then
// unconditionally delivered the completion payload to m_commitBridge via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection). A caller that invoked
// dispatchAndAwait() on the bridge's own thread blocked in a condition-variable
// wait WITHOUT running that thread's event loop, so the queued meta-call never
// executed, the condition variable was never notified, and the call hung until
// its (30-minute) timeout. It also leaked one detached thread per watch.
//
// The wiring below removes both defects structurally:
//   * the watcher registers a TaskCenter completion callback that fires on the
//     terminal-transition thread (a JobEngine worker) — no detached threads;
//   * async payload delivery still marshals onto the bridge thread via
//     QueuedConnection (Data Manager affinity), which is safe because async
//     consumers pump their event loops;
//   * dispatchAndAwait() uses the plane's event-loop-free await (setSyncAwait):
//     its wakeup rides the thread-safe completion channel and the transactional
//     payload commit runs on the calling thread — the Data Manager's owning
//     thread in every production caller — so a sync wait can never wedge a
//     queued delivery, and the commit happens exactly once.
#include "tool_call_dispatcher.h"
#include "execution_plane.h"
#include "task_center.h"

#include <QCoreApplication>
#include <QObject>

namespace sicnu::processing {

ToolCallDispatcher::ToolCallDispatcher()
  : mSink( [this]( const QString &algorithmId, const QVariantMap &params ) -> long {
      ExecutionRequest request;
      request.algorithmId = algorithmId;
      request.params = params;
      // The committed stable asset is auto-displayed via DataManager::assetAdded /
      // QgisDisplayManager. Leaving autoLoad=true would also emit
      // TaskCenter::layerAutoLoadRequested with the temp path, producing a
      // duplicate layer or a layer pointing at a moved-away temp file (P0-L1).
      request.autoLoad = false;
      request.source = mSourceTag;
      return ExecutionPlane::instance().submit( request ).taskId();
    } )
  , mWatcher( [this]( long taskId, CompletionCallback onComplete ) {
      OutputCommitterHandler committerHandler = mOutputCommitterHandler;
      OutputVerificationHandler verificationHandler = mOutputVerificationHandler;
      std::shared_ptr<QObject> bridge = m_commitBridge;
      // deliver runs on the bridge (Data Manager owner) thread whenever
      // needed; buildCommittedResultPayload applies the transactional commit
      // exactly once per task, so a null callback still yields the committed
      // asset (MCP relies on this side effect) and copilot-style duplicate
      // builders reuse the cached payload instead of racing the commit.
      ExecutionPlane::instance().watch(
        taskId,
        [bridge, cb = std::move( onComplete ), committerHandler = std::move( committerHandler ),
         verificationHandler = std::move( verificationHandler )]( const sicnu::AlgorithmTaskInfo &info ) mutable {
          ExecutionPlane::deliverOnAffinity(
            bridge.get(),
            [info, cb = std::move( cb ), committerHandler = std::move( committerHandler ),
             verificationHandler = std::move( verificationHandler )]() mutable {
              const Json::Value payload =
                ExecutionPlane::instance().buildCommittedResultPayload( info, committerHandler, verificationHandler );
              if ( cb )
                cb( payload );
            } );
        },
        /*affinityContext=*/bridge.get() );
    } )
  , m_commitBridge( std::make_shared<QObject>() )
{
  std::shared_ptr<QObject> bridge = m_commitBridge;
  mSyncAwait = [this, bridge]( long taskId, std::chrono::milliseconds timeout ) -> Json::Value {
    // The handler is read at await time (setDataManager may run after the
    // constructor); the commit then runs on the calling thread — the Data
    // Manager's owning thread for every production caller of the sync path.
    return ExecutionPlane::instance().awaitResult( taskId, timeout, mOutputCommitterHandler,
                                                   bridge.get(), /*cancelOnTimeout=*/true,
                                                   mOutputVerificationHandler );
  };
}

Json::Value ToolCallDispatcher::buildCommittedResultPayload( const sicnu::AlgorithmTaskInfo &info ) const
{
  Json::Value payload =
    ExecutionPlane::instance().buildCommittedResultPayload( info, mOutputCommitterHandler, mOutputVerificationHandler );
  rollbackVerificationFailure( payload );
  return payload;
}

} // namespace sicnu::processing
