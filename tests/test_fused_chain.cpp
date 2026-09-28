// test_fused_chain.cpp — Phase C intermediate materialization elimination:
// chain planning rules, adapter fail-closed behavior, and BIT-FOR-BIT
// equivalence of the fused executor against the real operators
// (rs:spectral_index NDVI → rs:threshold_raster) on identical inputs.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/fused_chain.h"
#include "processing/framework/task_center.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "workflow/workflow_definition.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace sicnu::processing;
using namespace sicnu::workflow;

namespace
{
float lcgFloat( uint32_t &state )
{
    state = state * 1103515245u + 12345u;
    return static_cast<float>( state >> 8 ) / static_cast<float>( 0xFFFFFFu ) * 1000.0f;
}

void writeInputRaster( const QString &path, int width, int height )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, width, height, 4, GDT_Float32, gt,
                                        QStringLiteral( "EPSG:4326" ) );
    REQUIRE( ds != nullptr );
    const size_t pixels = static_cast<size_t>( width ) * height;
    std::vector<float> buf( pixels );
    for ( int b = 1; b <= 4; ++b )
    {
        uint32_t st = 0x12345678u + static_cast<uint32_t>( b ) * 9781u;
        for ( size_t i = 0; i < pixels; ++i )
            buf[i] = lcgFloat( st );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                               buf.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
}

StepDef makeNdviStep( const std::string &id, const std::string &input,
                      const std::string &output, bool withEdge = false,
                      const std::string &fromStep = {} )
{
    StepDef s;
    s.id = id;
    s.kind = StepKind::Operator;
    s.operatorId = "rs:spectral_index";
    s.params["input"] = input;
    s.params["output"] = output;
    s.params["index"] = "NDVI";
    s.params["nir"] = 1;
    s.params["red"] = 2;
    if ( withEdge )
    {
        StepConnection c;
        c.fromStepId = fromStep;
        c.fromPort = "output";
        c.toPort = "input";
        s.inputs.push_back( c );
    }
    return s;
}

StepDef makeThresholdStep( const std::string &id, const std::string &input,
                           const std::string &output, double threshold = 0.3,
                           bool withEdge = true, const std::string &fromStep = {} )
{
    StepDef s;
    s.id = id;
    s.kind = StepKind::Operator;
    s.operatorId = "rs:threshold_raster";
    s.params["input"] = input;
    s.params["output"] = output;
    s.params["threshold"] = threshold;
    if ( withEdge )
    {
        StepConnection c;
        c.fromStepId = fromStep;
        c.fromPort = "output";
        c.toPort = "input";
        s.inputs.push_back( c );
    }
    return s;
}

/// Runs a registered operator directly (no TaskCenter admission).
Json::Value runOperator( const std::string &operatorId, const Json::Value &params,
                         sicnu::operators::RSOperatorContext &ctx )
{
    sicnu::operators::RSOperatorRegistry::instance(); // call_once chain
    sicnu::operators::rs::installRsOperatorProvider();
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
    REQUIRE( op != nullptr );
    return op->run( params, ctx );
}

/// Reads band 1 of a byte raster into a vector.
std::vector<uint8_t> readByteBand( const QString &path, int *width, int *height )
{
    ensureGdalInit();
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds != nullptr );
    *width = ds->GetRasterXSize();
    *height = ds->GetRasterYSize();
    std::vector<uint8_t> out( static_cast<size_t>( *width ) * *height );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, *width, *height, out.data(),
                                               *width, *height, GDT_Byte, 0, 0, nullptr )
             == CE_None );
    GDALClose( ds );
    return out;
}
} // namespace

TEST_CASE( "planFusedChain detects a linear NDVI→threshold chain", "[fused_chain][plan]" )
{
    WorkflowDefinition def;
    def.id = "fusable";
    def.steps = {
        makeNdviStep( "ndvi", "/data/in.tif", "/data/ndvi.tif" ),
        makeThresholdStep( "thr", "$ndvi.output", "/data/final.tif", 0.3, true, "ndvi" ),
    };
    const FusedChainPlan plan = planFusedChain( def );
    REQUIRE( plan.stepIds.size() == 2 );
    REQUIRE( plan.headStepId == "ndvi" );
    REQUIRE( plan.tailStepId == "thr" );
    REQUIRE( plan.inputPath == "/data/in.tif" );
    REQUIRE( plan.stages.size() == 2 );
    REQUIRE( plan.stages[0].headInputBands == std::vector<int>{ 1, 2 } );
    REQUIRE( plan.stages[1].tailOutputDtype == GDT_Byte );
}

TEST_CASE( "planFusedChain refuses unsupported operators and fan-out", "[fused_chain][plan]" )
{
    // Placeholder input at the head: nothing fusable (head needs a real path).
    WorkflowDefinition placeholder;
    placeholder.id = "placeholder";
    placeholder.steps = {
        makeNdviStep( "ndvi", "$upstream.output", "/data/ndvi.tif" ),
        makeThresholdStep( "thr", "$ndvi.output", "/data/final.tif", 0.3, true, "ndvi" ),
    };
    REQUIRE( planFusedChain( placeholder ).stepIds.empty() );

    // Unsupported operator in the chain position.
    WorkflowDefinition unsupported;
    unsupported.id = "unsupported";
    StepDef unsupportedOp;
    unsupportedOp.id = "ndvi";
    unsupportedOp.kind = StepKind::Operator;
    unsupportedOp.operatorId = "rs:atmospheric_correction"; // no fused adapter
    unsupportedOp.params["input"] = "/data/in.tif";
    unsupportedOp.params["output"] = "/data/mid.tif";
    unsupported.steps = {
        unsupportedOp,
        makeThresholdStep( "thr", "$ndvi.output", "/data/final.tif", 0.3, true, "ndvi" ),
    };
    REQUIRE( planFusedChain( unsupported ).stepIds.empty() );

    // Fan-out: two consumers of the head's output split the chain.
    WorkflowDefinition fanout;
    fanout.id = "fanout";
    fanout.steps = {
        makeNdviStep( "ndvi", "/data/in.tif", "/data/ndvi.tif" ),
        makeThresholdStep( "thr1", "$ndvi.output", "/data/final1.tif", 0.3, true, "ndvi" ),
        makeThresholdStep( "thr2", "$ndvi.output", "/data/final2.tif", 0.5, true, "ndvi" ),
    };
    REQUIRE( planFusedChain( fanout ).stepIds.empty() );

    // Adapter fail-closed: NDVI without explicit band numbers (role resolution
    // would need catalog metadata) is not fused.
    WorkflowDefinition roles;
    roles.id = "roles";
    StepDef roleNdvi;
    roleNdvi.id = "ndvi";
    roleNdvi.kind = StepKind::Operator;
    roleNdvi.operatorId = "rs:spectral_index";
    roleNdvi.params["input"] = "/data/in.tif";
    roleNdvi.params["output"] = "/data/ndvi.tif";
    roleNdvi.params["index"] = "NDVI"; // no nir/red
    roles.steps = {
        roleNdvi,
        makeThresholdStep( "thr", "$ndvi.output", "/data/final.tif", 0.3, true, "ndvi" ),
    };
    REQUIRE( planFusedChain( roles ).stepIds.empty() );
}

namespace
{
struct FusedEquivalenceFixture
{
    QTemporaryDir dir;
    QString input;
    int width = 333; // non-tile-aligned to exercise edge clamping
    int height = 217;

    FusedEquivalenceFixture()
    {
        // QCoreApplication stores POINTERS to argc/argv (it does not copy
        // them); locals here died at scope exit, so the first deep Qt path
        // (QSettings via QgsApplication::members()) dereferenced dead stack
        // and SIGSEGV'd (issue #1356). The rest of the suite pins the same
        // fixture with static storage — match it.
        static int argc = 1;
        static char arg0[] = "test_fused_chain";
        static char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );
        input = dir.filePath( "in.tif" );
        writeInputRaster( input, width, height );
    }
};
} // namespace

TEST_CASE( "fused NDVI→threshold is bit-identical to the real operator chain",
           "[fused_chain][equivalence]" )
{
    FusedEquivalenceFixture fx;

    // 1) Unfused reference: run the REAL operators sequentially.
    const QString refNdvi = fx.dir.filePath( "ref_ndvi.tif" );
    const QString refFinal = fx.dir.filePath( "ref_final.tif" );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value ndviParams;
    ndviParams["input"] = fx.input.toStdString();
    ndviParams["output"] = refNdvi.toStdString();
    ndviParams["index"] = "NDVI";
    ndviParams["nir"] = 1;
    ndviParams["red"] = 2;
    runOperator( "rs:spectral_index", ndviParams, ctx );
    Json::Value thrParams;
    thrParams["input"] = refNdvi.toStdString();
    thrParams["output"] = refFinal.toStdString();
    thrParams["threshold"] = 0.3;
    const Json::Value refThrPayload = runOperator( "rs:threshold_raster", thrParams, ctx );

    // 2) Fused: plan against the same logical chain and execute directly.
    WorkflowDefinition def;
    def.id = "equivalence";
    def.steps = {
        makeNdviStep( "ndvi", fx.input.toStdString(), fx.dir.filePath( "fused_ndvi.tif" ).toStdString() ),
        makeThresholdStep( "thr", "$ndvi.output", fx.dir.filePath( "fused_final.tif" ).toStdString(),
                           0.3, true, "ndvi" ),
    };
    const FusedChainPlan plan = planFusedChain( def );
    REQUIRE( plan.stepIds.size() == 2 );
    sicnu::operators::RSOperatorContext fusedCtx;
    const Json::Value fusedPayload = executeFusedChain( plan, fusedCtx );
    REQUIRE( fusedPayload["output"].asString()
             == fx.dir.filePath( "fused_final.tif" ).toStdString() );
    REQUIRE( fusedPayload["fused"].size() == 2 );
    // The head's intermediate output was NOT materialized.
    REQUIRE_FALSE( QFile( fx.dir.filePath( "fused_ndvi.tif" ) ).exists() );

    // 3) Bit-for-bit output comparison.
    int rw = 0, rh = 0, fw = 0, fh = 0;
    const std::vector<uint8_t> refBytes = readByteBand( refFinal, &rw, &rh );
    const std::vector<uint8_t> fusedBytes = readByteBand(
        fx.dir.filePath( "fused_final.tif" ), &fw, &fh );
    REQUIRE( rw == fw );
    REQUIRE( rh == fh );
    size_t mismatches = 0;
    for ( size_t i = 0; i < refBytes.size(); ++i )
        if ( refBytes[i] != fusedBytes[i] )
            ++mismatches;
    INFO( "mask mismatches: " << mismatches );
    REQUIRE( mismatches == 0 );

    // 4) Payload contract parity: the fused tail must report the same
    // run-derived values as the unfused operator (identical masks ⇒ identical
    // counts; thresholdUsed must survive the float round trip identically).
    REQUIRE( fusedPayload["maskedPixels"].asUInt64() == refThrPayload["maskedPixels"].asUInt64() );
    REQUIRE( fusedPayload["totalPixels"].asUInt64() == refThrPayload["totalPixels"].asUInt64() );
    REQUIRE( fusedPayload["maskedPercent"].asDouble() == refThrPayload["maskedPercent"].asDouble() );
    REQUIRE( fusedPayload["thresholdUsed"].asDouble() == refThrPayload["thresholdUsed"].asDouble() );

    // 5) Metadata parity: pixel values, NoData declaration, geotransform/CRS
    // and the change-family metadata items must all match (bit-identical in
    // the wide sense — the manifest is part of the product).
    {
        ensureGdalInit();
        GDALDataset *refDs = static_cast<GDALDataset *>(
            GDALOpen( refFinal.toUtf8().constData(), GA_ReadOnly ) );
        GDALDataset *fusedDs = static_cast<GDALDataset *>(
            GDALOpen( fx.dir.filePath( "fused_final.tif" ).toUtf8().constData(), GA_ReadOnly ) );
        REQUIRE( refDs != nullptr );
        REQUIRE( fusedDs != nullptr );
        double refGt[6] = {};
        double fusedGt[6] = {};
        REQUIRE( refDs->GetGeoTransform( refGt ) == CE_None );
        REQUIRE( fusedDs->GetGeoTransform( fusedGt ) == CE_None );
        for ( int i = 0; i < 6; ++i )
            REQUIRE( refGt[i] == fusedGt[i] );
        REQUIRE( std::string( refDs->GetProjectionRef() )
                 == std::string( fusedDs->GetProjectionRef() ) );
        REQUIRE( refDs->GetRasterBand( 1 )->GetNoDataValue()
                 == fusedDs->GetRasterBand( 1 )->GetNoDataValue() );
        const char *refMethod = refDs->GetMetadataItem( "SICNU_CHANGE_METHOD", "" );
        const char *fusedMethod = fusedDs->GetMetadataItem( "SICNU_CHANGE_METHOD", "" );
        REQUIRE( refMethod != nullptr );
        REQUIRE( fusedMethod != nullptr );
        REQUIRE( std::string( refMethod ) == std::string( fusedMethod ) );
        const char *refThreshold = refDs->GetMetadataItem( "SICNU_CHANGE_THRESHOLD", "" );
        const char *fusedThreshold = fusedDs->GetMetadataItem( "SICNU_CHANGE_THRESHOLD", "" );
        REQUIRE( refThreshold != nullptr );
        REQUIRE( fusedThreshold != nullptr );
        REQUIRE( std::string( refThreshold ) == std::string( fusedThreshold ) );
        GDALClose( refDs );
        GDALClose( fusedDs );
    }
}

TEST_CASE( "TaskCenter completes fused members with the tail payload",
           "[fused_chain][task_center]" )
{
    // Per-call env read: enable before this submission only.
    qputenv( "SICNU_FUSED_CHAIN", "1" );
    struct EnvReset
    {
        ~EnvReset() { qunsetenv( "SICNU_FUSED_CHAIN" ); }
    } envReset;

    // Same #1356 contract as the fixture above: static argc/argv —
    // QCoreApplication keeps the pointers, it never copies them.
    static int argc = 1;
    static char arg0[] = "test_fused_chain";
    static char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    QTemporaryDir dir;
    const QString input = dir.filePath( "in.tif" );
    writeInputRaster( input, 128, 128 );

    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();
    engine.setMaxWorkers( 2 );
    sicnu::TaskCenter::instance().shutdownForTests();
    sicnu::operators::RSOperatorRegistry::instance();
    sicnu::operators::rs::installRsOperatorProvider();
    sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

    WorkflowDefinition def;
    def.id = "fused_tc";
    def.steps = {
        makeNdviStep( "ndvi", input.toStdString(), dir.filePath( "tc_ndvi.tif" ).toStdString() ),
        makeThresholdStep( "thr", "$ndvi.output", dir.filePath( "tc_final.tif" ).toStdString(),
                           0.3, true, "ndvi" ),
    };

    auto &center = sicnu::TaskCenter::instance();
    const long pipelineId = center.submitPipeline( def, /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto pipeline = center.waitForPipeline( pipelineId, std::chrono::minutes( 5 ) );
    REQUIRE_FALSE( pipeline.isFailed );
    REQUIRE( pipeline.isCompleted );
    for ( const auto &statusEntry : pipeline.stepStatuses )
        REQUIRE( statusEntry == sicnu::TaskStatus::Completed );

    // Tail output exists; head's intermediate does not.
    REQUIRE( QFile( dir.filePath( "tc_final.tif" ) ).exists() );
    REQUIRE_FALSE( QFile( dir.filePath( "tc_ndvi.tif" ) ).exists() );

    // Members carry the fused marker payload.
    for ( const long taskId : pipeline.stepToTaskId )
    {
        const auto info = center.getTaskInfo( taskId );
        REQUIRE( info.resultPayload.isMember( "output" ) );
        if ( info.stepId == QStringLiteral( "ndvi" ) )
            REQUIRE( info.resultPayload["fused"].size() == 2 );
    }
}
