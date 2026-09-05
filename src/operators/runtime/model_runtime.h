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
// This translation unit requires OpenCV (cv::Mat is the tensor type); it is
// compiled only under SICNU_HAS_OPENCV. Without OpenCV no model runtime
// exists and rs:infer is disabled, exactly as before.
#pragma once

#include "operators/framework/model_catalog.h"
#include "operators/framework/model_readiness.h"

#include <opencv2/core.hpp>

#include <cstddef>
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
 *   SICNU_MODEL_GPU=0|1   force cudaAvailable
 *   SICNU_MODEL_VRAM_MB=N force the VRAM budget
 */
struct ModelHardwareCapabilities
{
  bool cudaAvailable = false;
  bool openclAvailable = false;
  int vramBudgetMb = 0; ///< 0 = unknown / not enforced

  static ModelHardwareCapabilities detect();
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
 * Process-wide session cache. Keyed by (framework, artifact, device) so the
 * same weights are loaded once and reused. LRU-bounded (default 2 sessions —
 * weights are the dominant memory cost), thread-safe, evictable via
 * releaseAll() for shutdown and tests.
 */
class ModelRuntimeRegistry
{
  public:
    static ModelRuntimeRegistry &instance();

    /// Acquire a session for the model, loading it on first use.
    /// @a errorMessage receives the load failure reason when nullptr is returned.
    ModelRuntimePtr acquire( const ModelInfo &model, std::string *errorMessage = nullptr );

    /// Drop all cached sessions (running callers keep their shared_ptrs).
    void releaseAll();

    void setMaxCachedSessions( std::size_t maxSessions );
    std::size_t maxCachedSessions() const;
    std::size_t cachedSessionCount() const;

    /// Cumulative successful session loads (test metric for reuse checks).
    std::size_t totalSessionsLoaded() const;
    void resetLoadCount();

    /// Register/replace a provider factory for a framework id. The built-in
    /// "onnx" provider (OpenCV DNN) is installed at construction; tests may
    /// override it or add fake frameworks.
    void registerProvider( const std::string &framework, ModelRuntimeFactory factory );
    bool hasProvider( const std::string &framework ) const;

    /// Current hardware capabilities (env-overridable detection, cached).
    ModelHardwareCapabilities hardware() const;
    /// Test seam: pin capabilities; pass nullopt to return to detection.
    void setHardwareForTest( const std::optional<ModelHardwareCapabilities> &capabilities );

  private:
    ModelRuntimeRegistry();

    struct CacheEntry
    {
      ModelRuntimePtr session;
      std::uint64_t lastUsed = 0;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, CacheEntry> m_cache;
    std::unordered_map<std::string, ModelRuntimeFactory> m_providers;
    std::optional<ModelHardwareCapabilities> m_hardwareOverride;
    std::size_t m_maxSessions = 2;
    std::size_t m_totalLoaded = 0;
    std::uint64_t m_useCounter = 0;
};

} // namespace sicnu::operators::runtime
