// tests/test_mcp_server.cpp
#include <catch2/catch_test_macros.hpp>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVariantMap>

#include <chrono>
#include <thread>

#include "agent/mcp_server.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "operators/framework/rs_operator_registry.h"
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"

// Helper subclass of McpServer to expose handlers directly for unit testing
class TestMcpServer : public McpServer
{
public:
    TestMcpServer() : McpServer() {}

    QVariantMap testListAlgorithms() { return handleListAlgorithms(); }
    QVariantMap testGetAlgorithmSchema(const QString &id) { return handleGetAlgorithmSchema(id); }
    QVariantMap testListLayers() { return handleListLayers(); }
    QVariantMap testDescribeDataset(const QString &id) { return handleDescribeDataset(id); }
    QVariantMap testListOperators() { return handleListOperators(); }
    QVariantMap testGetOperatorSchema(const QString &id) { return handleGetOperatorSchema(id); }
    QVariantMap testExecuteOperator(const QString &id, const QVariantMap &params)
    {
        return handleExecuteOperator(id, params);
    }
    QVariantMap testExecuteAlgorithm(const QString &id, const QVariantMap &params)
    {
        return handleExecuteAlgorithm(id, params);
    }
    QVariantMap testGetExecutionStatus(const QString &id) { return handleGetExecutionStatus(id); }
    QVariantMap testCancelExecution(const QString &id) { return handleCancelExecution(id); }
};

namespace {

/// Trivial fast operator: completes immediately with a fixed result, so
/// integration tests can drive execute → status → cancel through TaskCenter.
class NoopOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:mcp_noop"; }
    Json::Value run(const Json::Value &, sicnu::operators::RSOperatorContext &) override
    {
        Json::Value result(Json::objectValue);
        result["output"] = "/tmp/mcp_noop.tif";
        return result;
    }
};

void registerNoopOperator()
{
    sicnu::operators::RSOperatorRegistry::instance().registerOperator(
        "rs:mcp_noop", []() { return std::make_unique<NoopOperator>(); });
    // Mirror the new operator into AtomicAlgorithmRegistry (dispatcher surface).
    sicnu::processing::AtomicAlgorithmRegistry::instance().reset();
}

QString waitForTerminal(TestMcpServer &server, const QString &execId)
{
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        const QString status = server.testGetExecutionStatus(execId).value("status").toString();
        if (status == QLatin1String("completed") || status == QLatin1String("failed")
            || status == QLatin1String("canceled"))
        {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return QString();
}

} // namespace

TEST_CASE("MCP Server tests", "[agent][mcp]") {
    // Register providers if not already registered
    if (!QgsApplication::processingRegistry()->providerById("qgis_algorithms"))
    {
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    }

    TestMcpServer server;

    SECTION("list_algorithms lists RS algorithms") {
        QVariantMap res = server.testListAlgorithms();
        QVariantList algs = res.value("algorithms").toList();
        REQUIRE_FALSE(algs.isEmpty());

        bool foundBandMath = false;
        for (const QVariant &alg : algs) {
            QVariantMap algMap = alg.toMap();
            if (algMap.value("id").toString() == "qgis_algorithms:rs_band_math") {
                foundBandMath = true;
                QVariantMap meta = algMap.value("metadata").toMap();
                CHECK(meta.value("purpose").toString().contains("band algebra"));
            }
        }
        CHECK(foundBandMath);
    }

    SECTION("get_algorithm_schema returns valid schema") {
        QVariantMap schema = server.testGetAlgorithmSchema("qgis_algorithms:rs_band_math");
        REQUIRE_FALSE(schema.isEmpty());
        CHECK(schema.value("title").toString() == "qgis_algorithms:rs_band_math");
        CHECK(schema.value("type").toString() == "object");

        QVariantMap properties = schema.value("properties").toMap();
        REQUIRE(properties.contains("INPUT_LAYERS"));
        REQUIRE(properties.contains("EXPRESSION"));
    }

    SECTION("list_layers lists active project layers") {
        // Create a temporary project layer
        QgsRasterLayer *layer = new QgsRasterLayer("invalid_file_path", "test_mcp_layer");
        QgsProject::instance()->addMapLayer(layer);

        QVariantMap res = server.testListLayers();
        QVariantList layers = res.value("layers").toList();
        REQUIRE_FALSE(layers.isEmpty());

        bool foundLayer = false;
        for (const QVariant &l : layers) {
            QVariantMap lMap = l.toMap();
            if (lMap.value("name").toString() == "test_mcp_layer") {
                foundLayer = true;
                CHECK(lMap.value("type").toString() == "raster");
            }
        }
        CHECK(foundLayer);

        // Clean up
        QgsProject::instance()->removeMapLayer(layer);
    }

    SECTION("list_operators lists RSOperator kernel") {
        QVariantMap res = server.testListOperators();
        QVariantList ops = res.value("operators").toList();
        REQUIRE(res.value("count").toInt() == ops.size());
        REQUIRE(ops.size() >= 15);

        bool foundSpectral = false;
        bool foundReproject = false;
        for (const QVariant &op : ops) {
            QVariantMap opMap = op.toMap();
            const QString id = opMap.value("id").toString();
            if (id == "rs:spectral_index") {
                foundSpectral = true;
                CHECK(opMap.value("group").toString() == "spectral");
            }
            if (id == "gdal:reproject") {
                foundReproject = true;
            }
        }
        CHECK(foundSpectral);
        CHECK(foundReproject);
    }

    SECTION("get_operator_schema returns schema for spectral index") {
        QVariantMap schema = server.testGetOperatorSchema("rs:spectral_index");
        REQUIRE_FALSE(schema.contains("error"));
        CHECK(schema.value("operator_id").toString() == "rs:spectral_index");
        QVariantMap properties = schema.value("properties").toMap();
        REQUIRE(properties.contains("input"));
        REQUIRE(properties.contains("index"));
    }

    SECTION("get_operator_schema returns error for unknown operator") {
        QVariantMap schema = server.testGetOperatorSchema("no:such_operator");
        REQUIRE(schema.contains("error"));
    }
}

TEST_CASE("mcpStatusForTask maps TaskCenter states to MCP status", "[agent][mcp][status]") {
    SECTION("queued/running/paused map to running") {
        for (const sicnu::TaskStatus status : { sicnu::TaskStatus::Queued,
                                                sicnu::TaskStatus::Running,
                                                sicnu::TaskStatus::Paused }) {
            sicnu::AlgorithmTaskInfo info;
            info.taskId = 7;
            info.algorithmId = "rs:spectral_index";
            info.status = status;
            info.progressPercentage = 0.25;
            QVariantMap res = mcpStatusForTask(info);
            REQUIRE(res.value("status").toString() == "running");
            REQUIRE(res.value("progress").toDouble() == 0.25);
        }
    }

    SECTION("completed includes result payload and last log line") {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = 8;
        info.algorithmId = "rs:spectral_index";
        info.status = sicnu::TaskStatus::Completed;
        info.progressPercentage = 1.0;
        info.logBuffer = { QStringLiteral("queued"), QStringLiteral("running"), QStringLiteral("finished") };
        info.resultPayload["output"] = "/tmp/out.tif";
        QVariantMap res = mcpStatusForTask(info);
        REQUIRE(res.value("status").toString() == "completed");
        REQUIRE(res.value("progress").toDouble() == 1.0);
        REQUIRE(res.value("progressText").toString() == "finished");
        QVariantMap result = res.value("result").toMap();
        REQUIRE(result.value("output").toString() == "/tmp/out.tif");
    }

    SECTION("completed with null payload omits result") {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = 9;
        info.status = sicnu::TaskStatus::Completed;
        QVariantMap res = mcpStatusForTask(info);
        REQUIRE(res.value("status").toString() == "completed");
        REQUIRE_FALSE(res.contains("result"));
    }

    SECTION("empty log buffer omits progressText") {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = 10;
        info.status = sicnu::TaskStatus::Running;
        QVariantMap res = mcpStatusForTask(info);
        REQUIRE_FALSE(res.contains("progressText"));
    }

    SECTION("failed includes errorMessage") {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = 11;
        info.status = sicnu::TaskStatus::Failed;
        info.errorMessage = QStringLiteral("boom");
        QVariantMap res = mcpStatusForTask(info);
        REQUIRE(res.value("status").toString() == "failed");
        REQUIRE(res.value("errorMessage").toString() == "boom");
    }

    SECTION("canceled maps to canceled") {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = 12;
        info.status = sicnu::TaskStatus::Canceled;
        QVariantMap res = mcpStatusForTask(info);
        REQUIRE(res.value("status").toString() == "canceled");
    }
}

TEST_CASE("McpServer executes operators through TaskCenter", "[agent][mcp][execute]") {
    registerNoopOperator();
    TestMcpServer server;

    QVariantMap res = server.testExecuteOperator("rs:mcp_noop", QVariantMap());
    REQUIRE(res.value("status").toString() == "running");
    const QString execId = res.value("execution_id").toString();
    REQUIRE(execId.startsWith("task-"));

    const QString terminal = waitForTerminal(server, execId);
    REQUIRE(terminal == "completed");

    QVariantMap status = server.testGetExecutionStatus(execId);
    QVariantMap result = status.value("result").toMap();
    REQUIRE(result.value("output").toString() == "/tmp/mcp_noop.tif");
    REQUIRE(status.value("progress").toDouble() == 1.0);
}

TEST_CASE("McpServer cancel_execution reports the actual status of terminal tasks", "[agent][mcp][cancel]") {
    registerNoopOperator();
    TestMcpServer server;

    // Run a task to completion first.
    QVariantMap res = server.testExecuteOperator("rs:mcp_noop", QVariantMap());
    const QString execId = res.value("execution_id").toString();
    REQUIRE(waitForTerminal(server, execId) == "completed");

    // Cancelling an already-terminal task must report its ACTUAL status,
    // never the old blanket "canceled".
    QVariantMap cancelRes = server.testCancelExecution(execId);
    REQUIRE(cancelRes.value("execution_id").toString() == execId);
    REQUIRE(cancelRes.value("status").toString() == "completed");

    // Malformed and unknown execution ids keep the historical error text.
    for (const QString &badId : { QStringLiteral("not-a-task-id"), QStringLiteral("task-999999") }) {
        try {
            server.testCancelExecution(badId);
            FAIL("expected std::runtime_error for " + badId.toStdString());
        } catch (const std::runtime_error &e) {
            REQUIRE(QString::fromStdString(e.what()).contains("Execution ID not found"));
        }
        try {
            server.testGetExecutionStatus(badId);
            FAIL("expected std::runtime_error for " + badId.toStdString());
        } catch (const std::runtime_error &e) {
            REQUIRE(QString::fromStdString(e.what()).contains("Execution ID not found"));
        }
    }
}

TEST_CASE("McpServer synchronously rejects unknown algorithms and operators", "[agent][mcp][rejection]") {
    TestMcpServer server;

    SECTION("execute_algorithm throws Algorithm not found for unresolvable id") {
        for (const char *id : { "gdal:no_such_algorithm", "processing:gdal:no_such_algorithm", "rs:no_such_algorithm" }) {
            try {
                server.testExecuteAlgorithm(id, QVariantMap());
                FAIL(std::string("expected std::runtime_error for ") + id);
            } catch (const std::runtime_error &e) {
                REQUIRE(QString::fromStdString(e.what()) == QString("Algorithm not found: ") + id);
            }
        }
    }

    SECTION("execute_operator throws Operator not found for unresolvable id") {
        try {
            server.testExecuteOperator("rs:no_such_operator", QVariantMap());
            FAIL("expected std::runtime_error");
        } catch (const std::runtime_error &e) {
            REQUIRE(QString::fromStdString(e.what()) == "Operator not found: rs:no_such_operator");
        }
    }
}

TEST_CASE("McpServer executes qgis processing algorithms to terminal state", "[agent][mcp][execute_algorithm]") {
    if (!QgsApplication::processingRegistry()->providerById("qgis_algorithms"))
    {
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    }

    TestMcpServer server;

    QVariantMap params;
    params["INPUT_LAYERS"] = QStringLiteral("/tmp/input.tif");
    params["EXPRESSION"] = QStringLiteral("A + 10");
    params["OUTPUT"] = QStringLiteral("/tmp/band_math_out.tif");

    QVariantMap res = server.testExecuteAlgorithm("qgis_algorithms:rs_band_math", params);
    REQUIRE(res.value("status").toString() == "running");
    const QString execId = res.value("execution_id").toString();
    REQUIRE(execId.startsWith("task-"));

    const QString terminal = waitForTerminal(server, execId);
    REQUIRE((terminal == "completed" || terminal == "failed"));

    QVariantMap status = server.testGetExecutionStatus(execId);
    REQUIRE(status.value("execution_id").toString() == execId);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sicnu::jobs::JobEngine::instance().shutdown();
}
