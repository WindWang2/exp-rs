/***************************************************************************
 * rs_schema.cpp  —  JSON Schema helper implementations
 ***************************************************************************/
#include "rs_schema.h"

#include <stdexcept>

namespace sicnu::operators::schema {

namespace {

Json::Value baseParam(const std::string& type,
                      const std::string& name,
                      const std::string& description) {
    Json::Value p(Json::objectValue);
    p["name"] = name;
    p["type"] = type;
    p["description"] = description;
    return p;
}

} // anonymous namespace

Json::Value makeRootSchema(const std::string& title,
                           const std::string& description,
                           const Json::Value& params,
                           const Json::Value& outputs) {
    Json::Value root(Json::objectValue);
    root["$schema"] = "http://json-schema.org/draft-07/schema#";
    root["title"] = title;
    root["description"] = description;
    root["type"] = "object";
    root["properties"] = params;
    root["outputs"] = outputs;
    return root;
}

void stampDeterminismGrade(Json::Value& schemaRoot, const std::string& grade) {
    // Fail fast at schema build time on an invalid grade (#659): a typo'd
    // override must never reach the uniform schema surface.
    if (grade != "bit-exact" && grade != "tolerance") {
        throw std::invalid_argument("invalid determinism grade '" + grade
                                    + "' (expected bit-exact|tolerance)");
    }
    schemaRoot["determinismGrade"] = grade;
}

Json::Value makeRequired(const std::vector<std::string>& names) {
    Json::Value req(Json::arrayValue);
    for (const auto& name : names) {
        req.append(name);
    }
    return req;
}

Json::Value makeStringParam(const std::string& name,
                            const std::string& description,
                            const std::string& defaultValue) {
    auto p = baseParam("string", name, description);
    if (!defaultValue.empty()) {
        p["default"] = defaultValue;
    }
    return p;
}

Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const Json::Value& items,
                           int minItems,
                           int maxItems) {
    auto p = baseParam("array", name, description);
    p["items"] = items;
    if (minItems > 0)
        p["minItems"] = minItems;
    if (maxItems > 0)
        p["maxItems"] = maxItems;
    return p;
}

Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const std::string& itemType,
                           int minItems,
                           int maxItems) {
    Json::Value item(Json::objectValue);
    item["type"] = itemType;
    return makeArrayParam(name, description, item, minItems, maxItems);
}

Json::Value makeArrayParam(const std::string& name,
                           const std::string& description,
                           const char* itemType,
                           int minItems,
                           int maxItems) {
    return makeArrayParam(name, description,
                          std::string(itemType ? itemType : ""),
                          minItems, maxItems);
}

Json::Value makeNumberParam(const std::string& name,
                            const std::string& description,
                            double defaultValue) {
    auto p = baseParam("number", name, description);
    p["default"] = defaultValue;
    return p;
}

Json::Value makeIntegerParam(const std::string& name,
                             const std::string& description,
                             int defaultValue) {
    auto p = baseParam("integer", name, description);
    p["default"] = defaultValue;
    return p;
}

Json::Value makeBooleanParam(const std::string& name,
                             const std::string& description,
                             bool defaultValue) {
    auto p = baseParam("boolean", name, description);
    p["default"] = defaultValue;
    return p;
}

Json::Value makeEnumParam(const std::string& name,
                          const std::string& description,
                          const std::vector<std::string>& values,
                          const std::string& defaultValue) {
    auto p = baseParam("string", name, description);
    Json::Value enumValues(Json::arrayValue);
    for (const auto& v : values) {
        enumValues.append(v);
    }
    p["enum"] = enumValues;
    if (!defaultValue.empty()) {
        p["default"] = defaultValue;
    }
    return p;
}

Json::Value makeRasterParam(const std::string& name,
                            const std::string& description,
                            bool required) {
    auto p = baseParam("string", name, description);
    p["format"] = "raster";
    p["SicnuFileRole"] = "input";
    p["required"] = required;
    return p;
}

Json::Value makeVectorParam(const std::string& name,
                            const std::string& description,
                            bool required) {
    auto p = baseParam("string", name, description);
    p["format"] = "vector";
    p["SicnuFileRole"] = "input";
    p["required"] = required;
    return p;
}

Json::Value makeOutputParam(const std::string& name,
                            const std::string& description,
                            const std::string& fileType) {
    auto p = baseParam("string", name, description);
    p["format"] = fileType;
    p["SicnuFileRole"] = "output";
    return p;
}

Json::Value& setRange(Json::Value& param, double minimum, double maximum) {
    param["minimum"] = minimum;
    param["maximum"] = maximum;
    return param;
}

} // namespace sicnu::operators::schema
