// tests/test_surface_parity.cpp
//
// Surface-11 drift gate: the union projection (meta protocol tools + data
// platform tools + AgentToolCatalog) is the ONE discovery surface that MCP
// tools/list, the CLI `tools` command, and get_tool_schema render. These
// tests pin:
//   1. projection invariants (unique names, schema shape, determinism);
//   2. MCP tools/list == projection (in-process, same registry state);
//   3. every projected meta/data-platform tool is dispatchable (a table row
//      without a dispatch branch, or the reverse, fails);
//   4. dispatch-visible spatial families are listing-visible (surfaceIdAllowed).
// Independent oracle: the expected schemas are re-derived here directly from
// the source tables, never by reusing the projection's builders.

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <string>
#include <vector>

#include "agent/mcp_server.h"
#include "agent/tool_catalog/surface_registry.h"
#include "agent/tool_catalog/meta_protocol_tools.h"
#include "agent/data_platform_tools.h"

namespace {

using namespace sicnu::agent::tool_catalog;

std::string canonicalJson(const Json::Value &value)
{
    // Normalize through QJsonDocument so projection (jsoncpp) and MCP
    // (QVariant) sides are compared under one serializer: both sort object
    // keys and emit compact spacing.
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    const std::string raw = Json::writeString(builder, value);
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(raw));
    return std::string(doc.toJson(QJsonDocument::Compact).constData());
}

std::string canonicalJson(const QVariant &value)
{
    const QJsonDocument doc = QJsonDocument::fromVariant(value);
    return std::string(doc.toJson(QJsonDocument::Compact).constData());
}

QVariantMap probeArgsFor(const SurfaceTool &tool)
{
    // Sentinel arguments that fail validation BEFORE any side effect: every
    // required string becomes a nonexistent id, integers 0, objects empty.
    QVariantMap args;
    if (tool.source != SurfaceToolSource::MetaProtocol)
        return args;
    const Json::Value &required = tool.inputSchema["required"];
    const Json::Value &properties = tool.inputSchema["properties"];
    for (const auto &name : required) {
        const std::string key = name.asString();
        const std::string type = properties[key].get("type", "string").asString();
        if (type == "integer")
            args.insert(QString::fromStdString(key), 0);
        else if (type == "object")
            args.insert(QString::fromStdString(key), QVariantMap{});
        else if (type == "boolean")
            args.insert(QString::fromStdString(key), false);
        else
            args.insert(QString::fromStdString(key), QStringLiteral("__surface_probe__"));
    }
    return args;
}

/// Capturing server double (same pattern as test_mcp_server's TestMcpServer).
class SurfaceProbeServer : public McpServer
{
public:
    QVariant lastResponseId;
    QVariantMap lastResponseResult;
    QVariant lastErrorId;
    int lastErrorCode = 0;
    QString lastErrorMessage;

    void request(const QVariantMap &req) { handleRequest(req); }

    void sendResponse(const QVariant &id, const QVariantMap &result) override
    {
        lastResponseId = id;
        lastResponseResult = result;
        lastErrorCode = 0;
    }
    void sendError(const QVariant &id, int code, const QString &message) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendError(const QVariant &id, int code, const QString &message, const QVariantMap &) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendNotification(const QString &, const QVariantMap &) override {}

    /// tools/call probe: returns the error text when the call failed as a
    /// tool result (isError), or an empty string on success. rpcError is set
    /// when the server answered with a JSON-RPC error instead.
    QString callTool(const QString &name, const QVariantMap &args, int *rpcError = nullptr)
    {
        QVariantMap params;
        params[QStringLiteral("name")] = name;
        params[QStringLiteral("arguments")] = args;
        request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 1 },
            { QStringLiteral("method"), QStringLiteral("tools/call") },
            { QStringLiteral("params"), params } });
        if (rpcError)
            *rpcError = lastErrorCode;
        if (lastErrorCode != 0)
            return lastErrorMessage;
        if (lastResponseResult.value(QStringLiteral("isError")).toBool())
            return lastResponseResult.value(QStringLiteral("content")).toList()
                .value(0).toMap().value(QStringLiteral("text")).toString();
        return QString();
    }
};

SurfaceProbeServer &server()
{
    static SurfaceProbeServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_surface_parity";
        static char *argv[] = { arg0 };
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
        SurfaceProbeServer *s = new SurfaceProbeServer();
        s->request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 0 },
            { QStringLiteral("method"), QStringLiteral("initialize") },
            { QStringLiteral("params"), QVariantMap{ { QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05") } } } });
        return s;
    }();
    return *instance;
}

} // namespace

TEST_CASE("Surface projection invariants", "[surface_parity]")
{
    const std::vector<SurfaceTool> tools = collectSurfaceTools();

    REQUIRE(tools.size() > 20);

    // Deterministic: two calls produce identical names and schemas.
    const std::vector<SurfaceTool> again = collectSurfaceTools();
    REQUIRE(again.size() == tools.size());
    for (size_t i = 0; i < tools.size(); ++i) {
        REQUIRE(again[i].name == tools[i].name);
        REQUIRE(canonicalJson(again[i].inputSchema) == canonicalJson(tools[i].inputSchema));
    }

    std::vector<std::string> seen;
    for (const SurfaceTool &tool : tools) {
        INFO("tool: " << tool.name);
        REQUIRE_FALSE(tool.name.empty());
        REQUIRE_FALSE(tool.description.empty());
        REQUIRE(tool.family == surfaceToolFamily(tool.name));
        // Schema shape: object root with typed properties.
        REQUIRE(tool.inputSchema.isObject());
        REQUIRE(tool.inputSchema["type"].asString() == "object");
        REQUIRE(tool.inputSchema["properties"].isObject());
        // Unique names.
        REQUIRE(std::find(seen.begin(), seen.end(), tool.name) == seen.end());
        seen.push_back(tool.name);
    }

    // Every meta-protocol table row is projected verbatim (description too).
    for (const meta_protocol::MetaToolDef &def : meta_protocol::defs()) {
        auto found = findSurfaceTool(def.name);
        INFO("meta tool: " << def.name);
        REQUIRE(found.has_value());
        REQUIRE(found->source == SurfaceToolSource::MetaProtocol);
        REQUIRE(found->family == "meta");
        REQUIRE(found->description == def.description);
    }

    // Every data-platform table row is projected, with a schema re-derived
    // here from the table (independent of the projection's builder).
    for (const auto &def : sicnu::agent::dataPlatformToolDefs()) {
        auto found = findSurfaceTool(def.name);
        INFO("data platform tool: " << def.name);
        REQUIRE(found.has_value());
        REQUIRE(found->source == SurfaceToolSource::DataPlatform);
        REQUIRE(found->description == def.description);
        Json::Value expected = Json::Value(Json::objectValue);
        expected["type"] = "object";
        Json::Value props = Json::Value(Json::objectValue);
        Json::Value req = Json::Value(Json::arrayValue);
        for (const auto &input : def.inputs) {
            Json::Value prop = Json::Value(Json::objectValue);
            prop["type"] = input.type;
            prop["description"] = input.description;
            props[input.name] = prop;
            if (input.required)
                req.append(input.name);
        }
        expected["properties"] = props;
        if (!req.empty())
            expected["required"] = req;
        REQUIRE(canonicalJson(found->inputSchema) == canonicalJson(expected));
    }

    // Families: sorted, unique, and cover the three sources' namespaces.
    const std::vector<std::string> families = surfaceFamilies();
    REQUIRE(std::is_sorted(families.begin(), families.end()));
    REQUIRE(std::adjacent_find(families.begin(), families.end()) == families.end());
    REQUIRE(std::find(families.begin(), families.end(), "meta") != families.end());
    REQUIRE(std::find(families.begin(), families.end(), "dataset") != families.end());
    REQUIRE(std::find(families.begin(), families.end(), "rs") != families.end());

    // findSurfaceTool round-trip for a catalog-sourced tool (rs: algorithms
    // are always registered through the default provider).
    auto metaTool = findSurfaceTool("list_algorithms");
    REQUIRE(metaTool.has_value());
    REQUIRE(metaTool->source == SurfaceToolSource::MetaProtocol);
    REQUIRE_FALSE(findSurfaceTool("__definitely_not_a_tool__").has_value());
}

TEST_CASE("MCP allow-prefix policy (moved verbatim)", "[surface_parity]")
{
    REQUIRE(surfaceIdAllowed(QStringLiteral("rs:spectral_index")));
    REQUIRE(surfaceIdAllowed(QStringLiteral("processing:rs:spectral_index"))); // stripped
    REQUIRE(surfaceIdAllowed(QStringLiteral("spatial:inspect_layer")));
    REQUIRE(surfaceIdAllowed(QStringLiteral("harness:taxonomy")));
    REQUIRE(surfaceIdAllowed(QStringLiteral("io:clip")));

    bool isCustom = false;
    REQUIRE_FALSE(surfaceIdAllowed(QStringLiteral("custom_tools:my_tool"), &isCustom));
    REQUIRE(isCustom);
    REQUIRE_FALSE(surfaceIdAllowed(QStringLiteral("python:run")));
    REQUIRE_FALSE(surfaceIdAllowed(QStringLiteral(" totally_not:a_tool")));
    // Meta protocol tools are unprefixed and NOT catalog ids: the allow-list
    // never admits them (they route through their own dispatch branch).
    REQUIRE_FALSE(surfaceIdAllowed(QStringLiteral("list_algorithms")));
}

TEST_CASE("MCP tools/list equals the union projection", "[surface_parity]")
{
    SurfaceProbeServer &s = server();

    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 2 },
        { QStringLiteral("method"), QStringLiteral("tools/list") },
        { QStringLiteral("params"), QVariantMap{ { QStringLiteral("includeSchemas"), true } } } });
    REQUIRE(s.lastErrorCode == 0);
    const QVariantList listed = s.lastResponseResult.value(QStringLiteral("tools")).toList();
    REQUIRE_FALSE(listed.isEmpty());

    const std::vector<SurfaceTool> projected = collectSurfaceTools();
    REQUIRE(listed.size() == static_cast<int>(projected.size()));

    for (int i = 0; i < listed.size(); ++i) {
        const QVariantMap entry = listed.at(i).toMap();
        const SurfaceTool &tool = projected[static_cast<size_t>(i)];
        INFO("entry " << i << ": " << entry.value("name").toString().toStdString()
                      << " vs " << tool.name);
        REQUIRE(entry.value(QStringLiteral("name")).toString().toStdString() == tool.name);
        REQUIRE(entry.value(QStringLiteral("description")).toString().toStdString() == tool.description);
        // Schema equality under the shared canonical serializer.
        REQUIRE(canonicalJson(entry.value(QStringLiteral("inputSchema"))) == canonicalJson(tool.inputSchema));
    }
}

TEST_CASE("Every projected meta and data-platform tool is dispatchable", "[surface_parity]")
{
    SurfaceProbeServer &s = server();

    // Baseline: a name that NO dispatch branch knows answers -32602.
    int rpcCode = 0;
    const QString unknown = s.callTool(QStringLiteral("__no_such_tool__"), {}, &rpcCode);
    REQUIRE(rpcCode == -32602);
    REQUIRE(unknown.contains(QStringLiteral("Unknown tool")));

    for (const SurfaceTool &tool : collectSurfaceTools()) {
        if (tool.source == SurfaceToolSource::Catalog)
            continue; // catalog dispatch is structural (prefix/allow-list); no side-effect probes
        INFO("dispatch probe: " << tool.name);
        rpcCode = 0;
        const QString error = s.callTool(QString::fromStdString(tool.name), probeArgsFor(tool), &rpcCode);
        // A dispatched tool answers with a result (possibly isError) — never
        // "Unknown tool". A meta row without a tools/call branch, or a
        // data-platform def without a handler, fails here.
        REQUIRE_FALSE(rpcCode == -32602);
        if (!error.isEmpty())
            REQUIRE_FALSE(error.contains(QStringLiteral("unknown data-platform tool")));
    }
}

TEST_CASE("Dispatch-visible spatial families are listing-visible", "[surface_parity]")
{
    // The tools/call routing (mcp_server.cpp handleRequest) dispatches these
    // families to SpatialToolRegistry BEFORE the allow-list. Every one of
    // them must ALSO pass surfaceIdAllowed, or the family would be callable
    // but invisible to tools/list / the projection — the inverse drift.
    static const char *kSpatialDispatchFamilies[] = {
        "spatial", "layout", "cartography", "workbench", "symbology", "workflow",
        "workspace", "project", "asset", "collection", "lineage", "result",
        "harness", "run", "temporal",
    };
    for (const char *family : kSpatialDispatchFamilies) {
        INFO("family: " << family);
        REQUIRE(surfaceIdAllowed(QStringLiteral("%1:__probe__").arg(family)));
    }
}
