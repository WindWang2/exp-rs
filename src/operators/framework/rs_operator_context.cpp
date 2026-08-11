/***************************************************************************
 * rs_operator_context.cpp  —  RSOperatorContext implementation
 ***************************************************************************/
#include "rs_operator_context.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>

namespace sicnu::operators {

namespace {

std::filesystem::path ensureWorkDir(const std::string& userWorkDir) {
    std::filesystem::path dir;
    if (userWorkDir.empty()) {
        dir = std::filesystem::temp_directory_path() / "sicnu_operators";
    } else {
        dir = userWorkDir;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        throw RSOperatorError(ErrorCode::DirectoryNotFound,
                              "Failed to create operator work directory: " + dir.string(),
                              Json::Value(ec.message()));
    }
    return dir;
}

std::string currentThreadId() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

} // anonymous namespace

RSOperatorContext::RSOperatorContext(const std::string& workDir)
    : m_workDir(ensureWorkDir(workDir)) {
}

void RSOperatorContext::setProgressCallback(ProgressFn callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_progressCallback = std::move(callback);
}

void RSOperatorContext::emitProgress(double progress, const std::string& message) const {
    progress = std::clamp(progress, 0.0, 1.0);
    if (m_progressCallback) {
        m_progressCallback(progress, message);
    }
    m_lastProgress = progress;
    m_lastProgressMessage = message;
}

void RSOperatorContext::reportProgress(double progress, const std::string& message) const {
    progress = std::clamp(progress, 0.0, 1.0);
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    const bool boundary = (progress <= 0.0) || (progress >= 1.0);
    const bool messageChanged = (message != m_lastProgressMessage);
    const bool jumped = (m_lastProgress < 0.0) ||
                        (std::abs(progress - m_lastProgress) >= kProgressThrottle);
    if (boundary || messageChanged || jumped) {
        emitProgress(progress, message);
    }
}

void RSOperatorContext::reportProgressForced(double progress, const std::string& message) const {
    progress = std::clamp(progress, 0.0, 1.0);
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    emitProgress(progress, message);
}

void RSOperatorContext::setLogCallback(LogFn callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_logCallback = std::move(callback);
}

void RSOperatorContext::logInfo(const std::string& message) const {
    log("info", message);
}

void RSOperatorContext::logWarning(const std::string& message) const {
    log("warning", message);
}

void RSOperatorContext::logError(const std::string& message) const {
    log("error", message);
}

void RSOperatorContext::log(const std::string& level, const std::string& message) const {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_logCallback) {
        m_logCallback(message, level);
    }
}

void RSOperatorContext::setCancelFlag(std::atomic<bool>* flag) {
    m_cancelFlag = flag;
}

void RSOperatorContext::setCancelCallback(CancelFn callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_cancelCallback = std::move(callback);
}

bool RSOperatorContext::isCancelled() const {
    // The flag is the primary cancel source (cheap, no lock). The callback is
    // consulted when no flag is wired, so an external predicate (JobEngine /
    // TaskCenter / adapter isCancelledFn) is observed mid-run. The callback is
    // copied under the mutex and invoked outside it: no data race with a
    // concurrent setCancelCallback(), and a callback that re-enters
    // setCancelCallback() cannot deadlock.
    if (m_cancelFlag && m_cancelFlag->load(std::memory_order_acquire)) {
        return true;
    }
    CancelFn cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_cancelCallback;
    }
    return cb ? cb() : false;
}

void RSOperatorContext::throwIfCancelled() const {
    if (isCancelled()) {
        throw RSOperatorError(ErrorCode::Cancelled, "Operator execution was cancelled");
    }
}

std::string RSOperatorContext::workDir() const {
    return m_workDir.string();
}

std::string RSOperatorContext::tempPath(const std::string& suffix) const {
    const int counter = m_tempCounter.fetch_add(1, std::memory_order_relaxed);

    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    std::filesystem::path path = m_workDir;
    std::ostringstream name;
    name << "sicnu_op_" << micros << "_" << counter << "_" << currentThreadId();
    if (!suffix.empty()) {
        name << suffix;
    }
    path /= name.str();
    return path.string();
}

} // namespace sicnu::operators
