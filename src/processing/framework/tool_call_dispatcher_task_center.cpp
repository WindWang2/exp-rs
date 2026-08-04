// src/processing/framework/tool_call_dispatcher_task_center.cpp
#include "tool_call_dispatcher.h"
#include "task_center.h"

#include <thread>

namespace sicnu::processing {

ToolCallDispatcher::ToolCallDispatcher()
  : mSink( []( const QString &algorithmId, const QVariantMap &params ) -> long {
      return sicnu::TaskCenter::instance().enqueueTask(
        algorithmId, params, /*autoLoad=*/true, sicnu::TaskPriority::Normal, {}, /*autoDispatch=*/true );
    } )
  , mWatcher( [this]( long taskId, CompletionCallback onComplete ) {
      OutputCommitterHandler committerHandler = mOutputCommitterHandler;
      std::thread( [taskId, cb = std::move( onComplete ), committerHandler = std::move( committerHandler )]() mutable {
        const auto info = sicnu::TaskCenter::instance().waitForTask( taskId );
        const Json::Value payload = buildTaskResultPayload( info, committerHandler );
        if ( cb )
          cb( payload );
      } ).detach();
    } )
{
}

} // namespace sicnu::processing
