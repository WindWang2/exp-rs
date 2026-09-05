/***************************************************************************
 * rs_operator_error.cpp  —  RSOperatorError implementation
 ***************************************************************************/
#include "rs_operator_error.h"

namespace sicnu::operators {

const char* errorCodeToString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::InvalidParameter: return "InvalidParameter";
        case ErrorCode::MissingRequiredParameter: return "MissingRequiredParameter";
        case ErrorCode::TypeMismatch: return "TypeMismatch";
        case ErrorCode::OutOfRange: return "OutOfRange";
        case ErrorCode::InvalidEnumValue: return "InvalidEnumValue";
        case ErrorCode::FileNotFound: return "FileNotFound";
        case ErrorCode::FileNotReadable: return "FileNotReadable";
        case ErrorCode::FileNotWritable: return "FileNotWritable";
        case ErrorCode::DirectoryNotFound: return "DirectoryNotFound";
        case ErrorCode::InvalidInputData: return "InvalidInputData";
        case ErrorCode::GdalError: return "GdalError";
        case ErrorCode::OpenCvError: return "OpenCvError";
        case ErrorCode::OtbError: return "OtbError";
        case ErrorCode::QgisProcessingError: return "QgisProcessingError";
        case ErrorCode::ComputationError: return "ComputationError";
        case ErrorCode::Cancelled: return "Cancelled";
        case ErrorCode::AlreadyRunning: return "AlreadyRunning";
        case ErrorCode::NotInitialized: return "NotInitialized";
        case ErrorCode::ExternalProcessTimeout: return "ExternalProcessTimeout";
        case ErrorCode::ExternalProcessFailed: return "ExternalProcessFailed";
        case ErrorCode::Unknown: return "Unknown";
    }
    return "Unknown";
}

RSOperatorError::RSOperatorError(ErrorCode code, std::string message, Json::Value details)
    : m_code(code)
    , m_message(std::move(message))
    , m_details(std::move(details)) {
}

ErrorCode RSOperatorError::code() const noexcept {
    return m_code;
}

const char* RSOperatorError::what() const noexcept {
    if (m_cachedWhat.empty()) {
        m_cachedWhat = std::string(errorCodeToString(m_code)) + ": " + m_message;
    }
    return m_cachedWhat.c_str();
}

const std::string& RSOperatorError::message() const noexcept {
    return m_message;
}

const Json::Value& RSOperatorError::details() const noexcept {
    return m_details;
}

Json::Value RSOperatorError::toJson() const {
    Json::Value root(Json::objectValue);
    root["code"] = static_cast<int>(m_code);
    root["codeName"] = errorCodeToString(m_code);
    root["message"] = m_message;
    if (!m_details.isNull()) {
        root["details"] = m_details;
    }
    return root;
}

} // namespace sicnu::operators
