// gpu_plane.h — Shared GPU lifecycle for model inference (Data Plane 3.0,
// Phase H). Replaces the per-call "load model → run → destroy model" pattern:
// operators acquire a pooled ModelSession keyed by model identity (path +
// content signature), the pool admits against a per-device VRAM budget, and
// OOM degrades along a caller-supplied ladder down to a CPU fallback verdict
// instead of crashing or thrashing.
//
// Qt-free leaf in sicnu_runtime; the backend seam is abstract so tests run
// against a fake device and production binds the model runtime's allocator.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::runtime::gpu
{

struct DeviceInfo
{
    int deviceId = -1;
    std::string name;
    size_t totalVramMb = 0;
    bool available = false;
};

/// Backend seam. Implementations own real device allocation; the pool only
/// tracks accounting through these calls.
class GpuBackend
{
  public:
    virtual ~GpuBackend() = default;
    virtual std::vector<DeviceInfo> enumerate() = 0;
    /// Reserves @p mb on @p deviceId. False = not enough VRAM (OOM) or device
    /// unavailable. Implementations must be cheap and non-blocking.
    virtual bool allocateVram( int deviceId, size_t mb ) = 0;
    virtual void freeVram( int deviceId, size_t mb ) = 0;
};

/// Identity of a model: its path plus a content signature (mtime+size at
/// minimum). A changed file ⇒ a different identity ⇒ old sessions recycle.
struct ModelIdentity
{
    std::string modelId;    ///< logical name (fairness key)
    std::string modelPath;  ///< file backing the model
    std::string signature;  ///< content signature (mtime+size or digest)
};

struct SessionRequest
{
    ModelIdentity model;
    size_t vramMb = 0;    ///< preferred VRAM footprint
    int deviceId = -1;    ///< -1 = first available
    /// OOM ladder: acceptable reduced footprints, tried in order after the
    /// preferred size fails. An empty ladder means no downgrade attempts.
    std::vector<size_t> reducedVramMb;
};

/// A live model session. The operator runs inference through it and releases
/// it back to the pool; loading cost is paid once per identity.
struct ModelSession
{
    std::string sessionId;
    ModelIdentity model;
    size_t grantedVramMb = 0;
    int deviceId = -1;
    /// Monotonic use counter (fairness diagnostics).
    unsigned long useCount = 0;
};

enum class AcquireOutcome
{
    Acquired,          ///< preferred footprint
    AcquiredReduced,   ///< some ladder step fit
    CpuFallback,       ///< nothing fit; run on CPU instead
    Busy,              ///< fairness bound hit (in-flight load / session cap):
                       ///< retry shortly; never queue-bound the caller
    NoDevice,          ///< no GPU device exists / none available
};

struct AcquireResult
{
    AcquireOutcome outcome = AcquireOutcome::NoDevice;
    std::shared_ptr<ModelSession> session;
};

/// Pooled session manager with VRAM budget admission and identity-keyed
/// reuse. Fairness: one in-flight load per model id, and a bounded number of
/// live sessions per device (default 2) so many small models cannot evict a
/// big one. Thread-safe.
class ModelSessionPool
{
  public:
    ModelSessionPool( std::shared_ptr<GpuBackend> backend, size_t vramBudgetMbPerDevice );
    ~ModelSessionPool();

    /// Acquires (loading on demand) a session for @p request. The returned
    /// session is shared: concurrent operators may reuse a loaded model.
    AcquireResult acquireSession( const SessionRequest &request );

    /// Marks a session released (shared_ptr refcount governs actual eviction;
    /// released sessions stay warm for reuse until evicted by budget press).
    void releaseSession( const std::string &sessionId );

    /// Diagnostics / tests.
    size_t liveSessionCount() const;
    size_t usedVramMb( int deviceId ) const;
    std::vector<DeviceInfo> devices() const;

    /// Drops sessions whose model file's identity no longer matches (stale
    /// model detection is identity-based; the pool never revalidates I/O here).
    void evictStale( const std::string &modelId, const std::string &currentSignature );

  private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace sicnu::runtime::gpu
