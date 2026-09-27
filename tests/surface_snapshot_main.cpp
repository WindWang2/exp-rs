// tests/surface_snapshot_main.cpp — Track 9 parity lock: snapshot GENERATOR.
//
// Writes tests/surface_diff_snapshot.json from the in-process union
// projection (the same hermetic collectSurfaceTools() source the MCP
// tools/list renderer consumes at mcp_server.cpp:1636's allow-prefix
// filter). The snapshot is the single shared surface contract consumed by
// BOTH ends: tests/test_surface_snapshot.cpp (C++: registry vs projection vs
// wire) and pi/test/surface_snapshot.test.mjs (pi: mapping, categories,
// collision, schema round-trip).
//
// Why a standalone binary instead of a Catch2 mode: regeneration must be an
// explicit, deliberate command — never a side effect of running tests (a
// test that rewrites its own oracle defeats the drift gate).
//
// Regenerate after a LEGITIMATE surface change (new tool, schema fix) with:
//   ninja -C build-r4 surface_snapshot_regen
// then commit the refreshed tests/surface_diff_snapshot.json in the SAME
// commit as the surface change. A snapshot edit without the matching
// surface change (or the reverse) turns test_surface_snapshot red.
#include <QCoreApplication>
#include <QFile>
#include <json/json.h>

#include <iostream>

#include "agent/spatial_tools/spatial_tool.h"
#include "agent/tool_catalog/surface_registry.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int outIndex = static_cast<int>(args.indexOf(QStringLiteral("--output")));
    if (outIndex < 0 || outIndex + 1 >= args.size())
    {
        std::cerr << "usage: surface_snapshot_gen --output <path-to-surface_diff_snapshot.json>\n";
        return 2;
    }
    const QString outputPath = args.at(outIndex + 1);

    // Warm the singletons exactly as the comparator test process does
    // (registerBuiltinTools is once-per-process idempotent): the projection
    // must not depend on whether some other code happened to touch the
    // registry first.
    sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerBuiltinTools();

    const std::vector<sicnu::agent::tool_catalog::SurfaceTool> tools =
        sicnu::agent::tool_catalog::collectSurfaceTools();
    if (tools.empty())
    {
        std::cerr << "surface_snapshot_gen: empty projection - refusing to write a dead snapshot\n";
        return 1;
    }

    Json::Value root(Json::objectValue);
    root["schemaVersion"] = 1;
    root["generator"] = "ninja -C build-r4 surface_snapshot_regen  (tests/surface_snapshot_main)";
    Json::Value toolArray(Json::arrayValue);
    for (const auto &tool : tools)
    {
        Json::Value entry(Json::objectValue);
        entry["name"] = tool.name;
        entry["family"] = tool.family;
        entry["description"] = tool.description;
        entry["inputSchema"] = tool.inputSchema;
        toolArray.append(entry);
    }
    root["tools"] = toolArray;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    const std::string text = Json::writeString(builder, root) + "\n";

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        std::cerr << "surface_snapshot_gen: cannot open " << outputPath.toStdString() << "\n";
        return 1;
    }
    out.write(QByteArray::fromStdString(text));
    out.close();
    std::cout << "surface_snapshot_gen: wrote " << tools.size() << " tools to "
              << outputPath.toStdString() << "\n";
    return 0;
}
