// tests/test_mcp_server.cpp
#include <catch2/catch_test_macros.hpp>
#include <QJsonObject>
#include <QJsonDocument>
#include <QVariantMap>

#include "agent/mcp_server.h"
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
    QVariantMap testGetExecutionStatus(const QString &id) { return handleGetExecutionStatus(id); }
};

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
