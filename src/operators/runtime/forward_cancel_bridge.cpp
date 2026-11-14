#include "operators/runtime/forward_cancel_bridge.h"

#include <chrono>

namespace sicnu::operators::runtime
{

ForwardCancelBridge::ForwardCancelBridge( const ModelRuntimePtr &runtime, RSOperatorContext &context )
{
  if ( !runtime )
    return;
  // Honest capability gate: only providers that can interrupt a RUNNING
  // forward AND isolate the cancel to one invocation get a watcher. Everyone
  // else keeps batch-boundary cancellation (the engines poll their context
  // between forwards, exactly as before this bridge existed).
  if ( !runtime->capabilities().cancelInForward )
    return;
  m_handle = runtime->beginForwardInvocation();
  if ( !m_handle )
    return;
  RSOperatorContext *contextPtr = &context;
  std::shared_ptr<ForwardCancelHandle> handle = m_handle;
  std::atomic<bool> &stop = m_stop;
  m_watcher = std::thread( [ contextPtr, handle, &stop ] {
    // Causal forwarder: the run context is the authority; the handle is the
    // delivery channel. 50 ms is responsiveness, not correctness — a slower
    // poll only delays the interrupt, never misses it (the loop runs until
    // the bridge is destroyed).
    while ( !stop.load( std::memory_order_relaxed ) )
    {
      if ( contextPtr->isCancelled() )
      {
        handle->cancel();
        return;
      }
      std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }
  } );
}

ForwardCancelBridge::~ForwardCancelBridge()
{
  m_stop.store( true, std::memory_order_relaxed );
  if ( m_watcher.joinable() )
    m_watcher.join();
  // The handle dies with the shared_ptr — an expired scope stops matching
  // forwards on this thread (weak_ptr semantics inside the provider), and
  // the session-global flag was never touched.
}

} // namespace sicnu::operators::runtime
