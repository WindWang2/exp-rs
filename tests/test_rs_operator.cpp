/***************************************************************************
 * test_rs_operator.cpp  —  Unit tests for RSOperator framework
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_schema.h"

#include <QCoreApplication>

#include <atomic>
#include <thread>

using namespace sicnu::operators;
using namespace sicnu::operators::schema;

namespace {

int &rsOpAppArgc()
{
    static int argc = 1;
    return argc;
}
char rsOpAppArgv0[] = "test_rs_operator";
char *rsOpAppArgv[] = {rsOpAppArgv0, nullptr};

void ensureQtApp()
{
    if (!QCoreApplication::instance())
        new QCoreApplication(rsOpAppArgc(), rsOpAppArgv);
}



class TestAddOperator : public RSOperator {
public:
    std::string name() const override { return "test:add"; }
    std::string displayName() const override { return "Add Two Numbers"; }
    std::string group() const override { return "math"; }
    std::string description() const override { return "Adds two numbers."; }

    Json::Value schema() const override {
        Json::Value params(Json::objectValue);
        params["a"] = makeNumberParam("a", "First summand", 0.0);
        params["b"] = makeNumberParam("b", "Second summand", 0.0);

        Json::Value outputs(Json::objectValue);
        outputs["result"] = makeNumberParam("result", "Sum");

        Json::Value root = makeRootSchema(displayName(), description(), params, outputs);
        root["required"] = makeRequired({"a", "b"});
        return root;
    }

    Json::Value metadata() const override {
        Json::Value meta = RSOperator::metadata();
        meta["tags"].append("math");
        meta["purpose"] = "Demonstrate RSOperator interface";
        return meta;
    }

    Json::Value run(const Json::Value& params, RSOperatorContext& context) override {
        if (!params.isMember("a") || !params.isMember("b")) {
            throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                                  "Parameters 'a' and 'b' are required");
        }
        if (!params["a"].isNumeric() || !params["b"].isNumeric()) {
            throw RSOperatorError(ErrorCode::TypeMismatch,
                                  "Parameters 'a' and 'b' must be numeric");
        }

        context.throwIfCancelled();
        context.logInfo("Starting addition");
        context.reportProgress(0.5, "Computing sum");

        const double a = params["a"].asDouble();
        const double b = params["b"].asDouble();

        context.reportProgress(1.0, "Done");

        Json::Value result(Json::objectValue);
        result["result"] = a + b;
        return result;
    }
};

class TestCancelOperator : public RSOperator {
public:
    std::string name() const override { return "test:cancel"; }

    Json::Value run(const Json::Value&, RSOperatorContext& context) override {
        for (int i = 0; i < 100; ++i) {
            context.throwIfCancelled();
            context.reportProgress(i / 100.0);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return Json::Value(Json::objectValue);
    }
};

} // anonymous namespace

TEST_CASE("RSOperator base interface works", "[rsoperator]") {
    TestAddOperator op;

    REQUIRE(op.name() == "test:add");
    REQUIRE(op.displayName() == "Add Two Numbers");
    REQUIRE(op.group() == "math");

    Json::Value schema = op.schema();
    REQUIRE(schema["title"].asString() == "Add Two Numbers");
    REQUIRE(schema["properties"].isMember("a"));
    REQUIRE(schema["properties"].isMember("b"));
    REQUIRE(schema["properties"]["a"]["type"].asString() == "number");

    Json::Value meta = op.metadata();
    REQUIRE(meta["name"].asString() == "test:add");
    REQUIRE(meta["purpose"].asString() == "Demonstrate RSOperator interface");
}

TEST_CASE("RSOperator run returns expected result", "[rsoperator]") {
    ensureQtApp();
    TestAddOperator op;
    RSOperatorContext ctx;

    Json::Value params(Json::objectValue);
    params["a"] = 2.0;
    params["b"] = 3.0;

    Json::Value result = op.run(params, ctx);
    REQUIRE_THAT(result["result"].asDouble(), Catch::Matchers::WithinAbs(5.0, 1e-9));
}

TEST_CASE("RSOperator missing parameter throws typed error", "[rsoperator]") {
    ensureQtApp();
    TestAddOperator op;
    RSOperatorContext ctx;

    Json::Value params(Json::objectValue);
    params["a"] = 1.0;

    try {
        op.run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::MissingRequiredParameter);
        REQUIRE(e.toJson()["codeName"].asString() == "MissingRequiredParameter");
    }
}

TEST_CASE("RSOperator type mismatch throws typed error", "[rsoperator]") {
    ensureQtApp();
    TestAddOperator op;
    RSOperatorContext ctx;

    Json::Value params(Json::objectValue);
    params["a"] = "not a number";
    params["b"] = 2.0;

    try {
        op.run(params, ctx);
        FAIL("Expected RSOperatorError");
    } catch (const RSOperatorError& e) {
        REQUIRE(e.code() == ErrorCode::TypeMismatch);
    }
}

TEST_CASE("RSOperatorContext progress callback is invoked", "[rsoperator]") {
    ensureQtApp();
    TestAddOperator op;
    RSOperatorContext ctx;

    double lastProgress = -1.0;
    std::string lastMessage;
    ctx.setProgressCallback([&](double p, const std::string& msg) {
        lastProgress = p;
        lastMessage = msg;
    });

    Json::Value params(Json::objectValue);
    params["a"] = 1.0;
    params["b"] = 2.0;

    op.run(params, ctx);

    REQUIRE_THAT(lastProgress, Catch::Matchers::WithinAbs(1.0, 1e-9));
    REQUIRE(!lastMessage.empty());
}

TEST_CASE("RSOperatorContext log callback is invoked", "[rsoperator]") {
    ensureQtApp();
    TestAddOperator op;
    RSOperatorContext ctx;

    std::string capturedLevel;
    std::string capturedMsg;
    ctx.setLogCallback([&](const std::string& msg, const std::string& level) {
        capturedMsg = msg;
        capturedLevel = level;
    });

    Json::Value params(Json::objectValue);
    params["a"] = 1.0;
    params["b"] = 2.0;

    op.run(params, ctx);

    REQUIRE(capturedLevel == "info");
    REQUIRE(capturedMsg == "Starting addition");
}

TEST_CASE("RSOperator cancellation works from another thread", "[rsoperator]") {
    ensureQtApp();
    TestCancelOperator op;
    RSOperatorContext ctx;
    std::atomic<bool> cancelFlag{false};
    ctx.setCancelFlag(&cancelFlag);

    std::thread runner([&]() {
        try {
            op.run(Json::Value(Json::objectValue), ctx);
            FAIL("Expected cancellation");
        } catch (const RSOperatorError& e) {
            REQUIRE(e.code() == ErrorCode::Cancelled);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cancelFlag.store(true);
    runner.join();
}

TEST_CASE("RSOperatorContext tempPath generates unique paths", "[rsoperator]") {
    ensureQtApp();
    RSOperatorContext ctx;
    std::string p1 = ctx.tempPath(".tif");
    std::string p2 = ctx.tempPath(".tif");

    REQUIRE(!p1.empty());
    REQUIRE(!p2.empty());
    REQUIRE(p1 != p2);
    REQUIRE(p1.find(".tif") != std::string::npos);
}

TEST_CASE("RSOperatorRegistry registers and creates operators", "[rsoperator]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();

    REQUIRE(!reg.hasOperator("test:registered"));
    reg.registerOperator("test:registered", []() -> std::unique_ptr<RSOperator> {
        return std::make_unique<TestAddOperator>();
    });
    REQUIRE(reg.hasOperator("test:registered"));

    auto op = reg.create("test:registered");
    REQUIRE(op != nullptr);
    REQUIRE(op->name() == "test:add");

    REQUIRE(reg.create("test:nonexistent") == nullptr);
}

TEST_CASE("RSOperatorRegistry listSchemas returns array", "[rsoperator]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();
    reg.registerOperator("test:list_schema", []() -> std::unique_ptr<RSOperator> {
        return std::make_unique<TestAddOperator>();
    });

    Json::Value schemas = reg.listSchemas();
    REQUIRE(schemas.isArray());
    REQUIRE(schemas.size() > 0);
}

// Verify the REGISTER_RS_OPERATOR macro performs static registration.
REGISTER_RS_OPERATOR(TestAddOperator, "test:macro_registered")

TEST_CASE("REGISTER_RS_OPERATOR macro registers operator at static init", "[rsoperator]") {
    RSOperatorRegistry& reg = RSOperatorRegistry::instance();
    REQUIRE(reg.hasOperator("test:macro_registered"));
    auto op = reg.create("test:macro_registered");
    REQUIRE(op != nullptr);
    REQUIRE(op->name() == "test:add");
}

TEST_CASE("Schema helpers produce expected shapes", "[rsoperator]") {
    Json::Value strParam = makeStringParam("path", "Input path", "/default");
    REQUIRE(strParam["type"].asString() == "string");
    REQUIRE(strParam["default"].asString() == "/default");

    Json::Value enumParam = makeEnumParam("mode", "Mode", {"a", "b", "c"}, "a");
    REQUIRE(enumParam["enum"].isArray());
    REQUIRE(enumParam["enum"].size() == 3);

    Json::Value rasterParam = makeRasterParam("input", "Input raster");
    REQUIRE(rasterParam["format"].asString() == "raster");

    Json::Value numParam = makeNumberParam("threshold", "Threshold", 0.5);
    setRange(numParam, 0.0, 1.0);
    REQUIRE_THAT(numParam["minimum"].asDouble(), Catch::Matchers::WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(numParam["maximum"].asDouble(), Catch::Matchers::WithinAbs(1.0, 1e-9));
}

// ---------------------------------------------------------------------------
// Large-raster memory policy (A6)
// ---------------------------------------------------------------------------

TEST_CASE( "memoryPolicyName maps every policy to a stable id", "[operators][framework][memory]" )
{
  CHECK( std::string( memoryPolicyName( RSOperatorMemoryPolicy::Streaming ) )
         == "streaming" );
  CHECK( std::string( memoryPolicyName( RSOperatorMemoryPolicy::MultiPassStreaming ) )
         == "multipass_streaming" );
  CHECK( std::string( memoryPolicyName( RSOperatorMemoryPolicy::FullRaster ) )
         == "full_raster" );
  CHECK( std::string( memoryPolicyName( RSOperatorMemoryPolicy::ExternalProcess ) )
         == "external_process" );
  CHECK( std::string( memoryPolicyName( RSOperatorMemoryPolicy::UnsupportedForLargeRaster ) )
         == "unsupported_for_large_raster" );
}

TEST_CASE( "Every registered operator declares a valid memory policy", "[operators][framework][memory]" )
{
  auto &registry = RSOperatorRegistry::instance();
  const auto names = registry.operatorNames();
  REQUIRE_FALSE( names.empty() );

  const auto validPolicy = []( const std::string &p ) {
    return p == "streaming" || p == "multipass_streaming" || p == "full_raster"
           || p == "external_process" || p == "unsupported_for_large_raster";
  };

  for ( const auto &name : names )
  {
    auto op = registry.create( name );
    REQUIRE( op != nullptr );
    CHECK( validPolicy( memoryPolicyName( op->memoryPolicy() ) ) );
  }

  // Spot checks: streaming / multi-pass / external-process / full-raster.
  CHECK( registry.create( "rs:radiometric_calibration" )->memoryPolicy()
         == RSOperatorMemoryPolicy::Streaming );
  CHECK( registry.create( "rs:atmospheric_correction" )->memoryPolicy()
         == RSOperatorMemoryPolicy::MultiPassStreaming );
  CHECK( registry.create( "gdal:orthorectification" )->memoryPolicy()
         == RSOperatorMemoryPolicy::Streaming );
  CHECK( registry.create( "otb:meanshift_segmentation" )->memoryPolicy()
         == RSOperatorMemoryPolicy::ExternalProcess );
  CHECK( registry.create( "rs:spectral_index" )->memoryPolicy()
         == RSOperatorMemoryPolicy::FullRaster );
}

// ---------------------------------------------------------------------------
// Determinism grade (ADR 0124)
// ---------------------------------------------------------------------------

TEST_CASE( "determinismGradeName maps every grade to a stable id", "[operators][framework][determinism]" )
{
  CHECK( std::string( determinismGradeName( RSOperatorDeterminism::BitExact ) )
         == "bit_exact" );
  CHECK( std::string( determinismGradeName( RSOperatorDeterminism::Tolerance ) )
         == "tolerance" );
}

TEST_CASE( "Every registered operator declares a determinism grade", "[operators][framework][determinism]" )
{
  auto &registry = RSOperatorRegistry::instance();
  const auto names = registry.operatorNames();
  REQUIRE_FALSE( names.empty() );

  const auto validGrade = []( const std::string &g ) {
    return g == "bit_exact" || g == "tolerance";
  };

  for ( const auto &name : names )
  {
    auto op = registry.create( name );
    REQUIRE( op != nullptr );
    CHECK( validGrade( determinismGradeName( op->determinism() ) ) );
  }

  // The default grade is the serial baseline: bit-exact. Operators that
  // adopt parallel floating-point reductions must override explicitly
  // (a schema-visible, reviewable event per ADR 0124).
  CHECK( registry.create( "rs:spectral_index" )->determinism()
         == RSOperatorDeterminism::BitExact );
  CHECK( registry.create( "rs:change_difference" )->determinism()
         == RSOperatorDeterminism::BitExact );
}

