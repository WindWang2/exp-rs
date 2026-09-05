/***************************************************************************
 * rs_operator_error.h  —  Strongly-typed errors for RSOperator framework
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <exception>
#include <string>

namespace sicnu::operators {

/**
 * Structured error codes for remote sensing operators.
 *
 * Error codes are grouped by category:
 *   0       : Success
 *   1000-1999: Parameter / input validation errors
 *   2000-2999: I/O errors (file, GDAL, network)
 *   3000-3999: Computation / library errors
 *   4000-4999: Cancellation / lifecycle errors
 *   9999    : Unknown
 */
enum class ErrorCode : int {
    Success = 0,

    // Parameter errors (1000-1999)
    InvalidParameter = 1000,
    MissingRequiredParameter,
    TypeMismatch,
    OutOfRange,
    InvalidEnumValue,

    // I/O errors (2000-2999)
    FileNotFound = 2000,
    FileNotReadable,
    FileNotWritable,
    DirectoryNotFound,
    InvalidInputData,

    // Library-specific errors (3000-3999)
    GdalError = 3000,
    OpenCvError,
    OtbError,
    QgisProcessingError,
    ComputationError,

    // Lifecycle errors (4000-4999)
    Cancelled = 4000,
    AlreadyRunning,
    NotInitialized,
    ExternalProcessTimeout = 4100,  ///< external process exceeded its budget (append-only)
    ExternalProcessFailed = 4101,   ///< external process failed (append-only)

    Unknown = 9999
};

/**
 * Converts an ErrorCode to its string representation.
 */
const char* errorCodeToString(ErrorCode code) noexcept;

/**
 * Exception type thrown by RSOperator implementations.
 *
 * Carries a machine-readable ErrorCode, a human-readable message, and an
 * optional Json::Value with structured details (e.g., which parameter failed,
 * underlying library error code).
 */
class RSOperatorError : public std::exception {
public:
    RSOperatorError(ErrorCode code, std::string message, Json::Value details = {});

    ErrorCode code() const noexcept;
    const char* what() const noexcept override;
    const std::string& message() const noexcept;
    const Json::Value& details() const noexcept;

    /**
     * Serializes the error to a JSON object suitable for Agent consumption.
     */
    Json::Value toJson() const;

private:
    ErrorCode m_code;
    std::string m_message;
    Json::Value m_details;
    mutable std::string m_cachedWhat;
};

} // namespace sicnu::operators
