// src/processing/framework/tool_call_dispatcher_task_center.cpp
#include "tool_call_dispatcher.h"
#include "task_center.h"

#include <QCoreApplication>
#include <QObject>

#include <thread>

namespace sicnu::processing {

ToolCallDispatcher::ToolCallDispatcher()
  : mSink( []( const QString &algorithmId, const QVariantMap &params ) -> long {
      return sicnu::TaskCenter::instance().enqueueTask(
        algorithmId, params, /*autoLoad=*/true, sicnu::TaskPriority::Normal, {}, /*autoDispatch=*/true );
    } )
  , m_commitBridge( std::make_shared<QObject>() )
  , mWatcher( [this]( long taskId, CompletionCallback onComplete ) {
      OutputCommitterHandler committerHandler = mOutputCommitterHandler;
      std::shared_ptr<QObject> bridge = m_commitBridge;
      std::thread( [taskId, cb = std::move( onComplete ), committerHandler = std::move( committerHandler ), bridge]() mutable {
        const auto info = sicnu::TaskCenter::instance().waitForTask( taskId );
        // Payload construction commits outputs through the Data Manager, which
        // enforces owning-thread access. Route it back to the construction
        // thread (the Data Manager's thread in production); the detached
        // watcher thread must never touch the catalog directly.
        if ( bridge && QCoreApplication::instance() )
        {
          QMetaObject::invokeMethod( bridge.get(), [info, cb = std::move( cb ), committerHandler = std::move( committerHandler )]() mutable {
            const Json::Value payload = buildTaskResultPayload( info, committerHandler );
            if ( cb )
              cb( payload );
          }, Qt::QueuedConnection );
        }
        else if ( cb )
        {
          const Json::Value payload = buildTaskResultPayload( info, committerHandler );
          cb( payload );
        }
      } ).detach();
    } )
{
}

} // namespace sicnu::processing
