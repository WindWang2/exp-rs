// tests/test_preflight.cpp
//
// Algorithm preflight: schema validation + dataset probes + compatibility +
// dynamic resource estimate, without executing the algorithm (PLAN →
// PREFLIGHT → EXECUTE contract).
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <array>

#include "processing/framework/algorithm_preflight.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/rs/rs_change_detection_operator.h"
#include "operators/rs/rs_change_primitives.h"

using namespace sicnu::processing;
using namespace sicnu::operators;
using namespace sicnu::operators::rs;

namespace {

/// Stub adapter with a caller-controlled descriptor for preflight contract tests.
class ContractStubAdapter : public AtomicAlgorithmAdapter
{
public:
    explicit ContractStubAdapter( std::string id, AlgorithmDescriptor desc )
      : mId( std::move( id ) )
      , mDesc( std::move( desc ) ) {}

    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override { return mDesc; }
    Json::Value execute( const Json::Value &, ProgressCallback, std::function<bool()> ) override
    {
        return Json::Value( Json::objectValue );
    }

private:
    std::string mId;
    AlgorithmDescriptor mDesc;
};

void writeRaster( const QString &path, int width, int height, float value )
{
    std::vector<float> band( static_cast<size_t>( width ) * height, value );
    std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
    QString err;
    std::vector<std::vector<float>> bands = { band };
    REQUIRE( writeGdalOutput( path, width, height, bands, gt, "EPSG:32648", &err ) );
}

} // namespace

TEST_CASE( "preflightAlgorithm validates a real raster pair", "[processing][preflight]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString beforePath = tmp.path() + "/before.tif";
    const QString afterPath = tmp.path() + "/after.tif";
    writeRaster( beforePath, 16, 16, 100.0f );
    writeRaster( afterPath, 16, 16, 120.0f );

    Json::Value params( Json::objectValue );
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = ( tmp.path() + "/out.tif" ).toStdString();
    params["method"] = "difference";

    const Json::Value preflight = preflightAlgorithm( "rs:change_difference", params );

    REQUIRE( preflight["valid"].asBool() == true );
    REQUIRE( preflight["algorithmId"].asString() == "rs:change_difference" );
    REQUIRE( preflight["schemaValidation"]["valid"].asBool() == true );
    REQUIRE( preflight["datasets"]["before"]["exists"].asBool() == true );
    REQUIRE( preflight["datasets"]["before"]["width"].asInt() == 16 );
    REQUIRE( preflight["datasets"]["before"]["bandCount"].asInt() == 1 );
    REQUIRE( preflight["compatibility"]["ok"].asBool() == true );
    // Dynamic estimate from the real band count (tile*bands*4*3).
    REQUIRE( preflight["resources"]["basis"].asString() == "dynamic" );
    REQUIRE( preflight["resources"]["estimatedRamBytes"].asUInt64() > 0 );
    // Streaming single-band primitive is large-raster safe.
    REQUIRE( preflight["metadata"]["largeRasterSafe"].asBool() == true );
}

TEST_CASE( "preflightAlgorithm reports schema and file blockers", "[processing][preflight]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    Json::Value params( Json::objectValue );
    params["after"] = ( tmp.path() + "/after.tif" ).toStdString();
    params["output"] = ( tmp.path() + "/out.tif" ).toStdString();

    const Json::Value preflight = preflightAlgorithm( "rs:change_difference", params );

    REQUIRE( preflight["valid"].asBool() == false );
    REQUIRE_FALSE( preflight["blockers"].empty() );
    REQUIRE( preflight["schemaValidation"]["errors"][0]["code"].asString() == "missing_required" );

    // Nonexistent input file → input_not_found compatibility issue.
    Json::Value withFiles = params;
    withFiles["before"] = ( tmp.path() + "/missing.tif" ).toStdString();
    const Json::Value preflight2 = preflightAlgorithm( "rs:change_difference", withFiles );
    REQUIRE( preflight2["datasets"]["before"]["exists"].asBool() == false );
}

TEST_CASE( "preflightAdapter checks same-grid and radiometric contracts", "[processing][preflight]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString rasterA = tmp.path() + "/a.tif";
    const QString rasterB = tmp.path() + "/b.tif";
    const QString rasterC = tmp.path() + "/c.tif";
    writeRaster( rasterA, 16, 16, 1.0f );
    writeRaster( rasterB, 8, 8, 1.0f );   // different grid
    writeRaster( rasterC, 16, 16, 2.0f );

    AlgorithmDescriptor desc;
    desc.id = "stub:samegrid";

    PortDescriptor inA;
    inA.name = "a";
    inA.type = DataType::Raster;
    inA.required = true;
    inA.rsContract["dataKind"] = "raster";
    inA.rsContract["gridRelation"] = "same-grid";
    inA.rsContract["radiometricState"].append( "toa_reflectance" );
    desc.inputs.push_back( inA );

    PortDescriptor inB;
    inB.name = "b";
    inB.type = DataType::Raster;
    inB.required = true;
    inB.rsContract["dataKind"] = "raster";
    inB.rsContract["gridRelation"] = "same-grid";
    desc.inputs.push_back( inB );

    ContractStubAdapter adapter( "stub:samegrid", desc );

    // Same-grid mismatch is reported.
    Json::Value mismatched( Json::objectValue );
    mismatched["a"] = rasterA.toStdString();
    mismatched["b"] = rasterB.toStdString();
    Json::Value preflight = preflightAdapter( adapter, mismatched );
    REQUIRE( preflight["valid"].asBool() == false );
    REQUIRE( preflight["compatibility"]["ok"].asBool() == false );
    REQUIRE( preflight["compatibility"]["issues"][0]["code"].asString() == "grid_mismatch" );

    // Matching grid passes.
    Json::Value matched( Json::objectValue );
    matched["a"] = rasterA.toStdString();
    matched["b"] = rasterC.toStdString();
    preflight = preflightAdapter( adapter, matched );
    REQUIRE( preflight["valid"].asBool() == true );
}
