// test_swarm_fix_experiment.cpp — swarm-review 2026-11-04 EXP-series
// regressions (SWARM_REVIEW.md): the reproduction-bundle importer dropped the
// software_revision pin (EXP-1), a benchmark definition with empty protocol
// pins was writable but not readable (EXP-2), the across-seeds summary sort
// ignored classCode (EXP-3), ignoreLabels only bound the truth side (EXP-4),
// a Failed bridge event constructed and then discarded its workflow evidence
// (EXP-5), and the KMeans accuracy path fell back to class 0 where the Apply
// path falls back to the unclassified sentinel (EXP-6).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "experiment/benchmark_compare.h"
#include "experiment/benchmark_definition.h"
#include "experiment/benchmark_runner.h"
#include "experiment/benchmark_service.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"
#include "experiment/run_bridge.h"
#include "experiment/run_recorder.h"

#include <gdal_priv.h>

#include "rs_classification_pipeline.h"
#include "rs_classifier_backend.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStringList>

#include <vector>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

// --- shared fixtures ---------------------------------------------------------

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    QJsonObject parameters;
    parameters.insert( QStringLiteral( "bands" ), QJsonArray{ 1, 2, 3 } );
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "rf" ) );
    run.setParameters( parameters );
    run.setDatasetFingerprint( QStringLiteral( "df1" ) );
    run.setSplitFingerprint( QStringLiteral( "sf1" ) );
    run.setSeed( 42 );
    return run;
}

BenchmarkDefinition makeDefinition( const QString &benchmarkId )
{
    BenchmarkDefinition def;
    def.setBenchmarkId( benchmarkId );
    def.setBenchmarkVersion( 1 );
    def.setName( QStringLiteral( "swarm EXP definition" ) );
    def.setTaskFamily( BenchmarkTaskFamily::Classification );
    def.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    def.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    def.metricNames() = QStringList{ QStringLiteral( "overall_accuracy" ) };
    def.protocol().setSubset( QStringLiteral( "test" ) );
    def.setRefusePseudoLabelsInTest( false );
    def.setSeedPolicy( 42 );
    return def;
}

BenchmarkTruth truthRow( const QString &id, const QString &label )
{
    BenchmarkTruth truth;
    truth.sampleId = id;
    truth.truthClass = label;
    truth.labelSource = AnnotationSourceType::Human;
    return truth;
}

BenchmarkPrediction predictionRow( const QString &id, const QString &label )
{
    BenchmarkPrediction prediction;
    prediction.sampleId = id;
    prediction.predictedClass = label;
    return prediction;
}

/// EXP-6 stub: a remapping backend whose cluster ids are NOT a function of
/// the input, so the test can place a held-out sample in a cluster the
/// training set never produced — the exact shape that made the hard-coded
/// fallback observable.
class RemapStubBackend : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &X, const cv::Mat &y ) override
    {
        (void)y;
        m_fittedRows = X.rows;
        m_fitted = !X.empty();
        return m_fitted;
    }

    cv::Mat predict( const cv::Mat &X ) const override
    {
        cv::Mat out( X.rows, 1, CV_32S );
        for ( int i = 0; i < X.rows; ++i )
        {
            int cluster = 1;
            if ( X.rows == m_fittedRows )
                // Training pattern: first half cluster 2, second half cluster 1.
                cluster = ( i < m_fittedRows / 2 ) ? 2 : 1;
            else if ( X.rows == 2 )
                // Held-out pattern: row 1 lands in the unmapped cluster 2.
                cluster = ( i == 0 ) ? 1 : 2;
            out.at<int>( i, 0 ) = cluster;
        }
        return out;
    }

    QString name() const override { return QStringLiteral( "remap-stub" ); }
    bool isFitted() const override { return m_fitted; }
    bool needsLabelRemap() const override { return true; }

  private:
    int m_fittedRows = 0;
    bool m_fitted = false;
};

} // namespace

// --- EXP-1 -------------------------------------------------------------------

TEST_CASE( "EXP-1: reproduction bundle round-trip restores the software_revision pin",
           "[swarm][experiment][reproduction][exp1]" )
{
    // The exporter writes software.json and folds software_revision into the
    // execution fingerprint; the importer never read it back, so every run
    // with a pinned revision was refused by its own identity gate ("identity
    // pins do not reproduce the recorded execution fingerprint"). The
    // production path left the revision empty, which is why the round-trip
    // suites passed while the gate was dead for real bundles.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-swarm-exp1" ) );
    experiment.setName( QStringLiteral( "swarm EXP-1" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-exp1-1" ),
                                 QStringLiteral( "exp-swarm-exp1" ) );
    run.setSoftwareRevision( QStringLiteral( "git-rev-exp1" ) );
    run.setStatus( RunStatus::Created );
    REQUIRE( store.upsertRun( run ).has_value() );

    DatasetStore datasets;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "ds.db" ) ) ) );
    ReproductionBundleExporter exporter( store, datasets );
    ReproductionBundleOptions exportOptions;
    exportOptions.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
    exportOptions.currentSoftwareRevision = QStringLiteral( "git-rev-exporter" );
    const auto exportReport = exporter.exportRun( run.runId(), exportOptions );
    INFO( exportReport.warnings.join( QStringLiteral( " | " ) ).toStdString() );
    REQUIRE( exportReport.ok );

    ExperimentStore replay;
    REQUIRE( replay.open( dir.filePath( QStringLiteral( "replay.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-swarm-exp1-replay" ) );
    home.setName( QStringLiteral( "replay destination" ) );
    REQUIRE( replay.upsertExperiment( home ).has_value() );

    ReproductionBundleImporter importer( replay );
    ReproductionBundleImportOptions importOptions;
    importOptions.bundleDir = exportReport.bundlePath;
    importOptions.targetExperimentId = QStringLiteral( "exp-swarm-exp1-replay" );
    importOptions.keepOriginalRunId = true;
    const auto importReport = importer.importRun( importOptions );
    INFO( importReport.warnings.join( QStringLiteral( " | " ) ).toStdString() );
    REQUIRE( importReport.ok );

    const auto replayed = replay.runById( importReport.runId );
    REQUIRE( replayed.has_value() );
    // The pin survived the round trip — and with it the identity gate.
    CHECK( replayed->softwareRevision() == QStringLiteral( "git-rev-exp1" ) );
    CHECK( importReport.executionFingerprint ==
           runExecutionFingerprint( run.executionIdentity() ) );
}

// --- EXP-2 -------------------------------------------------------------------

TEST_CASE( "EXP-2: benchmark definition with empty protocol pins round-trips",
           "[swarm][experiment][benchmark][exp2]" )
{
    // validate() treats an empty protocol pin as "definition pins are
    // authoritative", but EvaluationProtocol::fromJson validates what it
    // parses, so the post-parse backfill was unreachable dead code: a
    // definition this write gate accepts was refused by its own read gate,
    // and one such row made listBenchmarkDefinitions — and therefore
    // hydrateFromStore — fail store-wide.
    BenchmarkDefinition def = makeDefinition( QStringLiteral( "bench-swarm-exp2" ) );
    // Protocol pins deliberately left EMPTY; the definition pins carry them.
    REQUIRE( def.validate().has_value() );

    const auto parsed = BenchmarkDefinition::fromJson( def.toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->protocol().datasetVersionId() == def.datasetVersionId() );
    CHECK( parsed->protocol().splitManifestId() == def.splitManifestId() );
    CHECK( parsed->validate().has_value() );

    // Store round trip: publish, then hydrate a cold service from the store.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    BenchmarkService writer( &store );
    REQUIRE( writer.publishDefinition( def ).has_value() );

    BenchmarkService reader( &store );
    const auto hydrated = reader.hydrateFromStore( 100, 100 );
    // Parenthesized: INFO() streams its argument, and << binds tighter than
    // ?: — a bare ternary would parse as (stream << condition) ? a : b.
    INFO( ( hydrated.diagnostics().isEmpty()
                ? std::string( "hydrate reported no diagnostic" )
                : hydrated.diagnostics().first().message.toStdString() ) );
    REQUIRE( hydrated.has_value() );
    const auto listed = reader.listDefinitions( 100 );
    REQUIRE( listed.size() == 1 );
    CHECK( listed.first().benchmarkId() == QStringLiteral( "bench-swarm-exp2" ) );
    CHECK( listed.first().protocol().datasetVersionId() == def.datasetVersionId() );
    CHECK( listed.first().protocol().splitManifestId() == def.splitManifestId() );
}

// --- EXP-3 -------------------------------------------------------------------

TEST_CASE( "EXP-3: summarizeAcrossSeeds orders per-class rows by the full bucket key",
           "[swarm][experiment][benchmark][exp3][determinism]" )
{
    // The sort carried only name+scope, so per-class rows sharing both
    // compared "equivalent" and the non-stable std::sort emitted them in
    // QHash (process-seeded) order — the exact drift the sort exists to
    // prevent. The buckets are keyed by name+scope+classCode, so the
    // comparator must order on the same triple.
    BenchmarkResult result;
    result.setResultId( QStringLiteral( "res-exp3" ) );
    result.setBenchmarkId( QStringLiteral( "bench-swarm-exp3" ) );
    result.setBenchmarkVersion( 1 );
    result.setStatus( BenchmarkRunStatus::Completed );
    result.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    result.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    // Scrambled insertion order on purpose.
    const QStringList classCodes{ QStringLiteral( "d" ), QStringLiteral( "b" ),
                                 QStringLiteral( "a" ), QStringLiteral( "c" ) };
    for ( const QString &classCode : classCodes )
    {
        MetricResult metric;
        metric.name = QStringLiteral( "per_class_f1" );
        metric.scope = QStringLiteral( "class" );
        metric.classCode = classCode;
        metric.value = 0.5;
        result.metrics().append( metric );
    }

    const QVector<BenchmarkSeedSummary> summaries =
        summarizeAcrossSeeds( { result }, QStringList{} );
    REQUIRE( summaries.size() == 4 );
    QStringList ordered;
    for ( const BenchmarkSeedSummary &summary : summaries )
        ordered.append( summary.classCode );
    CHECK( ordered == QStringList{ QStringLiteral( "a" ), QStringLiteral( "b" ),
                                   QStringLiteral( "c" ), QStringLiteral( "d" ) } );
}

// --- EXP-4 -------------------------------------------------------------------

TEST_CASE( "EXP-4: ignoreLabels excludes rows from BOTH sides of the matrix",
           "[swarm][experiment][benchmark][exp4]" )
{
    // The contract says ignore labels are "excluded from ALL metric
    // denominators", but only the truth side was checked: a prediction that
    // NAMED an ignored class still landed in the confusion matrix as a false
    // positive for the kept truth class.
    BenchmarkRunRequest request;
    request.definition = makeDefinition( QStringLiteral( "bench-swarm-exp4" ) );
    request.definition.protocol().ignoreLabels().append( QStringLiteral( "water" ) );
    request.modelId = QStringLiteral( "model-a" );
    request.truths.append( truthRow( QStringLiteral( "s1" ), QStringLiteral( "land" ) ) );
    request.truths.append( truthRow( QStringLiteral( "s2" ), QStringLiteral( "land" ) ) );
    request.predictions.append( predictionRow( QStringLiteral( "s1" ), QStringLiteral( "land" ) ) );
    // Prediction naming an ignored class: must be skipped, not scored.
    request.predictions.append( predictionRow( QStringLiteral( "s2" ), QStringLiteral( "water" ) ) );

    const auto result = BenchmarkRunner::run( request );
    REQUIRE( result.has_value() );
    CHECK( result->status() == BenchmarkRunStatus::Completed );

    const QJsonObject coverage =
        result->rawMetrics().value( QStringLiteral( "coverage" ) ).toObject();
    CHECK( coverage.value( QStringLiteral( "matched_samples" ) ).toInteger() == 1 );
    CHECK( coverage.value( QStringLiteral( "predictions_skipped_by_ignore_labels" ) )
               .toInteger() == 1 );
    CHECK( coverage.value( QStringLiteral( "unmatched_truth_rows" ) ).toInteger() == 1 );

    bool sawOa = false;
    for ( const MetricResult &metric : result->metrics() )
    {
        if ( metric.name == QLatin1String( "overall_accuracy" ) )
        {
            sawOa = true;
            // Only s1 was scored, and it was correct. Pre-fix the ignored
            // prediction counted as a land→water error (OA 0.5).
            CHECK( metric.value == Catch::Approx( 1.0 ).epsilon( 1e-12 ) );
        }
    }
    CHECK( sawOa );
}

// --- EXP-5 -------------------------------------------------------------------

TEST_CASE( "EXP-5: markFailed keeps the workflow evidence it is handed",
           "[swarm][experiment][recorder][exp5]" )
{
    // The Failed branch of the bridge built the workflow evidence document
    // and then dropped it: markFailed stored only error_code/message, while
    // the Completed branch passed its evidence through. A failure that
    // carries evidence must keep it.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-swarm-exp5" ) );
    experiment.setName( QStringLiteral( "swarm EXP-5" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRunRecorder recorder( store );
    RunStartRequest request;
    request.experimentId = QStringLiteral( "exp-swarm-exp5" );
    request.algorithmId = QStringLiteral( "workflow:under-test" );
    request.algorithmVersion = QStringLiteral( "1" );
    request.parameters = QJsonObject{ { QStringLiteral( "p" ), 1 } };
    request.executionRef = QStringLiteral( "exec-exp5" );
    const auto started = recorder.startRun( request );
    REQUIRE( started.has_value() );

    QJsonObject evidence;
    evidence.insert( QStringLiteral( "workflow" ),
                     QJsonObject{ { QStringLiteral( "execution_ref" ),
                                    QStringLiteral( "exec-exp5" ) },
                                  { QStringLiteral( "reported_state" ),
                                    QStringLiteral( "Failed" ) } } );
    REQUIRE( recorder.markFailed( started.value(), QStringLiteral( "workflow.failed" ),
                                  QStringLiteral( "operator crashed" ), evidence )
                 .has_value() );

    const auto failed = store.runById( started.value() );
    REQUIRE( failed.has_value() );
    CHECK( failed->status() == RunStatus::Failed );
    CHECK( failed->metrics().value( QStringLiteral( "workflow" ) ).toObject()
               .value( QStringLiteral( "reported_state" ) ).toString() ==
           QStringLiteral( "Failed" ) );
    const QJsonObject error = failed->metrics().value( QStringLiteral( "error" ) ).toObject();
    CHECK( error.value( QStringLiteral( "error_code" ) ).toString() ==
           QStringLiteral( "workflow.failed" ) );
    CHECK( error.value( QStringLiteral( "message" ) ).toString() ==
           QStringLiteral( "operator crashed" ) );

    // The default (no evidence) call still records the error block alone.
    const auto second = recorder.startRun( request );
    REQUIRE( second.has_value() );
    REQUIRE( recorder.markFailed( second.value(), QStringLiteral( "x" ),
                                  QStringLiteral( "y" ) )
                 .has_value() );
    const auto plain = store.runById( second.value() );
    REQUIRE( plain.has_value() );
    CHECK( plain->metrics().value( QStringLiteral( "error" ) ).toObject()
               .value( QStringLiteral( "error_code" ) ).toString() == QStringLiteral( "x" ) );
    CHECK( !plain->metrics().contains( QStringLiteral( "workflow" ) ) );
}

TEST_CASE( "EXP-5: a Failed execution event keeps its workflow evidence",
           "[swarm][experiment][bridge][exp5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    ExperimentRunBridge bridge( store );
    REQUIRE( bridge.ensureExperiment( QStringLiteral( "6d2b3d52-1111-4111-8111-111111111111" ),
                                      QStringLiteral( "auto-recording" ) )
                 .has_value() );

    const QString ref = QStringLiteral( "wf-run-exp5-fail" );
    ExecutionEvent running;
    running.executionRef = ref;
    running.workflowId = QStringLiteral( "wf-exp5" );
    running.state = QStringLiteral( "Running" );
    REQUIRE( bridge.handleExecutionEvent( running ).has_value() );

    ExecutionEvent failed = running;
    failed.state = QStringLiteral( "Failed" );
    failed.errorMessage = QStringLiteral( "operator crashed" );
    failed.startedMs = 1000;
    failed.finishedMs = 2500;
    REQUIRE( bridge.handleExecutionEvent( failed ).has_value() );

    const auto run = store.runById( bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    REQUIRE( run->status() == RunStatus::Failed );
    const QJsonObject workflow =
        run->metrics().value( QStringLiteral( "workflow" ) ).toObject();
    CHECK( workflow.value( QStringLiteral( "reported_state" ) ).toString() ==
           QStringLiteral( "Failed" ) );
    CHECK( workflow.value( QStringLiteral( "execution_ref" ) ).toString() == ref );
    CHECK( workflow.value( QStringLiteral( "finished_ms" ) ).toInteger() == 2500 );
    CHECK( workflow.value( QStringLiteral( "error_message" ) ).toString() ==
           QStringLiteral( "operator crashed" ) );
}

// --- EXP-6 -------------------------------------------------------------------

TEST_CASE( "EXP-6: unmapped KMeans clusters fall back to the unclassified sentinel",
           "[swarm][classification][pipeline][exp6]" )
{
    // The accuracy path remapped unmapped clusters to a hard-coded 0 while
    // the Apply path (same data, same remap table) falls back to
    // ignoreOptions.unclassifiedValue. When 0 is a real training class the
    // accuracy path therefore counted unclassified samples as correct
    // class-0 predictions, inflating OA/kappa.
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    {
        GDALAllRegister();
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        REQUIRE( drv != nullptr );
        GDALDataset *ds =
            drv->Create( srcPath.toUtf8().constData(), 8, 8, 3, GDT_Float32, nullptr );
        REQUIRE( ds != nullptr );
        std::vector<float> band( 8 * 8 );
        for ( int b = 0; b < 3; ++b )
        {
            for ( int r = 0; r < 8; ++r )
                for ( int c = 0; c < 8; ++c )
                    band[static_cast<size_t>( r * 8 + c )] =
                        ( b == 0 ? 200.0f : 20.0f ) + static_cast<float>( ( r + c + b ) % 5 );
            ds->GetRasterBand( b + 1 )->RasterIO( GF_Write, 0, 0, 8, 8, band.data(), 8, 8,
                                                  GDT_Float32, 0, 0 );
        }
        double gt[6] = { 0, 1, 0, 8, 0, -1 };
        ds->SetGeoTransform( gt );
        GDALClose( ds );
    }

    // Training labels include the REAL class 0 (rows 0..9) alongside class 1.
    cv::Mat trainX( 20, 3, CV_32F ), trainY( 20, 1, CV_32S );
    for ( int i = 0; i < 20; ++i )
    {
        const bool classOne = i >= 10;
        for ( int b = 0; b < 3; ++b )
            trainX.at<float>( i, b ) = ( classOne ? 200.0f : 20.0f )
                                      + static_cast<float>( ( i * 3 + b ) % 5 );
        trainY.at<int>( i, 0 ) = classOne ? 1 : 0;
    }

    // Held-out row 1 lands in a cluster the training set never produced, so
    // the Hungarian remap has no entry for it.
    cv::Mat testX( 2, 3, CV_32F ), testY( 2, 1, CV_32S );
    for ( int b = 0; b < 3; ++b )
    {
        testX.at<float>( 0, b ) = 200.0f;
        testX.at<float>( 1, b ) = 20.0f;
    }
    testY.at<int>( 0, 0 ) = 1;
    testY.at<int>( 1, 0 ) = 0;

    RsClassificationPipeline::Config cfg;
    cfg.sourceRaster = srcPath;
    cfg.outputRaster = tmp.path() + QStringLiteral( "/out.tif" );
    cfg.bandIndices = { 1, 2, 3 };
    cfg.backend.reset( new RemapStubBackend() );
    cfg.trainX = trainX;
    cfg.trainY = trainY;
    cfg.testX = testX;
    cfg.testY = testY;
    cfg.methodName = QStringLiteral( "KMeans" );
    cfg.ignoreOptions.unclassifiedValue = -1;

    const RsClassificationPipelineResult res = RsClassificationPipeline::run( std::move( cfg ) );
    INFO( res.errorMessage.toStdString() );
    REQUIRE( res.ok );

    // Row 1 (truth 0) fell back to the sentinel — not to the real class 0.
    CHECK( !res.accuracy.classIds.contains( 0 ) );
    CHECK( res.accuracy.classIds.contains( -1 ) );
    CHECK( res.accuracy.overallAccuracy == Catch::Approx( 0.5 ).epsilon( 1e-12 ) );
    CHECK( res.accuracy.kappa == Catch::Approx( 1.0 / 3.0 ).epsilon( 1e-12 ) );
}
