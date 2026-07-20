/***************************************************************************
 * rs_progress_callback.h  —  Progress and cancellation callbacks
 ***************************************************************************/
#pragma once

#include <functional>
#include <string>

namespace sicnu::operators {

/**
 * Function signature for progress reporting.
 *
 * @param progress  Progress in the range [0.0, 1.0].
 * @param message   Optional human-readable status message.
 */
using ProgressFn = std::function<void(double progress, const std::string& message)>;

/**
 * Function signature for log messages emitted by an operator.
 *
 * @param message  Log message.
 * @param level    Log level: "info", "warning", "error".
 */
using LogFn = std::function<void(const std::string& message, const std::string& level)>;

/**
 * Virtual interface for progress/cancellation feedback.
 *
 * Implementations must be thread-safe: methods may be called from the
 * operator's worker thread.
 */
class IProgressCallback {
public:
    virtual ~IProgressCallback() = default;

    virtual void onProgress(double progress, const std::string& message) = 0;
    virtual void onLog(const std::string& message, const std::string& level) = 0;
    virtual bool isCancelled() const = 0;
};

} // namespace sicnu::operators
