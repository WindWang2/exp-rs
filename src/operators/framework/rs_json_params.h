/***************************************************************************
 * rs_json_params.h  —  Shared JSON parameter helpers for all RSOperators
 ***************************************************************************/
#pragma once

#include "rs_operator_error.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::params {

bool fileExists(const std::string& path);

std::string requireString(const Json::Value& params, const std::string& key);

std::string getString(const Json::Value& params, const std::string& key,
                      const std::string& defaultValue = {});

int getInt(const Json::Value& params, const std::string& key, int defaultValue = 0);

double getDouble(const Json::Value& params, const std::string& key, double defaultValue = 0.0);

bool getBool(const Json::Value& params, const std::string& key, bool defaultValue = false);

bool hasNumber(const Json::Value& params, const std::string& key);

std::string getEnum(const Json::Value& params, const std::string& key,
                    const std::vector<std::string>& allowed,
                    const std::string& defaultValue = {},
                    bool caseInsensitive = true);

/**
 * Parse optional "bands" array of 1-based indices; default = 1..bandCount.
 */
std::vector<int> parseBands(const Json::Value& params, int bandCount);

/**
 * Optional string-array parameter. Missing key → empty vector.
 * @throws RSOperatorError if present but not an array of strings.
 */
std::vector<std::string> getStringArray(const Json::Value& params, const std::string& key);

} // namespace sicnu::operators::params
