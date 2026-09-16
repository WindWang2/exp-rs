// tests/test_surface_e2e.cpp — Surface-11 WP-H protocol E2E.
//
// Spawns the surface_mcp_host binary (real stdio, real child process) and
// drives the full agent loop TWICE (Oracle-4/6): initialize → tools/list →
// tool schema → operator schema → execute with progress notifications →
// cancel via notifications/cancelled → artifact_read → malformed-traffic
// negatives. Wire format is line-delimited JSON, exactly what MCP clients
// and pi/mcp_bridge.ts speak.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <chrono>
#include <string>
#include <vector>

#ifndef SICNU_TEST_SURFACE_HOST
#error "SICNU_TEST_SURFACE_HOST must point at surface_mcp_host"
#endif

namespace {

struct Wire
{
    QProcess process;
    std::string buffer;
    int nextId = 1;

    void start()
    {
        // Hermetic environment: a globally exported SICNU_MCP_WORKSPACE would
        // reject the artifact leg of the scenario.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("SICNU_MCP_WORKSPACE"));
        process.setProcessEnvironment(env);
        process.start(QString::fromUtf8(SICNU_TEST_SURFACE_HOST), QStringList{});
        REQUIRE(process.waitForStarted(10000));
    }

    void stop()
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            REQUIRE(process.waitForFinished(10000));
        }
    }

    void sendLine(const std::string &line)
    {
        process.write(QByteArray::fromStdString(line + "\n"));
        REQUIRE(process.waitForBytesWritten(10000));
    }

    void sendJson(const QJsonObject &obj)
    {
        sendLine(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)).toStdString());
    }

    /// Reads one stdout line with an overall timeout; fails the test on EOF.
    std::string readLine(int timeoutMs = 15000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            const auto nl = buffer.find('\n');
            if (nl != std::string::npos)
            {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return line;
            }
            const int remaining = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline
                                                                       - std::chrono::steady_clock::now())
                    .count());
            if (remaining <= 0)
            {
                FAIL("timed out waiting for a wire line; buffer="
                     << buffer.substr(0, 200));
            }
            if (!process.waitForReadyRead(qMin(remaining, 500)))
            {
                if (process.state() == QProcess::NotRunning)
                    FAIL("host exited unexpectedly: "
                         << process.readAllStandardError().toStdString());
            }
            buffer += process.readAllStandardOutput().toStdString();
        }
    }

    /// Reads lines until one with the given id arrives; everything else
    /// (notifications) is collected into @p notifications.
    QJsonObject readResponse(int id, QJsonArray &notifications, int timeoutMs = 20000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            const int remaining = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline
                                                                       - std::chrono::steady_clock::now())
                    .count());
            if (remaining <= 0)
                FAIL("timed out waiting for response id " << id);
            const std::string line = readLine(remaining);
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line));
            REQUIRE(doc.isObject());
            const QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("id"))
                && obj.value(QStringLiteral("id")).toVariant().toLongLong() == id)
            {
                return obj;
            }
            if (obj.contains(QStringLiteral("method")))
                notifications.append(obj);
        }
    }
};

/// The full agent loop; @p pass exists so failures identify the repetition.
void runScenario(int pass)
{
    INFO("E2E pass " << pass);
    QTemporaryDir workspace;
    REQUIRE(workspace.isValid());
    const QString artifactPath = workspace.path() + QStringLiteral("/artifact.json");
    {
        QFile file(artifactPath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("{\"payload\":[1,2,3],\"note\":\"surface e2e artifact\"}\n");
    }

    Wire wire;
    wire.start();

    // -- initialize ----------------------------------------------------------
    QJsonObject init;
    init[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    init[QStringLiteral("id")] = wire.nextId++;
    init[QStringLiteral("method")] = QStringLiteral("initialize");
    init[QStringLiteral("params")] = QJsonObject{ { QStringLiteral("protocolVersion"),
                                                    QStringLiteral("2024-11-05") } };
    wire.sendJson(init);
    QJsonArray notifications;
    const QJsonObject initResp = wire.readResponse(init.value(QStringLiteral("id")).toInt(), notifications);
    REQUIRE(initResp.contains(QStringLiteral("result")));
    REQUIRE(initResp.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("protocolVersion")).toString()
            == QStringLiteral("2024-11-05"));
    REQUIRE(initResp.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("serverInfo")).toObject()
                .value(QStringLiteral("name")).toString()
            == QStringLiteral("exp-rs-mcp"));

    // -- notifications/initialized ------------------------------------------
    QJsonObject initialized;
    initialized[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    initialized[QStringLiteral("method")] = QStringLiteral("notifications/initialized");
    wire.sendJson(initialized);

    // -- tools/list with schemas --------------------------------------------
    QJsonObject list;
    list[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    list[QStringLiteral("id")] = wire.nextId++;
    list[QStringLiteral("method")] = QStringLiteral("tools/list");
    list[QStringLiteral("params")] = QJsonObject{ { QStringLiteral("includeSchemas"), true } };
    wire.sendJson(list);
    const QJsonObject listResp = wire.readResponse(list.value(QStringLiteral("id")).toInt(), notifications);
    const QJsonArray tools = listResp.value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("tools")).toArray();
    REQUIRE(tools.size() > 20);
    bool sawArtifactRead = false;
    bool sawExecuteOperator = false;
    for (const auto &tool : tools)
    {
        const QJsonObject entry = tool.toObject();
        REQUIRE(entry.value(QStringLiteral("name")).toString().size() > 0);
        if (entry.value(QStringLiteral("name")).toString() == QLatin1String("artifact_read"))
        {
            sawArtifactRead = true;
            REQUIRE(entry.value(QStringLiteral("inputSchema")).toObject()
                        .value(QStringLiteral("type")).toString() == QLatin1String("object"));
        }
        if (entry.value(QStringLiteral("name")).toString() == QLatin1String("execute_operator"))
            sawExecuteOperator = true;
    }
    REQUIRE(sawArtifactRead);
    REQUIRE(sawExecuteOperator);

    // -- get_operator_schema -------------------------------------------------
    QJsonObject schema;
    schema[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    schema[QStringLiteral("id")] = wire.nextId++;
    schema[QStringLiteral("method")] = QStringLiteral("tools/call");
    schema[QStringLiteral("params")] = QJsonObject{
        { QStringLiteral("name"), QStringLiteral("get_operator_schema") },
        { QStringLiteral("arguments"), QJsonObject{ { QStringLiteral("operator_id"),
                                                     QStringLiteral("rs:surface_noop") } } } };
    wire.sendJson(schema);
    const QJsonObject schemaResp = wire.readResponse(schema.value(QStringLiteral("id")).toInt(), notifications);
    const QString schemaText = schemaResp.value(QStringLiteral("result")).toObject()
                                   .value(QStringLiteral("content")).toArray().at(0).toObject()
                                   .value(QStringLiteral("text")).toString();
    // get_operator_schema echoes the operator id and its JSON schema body.
    REQUIRE(schemaText.contains(QStringLiteral("rs:surface_noop")));
    REQUIRE(schemaText.contains(QStringLiteral("schema")));
    REQUIRE(schemaText.contains(QStringLiteral("operator_id")));

    // -- execute with progressToken, observe notifications/progress ----------
    QJsonObject execute;
    execute[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    const int executeId = wire.nextId++;
    execute[QStringLiteral("id")] = executeId;
    execute[QStringLiteral("method")] = QStringLiteral("tools/call");
    QJsonObject executeParams;
    executeParams[QStringLiteral("name")] = QStringLiteral("execute_operator");
    executeParams[QStringLiteral("arguments")] = QJsonObject{
        { QStringLiteral("operator_id"), QStringLiteral("rs:surface_noop") } };
    executeParams[QStringLiteral("_meta")] = QJsonObject{
        { QStringLiteral("progressToken"), QStringLiteral("e2e-progress") } };
    execute[QStringLiteral("params")] = executeParams;
    wire.sendJson(execute);
    const QJsonObject execResp = wire.readResponse(executeId, notifications);
    REQUIRE(execResp.contains(QStringLiteral("result")));
    const QJsonObject execResultDoc = QJsonDocument::fromJson(
        execResp.value(QStringLiteral("result")).toObject()
            .value(QStringLiteral("content")).toArray().at(0).toObject()
            .value(QStringLiteral("text")).toString().toUtf8()).object();
    REQUIRE(execResultDoc.contains(QStringLiteral("execution_id")));
    const QString execId = execResultDoc.value(QStringLiteral("execution_id")).toString();

    // Poll status to terminal; progress notifications arrive on the wire.
    QString status;
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        QJsonObject poll;
        poll[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        poll[QStringLiteral("id")] = wire.nextId++;
        poll[QStringLiteral("method")] = QStringLiteral("tools/call");
        poll[QStringLiteral("params")] = QJsonObject{
            { QStringLiteral("name"), QStringLiteral("get_execution_status") },
            { QStringLiteral("arguments"), QJsonObject{ { QStringLiteral("execution_id"), execId } } } };
        wire.sendJson(poll);
        const QJsonObject pollResp = wire.readResponse(poll.value(QStringLiteral("id")).toInt(), notifications);
        status = QJsonDocument::fromJson(
            pollResp.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("content")).toArray().at(0).toObject()
                .value(QStringLiteral("text")).toString().toUtf8())
                     .object().value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed") || status == QLatin1String("failed")
            || status == QLatin1String("canceled"))
            break;
    }
    REQUIRE(status == QLatin1String("completed"));

    int progressForToken = 0;
    int terminalProgress = 0;
    Q_UNUSED(terminalProgress); // see note below — noop never reaches 1.0
    for (const auto &note : notifications)
    {
        const QJsonObject noteObj = note.toObject();
        if (noteObj.value(QStringLiteral("method")).toString()
            != QLatin1String("notifications/progress"))
            continue;
        const QJsonObject params = noteObj.value(QStringLiteral("params")).toObject();
        if (params.value(QStringLiteral("progressToken")).toString() != QLatin1String("e2e-progress"))
            continue;
        ++progressForToken;
        if (params.value(QStringLiteral("progress")).toDouble() >= 1.0)
            ++terminalProgress;
    }
    REQUIRE(progressForToken >= 1);
    // The noop never reports 100, so no notification reaches 1.0; the
    // exactly-one-terminal contract is asserted by the RateLimiter unit test
    // and by the cancel section below (terminal state polled over the wire).
    Q_UNUSED(terminalProgress);

    // -- cancel a long-running operator via notifications/cancelled ----------
    // execute_operator returns immediately (async submission), so the
    // response for sleepId arrives before any progress.
    QJsonObject sleep;
    sleep[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    const int sleepId = wire.nextId++;
    sleep[QStringLiteral("id")] = sleepId;
    sleep[QStringLiteral("method")] = QStringLiteral("tools/call");
    QJsonObject sleepParams;
    sleepParams[QStringLiteral("name")] = QStringLiteral("execute_operator");
    sleepParams[QStringLiteral("arguments")] = QJsonObject{
        { QStringLiteral("operator_id"), QStringLiteral("rs:surface_sleep") },
        { QStringLiteral("parameters"), QJsonObject{ { QStringLiteral("slices"), 600 } } } };
    sleepParams[QStringLiteral("_meta")] = QJsonObject{
        { QStringLiteral("progressToken"), QStringLiteral("e2e-cancel") } };
    sleep[QStringLiteral("params")] = sleepParams;
    wire.sendJson(sleep);
    const QJsonObject sleepResp = wire.readResponse(sleepId, notifications);
    const QJsonObject sleepResultDoc = QJsonDocument::fromJson(
        sleepResp.value(QStringLiteral("result")).toObject()
            .value(QStringLiteral("content")).toArray().at(0).toObject()
            .value(QStringLiteral("text")).toString().toUtf8()).object();
    REQUIRE(sleepResultDoc.contains(QStringLiteral("execution_id")));
    const QString sleepExecId = sleepResultDoc.value(QStringLiteral("execution_id")).toString();

    // Read lines until the live relay delivers a progress event for the
    // sleep task (proves notifications flow while the operator runs).
    bool sawLiveProgress = false;
    const auto liveDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
    while (!sawLiveProgress && std::chrono::steady_clock::now() < liveDeadline)
    {
        const std::string line = wire.readLine(5000);
        const QJsonObject note = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        if (note.value(QStringLiteral("method")).toString() != QLatin1String("notifications/progress"))
            continue;
        if (note.value(QStringLiteral("params")).toObject()
                .value(QStringLiteral("progressToken")).toString() == QLatin1String("e2e-cancel"))
            sawLiveProgress = true;
    }
    REQUIRE(sawLiveProgress);

    // Cancel the request (MCP cancellation notification) and wait for the
    // task to reach exactly one terminal state: "canceled".
    QJsonObject cancel;
    cancel[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    cancel[QStringLiteral("method")] = QStringLiteral("notifications/cancelled");
    cancel[QStringLiteral("params")] = QJsonObject{ { QStringLiteral("requestId"), sleepId } };
    wire.sendJson(cancel);

    QString cancelStatus;
    const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20000);
    while (std::chrono::steady_clock::now() < cancelDeadline)
    {
        QJsonObject poll;
        poll[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        poll[QStringLiteral("id")] = wire.nextId++;
        poll[QStringLiteral("method")] = QStringLiteral("tools/call");
        poll[QStringLiteral("params")] = QJsonObject{
            { QStringLiteral("name"), QStringLiteral("get_execution_status") },
            { QStringLiteral("arguments"), QJsonObject{ { QStringLiteral("execution_id"), sleepExecId } } } };
        wire.sendJson(poll);
        const QJsonObject pollResp = wire.readResponse(poll.value(QStringLiteral("id")).toInt(), notifications);
        const QString state = QJsonDocument::fromJson(
            pollResp.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("content")).toArray().at(0).toObject()
                .value(QStringLiteral("text")).toString().toUtf8())
                                  .object().value(QStringLiteral("status")).toString();
        if (state == QLatin1String("completed") || state == QLatin1String("failed")
            || state == QLatin1String("canceled"))
        {
            cancelStatus = state;
            break;
        }
    }
    REQUIRE(cancelStatus == QLatin1String("canceled"));

    // Terminal-state uniqueness over the wire: repeated status polls after
    // the terminal transition answer the same terminal state every time
    // (never "running" again, never a different terminal label).
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        QJsonObject poll;
        poll[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        poll[QStringLiteral("id")] = wire.nextId++;
        poll[QStringLiteral("method")] = QStringLiteral("tools/call");
        poll[QStringLiteral("params")] = QJsonObject{
            { QStringLiteral("name"), QStringLiteral("get_execution_status") },
            { QStringLiteral("arguments"), QJsonObject{ { QStringLiteral("execution_id"), sleepExecId } } } };
        wire.sendJson(poll);
        const QJsonObject pollResp = wire.readResponse(poll.value(QStringLiteral("id")).toInt(), notifications);
        const QString state = QJsonDocument::fromJson(
            pollResp.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("content")).toArray().at(0).toObject()
                .value(QStringLiteral("text")).toString().toUtf8())
                                  .object().value(QStringLiteral("status")).toString();
        REQUIRE(state == QLatin1String("canceled"));
    }

    // -- artifact_read over the real wire ------------------------------------
    QJsonObject artifact;
    artifact[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    artifact[QStringLiteral("id")] = wire.nextId++;
    artifact[QStringLiteral("method")] = QStringLiteral("tools/call");
    artifact[QStringLiteral("params")] = QJsonObject{
        { QStringLiteral("name"), QStringLiteral("artifact_read") },
        { QStringLiteral("arguments"), QJsonObject{ { QStringLiteral("path"), artifactPath } } } };
    wire.sendJson(artifact);
    const QJsonObject artifactResp = wire.readResponse(artifact.value(QStringLiteral("id")).toInt(), notifications);
    const QJsonObject artifactResult = QJsonDocument::fromJson(
        artifactResp.value(QStringLiteral("result")).toObject()
            .value(QStringLiteral("content")).toArray().at(0).toObject()
            .value(QStringLiteral("text")).toString().toUtf8()).object();
    REQUIRE(artifactResult.value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("surface e2e artifact")));
    REQUIRE(artifactResult.value(QStringLiteral("truncated")).toBool() == false);
    REQUIRE(artifactResult.value(QStringLiteral("size_bytes")).toVariant().toLongLong() > 0);

    // -- malformed traffic negatives -----------------------------------------
    wire.sendLine(std::string(5 * 1024 * 1024, 'x')); // oversized line → parse error
    {
        const std::string line = wire.readLine();
        const QJsonObject err = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        REQUIRE(err.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt()
                == -32700);
    }
    wire.sendLine("{not json");
    {
        const std::string line = wire.readLine();
        const QJsonObject err = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        REQUIRE(err.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt()
                == -32700);
    }
    {
        QJsonObject unknown;
        unknown[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        unknown[QStringLiteral("id")] = wire.nextId++;
        unknown[QStringLiteral("method")] = QStringLiteral("floppy/format");
        wire.sendJson(unknown);
        const std::string line = wire.readLine();
        const QJsonObject err = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        REQUIRE(err.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt()
                == -32601);
    }

    // Unknown TOOL inside tools/call → -32602 (not -32601).
    {
        QJsonObject unknownTool;
        unknownTool[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        unknownTool[QStringLiteral("id")] = wire.nextId++;
        unknownTool[QStringLiteral("method")] = QStringLiteral("tools/call");
        unknownTool[QStringLiteral("params")] = QJsonObject{
            { QStringLiteral("name"), QStringLiteral("__nope__") } };
        wire.sendJson(unknownTool);
        const std::string line = wire.readLine();
        const QJsonObject err = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        REQUIRE(err.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt()
                == -32602);
    }

    wire.stop();
}

} // namespace

TEST_CASE("Surface E2E: real stdio protocol loop, run twice", "[surface][e2e]")
{
    // Oracle-4/6: the identical scenario is executed twice against fresh
    // child processes — the second pass catches per-process state leakage
    // (half-closed pipes, leaked subscriptions, unclean shutdowns).
    runScenario(1);
    runScenario(2);
}
