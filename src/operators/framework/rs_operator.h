/***************************************************************************
 * rs_operator.h  —  Abstract base class for remote sensing operators
 ***************************************************************************/
#pragma once

#include "rs_operator_context.h"
#include "rs_operator_error.h"
#include "rs_schema.h"

#include <json/json.h>

#include <memory>
#include <string>

namespace sicnu::operators {

/// Memory behavior of an operator when processing large rasters. Every
/// operator declares one so large-image capability is explicit rather than
/// implied by a few streaming kernels.
enum class RSOperatorMemoryPolicy
{
  Streaming,                  ///< out-of-core tile/block streaming (O(tile) memory)
  MultiPassStreaming,         ///< multiple streaming passes (O(tile) + O(histogram/global state))
  FullRaster,                 ///< whole raster(s) in memory (O(width*height*bands))
  ExternalProcess,            ///< delegates to an external process that manages its own tiling
  UnsupportedForLargeRaster,  ///< documented as unsuitable for large rasters
};

/// Stable lowercase identifier for a memory policy ("streaming",
/// "multipass_streaming", "full_raster", "external_process",
/// "unsupported_for_large_raster").
const char *memoryPolicyName( RSOperatorMemoryPolicy policy );

/**
 * Abstract base class for all remote sensing operators.
 *
 * Each operator represents a single, well-defined remote sensing or GIS
 * processing step (e.g. NDVI computation, Gaussian blur, orthorectification).
 * Operators are:
 *
 *   - Parameterized via Json::Value
 *   - Observable via RSOperatorContext progress/log callbacks
 *   - Cancellable via a cooperative atomic flag
 *   - Self-describing via schema() and metadata()
 *   - Thread-safe: run() may be called from any thread
 *
 * Implementations must not perform GUI operations or assume they are running
 * on the Qt main thread.
 */
class RSOperator {
public:
    virtual ~RSOperator() = default;

    /**
     * Unique operator identifier, e.g. "rs:spectral_index", "opencv:gaussian_blur".
     */
    virtual std::string name() const = 0;

    /**
     * Human-readable display name.
     */
    virtual std::string displayName() const;

    /**
     * Logical group for UI/Agent organization, e.g. "spectral", "filter", "geometry".
     */
    virtual std::string group() const;

    /**
     * Short human-readable description.
     */
    virtual std::string description() const;

    /**
     * Memory policy for large-raster processing. Defaults to FullRaster;
     * streaming / external-process operators override it.
     */
    virtual RSOperatorMemoryPolicy memoryPolicy() const
    {
      return RSOperatorMemoryPolicy::FullRaster;
    }

    /**
     * JSON Schema describing input parameters.
     */
    virtual Json::Value schema() const;

    /**
     * Agent-readable metadata: purpose, useCases, prerequisites, limitations,
     * workflowHints, tags, etc.
     */
    virtual Json::Value metadata() const;

    /**
     * Executes the operator.
     *
     * @param params  Algorithm parameters as Json::Value object.
     * @param context Execution context for progress, logs, cancellation, temp files.
     * @return        Algorithm results as Json::Value (commonly an object with
     *                "output" path and other derived values).
     * @throws RSOperatorError on validation or execution failure.
     */
    virtual Json::Value run(const Json::Value& params,
                            RSOperatorContext& context) = 0;

    /**
     * Convenience wrapper that runs the operator and automatically records
     * the execution in the RSOperationLogger for lab-report generation.
     *
     * This method is non-virtual; it delegates to run() and catches
     * RSOperatorError so the failure can be logged before rethrowing.
     */
    Json::Value execute(const Json::Value& params, RSOperatorContext& context);
};

using RSOperatorPtr = std::unique_ptr<RSOperator>;

} // namespace sicnu::operators
