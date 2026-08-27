// tests/test_preflight.cpp
//
// Algorithm preflight: schema validation + dataset probes + compatibility +
// dynamic resource estimate, without executing the algorithm (PLAN →
// PREFLIGHT → EXECUTE contract).
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>
#include <array>

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>

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

void createVsimemRaster( const char *vsiPath )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, vsiPath, 16, 16, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 500000, 30, 0, 4500000, 0, -30 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    {
        OGRSpatialReference srs;
        srs.importFromEPSG( 32648 );
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        if ( wkt )
        {
            GDALSetProjection( ds, wkt );
            CPLFree( wkt );
        }
    }
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( band != nullptr );
    std::vector<float> line( 16, 1.0f );
    for ( int row = 0; row < 16; ++row )
        REQUIRE( GDALRasterIO( band, GF_Write, 0, row, 16, 1, line.data(), 16, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
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

    // Mosaic: streaming tile working set estimate (bounded, O(tile))
    {
        RsMosaicOperator op;
        Json::Value params( Json::objectValue );
        Json::Value inputs( Json::arrayValue );
        inputs.append( rasterA.toStdString() );
        inputs.append( rasterB.toStdString() );
        params["inputs"] = inputs;
        const Json::Value est = op.estimateExecution( params );
        REQUIRE( est["basis"].asString() == "dynamic" );
        REQUIRE( est["tileWidth"].asInt() == 512 );
        REQUIRE( est["tileHeight"].asInt() == 512 );
        REQUIRE( est["estimatedRamBytes"].asUInt64() == 4194304 );
    }
}

// ——— Unified resolver regression tests (GH #560) ———————

TEST_CASE( "preflight does not emit input_not_found for /vsimem raster", "[processing][preflight][vsimem]" )
{
    const char *vsiPath = "/vsimem/preflight_test_raster.tif";
    createVsimemRaster( vsiPath );
    struct Guard { const char *p; ~Guard() { VSIUnlink( p ); } } guard{ vsiPath };

    AlgorithmDescriptor desc;
    desc.id = "stub:vsimem_preflight";
    PortDescriptor port;
    port.name = "input";
    port.type = DataType::Raster;
    port.required = true;
    desc.inputs.push_back( port );
    ContractStubAdapter adapter( "stub:vsimem_preflight", desc );

    Json::Value params( Json::objectValue );
    params["input"] = vsiPath;
    const Json::Value preflight = preflightAdapter( adapter, params );

    // After resolver migration: virtual source is probed via GDALOpenEx, not
    // filesystem existence. Must NOT produce input_not_found.
    const Json::Value issues = preflight["compatibility"]["issues"];
    for ( Json::ArrayIndex i = 0; i < issues.size(); ++i )
        CHECK( issues[i]["code"].asString() != "input_not_found" );

    // Valid raster -> overall preflight is valid (no compatibility blockers).
    // Before migration this would be invalid due to input_not_found; the check
    // documents the intended contract – if the resolver migration is not yet
    // present the assertion will expose the deviation.
    CHECK( preflight["valid"].asBool() == true );
    if ( preflight["datasets"].isMember( "input" ) )
    {
        // Probe should have observed a real raster (width/height present or at
        // least not marked missing). The exact shape is determined by GDAL open.
        const Json::Value dsInfo = preflight["datasets"]["input"];
        // Must not be the "exists=false" filesystem fallback.
        if ( dsInfo.isMember( "exists" ) )
            CHECK( dsInfo["exists"].asBool() == true );
    }
}

TEST_CASE( "preflight reports gdal_open_failed for OGR connection string", "[processing][preflight][resolver]" )
{
    AlgorithmDescriptor desc;
    desc.id = "stub:pg_preflight";
    PortDescriptor port;
    port.name = "input";
    port.type = DataType::Raster;
    port.required = true;
    desc.inputs.push_back( port );
    ContractStubAdapter adapter( "stub:pg_preflight", desc );

    Json::Value params( Json::objectValue );
    params["input"] = "PG:dbname=nonexistent_db_xyz";
    const Json::Value preflight = preflightAdapter( adapter, params );

    const Json::Value issues = preflight["compatibility"]["issues"];
    bool hasGdalOpenFailed = false;
    bool hasInputNotFound = false;
    for ( Json::ArrayIndex i = 0; i < issues.size(); ++i )
    {
        const std::string code = issues[i]["code"].asString();
        if ( code == "gdal_open_failed" ) hasGdalOpenFailed = true;
        if ( code == "input_not_found" ) hasInputNotFound = true;
    }
    // Virtual/connection source that fails to open must surface as
    // gdal_open_failed, not as a plain filesystem missing-file error.
    CHECK( hasGdalOpenFailed );
    CHECK_FALSE( hasInputNotFound );
}

