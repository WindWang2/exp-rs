// tests/test_agent_golden_workflow.cpp
//
// Agent-style integration contract (no real LLM): discover → inspect schema →
// preflight → execute → consume structured output → compose into the next
// algorithm → query provenance. Validates the agent contract end-to-end.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <array>

#include "processing/framework/algorithm_preflight.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/schema_validator.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/rs_operation_logger.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_change_primitives.h"
#include "operators/rs/rs_threshold_raster_operator.h"

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

namespace {

void writeRaster( const QString &path, int width, int height, const std::vector<float> &values )
{
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    QString err;
    std::vector<std::vector<float>> bands = { values };
    REQUIRE( writeGdalOutput( path, width, height, bands, gt, "EPSG:32648", &err ) );
}

} // namespace

TEST_CASE( "Agent golden workflow: discover→schema→preflight→execute→compose→provenance", "[agent][golden]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // --- 1. DISCOVER: the canonical registry exposes the spectral/change
    // primitives with real outputs. ----------------------------------------
    auto &registry = AtomicAlgorithmRegistry::instance();
    registry.initialize();
    const auto descriptors = registry.listDescriptors();

    const AlgorithmDescriptor *diffDesc = nullptr;
    const AlgorithmDescriptor *thresholdDesc = nullptr;
    for ( const auto &d : descriptors )
    {
        if ( d.id == "rs:change_difference" ) diffDesc = &d;
        else if ( d.id == "rs:threshold_raster" ) thresholdDesc = &d;
    }
    REQUIRE( diffDesc != nullptr );
    REQUIRE( thresholdDesc != nullptr );

    // --- 2. INSPECT SCHEMA: inputs are machine-readable and output is a
    // real raster port. ------------------------------------------------------
    REQUIRE( diffDesc->toInputSchema()["properties"].isMember( "before" ) );
    bool hasRasterOutput = false;
    for ( const auto &out : diffDesc->outputs )
        if ( out.name == "output" && out.type == DataType::Raster )
            hasRasterOutput = true;
    REQUIRE( hasRasterOutput );

    // --- 3. PREFLIGHT: build a raster pair, validate without executing. ----
    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    const QString magPath = tmp.path() + "/mag.tif";
    const QString maskPath = tmp.path() + "/mask.tif";

    std::vector<float> before( 16 * 16, 100.0f );
    std::vector<float> after( 16 * 16, 100.0f );
    for ( int i = 0; i < 4; ++i ) after[i] = 140.0f; // 4 hot pixels
    writeRaster( beforePath, 16, 16, before );
    writeRaster( afterPath, 16, 16, after );

    Json::Value diffParams( Json::objectValue );
    diffParams["before"] = beforePath.toStdString();
    diffParams["after"] = afterPath.toStdString();
    diffParams["output"] = magPath.toStdString();

    const Json::Value preflight = preflightAlgorithm( "rs:change_difference", diffParams );
    REQUIRE( preflight["valid"].asBool() == true );
    REQUIRE( preflight["datasets"]["before"]["bandCount"].asInt() == 1 );

    // --- 4. EXECUTE: run the atomic primitive through the registry adapter. -
    const auto diffAdapter = registry.findAdapter( "rs:change_difference" );
    REQUIRE( diffAdapter != nullptr );
    const Json::Value diffResult = diffAdapter->execute( diffParams );
    REQUIRE( diffResult.isMember( "output" ) );
    REQUIRE( diffResult["output"].asString() == magPath.toStdString() );
    REQUIRE( QFile::exists( magPath ) );

    // --- 5. CONSUME STRUCTURED OUTPUT: numeric auxiliaries. -----------------
    REQUIRE( diffResult["method"].asString() == "difference" );
    // 4 pixels of diff 40, 252 pixels of diff 0 → mean = 160/256 = 0.625.
    REQUIRE( diffResult["mean"].asDouble() == Catch::Approx( 0.625 ) );

    // --- 6. COMPOSE: feed the change magnitude into rs:threshold_raster. ---
    Json::Value thresholdParams( Json::objectValue );
    thresholdParams["input"] = magPath.toStdString();
    thresholdParams["output"] = maskPath.toStdString();
    thresholdParams["threshold"] = 20.0;
    thresholdParams["thresholdMethod"] = "manual";

    const Json::Value thresholdPreflight =
        preflightAlgorithm( "rs:threshold_raster", thresholdParams );
    REQUIRE( thresholdPreflight["valid"].asBool() == true );

    const auto thresholdAdapter = registry.findAdapter( "rs:threshold_raster" );
    REQUIRE( thresholdAdapter != nullptr );
    const Json::Value maskResult = thresholdAdapter->execute( thresholdParams );
    REQUIRE( maskResult["maskedPixels"].asUInt64() == 4 );
    REQUIRE( QFile::exists( maskPath ) );

    // --- 7. PROVENANCE: the execution is recorded and queryable. ------------
    auto &logger = RSOperationLogger::instance();
    logger.clear();
    {
        // op->execute() (non-virtual wrapper) records through the logger; the
        // adapter path records the same records via execute().
        auto op = RSOperatorRegistry::instance().create( "rs:change_difference" );
        REQUIRE( op != nullptr );
        RSOperatorContext ctx;
        op->execute( diffParams, ctx );
    }
    const Json::Value records = logger.toJson();
    REQUIRE( records.isArray() );
    REQUIRE( records.size() == 1 );
    REQUIRE( records[0]["operator"].asString() == "rs:change_difference" );
    REQUIRE( records[0]["success"].asBool() == true );
    REQUIRE( records[0]["parameters"]["before"].asString() == beforePath.toStdString() );
    REQUIRE( records[0]["result"]["output"].asString() == magPath.toStdString() );
}
