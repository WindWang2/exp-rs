#pragma once

/*******************************************************************************
 * R30-T02 (WP05): bridges ONE engine run's cancellation context into the
 * session's per-invocation forward-cancel scope.
 *
 * Constructed at the top of an engine run (single-input raster, multi-input
 * raster, scene, detection). When the provider declares
 * capabilities().cancelInForward AND supports per-invocation scopes
 * (beginForwardInvocation() != nullptr — today: ORT), a small watcher thread
 * polls the run context; the moment the context reports cancellation it
 * calls handle->cancel(), which terminates the ACTIVE forward (or the next
 * one) of exactly THIS invocation. Coarse providers (OpenCV DNN, HTTP, ...)
 * get no watcher and no session-global flag: they keep the historical
 * batch-boundary cancellation the engines already implement.
 *
 * The bridge never calls requestCancel()/clearCancel() on the session —
 * those remain the session-global legacy surface, and touching them here
 * would cancel or un-cancel ANOTHER task sharing the session.
 */

#include <atomic>
#include <memory>
#include <thread>

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"

namespace sicnu::operators::runtime
{

class ForwardCancelBridge
{
public:
  ForwardCancelBridge( const ModelRuntimePtr &runtime, RSOperatorContext &context );
  ~ForwardCancelBridge();

  ForwardCancelBridge( const ForwardCancelBridge & ) = delete;
  ForwardCancelBridge &operator=( const ForwardCancelBridge & ) = delete;

private:
  std::shared_ptr<ForwardCancelHandle> m_handle; // null = coarse provider: no watcher
  std::atomic<bool> m_stop{ false };
  std::thread m_watcher; // joinable only while watching
};

} // namespace sicnu::operators::runtime
