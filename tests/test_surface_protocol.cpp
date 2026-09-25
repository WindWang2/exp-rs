// tests/test_surface_protocol.cpp — Surface-11 WP-C/D/E/G protocol tests:
// progress rate limiting + MCP notifications/progress relay, artifact_read
// bounded reader (known-answer digests as independent oracles), protocol
// redaction boundary, and initialize negotiation negatives.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QVariantMap>

#include <chrono>
#include <thread>

#include "agent/mcp_server.h"
#include "agent/tool_catalog/surface_progress.h"
#include "agent/tool_catalog/surface_redaction.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include <QJsonObject>

// Review P1-1: the MCP sandbox is default-deny (unset SICNU_MCP_WORKSPACE =
// the process CWD). This binary's fixtures live at arbitrary absolute paths
// (QTemporaryDir, /tmp/...), so it opts into the widest sandbox explicitly —
// the documented SICNU_MCP_WORKSPACE=/ — unless the caller configured one.
// Sandbox-specific test cases set (and restore) their own root.
static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", QDir::rootPath().toUtf8() );
    return true;
}();


namespace {

using namespace sicnu::agent::tool_catalog;

// ---------------------------------------------------------------------------
// fixtures shared with test_mcp_server's harness pattern
// ---------------------------------------------------------------------------
class ProtocolServer : public McpServer
{
public:
    QVariantMap lastResponseResult;
    int lastErrorCode = 0;
    QString lastErrorMessage;
    QVariantList progressNotifications;

    void request(const QVariantMap &req) { handleRequest(req); }

    void sendResponse(const QVariant &, const QVariantMap &result) override
    {
        lastResponseResult = result;
        lastErrorCode = 0;
    }
    void sendError(const QVariant &, int code, const QString &message) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendError(const QVariant &, int code, const QString &message, const QVariantMap &) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendNotification(const QString &method, const QVariantMap &params) override
    {
        if (method == QLatin1String("notifications/progress"))
            progressNotifications.append(params);
    }

    void exposeErrorResult(const QVariant &id, const QString &message)
    {
        sendToolErrorResult(id, message, QStringLiteral("E_TEST"), QStringLiteral("test"), false);
    }

    QVariantMap call(const QString &tool, const QVariantMap &args,
                     const QVariant &progressToken = {})
    {
        QVariantMap params;
        params[QStringLiteral("name")] = tool;
        params[QStringLiteral("arguments")] = args;
        // MCP 2024-11-05: the progress token rides request.params._meta.
        if (progressToken.isValid())
        {
            QVariantMap meta;
            meta[QStringLiteral("progressToken")] = progressToken;
            params[QStringLiteral("_meta")] = meta;
        }
        request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 1 },
            { QStringLiteral("method"), QStringLiteral("tools/call") },
            { QStringLiteral("params"), params } });
        return lastResponseResult;
    }
};

/// Trivial fast operator (same shape as test_mcp_server's fixture).
class NoopOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:surface_noop"; }
    Json::Value run(const Json::Value &, sicnu::operators::RSOperatorContext &) override
    {
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/surface_noop.tif";
        return result;
    }
};

void registerNoopOperator()
{
    sicnu::operators::RSOperatorRegistry::instance().registerOperator(
        "rs:surface_noop", []() { return std::make_unique<NoopOperator>(); });
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    if (!registry.findAdapter("rs:surface_noop"))
    {
        auto op = sicnu::operators::RSOperatorRegistry::instance().create("rs:surface_noop");
        if (op)
            registry.registerAdapter(std::make_shared<sicnu::processing::RsOperatorAdapter>(std::move(op)));
    }
}

/// Hermetic fixtures: created by the test itself, never assumed to exist
/// (a fresh machine must pass without manual setup).
void ensureFixtures()
{
    {
        QFile known(QStringLiteral("/tmp/surface11_known.txt"));
        if (known.open(QIODevice::WriteOnly | QIODevice::Truncate))
            known.write("hello surface artifact\n");
    }
    {
        QFile big(QStringLiteral("/tmp/surface11_big.txt"));
        if (big.open(QIODevice::WriteOnly | QIODevice::Truncate))
            big.write(QByteArray(300000, 'A'));
    }
}

/// The tools/call result payload rides content[0].text as compact JSON.
QJsonObject payloadOf(const QVariantMap &callResult)
{
    return QJsonDocument::fromJson(
               callResult.value(QStringLiteral("content")).toList().value(0).toMap()
                   .value(QStringLiteral("text")).toString().toUtf8())
        .object();
}

QVariantMap payloadOfMap(const QVariantMap &callResult)
{
    return payloadOf(callResult).toVariantMap();
}

ProtocolServer &server()
{
    static ProtocolServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_surface_protocol";
        static char *argv[] = { arg0 };
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
        registerNoopOperator();
        ProtocolServer *s = new ProtocolServer();
        s->request(QVariantMap{
            { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
            { QStringLiteral("id"), 0 },
            { QStringLiteral("method"), QStringLiteral("initialize") },
            { QStringLiteral("params"), QVariantMap{} } });
        return s;
    }();
    return *instance;
}

void drainEvents(int attempts = 400)
{
    if (!QCoreApplication::instance())
        return;
    for (int i = 0; i < attempts; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// RateLimiter (pure unit)
// ---------------------------------------------------------------------------
TEST_CASE("Progress rate limiter bounds emissions", "[surface][progress]")
{
    progress::RateLimiter limiter(5.0);
    // First tick always emits.
    REQUIRE(limiter.shouldEmit(0.0, false, "running"));
    // Sub-treshold movement is swallowed.
    REQUIRE_FALSE(limiter.shouldEmit(1.0, false, "running"));
    REQUIRE_FALSE(limiter.shouldEmit(4.9, false, "running"));
    // Threshold crossing emits.
    REQUIRE(limiter.shouldEmit(5.0, false, "running"));
    // State change emits even below threshold.
    REQUIRE(limiter.shouldEmit(6.0, false, "waiting_resource"));
    // Terminal emits exactly once, even repeatedly reported.
    REQUIRE(limiter.shouldEmit(100.0, true, "completed"));
    REQUIRE_FALSE(limiter.shouldEmit(100.0, true, "completed"));
    REQUIRE(limiter.lastEmitted() == 100.0);
}

// ---------------------------------------------------------------------------
// Redaction (known-answer)
// ---------------------------------------------------------------------------
TEST_CASE("Redaction removes credential shapes at the boundary", "[surface][redaction]")
{
    using redaction::redact;
    REQUIRE(redact(QStringLiteral("Authorization: Bearer abc.def.ghi"))
            == QStringLiteral("Authorization: [REDACTED]"));
    REQUIRE(redact(QStringLiteral("failed to fetch, bearer eyJhbGciOi.static.part"))
            .contains(QStringLiteral("[REDACTED]")));
    REQUIRE_FALSE(redact(QStringLiteral("failed to fetch, bearer eyJhbGciOi.static.part"))
                      .contains(QStringLiteral("eyJhbGciOi")));

    REQUIRE(redact(QStringLiteral("api_key=sk-1234567890abcdef"))
                .contains(QStringLiteral("[REDACTED]")));
    REQUIRE(redact(QStringLiteral("password: hunter2!"))
                .contains(QStringLiteral("[REDACTED]")));
    REQUIRE_FALSE(redact(QStringLiteral("password: hunter2!")).contains(QStringLiteral("hunter2")));

    // PEM block (multi-line) disappears entirely.
    const QString pem = QStringLiteral(
        "-----BEGIN PRIVATE KEY-----\nMIIEv\n...lines...\n-----END PRIVATE KEY-----\n");
    REQUIRE(redact(pem) == QStringLiteral("[REDACTED PEM]\n"));

    // Connection URL password disappears; scheme/user survive in shape.
    REQUIRE(redact(QStringLiteral("postgresql://alice:s3cret@db.example.com/rs"))
                .contains(QStringLiteral("[REDACTED-URL]@")));
    REQUIRE_FALSE(redact(QStringLiteral("postgresql://alice:s3cret@db.example.com/rs"))
                      .contains(QStringLiteral("s3cret")));

    // Ordinary text and ordinary assignments pass through untouched.
    REQUIRE(redact(QStringLiteral("rs:spectral_index completed in 42ms"))
            == QStringLiteral("rs:spectral_index completed in 42ms"));
    REQUIRE(redact(QStringLiteral("max_tiles=16"))
            == QStringLiteral("max_tiles=16"));
}

TEST_CASE("Tool error results are redacted at the protocol boundary", "[surface][redaction]")
{
    ProtocolServer &s = server();
    s.exposeErrorResult(QVariant(9),
                        QStringLiteral("adapter failed with api_key=sk-supersecret-123"));
    REQUIRE(s.lastResponseResult.value(QStringLiteral("isError")).toBool());
    const QString text = s.lastResponseResult.value(QStringLiteral("content")).toList()
                             .value(0).toMap().value(QStringLiteral("text")).toString();
    REQUIRE_FALSE(text.contains(QStringLiteral("sk-supersecret-123")));
    REQUIRE(text.contains(QStringLiteral("[REDACTED]")));
}

// ---------------------------------------------------------------------------
// initialize negotiation negatives
// ---------------------------------------------------------------------------
TEST_CASE("initialize answers its supported version for any request", "[surface][protocol]")
{
    ProtocolServer &s = server();
    s.request(QVariantMap{
        { QStringLiteral("jsonrpc"), QStringLiteral("2.0") },
        { QStringLiteral("id"), 3 },
        { QStringLiteral("method"), QStringLiteral("initialize") },
        { QStringLiteral("params"),
          QVariantMap{ { QStringLiteral("protocolVersion"), QStringLiteral("1999-09-09") } } } });
    REQUIRE(s.lastErrorCode == 0);
    REQUIRE(s.lastResponseResult.value(QStringLiteral("protocolVersion")).toString()
            == QStringLiteral("2024-11-05"));
}

// ---------------------------------------------------------------------------
// artifact_read — bounded reader with independent known-answer oracles
// ---------------------------------------------------------------------------
TEST_CASE("artifact_read returns bounded slices with digests", "[surface][artifact]")
{
    ensureFixtures();
    ProtocolServer &s = server();

    const QString path = QStringLiteral("/tmp/surface11_known.txt");
    const QString kExpectedSha = QStringLiteral(
        "5e3d51ee86526a1f327befac17ec7efbbf1edb3ba4a72f8b06280bb276fee725");
    const QString kExpectedB64 = QStringLiteral("aGVsbG8gc3VyZmFjZSBhcnRpZmFjdAo=");

    // Full text read.
    QVariantMap result = s.call(QStringLiteral("artifact_read"),
                                QVariantMap{ { QStringLiteral("path"), path } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool() == false);
    QVariantMap content = payloadOfMap(result);
    REQUIRE(content.value(QStringLiteral("content")).toString()
            == QStringLiteral("hello surface artifact\n"));
    REQUIRE(content.value(QStringLiteral("sha256")).toString() == kExpectedSha);
    REQUIRE(content.value(QStringLiteral("truncated")).toBool() == false);

    // base64 mode against an independently produced constant.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("encoding"), QStringLiteral("base64") } });
    REQUIRE(payloadOfMap(result).value(QStringLiteral("content")).toString() == kExpectedB64);

    // Offset walk: slice [2, 8) with the cursor.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("offset"), 2 },
                                 { QStringLiteral("length"), 6 } });
    content = payloadOfMap(result);
    REQUIRE(content.value(QStringLiteral("content")).toString() == QStringLiteral("llo su"));
    REQUIRE(content.value(QStringLiteral("truncated")).toBool() == true);
    REQUIRE(content.value(QStringLiteral("next_offset")).toLongLong() == 8);
    REQUIRE(content.value(QStringLiteral("sha256")).toString() == kExpectedSha); // whole-file digest

    // Negative: unknown path, directory, offset beyond EOF, bad encoding.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), QStringLiteral("/tmp/__nope__.bin") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());
    REQUIRE(result.value(QStringLiteral("content")).toList().value(0).toMap()
                .value(QStringLiteral("text")).toString().contains(QStringLiteral("not found")));

    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), QStringLiteral("/tmp") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());

    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("offset"), 99999 } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());

    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("encoding"), QStringLiteral("hex") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());
}

TEST_CASE("artifact_read clamps to the 256 KiB chunk cap", "[surface][artifact]")
{
    ensureFixtures();
    ProtocolServer &s = server();
    const QString path = QStringLiteral("/tmp/surface11_big.txt"); // 300000 bytes of 'A'

    QVariantMap result = s.call(QStringLiteral("artifact_read"),
                                QVariantMap{ { QStringLiteral("path"), path } });
    QVariantMap content = payloadOfMap(result);
    REQUIRE(content.value(QStringLiteral("length")).toLongLong() == 262144);
    REQUIRE(content.value(QStringLiteral("truncated")).toBool() == true);
    REQUIRE(content.value(QStringLiteral("next_offset")).toLongLong() == 262144);

    // Cursor walk reaches the tail and terminates.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("offset"), 262144 } });
    content = payloadOfMap(result);
    REQUIRE(content.value(QStringLiteral("length")).toLongLong() == 300000 - 262144);
    REQUIRE(content.value(QStringLiteral("truncated")).toBool() == false);
    REQUIRE(content.value(QStringLiteral("content")).toString()
            == QString(300000 - 262144, QLatin1Char('A')));

    // Requesting an oversized length is clamped, not rejected.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("length"), 10 * 1000 * 1000 } });
    content = payloadOfMap(result);
    REQUIRE(content.value(QStringLiteral("length")).toLongLong() == 262144);
}

TEST_CASE("artifact_read rejects non-UTF-8 slices in text mode", "[surface][artifact]")
{
    ProtocolServer &s = server();
    const QString path = QStringLiteral("/tmp/surface11_binary.bin");
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        const char bytes[] = { char(0xff), char(0xfe), 'a', 'b' };
        file.write(bytes, sizeof(bytes));
    }
    QVariantMap result = s.call(QStringLiteral("artifact_read"),
                                QVariantMap{ { QStringLiteral("path"), path } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());
    REQUIRE(result.value(QStringLiteral("content")).toList().value(0).toMap()
                .value(QStringLiteral("text")).toString().contains(QStringLiteral("base64")));

    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), path },
                                 { QStringLiteral("encoding"), QStringLiteral("base64") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool() == false);
    QFile::remove(path);
}

TEST_CASE("artifact_read enforces the workspace sandbox", "[surface][artifact]")
{
    ProtocolServer &s = server();
    ensureFixtures();
    qputenv("SICNU_MCP_WORKSPACE", "/tmp/surface11_sandbox");
    QDir::root().mkpath(QStringLiteral("/tmp/surface11_sandbox"));
    QFile::remove(QStringLiteral("/tmp/surface11_sandbox/in.txt")); // copy() never overwrites
    REQUIRE(QFile::copy(QStringLiteral("/tmp/surface11_known.txt"),
                        QStringLiteral("/tmp/surface11_sandbox/in.txt")));

    // Inside the sandbox: allowed (relative resolution against the root).
    QVariantMap result = s.call(QStringLiteral("artifact_read"),
                                QVariantMap{ { QStringLiteral("path"), QStringLiteral("in.txt") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool() == false);
    REQUIRE(payloadOfMap(result).value(QStringLiteral("path")).toString()
            == QStringLiteral("/tmp/surface11_sandbox/in.txt"));

    // Outside the sandbox: rejected.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), QStringLiteral("/etc/hostname") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());

    // Relative escape (".." beyond the sandbox root): rejected — the
    // containment check must run on the RESOLVED path.
    result = s.call(QStringLiteral("artifact_read"),
                    QVariantMap{ { QStringLiteral("path"), QStringLiteral("../../etc/hostname") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool());
    REQUIRE(result.value(QStringLiteral("content")).toList().value(0).toMap()
                .value(QStringLiteral("text")).toString()
                .contains(QStringLiteral("rejected")));

    // Restore this binary's fixture sandbox (see kFixtureWorkspaceInstalled);
    // unsetting would now mean "sandbox to the CWD", not "unrestricted".
    qputenv("SICNU_MCP_WORKSPACE", QDir::rootPath().toUtf8());
}

// ---------------------------------------------------------------------------
// notifications/progress relay
// ---------------------------------------------------------------------------
TEST_CASE("tools/call with progressToken receives notifications/progress", "[surface][progress]")
{
    ProtocolServer &s = server();
    const int before = s.progressNotifications.size();

    QVariantMap result = s.call(QStringLiteral("execute_operator"),
                                QVariantMap{ { QStringLiteral("operator_id"), QStringLiteral("rs:surface_noop") } },
                                QVariant(QStringLiteral("tok-1")));
    REQUIRE(result.value(QStringLiteral("isError")).toBool() == false);
    // The tool result's text is the compact JSON of the result payload.
    const QJsonDocument resultDoc = QJsonDocument::fromJson(
        result.value(QStringLiteral("content")).toList().value(0).toMap()
            .value(QStringLiteral("text")).toString().toUtf8());
    REQUIRE(resultDoc.object().contains(QStringLiteral("execution_id")));
    const QString execId = resultDoc.object().value(QStringLiteral("execution_id")).toString();
    REQUIRE_FALSE(execId.isEmpty());

    // Wait for the terminal status, then drain queued taskUpdated signals.
    for (int i = 0; i < 400; ++i)
    {
        QVariantMap status = s.call(QStringLiteral("get_execution_status"),
                                    QVariantMap{ { QStringLiteral("execution_id"), execId } });
        const QString state = status.value(QStringLiteral("status")).toString();
        if (state == QLatin1String("completed") || state == QLatin1String("failed")
            || state == QLatin1String("canceled"))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    drainEvents();

    REQUIRE(s.progressNotifications.size() >= before + 1);
    for (int i = before; i < s.progressNotifications.size(); ++i)
    {
        const QVariantMap params = s.progressNotifications.at(i).toMap();
        REQUIRE(params.value(QStringLiteral("progressToken")).toString() == QStringLiteral("tok-1"));
        REQUIRE(params.value(QStringLiteral("total")).toDouble() == 1.0);
        const double progress = params.value(QStringLiteral("progress")).toDouble();
        REQUIRE(progress >= 0.0);
        REQUIRE(progress <= 1.0);
    }
    // The noop never reports 100, so its terminal notification carries the
    // frozen percentage — exactly-one-terminal is asserted by the RateLimiter
    // unit test; here we verify the wire fields.
}

TEST_CASE("tools/call without progressToken emits no progress notifications", "[surface][progress]")
{
    ProtocolServer &s = server();
    const int before = s.progressNotifications.size();

    QVariantMap result = s.call(QStringLiteral("execute_operator"),
                                QVariantMap{ { QStringLiteral("operator_id"), QStringLiteral("rs:surface_noop") } });
    REQUIRE(result.value(QStringLiteral("isError")).toBool() == false);
    const QJsonDocument resultDoc = QJsonDocument::fromJson(
        result.value(QStringLiteral("content")).toList().value(0).toMap()
            .value(QStringLiteral("text")).toString().toUtf8());
    const QString execId = resultDoc.object().value(QStringLiteral("execution_id")).toString();
    REQUIRE_FALSE(execId.isEmpty());
    for (int i = 0; i < 400; ++i)
    {
        QVariantMap status = s.call(QStringLiteral("get_execution_status"),
                                    QVariantMap{ { QStringLiteral("execution_id"), execId } });
        const QString state = status.value(QStringLiteral("status")).toString();
        if (state == QLatin1String("completed") || state == QLatin1String("failed")
            || state == QLatin1String("canceled"))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    drainEvents(40);
    REQUIRE(s.progressNotifications.size() == before);
}
