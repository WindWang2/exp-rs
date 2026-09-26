// tests/test_surface_snapshot.cpp — Track 9 parity lock (WP-A/WP-E).
//
// The snapshot tests/surface_diff_snapshot.json is the single shared
// surface-contract file consumed by BOTH ends:
//   - here (C++): SpatialToolRegistry truth vs union projection vs the MCP
//     tools/list wire render vs the snapshot;
//   - pi/test/surface_snapshot.test.mjs (pi): name mapping, category
//     curation, collision-freedom, schema round-trip.
// Any end that changes without the other two (the #1305-family drift)
// turns one of these red. The snapshot is HERMETIC (in-process projection,
// no QGIS/GUI providers), so the comparison is deterministic across
// machines; the real-binary leg keeps its own superset gate in
// test_surface_parity's CLI test.
//
// Oracles: the registry/projection/wire are three independent renderings;
// the snapshot is compared against each of them field-by-field under one
// canonical serializer (JSON semantics, key order irrelevant). No
// tolerance: a diff is a diff.
//
// Regenerate after a legitimate surface change with:
//   ninja -C build-r4 surface_snapshot_regen
// and commit the refreshed snapshot with the surface change.

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <json/json.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "agent/mcp_server.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/tool_catalog/surface_registry.h"

#ifndef SICNU_SOURCE_DIR
#error "SICNU_SOURCE_DIR must point at the repo source tree"
#endif

namespace {

using sicnu::agent::tool_catalog::SurfaceTool;
using sicnu::agent::tool_catalog::collectSurfaceTools;
using sicnu::agent::tool_catalog::surfaceToolFamily;

constexpr const char *kRegenHint =
    "Regenerate with: ninja -C build-r4 surface_snapshot_regen "
    "(then commit tests/surface_diff_snapshot.json with the surface change)";

std::string snapshotPath()
{
    return std::string(SICNU_SOURCE_DIR) + "/tests/surface_diff_snapshot.json";
}

// Normalize through QJsonDocument so jsoncpp-sourced (snapshot, projection)
// and QVariant-sourced (wire) values are compared under one serializer:
// sorted keys, compact spacing.
std::string canonicalJson(const Json::Value &value)
{
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

/// Capturing server double (same pattern as test_surface_parity's harness):
/// the only leg that must go through the JSON-RPC machinery is tools/list,
/// so the wire rendering is compared against the snapshot, not hand-parsed.
class SnapshotProbeServer : public McpServer
{
public:
    QVariantMap lastResponseResult;
    int lastErrorCode = 0;

    void request(const QVariantMap &req) { handleRequest(req); }
    void sendResponse(const QVariant &, const QVariantMap &result) override
    {
        lastResponseResult = result;
        lastErrorCode = 0;
    }
    void sendError(const QVariant &, int code, const QString &) override { lastErrorCode = code; }
    void sendError(const QVariant &, int code, const QString &, const QVariantMap &) override
    {
        lastErrorCode = code;
    }
    void sendNotification(const QString &, const QVariantMap &) override {}
};

SnapshotProbeServer &server()
{
    static SnapshotProbeServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_surface_snapshot";
        static char *argv[] = { arg0 };
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
        SnapshotProbeServer *s = new SnapshotProbeServer();
        s->request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 0 },
            { QStringLiteral("method"), QStringLiteral("initialize") },
            { QStringLiteral("params"), QVariantMap{} } });
        return s;
    }();
    return *instance;
}

struct SnapshotEntry
{
    std::string name;
    std::string family;
    std::string description;
    Json::Value inputSchema;
};

/// Loads and self-validates the snapshot. Assertions here cover the schema
/// version contract and the structural invariants BEFORE any cross-check,
/// so a corrupt snapshot fails with the regen hint instead of a confusing
/// downstream diff.
std::vector<SnapshotEntry> loadSnapshot()
{
    QFile file(QString::fromStdString(snapshotPath()));
    const bool opened = file.open(QIODevice::ReadOnly);
    if (!opened)
        INFO("snapshot missing: " << kRegenHint);
    REQUIRE(opened);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        INFO("snapshot is not a JSON object: " << kRegenHint);
    REQUIRE(doc.isObject());
    const QByteArray compact = doc.toJson(QJsonDocument::Compact);
    const Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errors;
    const bool parsed = reader->parse(compact.constData(), compact.constData() + compact.size(),
                                      &root, &errors);
    if (!parsed)
        INFO("snapshot does not round-trip through jsoncpp: " << errors << ". " << kRegenHint);
    REQUIRE(parsed);
    const int version = root["schemaVersion"].asInt();
    if (version != 1)
        INFO("snapshot schemaVersion must be 1 (got " << version << "): " << kRegenHint);
    REQUIRE(version == 1);
    const Json::Value &tools = root["tools"];
    const bool wellSized = tools.isArray() && tools.size() > 20;
    if (!wellSized)
        INFO("snapshot tools array missing or suspiciously small: " << kRegenHint);
    REQUIRE(wellSized);

    std::vector<SnapshotEntry> entries;
    std::set<std::string> seen;
    for (const auto &entry : tools)
    {
        SnapshotEntry e;
        e.name = entry["name"].asString();
        e.family = entry["family"].asString();
        e.description = entry["description"].asString();
        e.inputSchema = entry["inputSchema"];
        INFO("snapshot entry: " << e.name);
        REQUIRE_FALSE(e.name.empty());
        // A meta-protocol tool MAY carry a prefixed name (the one case:
        // scientific:agent_session) while the projection still reports its
        // family as "meta"; catalog/data-platform families always equal the
        // text before the ':'.
        REQUIRE((e.family == surfaceToolFamily(e.name) || e.family == "meta"));
        REQUIRE_FALSE(e.description.empty());
        REQUIRE(e.inputSchema.isObject());
        REQUIRE(seen.insert(e.name).second); // unique
        entries.push_back(std::move(e));
    }
    return entries;
}

} // namespace

TEST_CASE("Surface snapshot is self-validating", "[surface_snapshot]")
{
    REQUIRE(loadSnapshot().size() > 20);
}

TEST_CASE("Snapshot equals the live union projection", "[surface_snapshot]")
{
    // End 1 (C++ truth): the projection is what tools/list renders; the
    // snapshot must pin it name-for-name, field-for-field, in wire order.
    const std::vector<SnapshotEntry> snap = loadSnapshot();
    const std::vector<SurfaceTool> projected = collectSurfaceTools();
    if (snap.size() != projected.size())
        INFO("snapshot has " << snap.size() << " tools, projection has "
                             << projected.size() << ". " << kRegenHint);
    REQUIRE(snap.size() == projected.size());
    for (size_t i = 0; i < snap.size(); ++i)
    {
        INFO("tool " << i << ": snapshot=" << snap[i].name << " projection=" << projected[i].name);
        REQUIRE(snap[i].name == projected[i].name);
        REQUIRE(snap[i].family == projected[i].family);
        REQUIRE(snap[i].description == projected[i].description);
        REQUIRE(canonicalJson(snap[i].inputSchema) == canonicalJson(projected[i].inputSchema));
    }
}

TEST_CASE("MCP tools/list wire output matches the snapshot", "[surface_snapshot]")
{
    // End 2 (MCP translation): the JSON-RPC answer, not the projection
    // struct, is what pi consumes. Same iteration order, same fields.
    SnapshotProbeServer &s = server();
    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 2 },
        { QStringLiteral("method"), QStringLiteral("tools/list") },
        { QStringLiteral("params"), QVariantMap{ { QStringLiteral("includeSchemas"), true } } } });
    REQUIRE(s.lastErrorCode == 0);
    const QVariantList listed = s.lastResponseResult.value(QStringLiteral("tools")).toList();

    const std::vector<SnapshotEntry> snap = loadSnapshot();
    REQUIRE(listed.size() == static_cast<int>(snap.size()));
    for (int i = 0; i < listed.size(); ++i)
    {
        const QVariantMap entry = listed.at(i).toMap();
        const SnapshotEntry &expected = snap[static_cast<size_t>(i)];
        INFO("wire entry " << i << ": " << entry.value("name").toString().toStdString());
        REQUIRE(entry.value(QStringLiteral("name")).toString().toStdString() == expected.name);
        REQUIRE(entry.value(QStringLiteral("description")).toString().toStdString()
                == expected.description);
        REQUIRE(canonicalJson(entry.value(QStringLiteral("inputSchema")))
                == canonicalJson(expected.inputSchema));
    }
}

TEST_CASE("Every SpatialToolRegistry member is pinned by the snapshot", "[surface_snapshot]")
{
    // End 1b (dispatch truth): the registry is what tools/call routes to.
    // A registry member absent from the snapshot is a ghost re-introduced
    // by a rename or a skipped regen.
    auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
    registry.registerBuiltinTools();

    // By value: pointers into the loadSnapshot() temporary would dangle the
    // moment the range-for above ends (the exact bug that segfaulted here).
    const std::vector<SnapshotEntry> snapEntries = loadSnapshot();
    std::map<std::string, SnapshotEntry> snap;
    for (const SnapshotEntry &entry : snapEntries)
        snap.emplace(entry.name, entry);

    REQUIRE(registry.size() > 50); // sweep must actually see the builtin set
    for (const auto &tool : registry.tools())
    {
        const std::string name = tool->name();
        INFO("registry tool: " << name);
        const auto it = snap.find(name);
        if (it == snap.end())
            INFO("registry tool '" << name << "' is not pinned by the snapshot. "
                                   << kRegenHint);
        REQUIRE(it != snap.end());
        REQUIRE(canonicalJson(it->second.inputSchema) == canonicalJson(tool->inputSchema()));
    }
}

TEST_CASE("Snapshot covers every allow-listed family that has tools", "[surface_snapshot]")
{
    // A partial regen (hand-trimmed snapshot) could drop whole families
    // without any single-tool diff noticing. Families come from the live
    // projection — the same oracle the pi category gate reads.
    std::map<std::string, int> snapByFamily;
    for (const SnapshotEntry &entry : loadSnapshot())
        ++snapByFamily[entry.family];

    for (const std::string &family : sicnu::agent::tool_catalog::surfaceFamilies())
    {
        INFO("family: " << family);
        if (snapByFamily.count(family) == 0)
            INFO("family '" << family << "' vanished from the snapshot. " << kRegenHint);
        REQUIRE(snapByFamily.count(family) > 0);
    }
}

TEST_CASE("Snapshot inputSchemas survive a serialization round-trip", "[surface_snapshot]")
{
    // Schema 往返 (C++ leg): every pinned schema must survive
    // serialize -> parse -> serialize under the cross-serializer
    // canonicalizer (jsoncpp <-> QJsonDocument). A schema that wobbles on
    // the wire (NaN, precision, duplicate keys) fails here before any
    // client sees it. Semantic floor: required ⊆ properties.
    for (const SnapshotEntry &entry : loadSnapshot())
    {
        INFO("schema round-trip: " << entry.name);
        const std::string once = canonicalJson(entry.inputSchema);
        const QJsonDocument reparsed = QJsonDocument::fromJson(QByteArray::fromStdString(once));
        REQUIRE(reparsed.isObject());
        REQUIRE(canonicalJson(reparsed.toVariant()) == once);

        if (entry.inputSchema.isMember("required"))
        {
            const Json::Value &properties = entry.inputSchema["properties"];
            for (const auto &required : entry.inputSchema["required"])
            {
                INFO("required key: " << required.asString());
                REQUIRE(properties.isObject());
                REQUIRE(properties.isMember(required.asString()));
            }
        }
    }
}
