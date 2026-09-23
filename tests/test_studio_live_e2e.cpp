// test_studio_live_e2e.cpp — the Experiment Exploration Studio's LIVE
// execution closed loop (completion/experiment-studio-live-execution).
//
// Narrow end-to-end over REAL authorities, no demo data anywhere:
//   real spine study (fake backend + ExecutionPlane → TaskCenter backend)
//     → ExperimentStore truth → Studio projections
//     → GDAL spatial compare (typed grid-mismatch refusal, never resample)
//     → debugger first divergence over recorded evidence (typed absence,
//       confidence ≤ evidence completeness)
//     → faultlab REAL sandbox run (fault hits the copy only, source
//       digest proven unchanged)
//     → real CapsuleBuilder/CapsuleIO export → reload → readiness
//     → Studio export bundle + session carrying REFS only.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <vector>

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>

#include "dataset/dataset_store.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment_studio/first_divergence_projection.h"
#include "experiment_studio/live/studio_live.h"
#include "experiment_studio/run_matrix_projection.h"
#include "experiment_studio/sensitivity_projection.h"
#include "experiment_studio/studio_export.h"
#include "experiment_studio/studio_session.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "qgsapplication.h"
#include "study/bridge/study_execution_plane.h"
#include "study/study_sampling.h"
#include "study/study_spec.h"

using namespace sicnu::experiment_studio;
using namespace sicnu::experiment_studio::live;
using namespace sicnu::study;

namespace
{

// One QGIS application for the whole binary (house pattern, see
// test_study_e2e). Never exec'd per TEST_CASE.
QCoreApplication *ensureCoreApp()
{
    if ( QCoreApplication::instance() )
        return QCoreApplication::instance();
    static int argc = 1;
    static char arg0[] = "test_studio_live_e2e";
    char *argv[] = { arg0, nullptr };
    auto *app = new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    return app;
}

// Bridge the AtomicAlgorithmRegistry into JobEngine exactly like production
// (main.cpp ADR 0062 wiring) so plane submissions actually execute.
void wireRegistryFallback()
{
    sicnu::jobs::JobEngine::instance().setFallbackExecutor(
        []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx )
            -> Json::Value {
            const auto adapter =
                sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
                    req.algorithmId );
            if ( !adapter )
                throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
            sicnu::processing::ProgressCallback progressBridge =
                [&ctx]( int percent, const std::string &message ) {
                    ctx.reportProgress( percent / 100.0, message );
                };
            return adapter->execute( req.params, progressBridge,
                                     [&ctx]() { return ctx.isCancelled(); } );
        } );
}

/// Deterministic [0,1] gradient over a @p size × @p size grid.
void writeSyntheticNdvi( const QString &path, int size = 16 )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), size, size, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> pixels( static_cast<size_t>( size * size ) );
    for ( int y = 0; y < size; ++y )
        for ( int x = 0; x < size; ++x )
            pixels[static_cast<size_t>( y * size + x )] =
                static_cast<float>( x + y ) / static_cast<float>( 2 * size - 2 ); // 0 .. 1
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, size, size,
                           pixels.data(), size, size, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
}

/// A real-shaped fake backend: commits a REAL raster at the runner-assigned
/// path, then reports the same contract the ExecutionPlaneStudyBackend does
/// (payload carries the committed "output" plus recorded metrics). This is
/// the fake + real-shaped pair over ONE studio entry point (runStudy).
class RasterCommittingFakeBackend : public IStudyExecutionBackend
{
  public:
    sicnu::data::Result<std::unique_ptr<StudySubmission>> submit( const QString &algorithmId,
                                                     const QJsonObject &pointParameters,
                                                     const QString &correlationId,
                                                     std::chrono::milliseconds ) override
    {
        INFO( "fake backend submit for " + correlationId.toStdString() );
        const QString output = pointParameters.value( QStringLiteral( "output" ) ).toString();
        REQUIRE( !output.isEmpty() );
        writeSyntheticNdvi( output ); // the committed artifact the runner records

        StudyExecutionOutcome outcome;
        outcome.status = StudyExecutionOutcome::Status::Succeeded;
        outcome.payload = QJsonObject{
            { QStringLiteral( "output" ), output },
            { QStringLiteral( "maskedPercent" ), 42.5 },
        };
        return sicnu::data::Result<std::unique_ptr<StudySubmission>>::success(
            std::make_unique<FakeSubmission>( std::move( outcome ) ) );
    }

  private:
    class FakeSubmission : public StudySubmission
    {
      public:
        explicit FakeSubmission( StudyExecutionOutcome outcome )
            : m_outcome( std::move( outcome ) )
        {
        }
        QString executionRef() const override { return QStringLiteral( "fake-exec-1" ); }
        StudyExecutionOutcome wait( std::chrono::milliseconds ) override { return m_outcome; }
        void cancel() override {}

      private:
        StudyExecutionOutcome m_outcome;
    };
};

ParameterStudySpec threePointThresholdSpec( const QString &inputRaster )
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "studio-live-e2e-study" );
    spec.experimentId = QStringLiteral( "studio-live-e2e-exp" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.baseParameters = QJsonObject{
        { QStringLiteral( "input" ), inputRaster },
    };
    spec.strategy = SamplingStrategy::OneAtATime;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.2;
    dim.maxValue = 0.8;
    dim.stepCount = 3;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = 10;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 120000;
    spec.budget.seedReplicates = 1;
    spec.budget.seed = 42;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    spec.spatialComparison = true;
    spec.spatialEpsilon = 0.0;
    return spec;
}

/// `sicnu.lab.faults/1` document: band role swap over the two-band index
/// pair fixture (the faultlab suite's canonical teaching scenario).
QJsonObject bandRoleSwapScenario()
{
    QJsonObject fault;
    fault.insert( QStringLiteral( "family" ), QStringLiteral( "band_role_swap" ) );
    fault.insert( QStringLiteral( "params" ),
                  QJsonObject{ { QStringLiteral( "role_a" ), QStringLiteral( "red" ) },
                               { QStringLiteral( "role_b" ), QStringLiteral( "nir" ) } } );
    fault.insert( QStringLiteral( "seed" ), 42 );
    QJsonObject fixture;
    fixture.insert( QStringLiteral( "fixture_id" ), QStringLiteral( "two_band_index_pair" ) );
    fixture.insert( QStringLiteral( "params" ), QJsonObject{} );
    fixture.insert( QStringLiteral( "seed" ), 42 );
    QJsonObject sandbox;
    sandbox.insert( QStringLiteral( "class" ), QStringLiteral( "temp_copy" ) );
    sandbox.insert( QStringLiteral( "max_bytes" ), 1048576 );
    sandbox.insert( QStringLiteral( "require_source_unchanged" ), true );
    QJsonArray observables;
    QJsonObject o1;
    o1.insert( QStringLiteral( "id" ), QStringLiteral( "band_roles" ) );
    o1.insert( QStringLiteral( "relation" ), QStringLiteral( "changed" ) );
    observables.append( o1 );
    QJsonObject o2;
    o2.insert( QStringLiteral( "id" ), QStringLiteral( "index_mean" ) );
    o2.insert( QStringLiteral( "relation" ), QStringLiteral( "delta_ge" ) );
    o2.insert( QStringLiteral( "value" ), 0.5 );
    observables.append( o2 );
    QJsonObject expected;
    expected.insert( QStringLiteral( "observables" ), observables );
    expected.insert( QStringLiteral( "diagnosis" ),
                     QJsonObject{ { QStringLiteral( "signature" ),
                                    QStringLiteral( "all_negative_index" ) } } );
    QJsonObject objective;
    objective.insert( QStringLiteral( "id" ), QStringLiteral( "LO-03" ) );
    objective.insert(
        QStringLiteral( "statement" ),
        QStringLiteral( "Recognize that band metadata, not band position, defines an index." ) );
    QJsonObject doc;
    doc.insert( QStringLiteral( "schema_version" ), QStringLiteral( "sicnu.lab.faults/1" ) );
    doc.insert( QStringLiteral( "scenario_id" ), QStringLiteral( "fs_band_role_swap_ndvi" ) );
    doc.insert( QStringLiteral( "title" ), QStringLiteral( "Band role swap on an index pair" ) );
    doc.insert( QStringLiteral( "fault" ), fault );
    doc.insert( QStringLiteral( "base_fixture" ), fixture );
    doc.insert( QStringLiteral( "sandbox" ), sandbox );
    doc.insert( QStringLiteral( "expected" ), expected );
    doc.insert( QStringLiteral( "learning_objective" ), objective );
    doc.insert( QStringLiteral( "cleanup" ),
                QJsonObject{ { QStringLiteral( "required" ), true },
                             { QStringLiteral( "verify_no_residue" ), true } } );
    return doc;
}

QString firstRecordedRunId( sicnu::experiment::ExperimentStore &store,
                            sicnu::experiment::MatrixLedger &ledger, const QString &pointId )
{
    const auto runs = ledger.runsForCell( pointId );
    REQUIRE( runs.size() == 1 );
    return runs.first();
}

QStringList samplePointIds( const ParameterStudySpec &spec )
{
    // Bind the Result to a local first: .value() is a reference into the
    // Result, so iterating `sampleStudyPoints( spec ).value()` directly
    // walks a dangling vector (ASAN: stack-use-after-scope).
    const auto sampled = sampleStudyPoints( spec );
    if ( !sampled )
        return {};
    QStringList ids;
    for ( const StudyPoint &point : sampled.value() )
        ids.append( point.pointId );
    return ids;
}

} // namespace

TEST_CASE( "studio live: fake-backend study records real store truth that the "
           "Studio projections consume",
           "[experiment_studio][live]" )
{
    ensureCoreApp();
    QTemporaryDir workDir;
    REQUIRE( workDir.isValid() );

    const QString raster = workDir.filePath( QStringLiteral( "ndvi.tif" ) );
    writeSyntheticNdvi( raster );
    const ParameterStudySpec spec = threePointThresholdSpec( raster );
    const auto points = sampleStudyPoints( spec ).value();
    REQUIRE( points.size() == 3 );

    sicnu::experiment::ExperimentStore store;
    REQUIRE( store.open( workDir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    sicnu::experiment::MatrixLedger ledger( store );

    RasterCommittingFakeBackend backend;
    std::atomic<bool> cancel{ false };
    const QString outputDir = workDir.filePath( QStringLiteral( "study-outputs" ) );
    const auto ran = runStudy( store, ledger, spec, backend, outputDir, cancel );
    REQUIRE( ran.has_value() );
    CHECK( ran->recordedCount == 3 );
    CHECK( ran->failedCount == 0 );
    CHECK( ran->cancelledCount == 0 );

    // Run truth is in the store (single fact source), statuses truthful.
    for ( const StudyPoint &point : points )
    {
        const auto runs = ledger.runsForCell( point.pointId );
        REQUIRE( runs.size() == 1 );
        const auto run = store.runById( runs.first() );
        REQUIRE( run.has_value() );
        CHECK( run->status() == sicnu::dataset::RunStatus::Completed );
        CHECK( run->metrics().value( QStringLiteral( "maskedPercent" ) ) == 42.5 );
    }

    // The report is a pure projection of the recorded store truth.
    const StudyReport report = reportFromStore( store, ledger, spec, points, {}, &ran.value() );
    REQUIRE( report.runTable.size() == 3 );
    CHECK( report.recordedCount == 3 );

    // REAL store → Studio leaf view models: no synthetic report anywhere.
    const auto reportFromJson = StudyReport::fromJson( report.toJson() );
    REQUIRE( reportFromJson.has_value() );
    const RunMatrixViewModel matrixVm =
        projectRunMatrix( reportFromJson.value(), RunMatrixFilter{} );
    CHECK( matrixVm.rows.size() == 3 );
    CHECK( matrixVm.recordedCount == 3 );
    const SensitivityViewModel sensitivityVm = projectSensitivity( reportFromJson.value() );
    CHECK( sensitivityVm.curves.size() == 1 );
    CHECK( sensitivityVm.curves.first().points.size() == 3 );

    // Typed refusal: a committed output that does not exist is never guessed.
    const auto missing = committedOutputPath( outputDir, QStringLiteral( "no-such-point" ) );
    REQUIRE_FALSE( missing.has_value() );
    CHECK( missing.diagnostics().first().code == QStringLiteral( "experiment_studio.output_missing" ) );
}

TEST_CASE( "studio live: real spine closed loop — study, spatial refusal, "
           "divergence, fault sandbox, capsule reload, refs-only session",
           "[experiment_studio][live][e2e]" )
{
    ensureCoreApp();
    wireRegistryFallback();
    QTemporaryDir workDir;
    REQUIRE( workDir.isValid() );

    const QString raster = workDir.filePath( QStringLiteral( "ndvi.tif" ) );
    writeSyntheticNdvi( raster );
    const ParameterStudySpec spec = threePointThresholdSpec( raster );
    const auto points = sampleStudyPoints( spec ).value();
    const QStringList pointIds = samplePointIds( spec );

    sicnu::experiment::ExperimentStore store;
    REQUIRE( store.open( workDir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    sicnu::experiment::MatrixLedger ledger( store );

    // 1. Parameter study through the REAL ExecutionPlane → TaskCenter spine.
    ExecutionPlaneStudyBackend backend;
    std::atomic<bool> cancel{ false };
    const QString outputDir = workDir.filePath( QStringLiteral( "study-outputs" ) );
    const auto ran = runStudy( store, ledger, spec, backend, outputDir, cancel );
    REQUIRE( ran.has_value() );
    if ( ran->recordedCount != 3 )
    {
        for ( const QString &runId : ran->runIds )
        {
            const auto run = store.runById( runId );
            if ( run && run->status() != sicnu::dataset::RunStatus::Completed )
                WARN( QStringLiteral( "run %1 status %2 error: %3" )
                          .arg( runId )
                          .arg( sicnu::dataset::runStatusToString( run->status() ),
                                run->metrics()
                                    .value( QStringLiteral( "error" ) )
                                    .toObject()
                                    .value( QStringLiteral( "message" ) )
                                    .toString() )
                          .toStdString() );
        }
    }
    REQUIRE( ran->recordedCount == 3 );
    REQUIRE( ran->failedCount == 0 );

    const QString baselinePoint = pointIds.first();
    const QString variantPoint = pointIds.at( 1 );
    const QString baselineRun = firstRecordedRunId( store, ledger, baselinePoint );
    const QString variantRun = firstRecordedRunId( store, ledger, variantPoint );

    // 2. Spatial compare through the REAL GDAL summarizer: different
    // thresholds ⇒ changed masks on the gradient raster.
    const auto compared =
        compareRecordedPoints( outputDir, baselinePoint, variantPoint, spec.spatialEpsilon );
    REQUIRE( compared.has_value() );
    CHECK( compared->ok );
    CHECK( compared->summary.validPixels == 256 );
    CHECK( compared->summary.changedPixels > 0 );

    // 2b. Adversarial: a committed output whose grid does not match is a
    // TYPED refusal — the Studio never invents a resampled overlay.
    const QString mismatchPoint = pointIds.at( 2 );
    writeSyntheticNdvi(
        outputDir + QStringLiteral( "/" ) + mismatchPoint + QStringLiteral( "/output.tif" ), 8 );
    const auto mismatched =
        compareRecordedPoints( outputDir, baselinePoint, mismatchPoint, spec.spatialEpsilon );
    REQUIRE( mismatched.has_value() ); // refusal rides the VM, not an error
    CHECK_FALSE( mismatched->ok );
    CHECK( mismatched->gridMismatch );
    CHECK( mismatched->refuseCode == QStringLiteral( "study.spatial_mismatch" ) );
    CHECK( mismatched->summary == sicnu::study::SpatialDifferenceSummary{} );
    CHECK( mismatched->alignmentWorkflowHints.contains(
        QStringLiteral( "Do NOT silently overlay — Studio refuses resample" ) ) );

    // 3. Debugger first divergence over the RECORDED evidence. Study runs
    // carry no workflow step evidence: the honest verdict is a closed-set
    // divergence classification with named evidence gaps and DOWNGRADED
    // confidence — never a confident guess.
    const auto divergence =
        firstDivergenceReport( &store, outputDir, baselineRun, variantRun );
    REQUIRE( divergence.has_value() );
    const QJsonObject divergenceDoc = divergence.value();
    INFO( QJsonDocument( divergenceDoc ).toJson( QJsonDocument::Indented ).toStdString() );
    CHECK( divergenceDoc.value( QStringLiteral( "reference_run_id" ) ).toString()
           == baselineRun );
    const QString verdict = divergenceDoc.value( QStringLiteral( "verdict" ) ).toString();
    INFO( "divergence verdict: " + verdict.toStdString() );
    CHECK( ( verdict == QStringLiteral( "identical" ) || verdict == QStringLiteral( "equivalent" )
             || verdict == QStringLiteral( "divergent" ) || verdict == QStringLiteral( "incomplete" )
             || verdict == QStringLiteral( "non_comparable" ) ) );
    CHECK( divergenceDoc.value( QStringLiteral( "evidence_gaps" ) ).toArray().size() > 0 );
    const FirstDivergenceViewModel divergenceVm = projectFirstDivergence( divergenceDoc );
    CHECK( divergenceVm.confidenceDowngraded );

    // 3b. Adversarial: an unknown run id is a typed refusal, not a report.
    const auto unknownRun =
        firstDivergenceReport( &store, outputDir, baselineRun, QStringLiteral( "run-nope" ) );
    REQUIRE_FALSE( unknownRun.has_value() );
    CHECK( unknownRun.diagnostics().first().code
           == QStringLiteral( "experiment.debugger.unknown_run" ) );

    // 4. Fault teaching through the REAL faultlab sandbox pipeline: the
    // fault is applied to the copy only and the source digest is re-proven.
    const QJsonObject scenario = bandRoleSwapScenario();
    const auto faultRun =
        runFaultScenarioTeaching( scenario, QStringLiteral( "all_negative_index" ) );
    REQUIRE( faultRun.has_value() );
    CHECK( faultRun->sandboxUnchangedOriginal ); // digest before == digest after
    CHECK( faultRun->systemDiagnosis == QStringLiteral( "all_negative_index" ) );
    CHECK( faultRun->diagnosisMatch );
    const QJsonObject faultEvidence = faultRun->evidence;
    CHECK( faultEvidence.value( QStringLiteral( "fault" ) )
               .toObject()
               .value( QStringLiteral( "applied" ) )
               .toBool() );
    CHECK( faultEvidence.value( QStringLiteral( "cleanup" ) )
               .toObject()
               .value( QStringLiteral( "sandbox_removed" ) )
               .toBool() );
    CHECK( faultEvidence.value( QStringLiteral( "cleanup" ) )
               .toObject()
               .value( QStringLiteral( "source_unchanged" ) )
               .toBool() );
    CHECK( faultEvidence.value( QStringLiteral( "replay" ) )
               .toObject()
               .value( QStringLiteral( "deterministic" ) )
               .toBool() );

    // 4b. A wrong student prediction is a mismatch, never silently accepted.
    const auto wrongPrediction =
        runFaultScenarioTeaching( scenario, QStringLiteral( "crs_mismatch" ) );
    REQUIRE( wrongPrediction.has_value() );
    CHECK( wrongPrediction->diagnosisMismatch );

    // 4c. A foreign scenario document is a typed refusal.
    QJsonObject foreign = scenario;
    foreign.insert( QStringLiteral( "schema_version" ),
                    QStringLiteral( "sicnu.lab.faults/999" ) );
    const auto refused = runFaultScenarioTeaching( foreign, QStringLiteral( "x" ) );
    REQUIRE_FALSE( refused.has_value() );
    CHECK( refused.diagnostics().first().code
           == QStringLiteral( "experiment_studio.fault_scenario_invalid" ) );

    // 4d. The SHIPPED teaching scenario loads and runs through the same real
    // pipeline — the example file cannot silently rot.
    QFile shippedScenario( QStringLiteral( CMAKE_SOURCE_DIR
                                           "/examples/faults/band-role-swap.sicnu-faults.json" ) );
    REQUIRE( shippedScenario.open( QIODevice::ReadOnly ) );
    const QJsonObject shippedDoc =
        QJsonDocument::fromJson( shippedScenario.readAll() ).object();
    REQUIRE( !shippedDoc.isEmpty() );
    const auto shippedRun =
        runFaultScenarioTeaching( shippedDoc, QStringLiteral( "all_negative_index" ) );
    REQUIRE( shippedRun.has_value() );
    CHECK( shippedRun->sandboxUnchangedOriginal );
    CHECK( shippedRun->diagnosisMatch );

    // 5. Capsules through the REAL builder/IO/readiness; the Studio keeps a
    // REF only. Reload after export proves the artifact on disk loads.
    sicnu::dataset::DatasetStore datasets;
    const QString capsulePath =
        workDir.filePath( QStringLiteral( "capsule-%1.json" ).arg( baselineRun ) );
    const auto exported =
        exportRunCapsule( store, datasets, baselineRun, capsulePath, workDir.path(),
                          QStringLiteral( "2026-09-24T00:00:00Z" ) );
    REQUIRE( exported.has_value() );
    CHECK( exported->runId == baselineRun );
    CHECK( exported->bytes > 0 );

    const auto readiness = capsuleReloadReadiness( exported->path, &datasets );
    REQUIRE( readiness.has_value() );
    CHECK( readiness->value( QStringLiteral( "digest_ok" ) ).toBool() );
    const QJsonObject readinessReport =
        readiness->value( QStringLiteral( "readiness" ) ).toObject();
    // Honest rollup: study runs record raw input paths, not catalog dataset
    // pins — the REQUIRED dataset_version pin is Missing, so the level is
    // Impossible (fail-conservative). Unwired hooks stay Unknown; nothing
    // is upgraded to a fabricated Exact/BestEffort.
    CHECK( readinessReport.value( QStringLiteral( "level" ) ).toString()
           == QStringLiteral( "impossible" ) );
    bool datasetPinMissing = false;
    for ( const QJsonValue &check : readinessReport.value( QStringLiteral( "checks" ) ).toArray() )
    {
        if ( check.toObject().value( QStringLiteral( "dependency" ) ).toString()
                 == QStringLiteral( "dataset_version" )
             && check.toObject().value( QStringLiteral( "status" ) ).toString()
                 == QStringLiteral( "missing" ) )
            datasetPinMissing = true;
    }
    CHECK( datasetPinMissing );

    // 5b. Adversarial: a tampered capsule is refused by the real load gates.
    QByteArray bytes;
    {
        QFile capsuleFile( exported->path );
        REQUIRE( capsuleFile.open( QIODevice::ReadOnly ) );
        bytes = capsuleFile.readAll();
    }
    bytes[bytes.size() / 2] = static_cast<char>( bytes[bytes.size() / 2] ^ 0x40 );
    const QString tamperedPath =
        workDir.filePath( QStringLiteral( "capsule-tampered.json" ) );
    {
        QFile tampered( tamperedPath );
        REQUIRE( tampered.open( QIODevice::WriteOnly ) );
        REQUIRE( tampered.write( bytes ) == bytes.size() );
    }
    const auto tamperedReadiness = capsuleReloadReadiness( tamperedPath, &datasets );
    REQUIRE_FALSE( tamperedReadiness.has_value() );

    // 6. Export bundle + session: refs only (run ids, capsule paths), and
    // the bundle round-trips offline through the existing reader.
    StudioExportBundle bundle;
    bundle.studyId = spec.studyId;
    bundle.runIds = QStringList{ baselineRun, variantRun };
    bundle.capsuleRefs = QStringList{ exported->path };
    const StudyReport report = reportFromStore( store, ledger, spec, points, {}, &ran.value() );
    bundle.studyReport = report.toJson();
    bundle.csvRunTable = studyRunTableToCsv( bundle.studyReport );
    const QString exportPath = workDir.filePath( QStringLiteral( "studio-export.json" ) );
    REQUIRE( writeStudioExportJson( bundle, exportPath ).has_value() );
    QFile exportFile( exportPath );
    REQUIRE( exportFile.open( QIODevice::ReadOnly ) );
    const auto reloadedBundle =
        StudioExportBundle::fromJson( QJsonDocument::fromJson( exportFile.readAll() ).object() );
    REQUIRE( reloadedBundle.has_value() );
    CHECK( reloadedBundle->capsuleRefs == QStringList{ exported->path } );
    CHECK( reloadedBundle->runIds == QStringList{ baselineRun, variantRun } );
    // The capsule BODY never rides the bundle — only the ref.
    CHECK_FALSE( QJsonDocument( reloadedBundle->toJson() )
                     .toJson()
                     .contains( QStringLiteral( "capsule_id" ).toUtf8() ) );

    StudioSessionState session;
    session.studyId = spec.studyId;
    session.experimentId = spec.experimentId;
    session.referenceRunId = baselineRun;
    session.studentRunId = variantRun;
    session.faultScenarioId = QStringLiteral( "fs_band_role_swap_ndvi" );
    session.lastExportPath = exportPath;
    const StudioSessionState sessionBack = StudioSessionState::fromJson( session.toJson() );
    CHECK( sessionBack.referenceRunId == baselineRun );
    CHECK( sessionBack.studentRunId == variantRun );
    // The session stores the capsule ref facts, not a capsule document body.
    CHECK_FALSE( QJsonDocument( session.toJson() )
                     .toJson()
                     .contains( QStringLiteral( "digest" ).toUtf8() ) );
}
