// test_experiment_studio_core.cpp — Experiment Exploration Studio projection
// contracts (Catch2 + Sicnu::experiment_studio / Sicnu::study). No QGIS/GUI.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "experiment_studio/fault_teaching_projection.h"
#include "experiment_studio/first_divergence_projection.h"
#include "experiment_studio/run_matrix_projection.h"
#include "experiment_studio/sensitivity_projection.h"
#include "experiment_studio/spatial_compare_projection.h"
#include "experiment_studio/studio_export.h"
#include "experiment_studio/studio_session.h"
#include "experiment_studio/study_designer.h"
#include "study/study_export.h"
#include "study/study_sampling.h"
#include "study/study_spatial.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <limits>

using namespace sicnu::experiment_studio;
using namespace sicnu::study;

namespace
{

ParameterStudySpec validThresholdSpec( int steps = 5, qint64 maxRuns = 20 )
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "ndvi-threshold-sweep" );
    spec.experimentId = QStringLiteral( "exp-ndvi-sweep" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.baseParameters = QJsonObject{ { QStringLiteral( "input" ), QStringLiteral( "/data/ndvi.tif" ) } };
    spec.strategy = SamplingStrategy::OneAtATime;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.1;
    dim.maxValue = 0.9;
    dim.stepCount = steps;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = maxRuns;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 600000;
    spec.budget.seedReplicates = 1;
    spec.budget.seed = 42;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    return spec;
}

QJsonObject thresholdSchema()
{
    QJsonObject threshold;
    threshold.insert( QStringLiteral( "type" ), QStringLiteral( "number" ) );
    threshold.insert( QStringLiteral( "minimum" ), 0.0 );
    threshold.insert( QStringLiteral( "maximum" ), 1.0 );
    QJsonObject input;
    input.insert( QStringLiteral( "type" ), QStringLiteral( "string" ) );
    QJsonObject props;
    props.insert( QStringLiteral( "threshold" ), threshold );
    props.insert( QStringLiteral( "input" ), input );
    return QJsonObject{ { QStringLiteral( "properties" ), props } };
}

class FakeSpatialSummarizer final : public ISpatialDifferenceSummarizer
{
  public:
    bool forceMismatch = false;
    Result<SpatialDifferenceSummary> summarize( const QString &baselinePath,
                                                const QString &runPath ) const override
    {
        if ( forceMismatch || baselinePath.contains( QStringLiteral( "mismatch" ) )
             || runPath.contains( QStringLiteral( "mismatch" ) ) )
        {
            return Result<SpatialDifferenceSummary>::failure(
                studyError( QStringLiteral( "study.spatial_mismatch" ),
                            QStringLiteral( "dimensions differ" ) ) );
        }
        // nodata / NaN handling via buffer helper
        const float a[4] = { 0.f, 1.f, std::numeric_limits<float>::quiet_NaN(), 3.f };
        const float b[4] = { 0.f, 1.5f, 2.f, 3.f };
        return Result<SpatialDifferenceSummary>::success(
            summarizeBufferDifference( baselinePath, runPath, a, b, 4, 0.0 ) );
    }
};

StudyReport syntheticReport( int points, int seedReplicates = 1 )
{
    StudyReport report;
    report.studyId = QStringLiteral( "s1" );
    report.experimentId = QStringLiteral( "e1" );
    report.algorithmId = QStringLiteral( "rs:threshold_raster" );
    report.strategy = QStringLiteral( "one_at_a_time" );
    report.specJson.insert( QStringLiteral( "budget" ),
                            QJsonObject{ { QStringLiteral( "seed_replicates" ), seedReplicates } } );
    SensitivityCurve curve;
    curve.parameterPath = QStringLiteral( "threshold" );
    curve.metricName = QStringLiteral( "maskedPercent" );
    curve.trend = QStringLiteral( "increasing" );
    for ( int i = 0; i < points; ++i )
    {
        StudyRunRow row;
        row.pointId = QStringLiteral( "p%1" ).arg( i );
        row.replicateIndex = 0;
        row.runId = QStringLiteral( "run-%1" ).arg( i );
        row.status = ( i == points - 1 && points > 1 ) ? QStringLiteral( "failed" )
                                                       : QStringLiteral( "recorded" );
        if ( row.status == QStringLiteral( "failed" ) )
        {
            row.errorSummary = QStringLiteral( "study.run_failed: boom" );
            report.failedCount++;
        }
        else
            report.recordedCount++;
        row.parameterAssignments.insert( QStringLiteral( "threshold" ), QString::number( 0.1 + i * 0.1 ) );
        row.metrics.insert( QStringLiteral( "maskedPercent" ), 10.0 + i );
        row.outputAssetPath = QStringLiteral( "/tmp/out%1.tif" ).arg( i );
        report.runTable.append( row );
        CurvePoint cp;
        cp.dimensionValue = 0.1 + i * 0.1;
        cp.runCount = 1;
        cp.mean = 10.0 + i;
        cp.min = cp.mean;
        cp.max = cp.mean;
        curve.points.append( cp );
    }
    report.curves.append( curve );
    return report;
}

} // namespace

TEST_CASE( "experiment_studio valid StudySpec via designer", "[experiment_studio]" )
{
    const auto spec = validThresholdSpec();
    REQUIRE( spec.validate() );
    const auto vm = buildStudyDesignerViewModel(
        spec.algorithmId, thresholdSchema(), spec.baseParameters, spec.strategy, spec.dimensions,
        spec.budget, spec.metricNames, spec.studyId, spec.experimentId, defaultResourcePolicy(),
        QStringLiteral( "bit_exact" ) );
    REQUIRE( vm );
    CHECK( vm->draftValid );
    CHECK( vm->estimatedPoints == sampleStudyPoints( spec ).value().size() );
}

TEST_CASE( "experiment_studio invalid StudySpec ranges", "[experiment_studio]" )
{
    auto spec = validThresholdSpec();
    spec.dimensions[0].minValue = 0.9;
    spec.dimensions[0].maxValue = 0.1;
    REQUIRE_FALSE( spec.validate() );
    const auto vm = buildStudyDesignerViewModel(
        spec.algorithmId, thresholdSchema(), spec.baseParameters, spec.strategy, spec.dimensions,
        spec.budget, spec.metricNames, spec.studyId, spec.experimentId );
    REQUIRE( vm );
    CHECK_FALSE( vm->draftValid );
    REQUIRE_FALSE( vm->draftIssues.isEmpty() );
}

TEST_CASE( "experiment_studio too-many-combinations refused", "[experiment_studio]" )
{
    ParameterStudySpec spec = validThresholdSpec( 50, 10 );
    spec.strategy = SamplingStrategy::Grid;
    ParameterDimension d2;
    d2.parameterPath = QStringLiteral( "gain" );
    d2.minValue = 0.0;
    d2.maxValue = 1.0;
    d2.stepCount = 50;
    spec.dimensions.append( d2 );
    // 50*50 = 2500 > maxRuns 10 and > policy
    const auto vm = buildStudyDesignerViewModel(
        spec.algorithmId, thresholdSchema(), spec.baseParameters, spec.strategy, spec.dimensions,
        spec.budget, spec.metricNames, spec.studyId, spec.experimentId );
    REQUIRE( vm );
    CHECK_FALSE( vm->draftValid );
    bool explosion = false;
    for ( const QString &issue : vm->draftIssues )
    {
        if ( issue.contains( QStringLiteral( "combo_explosion" ) )
             || issue.contains( QStringLiteral( "budget" ) ) )
            explosion = true;
    }
    CHECK( explosion );
}

TEST_CASE( "experiment_studio deterministic point identity", "[experiment_studio]" )
{
    const auto spec = validThresholdSpec( 5, 20 );
    const auto a = sampleStudyPoints( spec );
    const auto b = sampleStudyPoints( spec );
    REQUIRE( a );
    REQUIRE( b );
    REQUIRE( a->size() == b->size() );
    for ( int i = 0; i < a->size(); ++i )
        CHECK( ( *a )[i].pointId == ( *b )[i].pointId );
}

TEST_CASE( "experiment_studio run matrix one failed point + filter", "[experiment_studio]" )
{
    const StudyReport report = syntheticReport( 5 );
    RunMatrixFilter filter;
    filter.onlyFailed = true;
    const auto vm = projectRunMatrix( report, filter );
    CHECK( vm.failedCount == 1 );
    REQUIRE( vm.rows.size() == 1 );
    CHECK( vm.rows[0].status == QStringLiteral( "failed" ) );
    CHECK( pointIdentityKey( vm.rows[0] ).contains( QLatin1Char( '#' ) ) );
}

TEST_CASE( "experiment_studio report reload export roundtrip", "[experiment_studio]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    StudioExportBundle bundle;
    bundle.studyId = QStringLiteral( "s1" );
    bundle.studyReport = syntheticReport( 3 ).toJson();
    bundle.runIds = { QStringLiteral( "run-0" ), QStringLiteral( "run-1" ) };
    bundle.csvRunTable = studyRunTableToCsv( bundle.studyReport );
    const QString path = dir.filePath( QStringLiteral( "export.json" ) );
    REQUIRE( writeStudioExportJson( bundle, path ) );
    const auto loaded = readStudioExportJson( path );
    REQUIRE( loaded );
    CHECK( loaded->studyId == bundle.studyId );
    CHECK( loaded->runIds.size() == 2 );
    const auto report = StudyReport::fromJson( loaded->studyReport );
    REQUIRE( report );
    CHECK( report->runTable.size() == 3 );
}

TEST_CASE( "experiment_studio spatial grid mismatch typed refusal", "[experiment_studio]" )
{
    FakeSpatialSummarizer summarizer;
    summarizer.forceMismatch = true;
    const auto vm = compareSpatialOutputs( QStringLiteral( "a" ), QStringLiteral( "b" ),
                                           QStringLiteral( "/a.tif" ), QStringLiteral( "/b.tif" ),
                                           summarizer );
    REQUIRE( vm );
    CHECK_FALSE( vm->ok );
    CHECK( vm->gridMismatch );
    CHECK( vm->refuseCode.contains( QStringLiteral( "spatial_mismatch" ) ) );
    CHECK_FALSE( vm->alignmentWorkflowHints.isEmpty() );
}

TEST_CASE( "experiment_studio spatial nodata handled in buffer summary", "[experiment_studio]" )
{
    FakeSpatialSummarizer summarizer;
    const auto vm = compareSpatialOutputs( QStringLiteral( "a" ), QStringLiteral( "b" ),
                                           QStringLiteral( "/a.tif" ), QStringLiteral( "/b.tif" ),
                                           summarizer );
    REQUIRE( vm );
    CHECK( vm->ok );
    // one NaN pair → validPixels < totalPixels
    CHECK( vm->summary.validPixels < vm->summary.totalPixels );
}

TEST_CASE( "experiment_studio uncertainty missing replicate flagged", "[experiment_studio]" )
{
    const auto vm = projectSensitivity( syntheticReport( 4, 1 ) );
    CHECK( vm.uncertaintyMissingReplicate );
    REQUIRE_FALSE( vm.issues.isEmpty() );
}

TEST_CASE( "experiment_studio fault sandbox unchanged + diagnosis mismatch", "[experiment_studio]" )
{
    QJsonObject scenario;
    scenario.insert( QStringLiteral( "scenario_id" ), QStringLiteral( "fault.demo" ) );
    scenario.insert( QStringLiteral( "expected_diagnosis_signature" ),
                     QStringLiteral( "all_nodata" ) );
    auto prior = projectFaultScenarioPredict( scenario );
    const auto ok = projectFaultDiagnosis( prior, QStringLiteral( "all_nodata" ),
                                           QStringLiteral( "all_nodata" ), {},
                                           QStringLiteral( "/sandbox" ), QStringLiteral( "fp1" ),
                                           QStringLiteral( "fp1" ) );
    CHECK( ok.sandboxUnchangedOriginal );
    CHECK( ok.diagnosisMatch );

    const auto bad = projectFaultDiagnosis( prior, QStringLiteral( "crs_mismatch" ),
                                            QStringLiteral( "all_nodata" ), {},
                                            QStringLiteral( "/sandbox" ), QStringLiteral( "fp1" ),
                                            QStringLiteral( "fp1" ) );
    CHECK( bad.diagnosisMismatch );

    const auto mutated = projectFaultDiagnosis( prior, QStringLiteral( "all_nodata" ),
                                                QStringLiteral( "all_nodata" ), {},
                                                QStringLiteral( "/sandbox" ), QStringLiteral( "fp1" ),
                                                QStringLiteral( "fp2" ) );
    CHECK_FALSE( mutated.sandboxUnchangedOriginal );
}

TEST_CASE( "experiment_studio debugger exact first divergence + confidence downgrade",
           "[experiment_studio]" )
{
    QJsonObject first;
    first.insert( QStringLiteral( "kind" ), QStringLiteral( "ParameterDivergence" ) );
    first.insert( QStringLiteral( "confidence" ), QStringLiteral( "high" ) );
    first.insert( QStringLiteral( "reference_step_id" ), QStringLiteral( "s1" ) );
    first.insert( QStringLiteral( "student_step_id" ), QStringLiteral( "s1" ) );
    first.insert( QStringLiteral( "evidence" ), QJsonArray{ QStringLiteral( "param" ) } );
    first.insert( QStringLiteral( "missing_evidence" ), QJsonArray{ QStringLiteral( "digest" ) } );
    QJsonObject report;
    report.insert( QStringLiteral( "reference_run_id" ), QStringLiteral( "ref" ) );
    report.insert( QStringLiteral( "student_run_id" ), QStringLiteral( "stu" ) );
    report.insert( QStringLiteral( "verdict" ), QStringLiteral( "incomplete" ) );
    report.insert( QStringLiteral( "has_first_divergence" ), true );
    report.insert( QStringLiteral( "first_divergence" ), first );
    report.insert( QStringLiteral( "evidence_gaps" ), QJsonArray{ QStringLiteral( "digest" ) } );
    const auto vm = projectFirstDivergence( report );
    CHECK( vm.hasFirstDivergence );
    CHECK( vm.divergenceKind == QStringLiteral( "ParameterDivergence" ) );
    CHECK( vm.referenceStepId == QStringLiteral( "s1" ) );
    CHECK( vm.confidenceDowngraded );
    CHECK( vm.causalConfidence == QStringLiteral( "low" ) );
}

TEST_CASE( "experiment_studio cancellation status surfaces in matrix", "[experiment_studio]" )
{
    StudyReport report = syntheticReport( 2 );
    report.runTable[0].status = QStringLiteral( "cancelled" );
    report.runTable[0].errorSummary = QStringLiteral( "cancelled: user" );
    report.cancelledCount = 1;
    report.recordedCount = 0;
    report.failedCount = 1;
    RunMatrixFilter f;
    f.statusEquals = QStringLiteral( "cancelled" );
    const auto vm = projectRunMatrix( report, f );
    REQUIRE( vm.rows.size() == 1 );
    CHECK( vm.rows[0].status == QStringLiteral( "cancelled" ) );
}

TEST_CASE( "experiment_studio 1000-point table projection baseline", "[experiment_studio][perf]" )
{
    const StudyReport report = syntheticReport( 1000 );
    const auto start = std::chrono::steady_clock::now();
    const auto vm = projectRunMatrix( report, {} );
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start )
                        .count();
    CHECK( vm.rows.size() == 1000 );
    // Generous offline baseline; projection must stay snappy for UI binding.
    CHECK( ms < 2000 );
}

TEST_CASE( "experiment_studio session roundtrip", "[experiment_studio]" )
{
    StudioSessionState s;
    s.studyId = QStringLiteral( "s" );
    s.activeTab = QStringLiteral( "matrix" );
    s.selectedPointIds = { QStringLiteral( "p0" ), QStringLiteral( "p1" ) };
    const auto back = StudioSessionState::fromJson( s.toJson() );
    CHECK( back.studyId == s.studyId );
    CHECK( back.selectedPointIds.size() == 2 );
}
