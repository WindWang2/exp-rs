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
#include "operators/rs/rs_atmospheric_correction_operator.h"
#include "operators/rs/rs_change_detection_operator.h"
#include "operators/rs/rs_change_primitives.h"
#include "operators/rs/rs_mosaic_operator.h"

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

TEST_CASE( "Dynamic estimates reflect actual working sets (QUAC / mosaic)", "[processing][preflight][estimate]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString rasterA = tmp.path() + "/a.tif";
    const QString rasterB = tmp.path() + "/b.tif";
    {
        std::vector<float> band( 32 * 32, 100.0f );
        std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
        QString err;
        std::vector<std::vector<float>> bands = { band };
        REQUIRE( writeGdalOutput( rasterA, 32, 32, bands, gt, "EPSG:32648", &err ) );
        REQUIRE( writeGdalOutput( rasterB, 32, 32, bands, gt, "EPSG:32648", &err ) );
    }

    // QUAC is full-raster: the dynamic estimate scales with the scene, not the
    // 0.5 MiB tile-streaming fallback.
    {
        RsAtmosphericCorrectionOperator op;
        Json::Value params( Json::objectValue );
        params["method"] = "quac";
        params["input"] = rasterA.toStdString();
        const Json::Value est = op.estimateExecution( params );
        REQUIRE( est["basis"].asString() == "dynamic" );
        // 32x32 x 1 band x 4 B x 2 buffers = 8192 B — small scene, but the
        // point is the basis is dynamic (not the static 512 KiB).
        REQUIRE( est["estimatedRamBytes"].asUInt64() > 0 );
        REQUIRE( est["estimatedRamBytes"].asUInt64() != 524288 );

        // DOS stays tile-streaming (static fallback — the preflight layer
        // annotates basis="static" when consuming; the raw estimate carries
        // the static tile figure).
        Json::Value dosParams( Json::objectValue );
        dosParams["method"] = "dos1";
        dosParams["input"] = rasterA.toStdString();
        const Json::Value dosEst = op.estimateExecution( dosParams );
        REQUIRE( dosEst["estimatedRamBytes"].asUInt64() == 524288 );
    }

    // Mosaic: dynamic estimate includes input buffers + union output buffer.
    {
        RsMosaicOperator op;
        Json::Value params( Json::objectValue );
        Json::Value inputs( Json::arrayValue );
        inputs.append( rasterA.toStdString() );
        inputs.append( rasterB.toStdString() );
        params["inputs"] = inputs;
        const Json::Value est = op.estimateExecution( params );
        REQUIRE( est["basis"].asString() == "dynamic" );
        // 2 inputs x 32x32x4 B + 1 output 32x32x4 B = 12288 B.
        REQUIRE( est["estimatedRamBytes"].asUInt64() == 12288 );
    }
}
