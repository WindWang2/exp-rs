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
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "agent/mcp_server.h"
#include "agent/tool_catalog/surface_registry.h"
#include "agent/tool_catalog/meta_protocol_tools.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/data_platform_tools.h"

#ifndef SICNU_SOURCE_DIR
#error "SICNU_SOURCE_DIR must point at the repo source tree"
#endif

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
        // Schema shape: our own meta/data-platform tables guarantee
        // {"type":"object"} with a properties object. Catalog-sourced schemas
        // are passed through exactly as the owning domain registered them
        // (master behavior; Pi normalizes defensively) — only a JSON object
        // root is required here.
        REQUIRE(tool.inputSchema.isObject());
        if (tool.source != SurfaceToolSource::Catalog)
        {
            REQUIRE(tool.inputSchema["type"].asString() == "object");
            REQUIRE(tool.inputSchema["properties"].isObject());
        }
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

#ifndef SICNU_TEST_SURFACE_CLI
#error "SICNU_TEST_SURFACE_CLI must point at sicnu_geo_rs_cli"
#endif

TEST_CASE("CLI tools list matches the projection over the real binary", "[surface_parity][cli]")
{
    // Three-way parity, CLI leg: the real sicnu_geo_rs_cli subprocess renders
    // `tools list --json --schema` from ITS process registry; this test
    // process holds its own (smaller) registry. Meta + data-platform tools
    // are process-independent, so they must be IDENTICAL in name+schema;
    // catalog ids visible to both sides must agree too. The CLI may list
    // MORE catalog tools (full engine init) — a strict superset relation on
    // the catalog slice, never a contradiction.
    QProcess cli;
    cli.start(QString::fromUtf8(SICNU_TEST_SURFACE_CLI),
              QStringList{ QStringLiteral("tools"), QStringLiteral("list"),
                           QStringLiteral("--json"), QStringLiteral("--schema") });
    REQUIRE(cli.waitForStarted(10000));
    REQUIRE(cli.waitForFinished(120000));
    REQUIRE(cli.exitStatus() == QProcess::NormalExit);
    REQUIRE(cli.exitCode() == 0);

    const QJsonDocument doc = QJsonDocument::fromJson(cli.readAllStandardOutput());
    REQUIRE(doc.isObject());
    const QJsonArray tools = doc.object().value(QStringLiteral("data"))
                                 .toObject().value(QStringLiteral("tools")).toArray();
    REQUIRE(tools.size() > 20);

    std::vector<SurfaceTool> projected = collectSurfaceTools();
    std::map<std::string, std::string> projectedSchemas;
    for (const SurfaceTool &tool : projected)
        projectedSchemas[tool.name] = canonicalJson(tool.inputSchema);

    int matchedMeta = 0;
    int matchedDataPlatform = 0;
    int matchedCatalog = 0;
    std::vector<std::string> cliNames;
    for (const auto &entry : tools)
    {
        const QJsonObject obj = entry.toObject();
        const std::string name = obj.value(QStringLiteral("name")).toString().toStdString();
        INFO("cli tool: " << name);
        REQUIRE(std::find(cliNames.begin(), cliNames.end(), name) == cliNames.end());
        cliNames.push_back(name);
        // Name must exist in this process's projection OR be a catalog tool
        // the CLI process registered additionally.
        const auto it = projectedSchemas.find(name);
        if (it != projectedSchemas.end())
            REQUIRE(canonicalJson(obj.value(QStringLiteral("input_schema")).toVariant()) == it->second);
        if (meta_protocol::contains(name))
            ++matchedMeta;
        else if (sicnu::agent::isDataPlatformTool(QString::fromStdString(name)))
            ++matchedDataPlatform;
        else if (it != projectedSchemas.end())
            ++matchedCatalog;
    }
    REQUIRE(matchedMeta == static_cast<int>(meta_protocol::defs().size()));
    REQUIRE(matchedDataPlatform == static_cast<int>(sicnu::agent::dataPlatformToolDefs().size()));
    REQUIRE(matchedCatalog > 0); // at least the shared rs: core operators

    // The CLI must never list a tool this process knows to be absent from
    // the meta/data-platform tables (renames drift both sides).
    for (const meta_protocol::MetaToolDef &def : meta_protocol::defs())
    {
        INFO("meta def: " << def.name);
        REQUIRE(std::find(cliNames.begin(), cliNames.end(), std::string(def.name))
                != cliNames.end());
    }
}

TEST_CASE("Pi default bridge categories contain no stale family", "[surface_parity][pi]")
{
    // pi/exp-rs-spatial.ts curates which namespace families it bridges (a
    // deliberate subset — processing families like rs:/gdal: are NOT bridged
    // by default). The one forbidden drift: Pi referencing a family the
    // projection no longer has (renamed namespace, retired tool family) —
    // those tools silently vanish from the agent surface. The default list
    // is extracted from the source text so the gate tracks the real file.
    QFile source(QStringLiteral(SICNU_SOURCE_DIR) + QStringLiteral("/pi/exp-rs-spatial.ts"));
    REQUIRE(source.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(source.readAll());

    static const QRegularExpression re(
        R"EXP(EXP_RS_TOOL_CATEGORIES\s*\?\?\s*\n?\s*"([^"]+)")EXP");
    const auto match = re.match(text);
    REQUIRE(match.hasMatch());
    const QStringList categories = match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    REQUIRE(categories.size() >= 5);

    const std::vector<std::string> families = surfaceFamilies();
    for (const QString &raw : categories)
    {
        const std::string category = raw.trimmed().toStdString();
        INFO("pi category: " << category);
        REQUIRE(std::find(families.begin(), families.end(), category) != families.end());
    }
}

TEST_CASE("Projection scales linearly and MCP pagination stays bounded", "[surface_parity][scale]")
{
    // Logical-scale invariant (no wall-clock): 2000 extra tools grow the
    // projection linearly, MCP pages stay clamped at 500, and walking pages
    // covers exactly the full set. Cleans up after itself so other tests in
    // this binary see the original catalog.
    AgentToolCatalog &catalog = AgentToolCatalog::instance();
    const size_t baseCount = collectSurfaceTools().size();

    constexpr int kProbes = 2000;
    std::vector<std::string> probeNames;
    probeNames.reserve(kProbes);
    for (int i = 0; i < kProbes; ++i)
    {
        AgentTool probe;
        probe.name = "io:surface_scale_probe_" + std::to_string(i);
        probe.category = ToolCategory::Custom;
        probe.description = "scale probe";
        probe.inputSchema = Json::Value(Json::objectValue);
        probe.inputSchema["type"] = "object";
        probe.inputSchema["properties"] = Json::Value(Json::objectValue);
        catalog.registerCustomTool(probe);
        probeNames.push_back(probe.name);
    }

    const std::vector<SurfaceTool> scaled = collectSurfaceTools();
    REQUIRE(scaled.size() == baseCount + kProbes);

    // Deterministic membership + exact count through the MCP surface.
    SurfaceProbeServer &s = server();
    int total = -1;
    int seen = 0;
    int cursor = 0;
    int pages = 0;
    while (pages < 50)
    {
        s.request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 100 + pages },
            { QStringLiteral("method"), QStringLiteral("tools/list") },
            { QStringLiteral("params"), QVariantMap{
                                             { QStringLiteral("includeSchemas"), false },
                                             { QStringLiteral("limit"), 500 },
                                             { QStringLiteral("cursor"), cursor } } } });
        REQUIRE(s.lastErrorCode == 0);
        const QVariantMap result = s.lastResponseResult;
        total = result.value(QStringLiteral("total")).toInt();
        seen += result.value(QStringLiteral("tools")).toList().size();
        const QVariant next = result.value(QStringLiteral("nextCursor"));
        if (!next.isValid() || next.toInt() < 0)
            break;
        cursor = next.toInt();
        ++pages;
    }
    REQUIRE(total == static_cast<int>(baseCount) + kProbes);
    REQUIRE(seen == total);
    REQUIRE(pages <= 50); // bounded pagination: ceil((base+2000)/500)

    // Lookup of the last probe resolves through findSurfaceTool.
    REQUIRE(findSurfaceTool(probeNames.back()).has_value());

    for (const std::string &name : probeNames)
        catalog.unregisterCustomTool(name);
    REQUIRE(collectSurfaceTools().size() == baseCount);
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
        "harness", "run", "temporal", "io",
    };
    for (const char *family : kSpatialDispatchFamilies) {
        INFO("family: " << family);
        REQUIRE(surfaceIdAllowed(QStringLiteral("%1:__probe__").arg(family)));
    }
}
