/***************************************************************************
 * rs_schema.h  —  JSON Schema helpers for RSOperator parameters/outputs
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::operators::schema {

/**
 * Builds a root schema object for an operator.
 *
 * @param title       Human-readable title.
 * @param description Operator description.
 * @param params      Array of parameter schema objects.
 * @param outputs     Array of output schema objects.
 */
Json::Value makeRootSchema(const std::string& title,
                           const std::string& description,
                           const Json::Value& params,
                           const Json::Value& outputs);

/**
 * Stamps the schema root with the operator's Determinism Grade (ADR 0124,
 * #659): "bit-exact" | "tolerance". Call after makeRootSchema.
 */
void stampDeterminismGrade(Json::Value& schemaRoot, const std::string& grade);

/**
 * Declares a required parameter name list.
 */
Json::Value makeRequired(const std::vector<std::string>& names);

Json::Value makeStringParam(const std::string& name,
                            const std::string& description,
                            const std::string& defaultValue = {});

/**
 * JSON-array parameter. `items` is the JSON-Schema description of one
 * element (a scalar `{"type":"string"}`, or a nested array/object).
 * minItems/maxItems are omitted when non-positive.
 *
 * Runtime readers such as getStringArray() reject a string, so declaring
 * these parameters "string" made a schema-validating agent refuse the
 * array the operator actually requires (#1642).
 */
Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const Json::Value& items,
                           int minItems = 0,
                           int maxItems = 0);

/// Scalar-item form of makeArrayParam (`itemType` is "string", "number", …).
Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const std::string& itemType,
                           int minItems = 0,
                           int maxItems = 0);

/// String-literal form. A bare literal is ambiguous between this overload and
/// the Json::Value items overload: jsoncpp's Value constructs from const char*.
Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const char* itemType,
                           int minItems = 0,
                           int maxItems = 0);

Json::Value makeNumberParam(const std::string& name,
                            const std::string& description,
                            double defaultValue = 0.0);

Json::Value makeIntegerParam(const std::string& name,
                             const std::string& description,
                             int defaultValue = 0);

Json::Value makeBooleanParam(const std::string& name,
                             const std::string& description,
                             bool defaultValue = false);

Json::Value makeEnumParam(const std::string& name,
                          const std::string& description,
                          const std::vector<std::string>& values,
                          const std::string& defaultValue = {});

/**
 * Raster layer/file input parameter.
 */
Json::Value makeRasterParam(const std::string& name,
                            const std::string& description,
                            bool required = true);

/**
 * Vector layer/file input parameter.
 */
Json::Value makeVectorParam(const std::string& name,
                            const std::string& description,
                            bool required = true);

/**
 * Output file parameter.
 */
Json::Value makeOutputParam(const std::string& name,
                            const std::string& description,
                            const std::string& fileType = "tif");

/**
 * Helper to add numeric range constraints to a number/integer parameter.
 */
Json::Value& setRange(Json::Value& param, double minimum, double maximum);

} // namespace sicnu::operators::schema
