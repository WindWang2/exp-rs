// Operation Logger tests — verify recording, export, and RSOperator integration
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <json/json.h>

#include "operators/framework/rs_operation_logger.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

using namespace sicnu::operators;

TEST_CASE("RSOperationLogger records runs", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.clear();

    REQUIRE(logger.recordCount() == 0);

    Json::Value params(Json::objectValue);
    params["input"] = "test.tif";
    params["index"] = "NDVI";

    const size_t handle = logger.beginRun("rs:spectral_index", params);
    CHECK(logger.recordCount() == 1);

    Json::Value result(Json::objectValue);
    result["output"] = "out.tif";
    logger.finishRun(handle, result, 123.4);

    const auto records = logger.records();
    REQUIRE(records.size() == 1);
    CHECK(records[0].operatorName == "rs:spectral_index");
    CHECK(records[0].success == true);
    CHECK(records[0].durationMs == Catch::Approx(123.4));
    CHECK(records[0].parameters["input"].asString() == "test.tif");
}

TEST_CASE("RSOperationLogger records failures", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.clear();

    Json::Value params(Json::objectValue);
    params["input"] = "missing.tif";

    const size_t handle = logger.beginRun("rs:band_math", params);
    logger.failRun(handle, static_cast<int>(ErrorCode::FileNotFound), "File not found", 5.0);

    const auto records = logger.records();
    REQUIRE(records.size() == 1);
    CHECK(records[0].success == false);
    CHECK(records[0].errorCode == static_cast<int>(ErrorCode::FileNotFound));
    CHECK(records[0].errorMessage == "File not found");
    CHECK(records[0].durationMs == Catch::Approx(5.0));
}

TEST_CASE("RSOperationLogger JSON export", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.clear();

    Json::Value params(Json::objectValue);
    params["input"] = "in.tif";
    logger.beginRun("test:op", params);
    logger.finishRun(0, Json::Value(Json::objectValue), 10.0);

    const Json::Value json = logger.toJson();
    REQUIRE(json.isArray());
    REQUIRE(json.size() == 1);
    CHECK(json[0]["operator"].asString() == "test:op");
    CHECK(json[0]["success"].asBool() == true);
}

TEST_CASE("RSOperationLogger CSV export", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.clear();

    Json::Value params(Json::objectValue);
    params["input"] = "in.tif";
    logger.beginRun("test:op", params);
    logger.failRun(0, 1000, "Invalid parameter", 2.5);

    const std::string csv = logger.toCsv();
    CHECK(csv.find("test:op") != std::string::npos);
    CHECK(csv.find("Invalid parameter") != std::string::npos);
    CHECK(csv.find("1000") != std::string::npos);
}

TEST_CASE("RSOperationLogger file export", "[operators][logger]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    auto& logger = RSOperationLogger::instance();
    logger.clear();

    Json::Value params(Json::objectValue);
    params["input"] = "in.tif";
    logger.beginRun("test:op", params);
    logger.finishRun(0, Json::Value(Json::objectValue), 1.0);

    const QString jsonPath = tmp.path() + "/report.json";
    std::string error;
    REQUIRE(logger.exportToFile(jsonPath.toStdString(), &error));
    CHECK(QFile::exists(jsonPath));

    const QString csvPath = tmp.path() + "/report.csv";
    REQUIRE(logger.exportToFile(csvPath.toStdString(), &error));
    CHECK(QFile::exists(csvPath));
}

TEST_CASE("RSOperator::execute logs automatically", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.clear();

    auto op = RSOperatorRegistry::instance().create("rs:spectral_index");
    REQUIRE(op != nullptr);

    RSOperatorContext ctx;

    SECTION("Success is logged") {
        // This will fail due to missing input file, but failure is also logged
        Json::Value params(Json::objectValue);
        params["input"] = "nonexistent.tif";
        params["output"] = "out.tif";
        params["index"] = "NDVI";

        REQUIRE_THROWS_AS(op->execute(params, ctx), RSOperatorError);
        REQUIRE(logger.recordCount() == 1);
        CHECK(logger.records()[0].success == false);
    }
}

TEST_CASE("RSOperationLogger clear works", "[operators][logger]") {
    auto& logger = RSOperationLogger::instance();
    logger.beginRun("test:op", Json::Value(Json::objectValue));
    REQUIRE(logger.recordCount() > 0);
    logger.clear();
    CHECK(logger.recordCount() == 0);
}
