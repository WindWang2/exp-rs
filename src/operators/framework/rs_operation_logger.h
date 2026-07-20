/***************************************************************************
 * rs_operation_logger.h  —  Lab experiment operation logger
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::operators {

/**
 * \brief Record of a single operator execution.
 */
struct OperationRecord {
    std::string operatorName;     //!< e.g. "rs:spectral_index"
    Json::Value parameters;       //!< Input parameters
    Json::Value result;           //!< Result JSON (may be empty)
    bool success = false;         //!< true if run completed without exception
    std::string errorMessage;     //!< Error message on failure
    int errorCode = 0;            //!< Numeric error code on failure
    std::string startTimeIso;     //!< ISO 8601 start timestamp
    std::string endTimeIso;       //!< ISO 8601 end timestamp
    double durationMs = 0.0;      //!< Wall-clock duration in milliseconds
};

/**
 * \brief Singleton logger that records every RSOperator execution.
 *
 * The logger is thread-safe.  Records are kept in memory until exported
 * or cleared.  Intended for teaching labs where students need a machine-
 * readable trail of every processing step for their reports.
 */
class RSOperationLogger {
public:
    static RSOperationLogger& instance();

    /**
     * \brief Begin tracking an operator run.
     *
     * Returns an internal handle (index) that must be passed to finishRun().
     */
    size_t beginRun(const std::string& operatorName, const Json::Value& params);

    /**
     * \brief Mark the run as finished successfully.
     * \param durationMs Wall-clock execution time in milliseconds.
     */
    void finishRun(size_t handle, const Json::Value& result, double durationMs = 0.0);

    /**
     * \brief Mark the run as failed with an error.
     * \param durationMs Wall-clock execution time in milliseconds.
     */
    void failRun(size_t handle, int errorCode, const std::string& errorMessage, double durationMs = 0.0);

    /**
     * \brief Export all records to a JSON array.
     */
    Json::Value toJson() const;

    /**
     * \brief Export all records to CSV text.
     */
    std::string toCsv() const;

    /**
     * \brief Write records to a file.  Format is inferred from extension
     * (.json or .csv).  Returns true on success.
     */
    bool exportToFile(const std::string& filePath, std::string* errorMessage = nullptr) const;

    /**
     * \brief Remove all records.
     */
    void clear();

    /**
     * \brief Number of records currently stored.
     */
    size_t recordCount() const;

    /**
     * \brief Get a copy of all records.
     */
    std::vector<OperationRecord> records() const;

private:
    RSOperationLogger() = default;
    ~RSOperationLogger() = default;
    RSOperationLogger(const RSOperationLogger&) = delete;
    RSOperationLogger& operator=(const RSOperationLogger&) = delete;

    static std::string nowIso8601();

    mutable std::mutex m_mutex;
    std::vector<OperationRecord> m_records;
};

} // namespace sicnu::operators
