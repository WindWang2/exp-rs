/***************************************************************************
 * rs_json_params.cpp
 ***************************************************************************/
#include <limits>
#include "rs_json_params.h"

#include <QFile>
#include <QString>
#include <QStringList>

#include <qgsdatasourceresolver.h>

#include <cpl_vsi.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sicnu::operators::params {

bool fileExists(const std::string& path) {
    // Map file:// URLs to their local path through the single resolver
    // authority before the Qt existence check (#1467/#1472 review):
    // classify() answers LocalFile for them, but QFile::exists on the raw
    // URL always fails, so operators' shared precheck used to reject valid
    // inputs as FileNotFound before the GDAL open could run. Every other
    // source spelling is identity-mapped.
    const QString p = QgsDataSourceResolver::localPath(QString::fromStdString(path));
    if (QFile::exists(p))
        return true;
    // GDAL VSI paths (/vsicurl/, /vsis3/, /vsizip/…) are not Qt files; ask
    // GDAL's VSI layer so remote COG inputs pass the same precheck as local
    // ones (execution-data-plane-3 Phase F).
    if (p.startsWith(QStringLiteral("/vsi")))
    {
        VSIStatBufL statBuf;
        return VSIStatL(p.toUtf8().constData(), &statBuf) == 0;
    }
    return false;
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

namespace {

// asInt()/asInt64() throw Json::LogicError outside their destination range.
// isInt64() may use asInt64(); every other numeric is ranged through asDouble()
// so a uint64 above INT64_MAX or a huge double cannot throw. Caller must have
// checked isNumeric(). In-range fractions truncate toward zero.
bool tryNarrowToInt(const Json::Value& value, int& out) {
    if (value.isInt64()) {
        const Json::Int64 wide = value.asInt64();
        if (wide < std::numeric_limits<int>::min()
            || wide > std::numeric_limits<int>::max()) {
            return false;
        }
        out = static_cast<int>(wide);
        return true;
    }
    const double wide = value.asDouble();
    if (!std::isfinite(wide)
        || wide < static_cast<double>(std::numeric_limits<int>::min())
        || wide > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(wide);
    return true;
}

} // namespace

int getInt(const Json::Value& params, const std::string& key, int defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    if (!params[key].isNumeric()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be an integer");
    }
    int value = 0;
    if (!tryNarrowToInt(params[key], value)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Parameter '" + key + "' is out of integer range");
    }
    return value;
}

std::uint64_t getUInt64(const Json::Value& params, const std::string& key,
                        std::uint64_t defaultValue) {
    if (!params.isMember(key))
        return defaultValue;
    const Json::Value& value = params[key];
    // jsoncpp throws Json::LogicError from asUInt64() for anything that is
    // not an unsigned integer (strings, negatives, reals, out-of-domain
    // ints) — an untyped library crash at the operator boundary. Type-check
    // first, then take the value; illegal input is a typed refusal.
    if (!value.isIntegral() || !value.isUInt64()) {
        throw RSOperatorError(ErrorCode::TypeMismatch,
                              "Parameter '" + key + "' must be a non-negative integer");
    }
    return value.asUInt64();
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
            int b = 0;
            if (!tryNarrowToInt(params["bands"][i], b)) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "Band out of integer range (1-" +
                                          std::to_string(bandCount) + ")");
            }
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
