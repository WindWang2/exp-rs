// src/operators/runtime/model_runtime.h — unified model inference runtime layer.
//
// Sits between ModelCatalog (which selects and verifies a model) and the
// operators that execute it (rs:infer, the tile inference engine). A runtime
// wraps ONE loaded model session: weights are read from disk when the session
// is created and reused across infer() calls. The registry caches sessions
// (LRU-bounded) so repeated inference on the same model does not re-parse
// weights, hands out shared_ptr sessions that are safe to use from any
// thread (implementations serialize forward passes internally), and provides
// honest hardware capability detection plus the runtime-layer readiness
// verdicts (UnsupportedRuntime / IncompatibleHardware).
//
// Session identity (Platform 4.0): the cache key is
//   framework | device | sha256(artifact bytes)
// so the same path with different bytes never shares a session — the content
// digest differs and the stale entry only leaves through LRU eviction. Bytes
// that are equal at different paths share one session (weights dominate the
// memory cost). Ad-hoc (non-catalog) models get their digest computed at
// acquire time, memoized per (path, size, mtime).
//
// This translation unit requires OpenCV (cv::Mat is the tensor type); it is
// compiled only under SICNU_HAS_OPENCV. Without OpenCV no model runtime
// exists and rs:infer is disabled, exactly as before.
#pragma once

#include "operators/framework/model_catalog.h"
#include "operators/framework/model_readiness.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/device_planner.h"
#include "operators/runtime/tensor_blob.h"

#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::operators::runtime {

/**
 * Host capabilities relevant to model execution. Detection combines
 * cv::dnn's backend/target enumeration with explicit environment overrides
 * for testability:
 *   SICNU_MODEL_GPU=0|1        force cudaAvailable
 *   SICNU_MODEL_VRAM_MB=N      force the VRAM budget
 *   SICNU_MODEL_CUDA_DEVICES=N force cudaDeviceCount (multi-GPU hosts; the
 *                              opencv_dnn backend can only address index 0)
 */
struct ModelHardwareCapabilities
{
  bool cudaAvailable = false;
  bool openclAvailable = false;
  int vramBudgetMb = 0; ///< 0 = unknown / not enforced
  int cudaDeviceCount = 0; ///< addressable CUDA devices (0 = none; >=1 with CUDA)

  static ModelHardwareCapabilities detect();
};

/// Requested execution device (Platform 4.0): the manifest's
/// `runtime.device` token ("cpu" | "cuda" | "cuda:N" | "auto") or an explicit
/// operator override. Parsing is strict — unknown tokens are a contract error,
/// never a silent fallback.
struct RequestedDevice
{
  enum class Kind
  {
    Auto,  ///< deterministic: lowest-index fitting CUDA device, else CPU
    Cpu,
    Cuda
  };
  Kind kind = Kind::Auto;
  int cudaIndex = 0; ///< meaningful only when kind == Cuda ("cuda:N")

  static RequestedDevice autoDetect() { return {}; }
  static RequestedDevice cpu() { return RequestedDevice{ Kind::Cpu, 0 }; }
  static RequestedDevice cuda( int index ) { return RequestedDevice{ Kind::Cuda, index }; }

  /// Parses the manifest/parameter token. Returns false on garbage (callers
  /// must fail loudly, not fall back).
  static bool parse( const std::string &token, RequestedDevice *out );
  std::string toString() const;
};

/// The device a session actually runs on (after auto resolution and any
/// demotion). Rendered as "cpu" or "cuda:<index>".
struct ResolvedDevice
{
  bool gpu = false;
  int cudaIndex = 0;

  std::string toString() const
  {
    return gpu ? "cuda:" + std::to_string( cudaIndex ) : "cpu";
  }
};

/// Deterministic device resolution — a pure function of the request, the
/// detected capabilities, the model's GPU preference and its VRAM estimate:
///   Auto:   cuda:0 (lowest addressable index) when CUDA is available, the
///           model tolerates GPU and the estimate fits the budget; otherwise
///           cpu. Equal inputs always yield equal outputs. When the model
///           forbids CPU fallback, an unresolvable auto request fails rather
///           than silently running where it must not.
///   Cpu:    always cpu.
///   Cuda:N: cuda:N when N is addressable (<= @p maxAddressableCudaIndex —
///           the opencv_dnn backend exposes only 0) and the estimate fits;
///           with @p allowCpuFallback a non-fitting budget demotes to cpu,
///           otherwise the request fails.
/// Returns false with @p why when the request cannot be honored — callers
/// must fail loudly, never silently run elsewhere.
bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    ResolvedDevice *out, std::string *why = nullptr );

/// Platform 7.0 ledger-aware resolution: same contract, but @p freeVramMbByIndex
/// carries the planner's per-device free VRAM (index → MiB; negative = the
/// device is unenforced/unknown). Auto is no longer a trivial cuda:0 — it
/// deterministically picks the LOWEST index whose free VRAM fits the model's
/// estimate; cuda:N requires that device to fit. Equal inputs always yield
/// equal outputs; refusal is always typed.
bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    const std::vector<int> &freeVramMbByIndex,
                    ResolvedDevice *out, std::string *why = nullptr );

// --- Platform 8.0 WP-B: placement policy + pressure observability ------------

/// Deterministic multi-GPU placement policy (WP-B). It is a PLACEMENT knob —
/// how `auto` chooses among FITTING devices; it never admits a device that
/// does not fit and never queues/retries (the registry's bounded pressure
/// valve is unchanged).
enum class DevicePlacementPolicy
{
  LowestFitting, ///< 7.0 default: lowest addressable index whose free VRAM fits
  LeastLoaded    ///< among fitting devices, the one with the MOST free VRAM
                 ///< (ties resolve to the lowest index). Spreads concurrent
                 ///< sessions across cards instead of stacking them on cuda:0.
};

/// Policy-aware resolution. Auto under @p LeastLoaded scans every fitting
/// device and picks the largest free VRAM; every other request kind behaves
/// exactly like the 7.0 contract. Equal inputs always yield equal outputs.
bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    const std::vector<int> &freeVramMbByIndex,
                    DevicePlacementPolicy policy,
                    ResolvedDevice *out, std::string *why = nullptr );

/// Backend capabilities the registry needs beyond the factory itself
/// (Platform 4.0). Defaults describe the historical built-in provider.
struct ProviderTraits
{
  /// Highest CUDA device index this backend can address (opencv_dnn: 0;
  /// multi-device runtimes: deviceCount-1). Index selection beyond this
  /// fails loudly instead of silently running on another card.
  int maxAddressableCudaIndex = 0;
};

/// Structured failure classification for forward-pass / load errors
/// (Platform 4.0, extended Platform 7.0): drives the OOM ladder, error
/// payloads and the external error-code projection. Classification is
/// message-based over exception types we actually see (cv::Exception,
/// std::bad_alloc, runtime_error) — honest pattern matching, not a guarantee.
enum class InferenceFailureKind
{
  Unknown,
  OutOfMemory,
  Canceled,
  ShapeMismatch,
  CorruptModel,
  NotLoaded,
  // --- Platform 7.0 taxonomy completion
  IncompatibleSchema,   ///< manifest/graph contract the runtime cannot honor
  DeviceUnavailable,    ///< requested/selected device missing or unaddressable
  ProviderCrash,        ///< external provider died / connection lost
  OutputInvalid         ///< forward ran but its output failed validation
};
InferenceFailureKind classifyInferenceError( const std::string &message );

/// Error-code projection for the taxonomy (Platform 7.0): one stable
/// RSOperatorError code per failure kind, shared by every consumer so
/// CLI/workflow/GUI payloads classify identically.
ErrorCode errorCodeForInferenceFailure( InferenceFailureKind kind );

/// Liveness/statistics probe for one loaded session (Platform 4.0).
struct SessionHealth
{
  bool ok = false;                    ///< session usable (loaded, not canceled, no fatal error)
  std::uint64_t forwardsCompleted = 0;
  std::uint64_t failures = 0;
  double lastForwardMs = 0.0;
  std::string lastError;              ///< most recent failure message ("" when none)
};

/// Memory estimate for one loaded session, in MiB (Platform 4.0). Values of 0
/// mean "unknown" — estimates are reported honestly, never invented.
struct SessionMemoryEstimate
{
  int weightsMb = 0;    ///< serialized weight bytes on disk, rounded up
  int workingSetMb = 0; ///< estimated peak per-forward working set (0 = unknown)
};

/**
 * One loaded model session. lifecycle: created by the registry (load),
 * used via infer() (possibly concurrently — implementations serialize),
 * released when the last shared_ptr and the registry cache drop it.
 */
class IModelRuntime
{
  public:
    virtual ~IModelRuntime() = default;

    /// Framework id this session executes ("onnx").
    virtual std::string framework() const = 0;
    /// Backend description for result payloads ("opencv_dnn").
    virtual std::string backendName() const = 0;
    /// Device actually used ("cpu" | "cuda" | ...).
    virtual std::string deviceName() const = 0;
    /// The artifact this session was loaded from (for cache keys / payloads).
    virtual std::string artifactPath() const = 0;

    /**
     * Run inference on one NCHW float32 blob (1, C, H, W) and return the
     * model's output blob (1, C', H', W'). Throws std::runtime_error on
     * forward-pass failure (callers translate to their error type).
     */
    virtual cv::Mat infer( const cv::Mat &nchwBlob ) = 0;

    /**
     * Names of the loaded graph's output tensors, in the order the runtime
     * would produce them. Empty when the implementation cannot enumerate
     * them — consumers then treat the manifest output.tensor_names contract
     * as advisory (#705).
     */
    virtual std::vector<std::string> outputTensorNames() const { return {}; }

    /**
     * infer() selecting a specific named output tensor (manifest
     * output.tensor_names contract, #705). The default ignores the name and
     * runs the default head, which is correct for single-output runtimes;
     * @p outputName empty also selects the default head. Throws
     * std::runtime_error on the same conditions as infer().
     */
    virtual cv::Mat infer( const cv::Mat &nchwBlob, const std::string &outputName )
    {
      ( void )outputName;
      return infer( nchwBlob );
    }

    // --- Platform 3.0: multi-input models (goal §9) --------------------------
    /**
     * True when this runtime can feed several named input blobs in one forward
     * pass (inferMulti). Multi-input manifests are invalid on runtimes that
     * report false.
     */
    virtual bool supportsMultiInput() const { return false; }

    /// One named input blob: (input name from the manifest contract, NCHW blob).
    using NamedBlob = std::pair<std::string, cv::Mat>;

    /**
     * Run one forward pass with several named inputs. The default refuses —
     * single-input runtimes never silently drop inputs. Implementations must
     * produce outputs in the graph's own head order; throws
     * std::runtime_error on the same conditions as infer().
     */
    virtual std::vector<cv::Mat> inferMulti( const std::vector<NamedBlob> &namedBlobs )
    {
      ( void )namedBlobs;
      throw std::runtime_error( "runtime does not support multi-input models" );
    }

    // --- Platform 7.0: N-D named tensors + capability negotiation ------------
    /// What THIS session/backend actually supports. Consumers negotiate before
    /// feeding; a manifest asking beyond the capabilities is a typed refusal,
    /// never silent reinterpretation. Defaults describe the historical
    /// contract: single-input, rank-4 float32, positional, batched by caller,
    /// coarse (batch-boundary) cancellation only.
    struct ProviderCapabilities
    {
      bool multiInput = false;      ///< inferNamed with several inputs
      bool namedBind = false;       ///< input NAMES honored (not positional)
      int maxRank = 4;              ///< highest tensor rank inferNamed accepts
      bool batch = true;            ///< leading batch dimension supported
      bool cancelInForward = false; ///< requestCancel interrupts a RUNNING forward
      /// Dtype tokens the provider consumes/produces; empty = {"float32"}.
      std::vector<std::string> inputDtypes;
      std::vector<std::string> outputDtypes;
    };
    virtual ProviderCapabilities capabilities() const { return ProviderCapabilities{}; }

    /**
     * Run one forward pass with N-D named input tensors and get every graph
     * output back, named in the graph's own head order ("" when the provider
     * cannot enumerate names). This is THE multi-input/temporal entry point;
     * the historical infer/inferMulti remain for the raster engines' fast
     * path and for older providers.
     *
     * The DEFAULT implementation bridges through cv::Mat (exact-dtype only —
     * Int64/Float16 refuse here) and therefore honors the historical
     * rank-4 float32 contract; providers override it for true N-D support.
     * Input names are matched by NAME when the backend can do so, else by
     * declaration order — the session's capabilities() always tells which.
     */
    virtual std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                                 const std::vector<std::string> &outputNames );

    // --- Platform 4.0 unified contract ---------------------------------------
    /**
     * Best-effort warmup: one throwaway forward pass so the first real tile
     * does not pay lazy graph compilation. MUST NOT fail the session — a
     * warmup mismatch (e.g. a fixed-shape graph) is recorded in health and
     * swallowed; execution correctness never depends on warmup.
     */
    virtual void warmup() {}

    /**
     * Cooperative cancellation: ask the session to stop feeding forward
     * passes. A forward pass ALREADY RUNNING cannot be interrupted (honest
     * limitation, backend-compatibility doc); the next check point throws.
     * Cleared with clearCancel() — pooled sessions are reusable after a
     * canceled run.
     */
    virtual void requestCancel() {}
    virtual void clearCancel() {}

    /// Statistics/liveness probe. Never throws.
    virtual SessionHealth health() const { return SessionHealth{}; }

    /// Memory estimate (0 = unknown). Never throws.
    virtual SessionMemoryEstimate memoryEstimate() const { return {}; }
};

using ModelRuntimePtr = std::shared_ptr<IModelRuntime>;

/// Factory: build a session for a model, or return nullptr with *errorMessage.
using ModelRuntimeFactory =
    std::function<ModelRuntimePtr( const ModelInfo &, const ModelHardwareCapabilities &, std::string * )>;

/**
 * Runtime-layer readiness for a catalog-static-ready model: does a provider
 * exist for the declared framework, and does the host satisfy the GPU/VRAM
 * contract? Returns Ready when executable; never demotes catalog states.
 */
ModelReadiness evaluateRuntimeReadiness( const ModelInfo &model,
                                         const ModelHardwareCapabilities &hw,
                                         std::string *reason = nullptr );

/**
 * Process-wide bounded model session pool (Platform 4.0 naming: ModelSessionPool).
 * Keyed by (framework, resolved device, artifact content digest) so the same
 * weights are loaded once and reused — and so the same path with different
 * bytes never shares a session. LRU-bounded (default 2 sessions — weights are
 * the dominant memory cost), thread-safe, evictable via releaseAll() for
 * shutdown and tests. Providers register per framework id; the built-in "onnx"
 * provider (OpenCV DNN) is installed at construction, and plugin runtimes
 * register through the same seam.
 */
class ModelRuntimeRegistry
{
  public:
    static ModelRuntimeRegistry &instance();

    /// Acquire a session for the model, loading it on first use. The device
    /// comes from the manifest contract (runtime.device, default auto).
    /// @a errorMessage receives the load failure reason when nullptr is returned.
    ModelRuntimePtr acquire( const ModelInfo &model, std::string *errorMessage = nullptr );

    /// Acquire with an explicit device request (operator override), e.g.
    /// RequestedDevice::cpu() or cuda(1). Same session cache.
    ModelRuntimePtr acquire( const ModelInfo &model, const RequestedDevice &request,
                             std::string *errorMessage = nullptr );

    /// Drop all cached sessions (running callers keep their shared_ptrs).
    void releaseAll();

    /// Drop the cached session for one identity (Platform 4.0 unload). Later
    /// acquires reload; callers holding shared_ptrs keep the session alive
    /// until they drop it — unload means "no longer handed out", never
    /// "invalidates live pointers".
    void release( const std::string &framework, const std::string &identity );

    /// LRU capacity bound (minimum 1).
    void setMaxCachedSessions( std::size_t maxSessions );
    std::size_t maxCachedSessions() const;
    std::size_t cachedSessionCount() const;

    /// Idle eviction (Platform 4.0): sessions untouched for longer than
    /// @p idleMs are dropped on the next acquire/inspect. 0 disables (default).
    void setIdleEvictionMs( std::uint64_t idleMs );
    std::uint64_t idleEvictionMs() const;

    /// Pool statistics snapshot (Platform 4.0 observability + benchmarks).
    struct PoolStats
    {
      std::size_t cachedSessions = 0;
      std::size_t maxSessions = 0;
      std::uint64_t totalLoads = 0;    ///< successful session loads
      std::uint64_t cacheHits = 0;
      std::uint64_t cacheMisses = 0;
      std::uint64_t evictions = 0;     ///< LRU/idle evictions (not releaseAll)
    };
    PoolStats poolStats() const;

    /// Cumulative successful session loads (test metric for reuse checks).
    std::size_t totalSessionsLoaded() const;
    void resetLoadCount();

    /// Register/replace a provider factory for a framework id. The built-in
    /// "onnx" provider (OpenCV DNN) is installed at construction; tests may
    /// override it or add fake frameworks. Traits declare the backend's
    /// device-addressing capability for explicit cuda:N requests.
    void registerProvider( const std::string &framework, ModelRuntimeFactory factory,
                           const ProviderTraits &traits = ProviderTraits{} );
    bool hasProvider( const std::string &framework ) const;
    /// Traits for a registered framework (nullopt when unregistered) — used by
    /// readiness evaluation so cuda:N readiness matches what acquire enforces.
    std::optional<ProviderTraits> providerTraits( const std::string &framework ) const;

    /// Current hardware capabilities (env-overridable detection, cached).
    ModelHardwareCapabilities hardware() const;
    /// Test seam: pin capabilities; pass nullopt to return to detection.
    void setHardwareForTest( const std::optional<ModelHardwareCapabilities> &capabilities );

    /// Platform 7.0 per-device VRAM ledger backing every acquisition: GPU
    /// sessions reserve their manifest estimate on the resolved device for
    /// the lifetime of their cache entry; the ledger is the admission
    /// authority behind device resolution (placement seam, not a scheduler).
    VramLedger &vramLedger() { return m_ledger; }

    // --- Platform 8.0 WP-B: policy + pressure observability ------------------
    /// Placement policy applied to every `auto` acquisition (default
    /// LowestFitting = the 7.0 semantics). Explicit cpu/cuda:N requests are
    /// policy-independent.
    void setPlacementPolicy( DevicePlacementPolicy policy );
    DevicePlacementPolicy placementPolicy() const;

    /// Per-device pressure snapshot (capacity/reserved/holders per GPU plus
    /// the active policy). No sub-allocation exists, so utilization IS the
    /// honest fragmentation view: freeMb = capacity − reserved.
    std::vector<VramLedger::DeviceState> deviceReport() const;

  private:
    ModelRuntimeRegistry();

    /// LRU/idle eviction shared by acquire paths. Caller holds m_mutex.
    void evictExpiredLocked( std::int64_t nowMs );
    /// Platform 7.0 memory-pressure valve: evicts every cached GPU session
    /// pinned to @p cudaIndex (LRU order) and releases its reservation.
    /// ONE bounded pass per failed admission — never an eviction loop.
    /// Caller holds m_mutex.
    void evictDeviceLocked( int cudaIndex );

    struct CacheEntry
    {
      ModelRuntimePtr session;
      std::uint64_t lastUsed = 0; ///< LRU tick (monotonic counter)
      std::int64_t lastUsedMs = 0; ///< wall clock, for idle eviction
      // Platform 7.0 reservation bookkeeping (GPU sessions only).
      int cudaIndex = -1;       ///< -1 = cpu / no reservation
      int reservedVramMb = 0;   ///< estimate reserved on the ledger
      std::string ledgerHolder; ///< session identity in the ledger
    };

    /// Releases a cache entry's VRAM reservation (no-op for CPU entries).
    /// Caller holds m_mutex.
    void dropReservationLocked( const CacheEntry &entry );

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, CacheEntry> m_cache;
    struct ProviderEntry
    {
      ModelRuntimeFactory factory;
      ProviderTraits traits;
    };
    std::unordered_map<std::string, ProviderEntry> m_providers;
    std::optional<ModelHardwareCapabilities> m_hardwareOverride;
    VramLedger m_ledger;
    DevicePlacementPolicy m_placementPolicy = DevicePlacementPolicy::LowestFitting;
    std::size_t m_maxSessions = 2;
    std::size_t m_totalLoaded = 0;
    std::uint64_t m_useCounter = 0;
    std::uint64_t m_idleEvictionMs = 0;
    std::uint64_t m_cacheHits = 0;
    std::uint64_t m_cacheMisses = 0;
    std::uint64_t m_evictions = 0;
};

} // namespace sicnu::operators::runtime
