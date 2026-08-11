/***************************************************************************
 * rs_operator_context.h  —  Execution context for RSOperator runs
 ***************************************************************************/
#pragma once

#include "rs_operator_error.h"
#include "rs_progress_callback.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace sicnu::operators {

/// Cooperative cancellation predicate: returns true when cancellation has been
/// requested (thread-safe; invoked from the operator's thread).
using CancelFn = std::function<bool()>;

/**
 * Context object passed to every RSOperator::run() call.
 *
 * The context provides:
 *   - Progress reporting callbacks
 *   - Cooperative cancellation flag
 *   - Log message forwarding
 *   - A working / scratch directory
 *
 * Context is intentionally non-copyable. It does not own the cancel flag;
 * the caller must guarantee the flag outlives the run().
 */
class RSOperatorContext {
public:
    /**
     * Creates a context.
     *
     * @param workDir  Working directory for temporary files. If empty, the
     *                 system temporary directory is used.
     */
    explicit RSOperatorContext(const std::string& workDir = {});
    ~RSOperatorContext() = default;

    RSOperatorContext(const RSOperatorContext&) = delete;
    RSOperatorContext& operator=(const RSOperatorContext&) = delete;
    // Holding a std::mutex (m_callbackMutex) makes the move special members
    // implicitly deleted; declare them delete explicitly so the build stays
    // -Wdefaulted-function-deleted-clean. RSOperatorContext is always used in
    // place (constructed once, passed by reference), so neither is needed.
    RSOperatorContext(RSOperatorContext&&) = delete;
    RSOperatorContext& operator=(RSOperatorContext&&) = delete;

    /**
     * Sets the progress callback. May be empty (no progress reporting).
     */
    void setProgressCallback(ProgressFn callback);

    /**
     * Reports progress in the range [0.0, 1.0]. Safe to call from any thread.
     *
     * Throttled: skips updates that advance by less than ~2% unless the
     * message changes or progress reaches 0/1 (reduces pipeline log spam
     * from fine-grained GDAL callbacks).
     */
    void reportProgress(double progress, const std::string& message = {}) const;

    /**
     * Force a progress report without throttling (e.g. step boundaries).
     */
    void reportProgressForced(double progress, const std::string& message = {}) const;

    /**
     * Sets the log callback. May be empty.
     */
    void setLogCallback(LogFn callback);

    void logInfo(const std::string& message) const;
    void logWarning(const std::string& message) const;
    void logError(const std::string& message) const;

    /**
     * Sets the cooperative cancellation flag. The context does not own it.
     */
    void setCancelFlag(std::atomic<bool>* flag);

    /**
     * Sets a cooperative cancellation callback (e.g. an external JobEngine /
     * TaskCenter cancel predicate). Polled by isCancelled()/throwIfCancelled()
     * from the operator's thread; mutually exclusive with a flag — the flag
     * wins when both are set. The context does not own the callback; the
     * caller must ensure it stays valid for the duration of run().
     */
    void setCancelCallback(CancelFn callback);

    /**
     * Returns true if cancellation has been requested.
     */
    bool isCancelled() const;

    /**
     * Throws RSOperatorError(ErrorCode::Cancelled) if cancellation requested.
     */
    void throwIfCancelled() const;

    /**
     * Returns the working directory used for temporary files.
     */
    std::string workDir() const;

    /**
     * Generates a unique temporary path inside the working directory.
     *
     * @param suffix  Optional suffix/extension (e.g. ".tif").
     */
    std::string tempPath(const std::string& suffix = {}) const;

private:
    void log(const std::string& level, const std::string& message) const;
    void emitProgress(double progress, const std::string& message) const;

    mutable std::mutex m_callbackMutex;
    ProgressFn m_progressCallback;
    LogFn m_logCallback;
    std::atomic<bool>* m_cancelFlag = nullptr;
    CancelFn m_cancelCallback;
    std::filesystem::path m_workDir;
    mutable std::atomic<int> m_tempCounter{0};
    mutable double m_lastProgress = -1.0;
    mutable std::string m_lastProgressMessage;
    static constexpr double kProgressThrottle = 0.02; // 2%
};

} // namespace sicnu::operators
