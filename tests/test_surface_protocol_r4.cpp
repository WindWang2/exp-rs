// tests/test_surface_protocol_r4.cpp — Track 9 (mcp-surface-r4) WP-C:
// protocol robustness against malformed and adversarial frames.
//
// New file, deliberately: test_surface_protocol.cpp is also being modified
// by in-flight #1334 (default-deny sandbox); Track 9's cases live here so
// the two changes never touch the same lines. Oracles are independent of
// the implementation under test: JSON-RPC 2.0 error codes (-32700/-32600/
// -32601/-32602), the MCP -32002 pre-initialize gate (#701.7), the #644
// explicit-null/absent-id response contract, the #634
// notifications/cancelled -> TaskCenter mapping, the #620 tool-faults-are-
// results rule, and the surfaceAllowedPrefixes() allow-list.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QVariantMap>

#include <json/json.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "agent/mcp_server.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/tool_catalog/meta_protocol_tools.h"
#include "agent/tool_catalog/surface_registry.h"
#include "agent/data_platform_tools.h"

namespace {

using sicnu::agent::tool_catalog::surfaceAllowedPrefixes;

/// Counting server double: every send* is recorded, and the #644 response
/// guard (an absent id suppresses regular responses; an explicit null id IS
/// answerable; parse/invalid-request errors answer with id:null) is
/// mirrored from McpServer::sendResponse/sendError so the counts reflect
/// what the WIRE would see rather than what the dispatcher attempted.
class CountingServer : public McpServer
{
public:
    struct Sent
    {
        QVariant id;
        QVariantMap result;
        int errorCode = 0;
        QString errorMessage;
    };
    QList<Sent> responses;
    QList<Sent> errors;
    QVariantList notifications;

    void request(const QVariantMap &req) { handleRequest(req); }

    void requestLine(const QString &line)
    {
        QMetaObject::invokeMethod(this, "onLineRead", Q_ARG(QString, line));
    }

    void sendResponse(const QVariant &id, const QVariantMap &result) override
    {
        // McpServer::sendResponse drops an ABSENT id (notification); an
        // explicit null id IS answered. Mirror that guard (#644).
        if (!id.isValid())
            return;
        Sent s;
        s.id = id;
        s.result = result;
        responses.append(s);
    }
    void sendError(const QVariant &id, int code, const QString &message) override
    {
        sendError(id, code, message, QVariantMap());
    }
    void sendError(const QVariant &id, int code, const QString &message,
                   const QVariantMap &) override
    {
        const bool explicitNullId = id.metaType().id() == QMetaType::Nullptr;
        const bool absentId = !id.isValid() || (id.isNull() && !explicitNullId);
        // Parse/Invalid-Request faults are answerable even with an absent id
        // (they carry id:null on the wire) - the rest are suppressed (#644).
        if (absentId && code != -32700 && code != -32600)
            return;
        Sent s;
        s.id = id;
        s.errorCode = code;
        s.errorMessage = message;
        errors.append(s);
    }
    void sendNotification(const QString &, const QVariantMap &params) override
    {
        notifications.append(params);
    }

    /// tools/call with full caller control over params. An INVALID id
    /// variant means NOTIFICATION: the "id" key is OMITTED (inserting an
    /// invalid QVariant would still create the key, and handleRequest treats
    /// a present-but-absent-id as the #644 explicit-null REQUEST case).
    void callRaw(const QVariant &id, const QVariantMap &params)
    {
        QVariantMap frame{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("method"), QStringLiteral("tools/call") },
            { QStringLiteral("params"), params } };
        if (id.isValid())
            frame.insert(QStringLiteral("id"), id);
        request(frame);
    }

    void initialize(int id)
    {
        request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), id },
            { QStringLiteral("method"), QStringLiteral("initialize") },
            { QStringLiteral("params"), QVariantMap{} } });
    }
};

/// The tools/call result payload rides content[0].text as compact JSON.
QJsonObject payloadOf(const QVariantMap &callResult)
{
    return QJsonDocument::fromJson(
               callResult.value(QStringLiteral("content")).toList().value(0).toMap()
                   .value(QStringLiteral("text")).toString().toUtf8())
        .object();
}

std::string resultTextOf(const CountingServer::Sent &sent)
{
    return sent.result.value(QStringLiteral("content")).toList().value(0).toMap()
        .value(QStringLiteral("text")).toString().toStdString();
}

} // namespace

TEST_CASE("parse faults answer -32700 instead of crashing or silence", "[surface][protocol][r4]")
{
    CountingServer s;
    // Non-JSON garbage on the stdio line.
    s.requestLine(QStringLiteral("this is absolutely not json {{{"));
    REQUIRE(s.errors.size() == 1);
    REQUIRE(s.errors.first().errorCode == -32700);
    REQUIRE(s.responses.isEmpty());
    REQUIRE(s.errors.first().errorMessage.contains(QStringLiteral("Parse error")));

    // A JSON scalar frame: Qt's parser rejects top-level scalars outright,
    // so the OBSERVED wire contract is -32700 (the JSON-RPC -32600 shape is
    // unreachable for scalars behind QJsonDocument - pinned as reality).
    s.requestLine(QStringLiteral("42"));
    REQUIRE(s.errors.size() == 2);
    REQUIRE(s.errors.last().errorCode == -32700);

    // A JSON array frame (batch) parses and is answered -32600 Invalid
    // Request: not a request object.
    s.requestLine(QStringLiteral("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]"));
    REQUIRE(s.errors.size() == 3);
    REQUIRE(s.errors.last().errorCode == -32600);

    // The StdinReader overflow sentinel is a parse fault, not silence.
    s.requestLine(QStringLiteral("__MCP_LINE_TOO_LONG__"));
    REQUIRE(s.errors.size() == 4);
    REQUIRE(s.errors.last().errorCode == -32700);
    REQUIRE(s.errors.last().errorMessage.contains(QStringLiteral("line too long")));
}

TEST_CASE("a request frame without a method member is Invalid Request (-32600), not Method not found", "[surface][protocol][r4]")
{
    // JSON-RPC 2.0: "method" is REQUIRED; a frame lacking it is not a valid
    // request and must answer -32600, not -32601 (which would imply an
    // empty method name exists to be not found). The -32002 pre-init gate
    // legitimately runs first, so the server is initialized here: this pins
    // the post-handshake contract.
    CountingServer s;
    s.initialize(7);
    s.responses.clear();
    s.errors.clear();
    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 7 } });
    REQUIRE(s.errors.size() == 1);
    REQUIRE(s.errors.first().errorCode == -32600);
    REQUIRE(s.errors.first().errorMessage.contains(QStringLiteral("method")));
    REQUIRE(s.responses.isEmpty());
}

TEST_CASE("pre-initialize gate: only initialize/ping pass, everything else is -32002", "[surface][protocol][r4]")
{
    // A FRESH (uninitialized) server: the shared fixture in the other file
    // already ran initialize, so this must stay independent of it.
    CountingServer fresh;
    fresh.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 1 },
        { QStringLiteral("method"), QStringLiteral("tools/list") },
        { QStringLiteral("params"), QVariantMap{} } });
    REQUIRE(fresh.errors.size() == 1);
    REQUIRE(fresh.errors.first().errorCode == -32002);

    // ping is explicitly exempt from the gate.
    fresh.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 2 },
        { QStringLiteral("method"), QStringLiteral("ping") },
        { QStringLiteral("params"), QVariantMap{} } });
    REQUIRE(fresh.responses.size() == 1);

    // Notifications are exempt too (a client may have died mid-handshake).
    fresh.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("notifications/initialized") },
        { QStringLiteral("params"), QVariantMap{} } });
    REQUIRE(fresh.errors.size() == 1);
    REQUIRE(fresh.responses.size() == 1);
}

TEST_CASE("explicit null id requests are answered with id:null (#644)", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(1); // post-handshake contract; the gate otherwise leads
    s.responses.clear();
    s.errors.clear();
    s.requestLine(QStringLiteral("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}"));
    REQUIRE(s.responses.size() == 1);
    REQUIRE(s.responses.first().id.metaType().id() == QMetaType::Nullptr);
    // The error path answers an explicit null id too.
    s.requestLine(QStringLiteral("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"no_such_method\"}"));
    REQUIRE(s.errors.size() == 1);
    REQUIRE(s.errors.first().errorCode == -32601);
    REQUIRE(s.errors.first().id.metaType().id() == QMetaType::Nullptr);
}

TEST_CASE("notifications are never answered, not even for unknown tools/methods (#644/#620)", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(1);
    REQUIRE(s.responses.size() == 1);

    // Notification-form unknown method: silently dropped.
    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("no_such_method") },
        { QStringLiteral("params"), QVariantMap{} } });
    // Notification-form unknown tool: silently dropped.
    s.callRaw(QVariant(), QVariantMap{
                              { QStringLiteral("name"), QStringLiteral("__nope__") } });
    REQUIRE(s.responses.size() == 1); // only the initialize response
    REQUIRE(s.errors.isEmpty());
}

TEST_CASE("tools/call rejects non-object arguments with -32602", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(11);
    s.callRaw(QVariant(11), QVariantMap{
                                { QStringLiteral("name"), QStringLiteral("ping") },
                                { QStringLiteral("arguments"), QStringLiteral("not-an-object") } });
    REQUIRE(s.errors.size() == 1);
    REQUIRE(s.errors.first().errorCode == -32602);
    REQUIRE(s.errors.first().errorMessage.contains(QStringLiteral("arguments")));
    REQUIRE(s.errors.first().errorMessage.contains(QStringLiteral("object")));
}

TEST_CASE("unknown tool inside a valid tools/call names the tool and answers -32602", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(12);
    s.responses.clear();
    s.callRaw(QVariant(12), QVariantMap{
                                { QStringLiteral("name"), QStringLiteral("python:never_allowed") } });
    REQUIRE(s.responses.isEmpty());
    REQUIRE(s.errors.size() == 1);
    REQUIRE(s.errors.first().errorCode == -32602);
    REQUIRE(s.errors.first().errorMessage.contains(QStringLiteral("python:never_allowed")));
}

TEST_CASE("notifications/cancelled is one-shot, maps the rpc id, and never answers (#634/#644)", "[surface][protocol][r4]")
{
    CountingServer canceller;
    canceller.initialize(21);

    // A cancelled notification is NEVER answered...
    canceller.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("notifications/cancelled") },
        { QStringLiteral("params"), QVariantMap{ { QStringLiteral("requestId"), 21 } } } });
    REQUIRE(canceller.responses.size() == 1); // only initialize
    REQUIRE(canceller.errors.isEmpty());
    // ...a duplicate cancel for the same id is a consumed-map no-op...
    canceller.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("notifications/cancelled") },
        { QStringLiteral("params"), QVariantMap{ { QStringLiteral("requestId"), 21 } } } });
    // ...and an id that was never seen cannot resurrect anything.
    canceller.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("notifications/cancelled") },
        { QStringLiteral("params"),
          QVariantMap{ { QStringLiteral("requestId"), QStringLiteral("__never_seen__") } } } });
    canceller.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("method"), QStringLiteral("notifications/cancelled") },
        { QStringLiteral("params"), QVariantMap{} } }); // no requestId at all
    REQUIRE(canceller.responses.size() == 1);
    REQUIRE(canceller.errors.isEmpty());

    // The cancelled request's own rpc id stays answerable afterwards: a
    // cancel must not poison the id -> task bookkeeping for later calls.
    canceller.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 22 },
        { QStringLiteral("method"), QStringLiteral("ping") },
        { QStringLiteral("params"), QVariantMap{} } });
    REQUIRE(canceller.responses.size() == 2);
    REQUIRE(canceller.responses.last().id.toInt() == 22);
}

TEST_CASE("spatial tool calls reject missing and mistyped required parameters with named-field errors", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(31);
    s.responses.clear();
    s.errors.clear();

    // Missing required parameter: a structured tool RESULT (isError) with
    // the INVALID_PARAMETER taxonomy - never a JSON-RPC error (#620), never
    // a crash, and it must go through the spatial dispatch path.
    s.callRaw(QVariant(31), QVariantMap{
                                { QStringLiteral("name"), QStringLiteral("spatial:sample_pixels") },
                                { QStringLiteral("arguments"), QVariantMap{} } });
    REQUIRE(s.errors.isEmpty());
    REQUIRE(s.responses.size() == 1);
    REQUIRE(s.responses.first().result.value(QStringLiteral("isError")).toBool());
    // The INVALID_PARAMETER taxonomy rides the STRUCTURED errorCode field
    // (sendToolErrorResult), the prose names the offending field.
    REQUIRE(s.responses.first().result.value(QStringLiteral("errorCode")).toString()
            == QStringLiteral("INVALID_PARAMETER"));
    const std::string missingText = resultTextOf(s.responses.first());
    INFO("missing-param text: " << missingText);
    REQUIRE(missingText.find("path") != std::string::npos);

    // Mistyped REQUIRED parameter (path declared string, sent as number):
    // the #620 declared-type check catches it with the same taxonomy.
    s.responses.clear();
    s.callRaw(QVariant(31), QVariantMap{
                                { QStringLiteral("name"), QStringLiteral("spatial:sample_pixels") },
                                { QStringLiteral("arguments"),
                                  QVariantMap{ { QStringLiteral("path"), 12345 },
                                               { QStringLiteral("points"), QVariantList{} } } } });
    REQUIRE(s.responses.size() == 1);
    REQUIRE(s.responses.first().result.value(QStringLiteral("isError")).toBool());
    REQUIRE(s.responses.first().result.value(QStringLiteral("errorCode")).toString()
            == QStringLiteral("INVALID_PARAMETER"));
    const std::string typeText = resultTextOf(s.responses.first());
    INFO("mistyped-param text: " << typeText);
    REQUIRE(typeText.find("path") != std::string::npos);

    // A spatial-family id that no registry member owns: the structured
    // unknown-spatial-tool error NAMES the tool (no silent fallthrough into
    // the algorithm dispatcher's "Algorithm not found" prose).
    s.responses.clear();
    s.callRaw(QVariant(31), QVariantMap{
                                { QStringLiteral("name"), QStringLiteral("spatial:not_registered_anywhere") } });
    REQUIRE(s.responses.size() == 1);
    const std::string unknownText = resultTextOf(s.responses.first());
    REQUIRE(unknownText.find("spatial:not_registered_anywhere") != std::string::npos);
}

TEST_CASE("tools/list exposes only allow-listed families (surface filter contract)", "[surface][protocol][r4]")
{
    CountingServer s;
    s.initialize(41);

    // A catalog tool under a NON-allowed family must be invisible to
    // tools/list even though it IS registered (the filter is the contract;
    // this is the inverse of the ghost-surface gate in test_surface_parity).
    auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
    sicnu::agent::tool_catalog::AgentTool forbidden;
    forbidden.name = "python:train_forbidden";
    forbidden.category = sicnu::agent::tool_catalog::ToolCategory::Custom;
    forbidden.description = "must never be listed";
    forbidden.inputSchema = Json::Value(Json::objectValue);
    catalog.registerCustomTool(forbidden);

    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 42 },
        { QStringLiteral("method"), QStringLiteral("tools/list") },
        { QStringLiteral("params"),
          QVariantMap{ { QStringLiteral("includeSchemas"), true } } } });
    REQUIRE(s.responses.size() == 2);
    const QVariantList listed =
        s.responses.last().result.value(QStringLiteral("tools")).toList();
    REQUIRE_FALSE(listed.isEmpty());
    const QStringList prefixes = surfaceAllowedPrefixes();
    for (const QVariant &entry : listed)
    {
        const QVariantMap tool = entry.toMap();
        const QString name = tool.value(QStringLiteral("name")).toString();
        INFO("listed tool: " << name.toStdString());
        const bool isMetaProtocolTool =
            sicnu::agent::tool_catalog::meta_protocol::contains(name.toStdString());
        const bool isDataPlatformTool =
            sicnu::agent::isDataPlatformTool(name);
        if (name.contains(QLatin1Char(':')) && !isMetaProtocolTool && !isDataPlatformTool)
        {
            // Every PREFIXED catalog listing must pass the allow-prefix
            // policy (processing: stripping keeps those ids visible).
            // Meta-protocol tools with prefixed names (the
            // scientific:agent_session exception) and data-platform tools
            // (dataset:/experiment:/... families) are exempt: the allow-list
            // governs CATALOG ids, not the meta/data-platform tables.
            QString checkId = name;
            if (checkId.startsWith(QStringLiteral("processing:")))
                checkId = checkId.mid(11);
            bool allowed = false;
            for (const QString &prefix : prefixes)
            {
                if (checkId.startsWith(prefix))
                {
                    allowed = true;
                    break;
                }
            }
            REQUIRE(allowed);
        }
        REQUIRE(name != QStringLiteral("python:train_forbidden"));
        // The embedded schema must exist and be a JSON object root; when it
        // declares a "type" it must be "object" (the wire contract pi's
        // registerBridgedTools relies on; catalog passthrough schemas may
        // omit the type keyword — the projection invariant only requires the
        // object root).
        REQUIRE(tool.contains(QStringLiteral("inputSchema")));
        const QVariantMap schema = tool.value(QStringLiteral("inputSchema")).toMap();
        const QVariant type = schema.value(QStringLiteral("type"));
        if (type.isValid())
            REQUIRE(type.toString() == QStringLiteral("object"));
    }
    catalog.unregisterCustomTool(forbidden.name);
}

TEST_CASE("every projected inputSchema survives the jsoncpp <-> QJsonDocument round-trip", "[surface][protocol][r4]")
{
    // Schema 往返 (wire leg): schemas rendered into tools/list must survive
    // serialize -> parse -> serialize identically under BOTH serializers
    // the surfaces use (jsoncpp in-process, QJsonDocument on the wire); a
    // schema that wobbles (numeric formatting, escaping) desyncs exactly
    // the clients Track 9 locks.
    using sicnu::agent::tool_catalog::collectSurfaceTools;
    const auto tools = collectSurfaceTools();
    REQUIRE(tools.size() > 20);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    Json::CharReaderBuilder readerBuilder;
    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
    for (const auto &tool : tools)
    {
        INFO("schema round-trip: " << tool.name);
        const std::string once = Json::writeString(builder, tool.inputSchema);
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(once));
        REQUIRE(doc.isObject());
        Json::Value back;
        std::string errors;
        REQUIRE(reader->parse(once.c_str(), once.c_str() + once.size(), &back, &errors));
        REQUIRE(Json::writeString(builder, back) == once);
    }
}
