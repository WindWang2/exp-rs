/***************************************************************************
 * rs_json_params.cpp
 ***************************************************************************/
#include <limits>
#include "rs_json_params.h"

#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cctype>

namespace sicnu::operators::params {

bool fileExists(const std::string& path) {
    return QFile::exists(QString::fromStdString(path));
}

std::string requireString(const Json::Value& params, const std::string& key) {
    if (!params.isMember(key) || !params[key].isString()) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "Missing required string parameter: " + key);
    }
    return params[key].asString();
}

std::string getString(const Json::Value& params, const std::string& key,
                      const std::string& defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    if (!params[key].isString()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be a string");
    }
    return params[key].asString();
}

int getInt(const Json::Value& params, const std::string& key, int defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    if (!params[key].isNumeric()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be an integer");
    }
    // jsoncpp throws Json::LogicError from asInt() for out-of-range numerics
    // (#647): range-check through int64 and surface a structured
    // InvalidParameter instead of leaking the raw library message.
    const Json::Int64 value = params[key].asInt64();
    if (value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Parameter '" + key + "' is out of integer range");
    }
    return static_cast<int>(value);
}

double getDouble(const Json::Value& params, const std::string& key, double defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    if (!params[key].isNumeric()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be a number");
    }
    return params[key].asDouble();
}

bool getBool(const Json::Value& params, const std::string& key, bool defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    if (params[key].isBool())
        return params[key].asBool();
    if (params[key].isInt())
        return params[key].asInt() != 0;
    throw RSOperatorError(ErrorCode::TypeMismatch,
                          "Parameter '" + key + "' must be a boolean");
}

bool hasNumber(const Json::Value& params, const std::string& key) {
    return params.isMember(key) && params[key].isNumeric();
}

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string getEnum(const Json::Value& params, const std::string& key,
                    const std::vector<std::string>& allowed,
                    const std::string& defaultValue,
                    bool caseInsensitive) {
    if (!params.isMember(key))
        return defaultValue;
    if (!params[key].isString()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be a string");
    }
    const std::string raw = params[key].asString();
    // Return the canonical entry from `allowed` so callers can compare against
    // schema constants (e.g. "NDVI") regardless of input case.
    for (const auto& candidate : allowed) {
        if (caseInsensitive) {
            if (toLowerCopy(raw) == toLowerCopy(candidate))
                return candidate;
        } else if (raw == candidate) {
            return candidate;
        }
    }
    QStringList list;
    for (const auto& v : allowed)
        list << QString::fromStdString(v);
    throw RSOperatorError(ErrorCode::InvalidEnumValue,
                          "Invalid value '" + raw + "' for parameter '" + key +
                              "'. Allowed: " + list.join(", ").toStdString());
}

std::vector<int> parseBands(const Json::Value& params, int bandCount) {
    std::vector<int> bands;
    if (params.isMember("bands") && params["bands"].isArray() && !params["bands"].empty()) {
        for (Json::ArrayIndex i = 0; i < params["bands"].size(); ++i) {
            if (!params["bands"][i].isNumeric()) {
                throw RSOperatorError(ErrorCode::TypeMismatch,
                                      "bands array elements must be integers");
            }
            const int b = params["bands"][i].asInt();
            if (b < 1 || b > bandCount) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "Band " + std::to_string(b) + " out of range (1-" +
                                          std::to_string(bandCount) + ")");
            }
            bands.push_back(b);
        }
        return bands;
    }
    bands.reserve(static_cast<size_t>(bandCount));
    for (int b = 1; b <= bandCount; ++b)
        bands.push_back(b);
    return bands;
}

std::vector<std::string> getStringArray(const Json::Value& params, const std::string& key) {
    if (!params.isMember(key))
        return {};
    if (!params[key].isArray()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be an array of strings");
    }
    std::vector<std::string> result;
    result.reserve(params[key].size());
    for (const auto& item : params[key]) {
        if (!item.isString()) {
            throw RSOperatorError(ErrorCode::TypeMismatch,
                                  "Parameter '" + key + "' must be an array of strings");
        }
        result.push_back(item.asString());
    }
    return result;
}

} // namespace sicnu::operators::params
