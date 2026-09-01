// tests/test_mcp_server.cpp
#include <catch2/catch_test_macros.hpp>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVariantMap>

#include <chrono>
#include <thread>
#include <array>
#include <vector>

#include "agent/mcp_server.h"
#include "data/asset_types.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/derivation_record.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/output_committer.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/rs_operator_registry.h"
#include "agent/interaction_tool_registry.h"
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/otb_tools/provider.h"
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

// Helper subclass of McpServer to expose handlers directly for unit testing
class TestMcpServer : public McpServer
{
public:
    TestMcpServer() : McpServer() {}

    QVariant lastResponseId;
    QVariantMap lastResponseResult;
    QVariant lastErrorId;
    int lastErrorCode = 0;
    QString lastErrorMessage;

    void testHandleRequest(const QVariantMap &req) { handleRequest(req); }

    void sendResponse(const QVariant &id, const QVariantMap &result) override
    {
        lastResponseId = id;
        lastResponseResult = result;
    }

    void sendError(const QVariant &id, int code, const QString &message) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
        lastErrorData.clear();
    }

    void sendError(const QVariant &id, int code, const QString &message, const QVariantMap &data) override
    {
        lastErrorId = id;
        lastErrorCode = code;
        lastErrorMessage = message;
        lastErrorData = data;
    }

    QVariantMap lastErrorData;

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
    QVariantMap testRunWorkflow(const QVariantMap &args) { return handleRunWorkflow(args); }
    QVariantMap testGetWorkflowStatus(long pipelineId) { return handleGetWorkflowStatus(pipelineId); }
    QVariantMap testSpatialToolCall(const QString &name, const QVariantMap &args)
    {
        return handleSpatialToolCall(name, args);
    }
    QVariantMap testGetLineage(const QString &id) { return handleGetLineage(id); }
    QVariantMap testPreflightAlgorithm(const QString &id, const QVariantMap &params)
    {
        return handlePreflightAlgorithm(id, params);
    }
    QVariantMap testListTools(const QString &category = QString(), bool compact = true)
    {
        return handleListTools(category, compact);
    }
    QVariantMap testSearchTools(const QString &query, const QString &group = QString(),
                                const QString &tag = QString(), const QString &inputType = QString(),
                                const QString &outputType = QString())
    {
        return handleSearchTools(query, group, tag, inputType, outputType);
    }
    QVariantMap testSearchAlgorithms(const QString &query, const QString &group = QString(),
                                     const QString &inputType = QString(), const QString &outputType = QString(),
                                     bool largeRasterSafeOnly = false)
    {
        return handleSearchAlgorithms(query, group, inputType, outputType, largeRasterSafeOnly);
    }
    QVariantMap testGetToolSchema(const QString &id) { return handleGetToolSchema(id); }
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
    // Mirror the new operator into AtomicAlgorithmRegistry (dispatcher
    // surface) WITHOUT reset(): reset() drops every provider-based adapter
    // (qgis_algorithms:*, ...) for the rest of the process and broke the
    // later search/schema sections. Register the single noop adapter
    // directly instead.
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    if (!registry.findAdapter("rs:mcp_noop")) {
        auto op = sicnu::operators::RSOperatorRegistry::instance().create("rs:mcp_noop");
        if (op)
            registry.registerAdapter(std::make_shared<sicnu::processing::RsOperatorAdapter>(std::move(op)));
    }
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

namespace {
/// Mirrors production app startup (src/app/main.cpp): the engine registers
/// its provider adapters, which mirror qgis_algorithms:*/gdal_tools:*/*
/// descriptors into AtomicAlgorithmRegistry. Without this the schema and
/// execute_algorithm surfaces see only rs: operators (#620 regression fix;
/// the call was lost when the legacy suites were pruned in the workflow-v2
/// test pass).
void ensureAlgorithmEngineInitialized()
{
    static const bool done = [] {
        sicnu::AlgorithmEngine::instance().initialize();
        // Mirror src/app/main.cpp: bridge the unified registry into the
        // JobEngine fallback so provider algorithms (qgis_algorithms:*,
        // gdal_tools:*, ...) are executable when submitted as jobs.
        sicnu::jobs::JobEngine::instance().setFallbackExecutor(
            []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
                const auto adapter =
                    sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
                if ( !adapter )
                    throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
                sicnu::processing::ProgressCallback progressBridge;
                progressBridge = [&ctx]( int percent, const std::string &message ) {
                    ctx.reportProgress( percent / 100.0, message );
                };
                return adapter->execute( req.params, progressBridge, [&ctx]() { return ctx.isCancelled(); } );
            } );
        return true;
    }();
    (void)done;
}
} // namespace

TEST_CASE("MCP Server tests", "[agent][mcp]") {
    // Register providers if not already registered
    if (!QgsApplication::processingRegistry()->providerById("qgis_algorithms"))
    {
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    }
    ensureAlgorithmEngineInitialized();

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
        // The canonical descriptor serves the algorithm under its registered
        // id; the schema title is the human-readable display name.
        CHECK(schema.value("algorithm_id").toString() == "qgis_algorithms:rs_band_math");
        CHECK_FALSE(schema.value("title").toString().isEmpty());
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

    SECTION("get_operator_schema throws error for unknown operator (#342)") {
        REQUIRE_THROWS_AS(server.testGetOperatorSchema("no:such_operator"), std::runtime_error);
    }

    // Simulate a GUI host: headless hiding (6b64259fbf) only lists
    // Interaction tools that are registered in the runtime registry.
    auto registerGuiHost = []() {
        auto &reg = sicnu::agent::InteractionToolRegistry::instance();
        reg.reset();
        sicnu::agent::InteractionToolDefinition view;
        view.name = "view:get_state";
        view.displayName = "Get view state";
        view.category = "view";
        reg.registerTool(view);
        sicnu::agent::InteractionToolDefinition composite;
        composite.name = "raster:set_band_composite";
        composite.displayName = "Set band composite";
        composite.category = "raster";
        reg.registerTool(composite);
    };

    SECTION("list_tools returns unified schema with category, name, description, schema") {
        registerGuiHost();
        // compact=false embeds the per-entry schemas (#643 default omits them).
        QVariantMap res = server.testListTools(QString(), false);
        QVariantList tools = res.value("tools").toList();
        REQUIRE_FALSE(tools.isEmpty());
        REQUIRE(res.value("count").toInt() == tools.size());

        bool foundProcessing = false;
        bool foundInteraction = false;
        bool foundData = false;

        for (const QVariant &t : tools) {
            QVariantMap tMap = t.toMap();
            REQUIRE(tMap.contains("category"));
            REQUIRE(tMap.contains("name"));
            REQUIRE(tMap.contains("description"));
            REQUIRE(tMap.contains("schema"));

            const QString cat = tMap.value("category").toString();
            if (cat == "Processing") foundProcessing = true;
            if (cat == "Interaction") foundInteraction = true;
            if (cat == "Data") foundData = true;
        }

        CHECK(foundProcessing);
        CHECK(foundInteraction);
        CHECK(foundData);
    }

    SECTION("list_tools defaults to compact: no per-entry schemas, pagination fields present") {
        registerGuiHost();
        QVariantMap res = server.testListTools();
        QVariantList tools = res.value("tools").toList();
        REQUIRE_FALSE(tools.isEmpty());
        CHECK(res.value("compact").toBool());
        CHECK(res.value("count").toInt() == tools.size());
        for (const QVariant &t : tools) {
            QVariantMap tMap = t.toMap();
            REQUIRE(tMap.contains("name"));
            REQUIRE_FALSE(tMap.contains("schema"));
        }
        // The full-schema surface remains reachable via compact=false and
        // per-tool schemas via get_tool_schema.
        QVariantMap full = server.testListTools(QString(), false);
        REQUIRE(full.value("count").toInt() == res.value("count").toInt());
        CHECK(full.value("tools").toList().first().toMap().contains("schema"));
    }

    SECTION("search_tools searches unified catalog by query") {
        registerGuiHost();
        QVariantMap res = server.testSearchTools("show raster");
        QVariantList tools = res.value("tools").toList();
        REQUIRE_FALSE(tools.isEmpty());

        bool foundBandComposite = false;
        for (const QVariant &t : tools) {
            QVariantMap tMap = t.toMap();
            if (tMap.value("name").toString() == "raster:set_band_composite") {
                foundBandComposite = true;
            }
        }
        CHECK(foundBandComposite);
    }

    SECTION("headless MCP hides GUI-only interaction tools when registry is empty") {
        sicnu::agent::InteractionToolRegistry::instance().reset();
        QVariantMap res = server.testListTools();
        QVariantList tools = res.value("tools").toList();
        REQUIRE_FALSE(tools.isEmpty());
        for (const QVariant &t : tools) {
            const QString name = t.toMap().value("name").toString();
            CHECK_FALSE(name.startsWith(QStringLiteral("view:")));
            CHECK_FALSE(name.startsWith(QStringLiteral("roi:")));
            CHECK_FALSE(name.startsWith(QStringLiteral("canvas:")));
            CHECK_FALSE(name.startsWith(QStringLiteral("raster:")));
        }
        // And search cannot surface the hidden GUI tool either.
        QVariantMap sres = server.testSearchTools("show raster");
        QVariantList stools = sres.value("tools").toList();
        for (const QVariant &t : stools) {
            CHECK(t.toMap().value("name").toString() != QStringLiteral("raster:set_band_composite"));
        }
        // Restore a non-empty registry for later sections/tests.
        sicnu::agent::InteractionToolRegistry::instance().reset();
    }

    SECTION("get_tool_schema returns schema for unified tools") {
        QVariantMap schema = server.testGetToolSchema("canvas:draw_roi");
        REQUIRE_FALSE(schema.contains("error"));
        CHECK(schema.value("category").toString() == "Interaction");
        CHECK(schema.value("name").toString() == "canvas:draw_roi");
        QVariantMap propMap = schema.value("schema").toMap().value("properties").toMap();
        REQUIRE(propMap.contains("bbox"));
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
    ensureAlgorithmEngineInitialized();
    if (!QgsApplication::processingRegistry()->providerById("qgis_algorithms"))
    {
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    }

    TestMcpServer server;

    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // #630: the previous version never created the input raster, used an
    // invalid expression ('A + 10' is not band-math syntax), and accepted
    // 'failed' as terminal - it passed purely through the failure path and
    // proved nothing about executing qgis algorithms.
    const QString inputPath = tempDir.filePath("input.tif");
    {
        std::vector<std::vector<float>> bands(2, std::vector<float>(16, 0.0f));
        for (size_t i = 0; i < 16; ++i)
        {
            bands[0][i] = 100.0f + static_cast<float>(i);
            bands[1][i] = 50.0f + static_cast<float>(i);
        }
        std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
        QString writeErr;
        REQUIRE(writeGdalOutput(inputPath, 4, 4, bands, gt, QString(), &writeErr));
    }

    QVariantMap params;
    params["INPUT_LAYERS"] = inputPath;
    params["EXPRESSION"] = QStringLiteral("b1 + 10");
    params["OUTPUT"] = tempDir.filePath("band_math_out.tif");

    QVariantMap res = server.testExecuteAlgorithm("qgis_algorithms:rs_band_math", params);
    REQUIRE(res.value("status").toString() == "running");
    const QString execId = res.value("execution_id").toString();
    REQUIRE(execId.startsWith("task-"));

    const QString terminal = waitForTerminal(server, execId);
    QVariantMap dbgStatus = server.testGetExecutionStatus(execId);
    const long dbgTaskId = execId.mid(QStringLiteral("task-").size()).toLong();
    const auto dbgInfo = sicnu::TaskCenter::instance().getTaskInfo(dbgTaskId);
    INFO("terminal=" << terminal.toStdString()
         << " taskError=" << dbgInfo.errorMessage.toStdString()
         << " taskStatus=" << static_cast<int>(dbgInfo.status));
    REQUIRE(terminal == "completed");
    CHECK(QFile::exists(tempDir.filePath("band_math_out.tif")));

    QVariantMap status = server.testGetExecutionStatus(execId);
    REQUIRE(status.value("execution_id").toString() == execId);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sicnu::jobs::JobEngine::instance().shutdownForTests();
}

TEST_CASE("McpServer describe_dataset exposes semantic band roles", "[agent][mcp][semantic]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // A stacked product-style raster: band 1 NIR, band 2 Red (SICNU_BAND_ROLE).
    const QString path = dir.filePath(QStringLiteral("product.tif"));
    std::vector<std::vector<float>> bands(2, std::vector<float>(4, 100.0f));
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(path, 2, 2, bands, gt, "EPSG:32648", &err));

    GDALDatasetH ds = GDALOpenEx(path.toUtf8().constData(),
                                 GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr);
    REQUIRE(ds != nullptr);
    GDALSetMetadataItem(GDALGetRasterBand(ds, 1), "SICNU_BAND_ROLE", "nir", nullptr);
    GDALSetMetadataItem(GDALGetRasterBand(ds, 2), "SICNU_BAND_ROLE", "red", nullptr);
    GDALClose(ds);

    QgsRasterLayer *layer = new QgsRasterLayer(path, QStringLiteral("product_semantic"));
    REQUIRE(layer->isValid());
    QgsProject::instance()->addMapLayer(layer);

    TestMcpServer server;
    QVariantMap result = server.testDescribeDataset(layer->id());
    REQUIRE(result.contains(QStringLiteral("bands")));
    const QVariantList bandList = result.value(QStringLiteral("bands")).toList();
    REQUIRE(bandList.size() == 2);
    CHECK(bandList[0].toMap().value(QStringLiteral("role")).toString() == QStringLiteral("nir"));
    CHECK(bandList[1].toMap().value(QStringLiteral("role")).toString() == QStringLiteral("red"));
    CHECK(bandList[0].toMap().value(QStringLiteral("index")).toInt() == 1);

    QgsProject::instance()->removeMapLayer(layer);
}

namespace
{

/// Minimal in-memory raster source provider for DataManager lineage tests.
class LineageMemoryProvider final : public sicnu::data::internal::SourceProvider
{
  public:
    bool supports( const sicnu::data::SourceDescriptor &source ) const override
    {
      return source.providerKey == QStringLiteral( "memory-raster" )
             || source.providerKey == QStringLiteral( "gdal" );
    }

    sicnu::data::Result<sicnu::data::internal::ResolvedSource> resolve(
      const sicnu::data::SourceDescriptor &source ) const override
    {
      const sicnu::data::StorageKind storage =
        ( source.providerKey == QStringLiteral( "gdal" ) )
          ? sicnu::data::StorageKind::File
          : sicnu::data::StorageKind::Memory;
      return sicnu::data::Result<sicnu::data::internal::ResolvedSource>::success(
        sicnu::data::internal::ResolvedSource{ sicnu::data::AssetKind::Raster,
                                     sicnu::data::AssetState::Ready,
                                     sicnu::data::AssetCapability::Renderable
                                       | sicnu::data::AssetCapability::ReadablePixels,
                                     storage,
                                     source.canonicalSource } );
    }
};

std::unique_ptr<sicnu::data::DataManager> makeLineageDataManager()
{
  sicnu::data::internal::SourceProviderRegistry providers;
  providers.add( std::make_unique<LineageMemoryProvider>() );
  return providers.createDataManager();
}

sicnu::data::SourceDescriptor memoryRasterSource( const QString &source )
{
  sicnu::data::SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "memory-raster" );
  descriptor.canonicalSource = source;
  return descriptor;
}

} // namespace

TEST_CASE( "McpServer get_lineage exposes provenance and lineage", "[agent][mcp][provenance]" )
{
  using namespace sicnu::data;

  const auto manager = makeLineageDataManager();
  REQUIRE( manager );

  TestMcpServer server;
  server.setDataManager( manager.get() );

  RegisterRequest inputRequest;
  inputRequest.source = memoryRasterSource( QStringLiteral( "scene-raw" ) );
  const auto input = manager->registerSource( inputRequest );
  REQUIRE_FALSE( input.assetId.isNull() );

  RegisterRequest outputRequest;
  outputRequest.source = memoryRasterSource( QStringLiteral( "ndvi-output" ) );
  const auto output = manager->registerSource( outputRequest );
  REQUIRE_FALSE( output.assetId.isNull() );

  DerivationRecord record = makeTaskDerivation(
    QStringLiteral( "rs:spectral_index" ),
    QJsonObject{ { QStringLiteral( "index" ), QStringLiteral( "NDVI" ) } },
    QStringLiteral( "task-42" ) );
  DerivationInput derivedFrom;
  derivedFrom.assetId = input.assetId;
  derivedFrom.revision = AssetRevision::initial();
  record.inputs = { derivedFrom };
  REQUIRE( manager->attachDerivationRecord( output.assetId, record ) );

  SECTION( "Derived asset reports provenance and inputs" )
  {
    const QVariantMap out = server.testGetLineage( output.assetId.toString() );
    CHECK( out.value( QStringLiteral( "id" ) ).toString() == output.assetId.toString() );
    CHECK( out.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "ndvi-output" ) );

    const QVariantMap prov = out.value( QStringLiteral( "provenance" ) ).toMap();
    CHECK( prov.value( QStringLiteral( "algorithmId" ) ).toString()
           == QStringLiteral( "rs:spectral_index" ) );
    CHECK( prov.value( QStringLiteral( "taskReference" ) ).toString() == QStringLiteral( "task-42" ) );
    CHECK( prov.value( QStringLiteral( "parameters" ) ).toMap()
               .value( QStringLiteral( "index" ) ).toString()
           == QStringLiteral( "NDVI" ) );

    const QVariantList inputs = out.value( QStringLiteral( "derivedFrom" ) ).toList();
    REQUIRE( inputs.size() == 1 );
    CHECK( inputs.first().toMap().value( QStringLiteral( "id" ) ).toString()
           == input.assetId.toString() );
    CHECK( out.value( QStringLiteral( "derivedOutputsOf" ) ).toList().isEmpty() );
  }

  SECTION( "Input asset reports its derived outputs" )
  {
    const QVariantMap in = server.testGetLineage( input.assetId.toString() );
    CHECK_FALSE( in.contains( QStringLiteral( "provenance" ) ) );
    const QVariantList outputs = in.value( QStringLiteral( "derivedOutputsOf" ) ).toList();
    REQUIRE( outputs.size() == 1 );
    CHECK( outputs.first().toMap().value( QStringLiteral( "id" ) ).toString()
           == output.assetId.toString() );
  }

  SECTION( "Invalid and unknown asset ids fail with actionable errors" )
  {
    try
    {
      server.testGetLineage( QStringLiteral( "not-an-asset-id" ) );
      FAIL( "expected std::runtime_error for malformed id" );
    }
    catch ( const std::runtime_error &e )
    {
      CHECK( QString::fromStdString( e.what() ).contains( QStringLiteral( "Invalid asset id" ) ) );
    }

    try
    {
      server.testGetLineage( AssetId::generate().toString() );
      FAIL( "expected std::runtime_error for unknown id" );
    }
    catch ( const std::runtime_error &e )
    {
      CHECK( QString::fromStdString( e.what() ).contains( QStringLiteral( "Asset not found" ) ) );
    }
  }
}



TEST_CASE( "OutputCommitter registers a published output on the owning thread", "[agent][mcp][commit]" )
{
  const auto manager = makeLineageDataManager();
  REQUIRE( manager );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString output = dir.filePath( QStringLiteral( "scene.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 10.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( output, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  sicnu::OutputCommitter committer( manager.get() );
  sicnu::AlgorithmOutputRequest request;
  request.kind = sicnu::data::AssetKind::Raster;
  request.tempPath = output;
  request.stablePath = output + QStringLiteral( ".stable.tif" );
  request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
  request.autoLoad = false;
  request.derivation.algorithmId = QStringLiteral( "rs:test" );

  const auto commitResult = committer.commit( request );
  if ( !commitResult )
  {
    INFO( "commit failed: "
          << ( commitResult.diagnostics().isEmpty()
                 ? QStringLiteral( "no diagnostics" )
                 : commitResult.diagnostics().first().message ).toStdString() );
    FAIL( "commit should succeed" );
  }

  bool registered = false;
  for ( const auto &snapshot : manager->assets() )
  {
    if ( snapshot.source().canonicalSource == output + QStringLiteral( ".stable.tif" ) )
      registered = true;
  }
  CHECK( registered );
}

TEST_CASE( "MCP Server protocol lifecycle handshake and meta handlers", "[agent][mcp][lifecycle]" )
{
  TestMcpServer server;

  // 1. initialize request
  QVariantMap initReq;
  initReq[QStringLiteral( "id" )] = 1;
  initReq[QStringLiteral( "method" )] = QStringLiteral( "initialize" );
  QVariantMap initParams;
  initParams[QStringLiteral( "protocolVersion" )] = QStringLiteral( "2024-11-05" );
  initReq[QStringLiteral( "params" )] = initParams;

  server.testHandleRequest( initReq );
  CHECK( server.lastResponseId.toInt() == 1 );
  CHECK( server.lastResponseResult.contains( QStringLiteral( "protocolVersion" ) ) );
  CHECK( server.lastResponseResult.contains( QStringLiteral( "capabilities" ) ) );
  CHECK( server.lastResponseResult.contains( QStringLiteral( "serverInfo" ) ) );
  const QVariantMap serverInfo = server.lastResponseResult.value( QStringLiteral( "serverInfo" ) ).toMap();
  CHECK( serverInfo.value( QStringLiteral( "name" ) ).toString() == QStringLiteral( "exp-rs-mcp" ) );

  // 2. notifications/initialized notification (no error, no response required)
  server.lastErrorId = QVariant();
  QVariantMap notifReq;
  notifReq[QStringLiteral( "method" )] = QStringLiteral( "notifications/initialized" );
  server.testHandleRequest( notifReq );
  CHECK_FALSE( server.lastErrorId.isValid() );

  // 3. ping request
  QVariantMap pingReq;
  pingReq[QStringLiteral( "id" )] = 2;
  pingReq[QStringLiteral( "method" )] = QStringLiteral( "ping" );
  server.testHandleRequest( pingReq );
  CHECK( server.lastResponseId.toInt() == 2 );

  // 4. tools/list request
  QVariantMap toolsListReq;
  toolsListReq[QStringLiteral( "id" )] = 3;
  toolsListReq[QStringLiteral( "method" )] = QStringLiteral( "tools/list" );
  server.testHandleRequest( toolsListReq );
  CHECK( server.lastResponseId.toInt() == 3 );
  CHECK( server.lastResponseResult.contains( QStringLiteral( "tools" ) ) );
  CHECK_FALSE( server.lastResponseResult.value( QStringLiteral( "tools" ) ).toList().isEmpty() );

  // 5. resources/list and prompts/list
  QVariantMap resReq;
  resReq[QStringLiteral( "id" )] = 4;
  resReq[QStringLiteral( "method" )] = QStringLiteral( "resources/list" );
  server.testHandleRequest( resReq );
  CHECK( server.lastResponseId.toInt() == 4 );

  // 6. Unknown method returns -32601
  QVariantMap unknownReq;
  unknownReq[QStringLiteral( "id" )] = 5;
  unknownReq[QStringLiteral( "method" )] = QStringLiteral( "custom/unknown_method" );
  server.testHandleRequest( unknownReq );
  CHECK( server.lastErrorId.toInt() == 5 );
  CHECK( server.lastErrorCode == -32601 );
}

// ---------------------------------------------------------------------------
// ADR 0122 — spatial intelligence layer: run_workflow, get_workflow_status,
// spatial: tool dispatch, and catalog tools in tools/list.
// ---------------------------------------------------------------------------
#include <string>

namespace {

QString createWorkflowGeoJson( const QString &path )
{
    GDALAllRegister();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GeoJSON" );
    if ( !driver )
        return QString();
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    if ( !ds )
        return QString();
    OGRLayer *layer = ds->CreateLayer( "points", nullptr, wkbPoint, nullptr );
    OGRFieldDefn nameField( "name", OFTString );
    layer->CreateField( &nameField );
    for ( int i = 0; i < 2; ++i )
    {
        OGRFeature *feature = OGRFeature::CreateFeature( layer->GetLayerDefn() );
        feature->SetField( "name", ( std::string( "p" ) + std::to_string( i ) ).c_str() );
        OGRPoint point( 116.0 + i, 39.0 );
        feature->SetGeometry( &point );
        layer->CreateFeature( feature );
        OGRFeature::DestroyFeature( feature );
    }
    GDALClose( ( GDALDatasetH )ds );
    return path;
}

} // namespace

TEST_CASE( "McpServer run_workflow executes agent-generated pipelines", "[agent][mcp][workflow]" )
{
    registerNoopOperator();
    TestMcpServer server;

    QVariantMap args;
    args[QStringLiteral( "pipeline" )] = QStringLiteral( R"({
        "id": "agent_pipeline",
        "name": "noop chain",
        "steps": [
            {"id": "s1", "title": "first", "operator": "rs:mcp_noop", "params": {}},
            {"id": "s2", "title": "second", "operator": "rs:mcp_noop", "params": {},
             "inputs": [{"fromStepId": "s1", "fromPort": "output", "toPort": "input"}]}
        ]
    })" );

    const QVariantMap submitted = server.testRunWorkflow( args );
    const long pipelineId = submitted.value( QStringLiteral( "pipeline_id" ) ).toLongLong();
    CHECK( pipelineId >= 0 );

    const QVariantList steps = submitted.value( QStringLiteral( "steps" ) ).toList();
    REQUIRE( steps.size() == 2 );
    CHECK( steps[0].toMap().value( QStringLiteral( "execution_id" ) ).toString()
               .startsWith( QStringLiteral( "task-" ) ) );
    CHECK( steps[1].toMap().value( QStringLiteral( "algorithm_id" ) ).toString()
               == QStringLiteral( "rs:mcp_noop" ) );

    // Poll the aggregate workflow status to a terminal state.
    bool completed = false;
    for ( int attempt = 0; attempt < 600; ++attempt )
    {
        const QVariantMap status = server.testGetWorkflowStatus( pipelineId );
        if ( status.value( QStringLiteral( "isCompleted" ) ).toBool() )
        {
            completed = true;
            const QVariantList statusSteps = status.value( QStringLiteral( "steps" ) ).toList();
            REQUIRE( statusSteps.size() == 2 );
            for ( const QVariant &stepVar : statusSteps )
            {
                CHECK( stepVar.toMap().value( QStringLiteral( "status" ) ).toString()
                           == QStringLiteral( "completed" ) );
            }
            break;
        }
        if ( status.value( QStringLiteral( "isFailed" ) ).toBool() )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    CHECK( completed );

    // Unknown pipeline ids surface a readable error.
    bool threw = false;
    try
    {
        server.testGetWorkflowStatus( 999999 );
    }
    catch ( const std::runtime_error & )
    {
        threw = true;
    }
    CHECK( threw );
}

TEST_CASE( "McpServer run_workflow rejects malformed pipelines", "[agent][mcp][workflow]" )
{
    TestMcpServer server;

    QVariantMap args;
    args[QStringLiteral( "pipeline" )] = QStringLiteral( "{not json" );
    bool threw = false;
    try
    {
        server.testRunWorkflow( args );
    }
    catch ( const std::runtime_error &e )
    {
        threw = true;
        CHECK( QString::fromUtf8( e.what() ).contains( QStringLiteral( "Invalid pipeline" ) ) );
    }
    CHECK( threw );

    QVariantMap missing;
    bool threwMissing = false;
    try
    {
        server.testRunWorkflow( missing );
    }
    catch ( const std::runtime_error & )
    {
        threwMissing = true;
    }
    CHECK( threwMissing );
}

TEST_CASE( "McpServer enforces the SICNU_MCP_WORKSPACE sandbox on every execution entry point", "[agent][mcp][sandbox]" )
{
    registerNoopOperator();
    TestMcpServer server;

    // Save/restore the process env so other test cases are unaffected.
    const QString savedWorkspace = qEnvironmentVariable( "SICNU_MCP_WORKSPACE" );
    struct EnvGuard {
        QString saved;
        ~EnvGuard()
        {
            if ( saved.isEmpty() )
                qunsetenv( "SICNU_MCP_WORKSPACE" );
            else
                qputenv( "SICNU_MCP_WORKSPACE", saved.toUtf8() );
        }
    } envGuard{ savedWorkspace };

    QTemporaryDir workspace;
    QTemporaryDir outside;
    REQUIRE( qputenv( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() ) );

    SECTION( "run_workflow rejects a JSON-text pipeline whose step param is outside" )
    {
        QVariantMap args;
        args[QStringLiteral( "pipeline" )] = QStringLiteral(
            "{\"id\":\"p1\",\"steps\":[{\"id\":\"s1\",\"operator\":\"rs:mcp_noop\","
            "\"params\":{\"output\":\"%1\"}}]}" )
            .arg( outside.filePath( QStringLiteral( "out.tif" ) ) );
        try
        {
            server.testRunWorkflow( args );
            FAIL( "expected PATH_OUTSIDE_WORKSPACE rejection" );
        }
        catch ( const std::runtime_error &e )
        {
            REQUIRE( QString::fromStdString( e.what() )
                         .contains( QStringLiteral( "Path outside SICNU_MCP_WORKSPACE" ) ) );
        }
    }

    SECTION( "run_workflow rejects an out-of-workspace path in a map pipeline" )
    {
        QVariantList steps;
        QVariantMap step;
        step[QStringLiteral( "id" )] = QStringLiteral( "s1" );
        step[QStringLiteral( "operator" )] = QStringLiteral( "rs:mcp_noop" );
        QVariantMap params;
        params[QStringLiteral( "output" )] = outside.filePath( QStringLiteral( "out.tif" ) );
        step[QStringLiteral( "params" )] = params;
        steps.append( step );
        QVariantMap pipeline;
        pipeline[QStringLiteral( "id" )] = QStringLiteral( "p1" );
        pipeline[QStringLiteral( "steps" )] = steps;
        QVariantMap args;
        args[QStringLiteral( "pipeline" )] = pipeline;
        try
        {
            server.testRunWorkflow( args );
            FAIL( "expected PATH_OUTSIDE_WORKSPACE rejection" );
        }
        catch ( const std::runtime_error &e )
        {
            REQUIRE( QString::fromStdString( e.what() )
                         .contains( QStringLiteral( "Path outside SICNU_MCP_WORKSPACE" ) ) );
        }
    }

    SECTION( "run_workflow still accepts in-workspace and relative paths" )
    {
        QVariantList steps;
        QVariantMap step;
        step[QStringLiteral( "id" )] = QStringLiteral( "s1" );
        step[QStringLiteral( "operator" )] = QStringLiteral( "rs:mcp_noop" );
        QVariantMap params;
        params[QStringLiteral( "output" )] = workspace.filePath( QStringLiteral( "out.tif" ) );
        params[QStringLiteral( "scratch" )] = QStringLiteral( "relative/scratch.tif" );
        step[QStringLiteral( "params" )] = params;
        steps.append( step );
        QVariantMap pipeline;
        pipeline[QStringLiteral( "id" )] = QStringLiteral( "p1" );
        pipeline[QStringLiteral( "steps" )] = steps;
        QVariantMap args;
        args[QStringLiteral( "pipeline" )] = pipeline;
        const QVariantMap submitted = server.testRunWorkflow( args );
        REQUIRE( submitted.value( QStringLiteral( "pipeline_id" ) ).toLongLong() >= 0 );
    }

    SECTION( "preflight_algorithm rejects paths outside before algorithm resolution" )
    {
        try
        {
            server.testPreflightAlgorithm( "rs:no_such_algorithm",
                                           { { QStringLiteral( "input" ), outside.filePath( QStringLiteral( "probe.tif" ) ) } } );
            FAIL( "expected PATH_OUTSIDE_WORKSPACE rejection" );
        }
        catch ( const std::runtime_error &e )
        {
            // The sandbox must run BEFORE algorithm resolution: the error is
            // the containment denial, not "Algorithm not found".
            REQUIRE( QString::fromStdString( e.what() )
                         .contains( QStringLiteral( "Path outside SICNU_MCP_WORKSPACE" ) ) );
        }
    }
}

TEST_CASE( "McpServer dispatches spatial: tools and lists them", "[agent][mcp][spatial]" )
{
    QTemporaryDir dir;
    const QString geojsonPath = createWorkflowGeoJson( dir.filePath( "points.geojson" ) );
    REQUIRE( !geojsonPath.isEmpty() );

    TestMcpServer server;

    // tools/call dispatch: full JSON-RPC path for a spatial: tool.
    QVariantMap callReq;
    callReq[QStringLiteral( "id" )] = 42;
    callReq[QStringLiteral( "method" )] = QStringLiteral( "tools/call" );
    QVariantMap callParams;
    callParams[QStringLiteral( "name" )] = QStringLiteral( "spatial:vector_inspect" );
    QVariantMap toolArgs;
    toolArgs[QStringLiteral( "path" )] = geojsonPath;
    callParams[QStringLiteral( "arguments" )] = toolArgs;
    callReq[QStringLiteral( "params" )] = callParams;

    server.testHandleRequest( callReq );
    CHECK( server.lastResponseId.toInt() == 42 );
    CHECK_FALSE( server.lastErrorId.isValid() );
    const QVariantMap callResult = server.lastResponseResult;
    REQUIRE( callResult.contains( QStringLiteral( "content" ) ) );
    const QString payload = callResult.value( QStringLiteral( "content" ) )
                                .toList()
                                .first()
                                .toMap()
                                .value( QStringLiteral( "text" ) )
                                .toString();
    CHECK( payload.contains( QStringLiteral( "\"status\":" ) ) );
    CHECK( payload.contains( QStringLiteral( "featureCount" ) ) );
    CHECK( payload.contains( geojsonPath ) );

    // Missing required parameter surfaces a tool-level error with structured codes.
    QVariantMap badReq = callReq;
    badReq[QStringLiteral( "id" )] = 43;
    QVariantMap badParams;
    badParams[QStringLiteral( "name" )] = QStringLiteral( "spatial:vector_inspect" );
    badReq[QStringLiteral( "params" )] = badParams;
    server.lastErrorId = QVariant();
    server.lastResponseId = QVariant();
    server.testHandleRequest( badReq );
    // Execution failures are MCP tool results with isError:true (#620), not
    // JSON-RPC errors; structured codes ride along in the result object.
    CHECK( server.lastErrorId.isNull() );
    CHECK( server.lastResponseId.toInt() == 43 );
    CHECK( server.lastResponseResult.value( QStringLiteral( "isError" ) ).toBool() );
    const auto badContent = server.lastResponseResult.value( QStringLiteral( "content" ) ).toList();
    REQUIRE( badContent.size() == 1 );
    CHECK( badContent.first().toMap().value( QStringLiteral( "text" ) ).toString().contains( QStringLiteral( "path" ) ) );
    CHECK( server.lastResponseResult.value( QStringLiteral( "errorCode" ) ).toString() == QStringLiteral( "INVALID_PARAMETER" ) );
    CHECK( server.lastResponseResult.value( QStringLiteral( "errorCategory" ) ).toString() == QStringLiteral( "validation" ) );

    // tools/list includes the spatial catalog tools alongside meta tools.
    QVariantMap listReq;
    listReq[QStringLiteral( "id" )] = 44;
    listReq[QStringLiteral( "method" )] = QStringLiteral( "tools/list" );
    server.testHandleRequest( listReq );
    CHECK( server.lastResponseId.toInt() == 44 );
    const QVariantList tools = server.lastResponseResult.value( QStringLiteral( "tools" ) ).toList();
    bool hasRunWorkflow = false, hasRasterInspect = false;
    for ( const QVariant &toolVar : tools )
    {
        const QString name = toolVar.toMap().value( QStringLiteral( "name" ) ).toString();
        if ( name == QStringLiteral( "run_workflow" ) )
            hasRunWorkflow = true;
        if ( name == QStringLiteral( "spatial:raster_inspect" ) )
            hasRasterInspect = true;
    }
    CHECK( hasRunWorkflow );
    CHECK( hasRasterInspect );
}

TEST_CASE( "McpServer::handleSearchAlgorithms performs case-insensitive inputType and outputType matching", "[agent][mcp]" )
{
    if (!QgsApplication::processingRegistry()->providerById("qgis_algorithms"))
    {
        QgsApplication::processingRegistry()->addProvider(new QgisAlgorithmsProvider());
    }
    sicnu::processing::AtomicAlgorithmRegistry::instance().reset();
    TestMcpServer server;

    // Search with lowercase "raster" inputType
    const QVariantMap res = server.testSearchAlgorithms( QString(), QString(), QStringLiteral( "raster" ) );
    const QVariantList results = res.value( QStringLiteral( "algorithms" ) ).toList();
    REQUIRE_FALSE( results.isEmpty() );

    // Verify all returned algorithms have at least one Raster input port
    for ( const QVariant &entryVar : results )
    {
        const QVariantMap entry = entryVar.toMap();
        const QString id = entry.value( QStringLiteral( "id" ) ).toString();
        const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( id.toStdString() );
        REQUIRE( adapter != nullptr );
        bool hasRasterInput = false;
        for ( const auto &port : adapter->descriptor().inputs )
        {
            if ( port.type == sicnu::processing::DataType::Raster )
            {
                hasRasterInput = true;
                break;
            }
        }
        CHECK( hasRasterInput );
    }
}



TEST_CASE( "McpServer data surfaces list and describe committed assets without "
           "QgsProject layers",
           "[agent][mcp][catalog]" )
{
  // A real temp raster registered in the DataManager catalog; no map layer is
  // added to QgsProject (headless shape, #688).
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "scene.tif" ) );
  std::vector<std::vector<float>> bands( 2, std::vector<float>( 4, 5.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( path, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  sicnu::data::DataManager manager;
  sicnu::data::RegisterRequest request;
  request.source.providerKey = QStringLiteral( "gdal" );
  request.source.canonicalSource = path;
  const auto registered = manager.registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  REQUIRE( manager.assets().size() == 1 );

  QgsProject::instance()->removeAllMapLayers();

  TestMcpServer server;
  server.setDataManager( &manager );

  SECTION( "list_layers reports the committed asset" )
  {
    const QVariantMap result = server.testListLayers();
    REQUIRE( result.contains( QStringLiteral( "layers" ) ) );
    const QVariantList layers = result.value( QStringLiteral( "layers" ) ).toList();
    REQUIRE( layers.size() == 1 );
    const QVariantMap layer = layers.first().toMap();
    CHECK( layer.value( QStringLiteral( "assetId" ) ).toString()
           == registered.assetId.toString() );
    CHECK( layer.value( QStringLiteral( "id" ) ).toString()
           == registered.assetId.toString() );
    CHECK( layer.value( QStringLiteral( "name" ) ).toString()
           == QStringLiteral( "scene" ) );
    CHECK( layer.value( QStringLiteral( "path" ) ).toString() == path );
    CHECK( layer.value( QStringLiteral( "type" ) ).toString() == QStringLiteral( "raster" ) );
    CHECK( layer.value( QStringLiteral( "kind" ) ).toString() == QStringLiteral( "raster" ) );
    CHECK( layer.value( QStringLiteral( "revision" ) ).toULongLong() == 1 );
    CHECK( layer.value( QStringLiteral( "bandCount" ) ).toInt() == 2 );
    // This binary has no QgsApplication::initQgis fixture, so the CRS may
    // resolve to an empty authid here; the authid itself is covered by the
    // interaction-tools suite. Structure wiring is what this asserts.
    const QString crs = layer.value( QStringLiteral( "crs" ) ).toString();
    CHECK( ( crs.isEmpty() || crs == QLatin1String( "EPSG:32648" ) ) );
    CHECK_FALSE( layer.contains( QStringLiteral( "displayed" ) ) );
  }

  SECTION( "describe_dataset describes the committed asset by id" )
  {
    const QVariantMap result = server.testDescribeDataset( registered.assetId.toString() );
    CHECK( result.value( QStringLiteral( "assetId" ) ).toString()
           == registered.assetId.toString() );
    CHECK( result.value( QStringLiteral( "width" ) ).toInt() == 2 );
    CHECK( result.value( QStringLiteral( "height" ) ).toInt() == 2 );
    CHECK( result.value( QStringLiteral( "band_count" ) ).toInt() == 2 );
    const QVariantList bandList = result.value( QStringLiteral( "bands" ) ).toList();
    REQUIRE( bandList.size() == 2 );
    CHECK( bandList.first().toMap().value( QStringLiteral( "index" ) ).toInt() == 1 );
    CHECK( bandList.first().toMap().contains( QStringLiteral( "role" ) ) );
  }

  SECTION( "describe_dataset also accepts the asset path" )
  {
    const QVariantMap result = server.testDescribeDataset( path );
    CHECK( result.value( QStringLiteral( "assetId" ) ).toString()
           == registered.assetId.toString() );
  }

  SECTION( "unknown targets still fail with the actionable error" )
  {
    try
    {
      server.testDescribeDataset( QStringLiteral( "no-such-layer" ) );
      FAIL( "expected std::runtime_error for unknown target" );
    }
    catch ( const std::runtime_error &e )
    {
      CHECK( QString::fromStdString( e.what() ).contains( QStringLiteral( "Layer not found" ) ) );
    }
  }

  QgsProject::instance()->removeAllMapLayers();
}

TEST_CASE( "Tool-call output commits stamp derivation input lineage (#698)",
           "[agent][mcp][provenance]" )
{
  using namespace sicnu::data;

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString inputPath = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString outputPath = dir.filePath( QStringLiteral( "out.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 3.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( inputPath, 2, 2, bands, gt, QString(), &err ) );
  REQUIRE( writeGdalOutput( outputPath, 2, 2, bands, gt, QString(), &err ) );

  DataManager manager;
  RegisterRequest inputRequest;
  inputRequest.source.providerKey = QStringLiteral( "gdal" );
  inputRequest.source.canonicalSource = inputPath;
  const auto input = manager.registerSource( inputRequest );
  REQUIRE_FALSE( input.assetId.isNull() );

  sicnu::processing::ToolCallDispatcher dispatcher( {}, {} );
  dispatcher.setDataManager( &manager );

  sicnu::AlgorithmTaskInfo info;
  info.taskId = 77;
  info.algorithmId = QStringLiteral( "rs:spectral_index" );
  info.status = sicnu::TaskStatus::Completed;
  info.outputLayerPath = outputPath;

  SECTION( "a registered input path becomes a derivedFrom edge" )
  {
    info.parameterMap.insert( QStringLiteral( "input" ), inputPath );
    const Json::Value payload = sicnu::processing::ToolCallDispatcher::buildTaskResultPayload(
      info, dispatcher.outputCommitterHandler(), {} );
    REQUIRE( payload["status"].asString() == "success" );
    REQUIRE( payload.isMember( "assetId" ) );

    const auto outputAsset =
      AssetId::fromString( QString::fromStdString( payload["assetId"].asString() ) );
    REQUIRE( outputAsset.has_value() );

    const QVector<AssetId> inputs = manager.derivedFrom( *outputAsset );
    REQUIRE( inputs.size() == 1 );
    CHECK( inputs.first() == input.assetId );
    CHECK( manager.derivedOutputsOf( input.assetId ).first() == *outputAsset );

    const auto provenance = manager.provenance( *outputAsset );
    REQUIRE( provenance.has_value() );
    REQUIRE( provenance->inputs.size() == 1 );
    CHECK( provenance->inputs.first().assetId == input.assetId );
    CHECK( provenance->inputs.first().revision == manager.asset( input.assetId )->revision() );
    CHECK( provenance->unresolvedInputPaths.isEmpty() );
  }

  SECTION( "an unregistered existing input path is recorded, not dropped" )
  {
    info.parameterMap.insert( QStringLiteral( "input" ), inputPath );
    // A second existing file that was never registered as an asset.
    const QString strayPath = dir.filePath( QStringLiteral( "stray.tif" ) );
    REQUIRE( writeGdalOutput( strayPath, 2, 2, bands, gt, QString(), &err ) );
    info.parameterMap.insert( QStringLiteral( "aux_input" ), strayPath );

    const Json::Value payload = sicnu::processing::ToolCallDispatcher::buildTaskResultPayload(
      info, dispatcher.outputCommitterHandler(), {} );
    REQUIRE( payload["status"].asString() == "success" );
    const auto outputAsset =
      AssetId::fromString( QString::fromStdString( payload["assetId"].asString() ) );
    REQUIRE( outputAsset.has_value() );

    // The registered input still resolves; the stray path is preserved in the
    // diagnostics field instead of vanishing.
    const QVector<AssetId> inputs = manager.derivedFrom( *outputAsset );
    REQUIRE( inputs.size() == 1 );
    CHECK( inputs.first() == input.assetId );

    const auto provenance = manager.provenance( *outputAsset );
    REQUIRE( provenance.has_value() );
    REQUIRE( provenance->unresolvedInputPaths.size() == 1 );
    CHECK( provenance->unresolvedInputPaths.first() == strayPath );
  }
}

TEST_CASE( "OutputCommitter re-commit over the stable path advances the revision "
           "and emits assetChanged (#687)",
           "[agent][mcp][commit]" )
{
  const auto manager = makeLineageDataManager();
  REQUIRE( manager );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString stablePath = dir.filePath( QStringLiteral( "scene_committed.tif" ) );

  // First commit registers the stable path at revision 1.
  const QString tempA = dir.filePath( QStringLiteral( "temp_a.tif" ) );
  {
    std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 1.0f ) );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    REQUIRE( writeGdalOutput( tempA, 2, 2, bands, gt, QString(), &err ) );
  }
  sicnu::OutputCommitter committer( manager.get() );
  sicnu::AlgorithmOutputRequest request;
  request.kind = sicnu::data::AssetKind::Raster;
  request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
  request.stablePath = stablePath;
  request.derivation.algorithmId = QStringLiteral( "rs:first" );
  request.derivation.taskReference = QStringLiteral( "task-1" );

  int changeEvents = 0;
  QObject::connect( manager.get(), &sicnu::data::DataManager::assetChanged,
                    [&]( sicnu::data::AssetId ) { ++changeEvents; } );

  {
    request.tempPath = tempA;
    const auto first = committer.commit( request );
    REQUIRE( first );
    const auto snapshot = manager->asset( first.value() );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->revision() == sicnu::data::AssetRevision::initial() );
    CHECK( manager->provenance( first.value() )->algorithmId == QStringLiteral( "rs:first" ) );
    CHECK( changeEvents == 0 );
  }

  // Re-commit: new bytes, same stable path. The asset must not silently keep
  // its stale snapshot: revision bumps and observers are notified.
  const QString tempB = dir.filePath( QStringLiteral( "temp_b.tif" ) );
  {
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 4, 2.0f ) );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    REQUIRE( writeGdalOutput( tempB, 2, 2, bands, gt, QString(), &err ) );
  }
  sicnu::data::AssetId committedId;
  {
    request.tempPath = tempB;
    request.derivation.algorithmId = QStringLiteral( "rs:second" );
    request.derivation.taskReference = QStringLiteral( "task-2" );
    const auto second = committer.commit( request );
    REQUIRE( second );
    committedId = second.value();
  }

  const auto snapshot = manager->asset( committedId );
  REQUIRE( snapshot.has_value() );
  CHECK( manager->assets().size() == 1 );
  CHECK( snapshot->revision() == sicnu::data::AssetRevision::initial().next() );
  // One change from the registerSource update, one from the replaced derivation.
  CHECK( changeEvents == 2 );
  const auto provenance = manager->provenance( committedId );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QStringLiteral( "rs:second" ) );
}
