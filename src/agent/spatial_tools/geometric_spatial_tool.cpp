// src/agent/spatial_tools/geometric_spatial_tool.cpp — F13: catalog adapter
// for rs::agent::GeometricTool (fixes the D14 registration gap: the tool was
// compiled but never surfaced through SpatialToolRegistry).
#include "geometric_spatial_tool.h"

#include "agent/tools/geometric_tool.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::agent::spatial_tools {

namespace {

/// jsoncpp -> Qt JSON conversion (objects/arrays/scalars; no recursion depth
/// risk from agent inputs in practice, but the cap keeps hostile payloads
/// bounded anyway).
QJsonValue toJsonQt(const Json::Value& value, int depth = 0)
{
    if (depth > 32)
        return {};
    switch (value.type()) {
    case Json::nullValue:
        return {};
    case Json::intValue:
        return static_cast<qint64>(value.asInt64());
    case Json::uintValue:
        return static_cast<qint64>(value.asUInt64());
    case Json::realValue:
        return value.asDouble();
    case Json::stringValue:
        return QString::fromStdString(value.asString());
    case Json::booleanValue:
        return value.asBool();
    case Json::arrayValue: {
        QJsonArray arr;
        for (const auto& item : value)
            arr.append(toJsonQt(item, depth + 1));
        return arr;
    }
    case Json::objectValue: {
        QJsonObject obj;
        for (const auto& key : value.getMemberNames())
            obj.insert(QString::fromStdString(key), toJsonQt(value[key], depth + 1));
        return obj;
    }
    }
    return {};
}

/// Qt JSON -> jsoncpp conversion.
Json::Value toJsonCpp(const QJsonValue& value, int depth = 0)
{
    if (depth > 32)
        return {};
    if (value.isNull() || value.isUndefined())
        return {};
    if (value.isBool())
        return Json::Value(value.toBool());
    if (value.isDouble())
        return Json::Value(value.toDouble());
    if (value.isString())
        return Json::Value(value.toString().toStdString());
    if (value.isArray()) {
        Json::Value arr(Json::arrayValue);
        const QJsonArray qa = value.toArray();
        for (const auto& item : qa)
            arr.append(toJsonCpp(item, depth + 1));
        return arr;
    }
    if (value.isObject()) {
        Json::Value obj(Json::objectValue);
        const QJsonObject qo = value.toObject();
        for (auto it = qo.begin(); it != qo.end(); ++it)
            obj[it.key().toStdString()] = toJsonCpp(it.value(), depth + 1);
        return obj;
    }
    return {};
}

Json::Value schemaFromQt(const QJsonObject& qtSchema)
{
    return toJsonCpp(qtSchema);
}

} // namespace

std::string GeometricSpatialTool::description() const
{
    const rs::agent::GeometricTool tool;
    return tool.toolDescription().toStdString();
}

std::vector<std::string> GeometricSpatialTool::tags() const
{
    return {"geometric", "registration", "gcp", "multimodal", "rpc", "read-only"};
}

Json::Value GeometricSpatialTool::inputSchema() const
{
    const rs::agent::GeometricTool tool;
    return schemaFromQt(tool.parameterSchema());
}

Json::Value GeometricSpatialTool::outputSchema() const
{
    Json::Value schema(Json::objectValue);
    schema["$schema"] = "http://json-schema.org/draft-07/schema#";
    schema["type"] = "object";
    schema["description"] =
        "Platform envelope: success, action, data (action-specific evidence), "
        "diagnostic_message. Registration actions carry status/reason refusal "
        "semantics inside data instead of failing the tool call.";
    Json::Value props(Json::objectValue);
    props["success"] = [] {
        Json::Value s(Json::objectValue);
        s["type"] = "boolean";
        return s;
    }();
    props["action"] = [] {
        Json::Value s(Json::objectValue);
        s["type"] = "string";
        return s;
    }();
    props["data"] = [] {
        Json::Value s(Json::objectValue);
        s["type"] = "object";
        return s;
    }();
    props["diagnostic_message"] = [] {
        Json::Value s(Json::objectValue);
        s["type"] = "string";
        return s;
    }();
    schema["properties"] = props;
    return schema;
}

SpatialToolResult GeometricSpatialTool::execute(const Json::Value& input)
{
    rs::agent::GeometricTool tool;
    const QJsonValue converted = toJsonQt(input);
    const QJsonObject params = converted.isObject() ? converted.toObject() : QJsonObject();
    const QJsonObject envelope = tool.execute(params);
    return SpatialToolResult::ok(toJsonCpp(envelope));
}

} // namespace sicnu::agent::spatial_tools
