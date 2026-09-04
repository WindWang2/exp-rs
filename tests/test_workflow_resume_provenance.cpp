// tests/test_workflow_resume_provenance.cpp — cross-crash-boundary provenance
// E2E (#727, in-process).
//
// A run crashes after N steps; the resuming process builds a BRAND-NEW
// DataManager. The provenance contract:
//   checkpoint-served asset registration + derivation FIRST,
//   then fresh resumed outputs — so every step's derivation record carries
//   real (assetId, revision) input edges with unresolvedInputPaths empty and
//   the actual substituted parameters, and the record identifies the
//   workflow/run/step — checked against the DataManager content, not logs.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <array>

#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "jobs/job_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include "cli/rs_pipeline_runner.h"

using namespace sicnu::workflow;

namespace {

void ensureApp()
{
    int argc = 1;
    static char arg0[] = "test_workflow_resume_provenance";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );
}

void writeTwoBandRaster( const QString &path, int W, int H )
{
    ::ensureGdalInit();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<size_t>( W ) * H, 100.0f );
    for ( int b = 1; b <= 2; ++b )
    {
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
}

struct ProvenanceFixture
{
    QTemporaryDir homeDir;   // checkpoint directory lives here
    QTemporaryDir workDir;
    sicnu::data::DataManager dataManager;
    QString inputPath;
    QString aPath;
    QString bPath;
    QString cPath;

    ProvenanceFixture()
    {
        ensureApp();
        REQUIRE( homeDir.isValid() );
        REQUIRE( workDir.isValid() );

        inputPath = workDir.filePath( "input.tif" );
        aPath = workDir.filePath( "a.tif" );
        bPath = workDir.filePath( "b.tif" );
        cPath = workDir.filePath( "c.tif" );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );

        WorkflowRunCoordinator::instance().setCheckpointDirectory(
            homeDir.filePath( "checkpoints" ) );
    }

    /// Registers the no-op executors; step "out" of @a outputs materializes a
    /// real raster when its executor runs (like a real algorithm would).
    void installExecutors( const std::string &prefix,
                           const QSet<QString> &materializes = {} )
    {
        auto &engine = sicnu::jobs::JobEngine::instance();
        const std::array<std::string, 3> steps = { "a", "b", "c" };
        for ( const auto &step : steps )
        {
            const QString outPath = workDir.filePath(
                QStringLiteral( "%1.tif" ).arg( QString::fromStdString( step ) ) );
            engine.registerExecutor(
                prefix + ":" + step,
                [this, outPath, materializes]( const sicnu::jobs::JobRequest &,
                                               sicnu::operators::RSOperatorContext & ) {
                    if ( materializes.contains( outPath ) )
                        writeTwoBandRaster( outPath, 32, 32 );
                    Json::Value r( Json::objectValue );
                    r["output"] = outPath.toStdString();
                    return r;
                } );
        }
    }

    /// Registers @a path as a TaskTemporary asset (the catalog entry an input
    /// raster needs so downstream lineage resolves to an (assetId, revision)).
    void registerRaster( const QString &path )
    {
        sicnu::data::SourceDescriptor source;
        source.providerKey = QStringLiteral( "gdal" );
        source.canonicalSource = path;
        sicnu::data::RegisterRequest request;
        request.source = source;
        request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
        request.notifyUpdateOnReuse = true;
        REQUIRE( !dataManager.registerSource( request ).assetId.isNull() );
    }

    /// Handcrafts the checkpoint of a crashed a→b→c run whose FIRST @a done
    /// steps completed pre-crash (their artifacts exist on disk). Declaration
    /// order is reversed on purpose — #727's DAG contract.
    std::string seedCrashedRun( const std::string &prefix, int done, int crashDuringStep )
    {
        const auto port = []( const QString &path ) {
            Json::Value p( Json::objectValue );
            p["output"] = path.toStdString();
            return p;
        };

        WorkflowDefinition def;
        def.id = prefix + "_def";
        def.title = "Provenance resume";

        // a: real input path; b/c: placeholders into the previous output.
        StepDef sa;
        sa.id = "a";
        sa.kind = StepKind::Operator;
        sa.operatorId = prefix + ":a";
        sa.params["input"] = inputPath.toStdString();
        sa.params["output"] = aPath.toStdString();

        StepDef sb;
        sb.id = "b";
        sb.kind = StepKind::Operator;
        sb.operatorId = prefix + ":b";
        sb.params["input"] = "$a.output";
        sb.params["output"] = bPath.toStdString();
        sb.inputs.push_back( StepConnection{ "a", "output", "input" } );

        StepDef sc;
        sc.id = "c";
        sc.kind = StepKind::Operator;
        sc.operatorId = prefix + ":c";
        sc.params["input"] = "$b.output";
        sc.params["output"] = cPath.toStdString();
        sc.inputs.push_back( StepConnection{ "b", "output", "input" } );

        // REVERSED declaration order: [c, b, a].
        def.steps = { sc, sb, sa };

        const std::string runId = prefix + "_run";
        WorkflowRun run;
        run.setDefinition( def );
        REQUIRE( run.setRunId( runId ) );
        run.forceSetState( WorkflowRunState::Running );

        std::vector<StepPlan> plans;
        StepPlan pa;
        pa.stepId = "a";
        pa.operatorId = prefix + ":a";
        pa.resolvedParams = sa.params;
        pa.rawParams = sa.params;
        StepPlan pb;
        pb.stepId = "b";
        pb.operatorId = prefix + ":b";
        pb.rawParams = sb.params;
        pb.resolvedParams = sb.params; // raw copy, as createFromDefinition leaves it
        StepPlan pc;
        pc.stepId = "c";
        pc.operatorId = prefix + ":c";
        pc.rawParams = sc.params;
        pc.resolvedParams = sc.params;

        pa.status = "Completed";
        pa.outputLayerPath = aPath.toStdString();
        pa.resultPayload = port( aPath );
        pb.status = done >= 2 ? "Completed" : ( crashDuringStep == 1 ? "Pending" : "Running" );
        pb.outputLayerPath = done >= 2 ? bPath.toStdString() : std::string();
        if ( done >= 2 )
            pb.resultPayload = port( bPath );
        pc.status = "Running";
        plans = { pa, pb, pc };
        run.setStepPlans( plans );

        WorkflowCheckpointManager checkpoints;
        REQUIRE( false == checkpoints.saveCheckpoint( run, homeDir.filePath( "checkpoints" ) )
                              .isEmpty() );
        return runId;
    }
};

} // namespace

TEST_CASE( "resumed run records cross-boundary lineage with resolved inputs "
           "(crash after step 2) (#727)",
           "[workflow][v2][recovery][provenance][e2e]" )
{
    ProvenanceFixture fx;
    const std::string prefix = "prov2";

    // Pre-crash world: the input and the artifacts of a and b exist as files.
    writeTwoBandRaster( fx.inputPath, 32, 32 );
    writeTwoBandRaster( fx.aPath, 32, 32 );
    writeTwoBandRaster( fx.bPath, 32, 32 );
    REQUIRE_FALSE( QFile::exists( fx.cPath ) );
    // The pipeline's input raster is a catalog asset (as in production,
    // where inputs are registered before/at first use).
    fx.registerRaster( fx.inputPath );

    const std::string runId = fx.seedCrashedRun( prefix, /*done=*/2, /*crashDuringStep=*/2 );

    sicnu::cli::RsPipelineRunner runner;
    runner.setAssetRegistry( &fx.dataManager );
    fx.installExecutors( prefix, { fx.cPath } );

    const auto result = runner.resumeRun( runId );
    if ( !result.success )
        INFO( "resume error: " << result.errorMessage );
    REQUIRE( result.success );

    auto &dm = fx.dataManager;

    // All three outputs are registered assets — a/b from the checkpoint-served
    // loop, c from the fresh loop.
    const auto aAsset = dm.findByPath( fx.aPath );
    const auto bAsset = dm.findByPath( fx.bPath );
    const auto cAsset = dm.findByPath( fx.cPath );
    REQUIRE( aAsset.has_value() );
    REQUIRE( bAsset.has_value() );
    REQUIRE( cAsset.has_value() );

    // Cross-boundary edge: the FRESH step c consumed the CHECKPOINT-SERVED
    // step b's output — the derivation must carry b's (assetId, revision).
    const auto cDerivation = dm.provenance( cAsset->id() );
    REQUIRE( cDerivation.has_value() );
    CHECK( cDerivation->inputs.size() == 1 );
    if ( !cDerivation->inputs.isEmpty() )
    {
        CHECK( cDerivation->inputs.front().assetId == bAsset->id() );
        CHECK( cDerivation->inputs.front().revision.value() >= 1 );
    }
    CHECK( cDerivation->unresolvedInputPaths.isEmpty() );
    CHECK( cDerivation->parameters["input"].toString().toStdString() == fx.bPath.toStdString() );
    CHECK( cDerivation->workflowRunId.toStdString() == runId );
    CHECK( cDerivation->stepId == "c" );
    CHECK( cDerivation->algorithmId == prefix + ":c" );
    CHECK_FALSE( cDerivation->taskReference.isEmpty() );

    // The CHECKPOINT-SERVED mid-chain step b keeps its own lineage too: its
    // parameters were substituted at resume time, so its record resolves the
    // a→b edge (this used to come out silently empty: inputs=[] AND
    // unresolvedInputPaths=[] — #727 RC4).
    const auto bDerivation = dm.provenance( bAsset->id() );
    REQUIRE( bDerivation.has_value() );
    CHECK( bDerivation->inputs.size() == 1 );
    if ( !bDerivation->inputs.isEmpty() )
        CHECK( bDerivation->inputs.front().assetId == aAsset->id() );
    CHECK( bDerivation->unresolvedInputPaths.isEmpty() );
    CHECK( bDerivation->parameters["input"].toString().toStdString() == fx.aPath.toStdString() );
    CHECK( bDerivation->taskReference.toStdString() == "resumed:" + runId + ":b" );
    CHECK( bDerivation->stepId == "b" );

    // ... and a anchors the chain on the real input raster.
    const auto aDerivation = dm.provenance( aAsset->id() );
    REQUIRE( aDerivation.has_value() );
    CHECK( aDerivation->inputs.size() == 1 );
    CHECK( aDerivation->unresolvedInputPaths.isEmpty() );
    CHECK( aDerivation->stepId == "a" );
}

TEST_CASE( "resumed run records lineage when the crash hit right after step 1 (#727)",
           "[workflow][v2][recovery][provenance][e2e]" )
{
    ProvenanceFixture fx;
    const std::string prefix = "prov1";

    writeTwoBandRaster( fx.inputPath, 32, 32 );
    writeTwoBandRaster( fx.aPath, 32, 32 );
    REQUIRE_FALSE( QFile::exists( fx.bPath ) );
    REQUIRE_FALSE( QFile::exists( fx.cPath ) );
    fx.registerRaster( fx.inputPath );

    const std::string runId = fx.seedCrashedRun( prefix, /*done=*/1, /*crashDuringStep=*/1 );

    sicnu::cli::RsPipelineRunner runner;
    runner.setAssetRegistry( &fx.dataManager );
    fx.installExecutors( prefix, { fx.bPath, fx.cPath } );

    const auto result = runner.resumeRun( runId );
    if ( !result.success )
        INFO( "resume error: " << result.errorMessage );
    REQUIRE( result.success );

    auto &dm = fx.dataManager;
    const auto aAsset = dm.findByPath( fx.aPath );
    const auto bAsset = dm.findByPath( fx.bPath );
    const auto cAsset = dm.findByPath( fx.cPath );
    REQUIRE( aAsset.has_value() );
    REQUIRE( bAsset.has_value() );
    REQUIRE( cAsset.has_value() );

    // Fresh b consumed checkpoint-served a.
    const auto bDerivation = dm.provenance( bAsset->id() );
    REQUIRE( bDerivation.has_value() );
    CHECK( bDerivation->inputs.size() == 1 );
    if ( !bDerivation->inputs.isEmpty() )
        CHECK( bDerivation->inputs.front().assetId == aAsset->id() );
    CHECK( bDerivation->unresolvedInputPaths.isEmpty() );
    CHECK( bDerivation->parameters["input"].toString().toStdString() == fx.aPath.toStdString() );

    // Fresh c consumed fresh b.
    const auto cDerivation = dm.provenance( cAsset->id() );
    REQUIRE( cDerivation.has_value() );
    CHECK( cDerivation->inputs.size() == 1 );
    if ( !cDerivation->inputs.isEmpty() )
        CHECK( cDerivation->inputs.front().assetId == bAsset->id() );
    CHECK( cDerivation->unresolvedInputPaths.isEmpty() );
}
