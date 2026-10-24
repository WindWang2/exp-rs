// studio_live.cpp — real-execution compositions (see studio_live.h).
//
// Every function delegates the FACT to an existing authority (StudyRunner,
// GdalRasterDifferenceSummarizer, DirectoryEvidenceSource +
// FirstDivergenceAnalyzer, runFaultScenario, CapsuleBuilder/CapsuleIO/
// CapsuleReadiness) and the VIEW to the projection leaf. What is added here
// is only the composition and the Studio-facing typed refusals.
#include "experiment_studio/live/studio_live.h"

#include "experiment/capsule/capsule_builder.h"
#include "experiment/capsule/capsule_document.h"
#include "experiment/capsule/capsule_io.h"
#include "experiment/capsule/capsule_readiness.h"
#include "experiment/debugger/evidence_source.h"
#include "experiment/debugger/first_divergence.h"
#include "experiment/debugger/run_snapshot.h"
#include "experiment/debugger/snapshot_builder.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "faultlab/fault_runner.h"
#include "faultlab/fault_types.h"
#include "study/bridge/study_execution_plane.h"
#include "study/study_spatial.h"
// R21-E (#1613): the faultlab verifier hook lives HERE by design — the
// jsoncpp-only faultlab leaf cannot link the grading stack, so this
// composition layer materializes the faulted artifact and runs the
// referenced lab rules through the OutputVerifier teaching seam.
#include "agent/output_verifier.h"
#include "geospatial/raster/raster_writer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::experiment_studio::live
{

using namespace sicnu::experiment_studio;
using sicnu::faultlab::FaultRunOptions;
using sicnu::faultlab::FaultRunReport;
using sicnu::faultlab::FaultScenario;
using sicnu::study::GdalRasterDifferenceSummarizer;

namespace
{

/// R21-E (#1613): the artifact-level verifier hook installed into
/// FaultRunOptions. The faultlab leaf cannot link the grading stack, so THIS
/// composition layer materializes the faulted grid to a staged GeoTIFF (the
/// geospatial writer contract: temp → write → fsync → validate → publish)
/// and runs the referenced lab rules through the OutputVerifier teaching
/// seam. The scenario's must_fail_assertions must all fire on the faulted
/// artifact — a fault the grader cannot catch must be visible, never passed.
sicnu::faultlab::FaultVerifierOutcome labFaultVerifierHook(
    const sicnu::faultlab::FaultGrid &grid, const sicnu::faultlab::FaultVerifierSpec &spec )
{
    sicnu::faultlab::FaultVerifierOutcome outcome;
    if ( grid.width <= 0 || grid.height <= 0 || grid.bands.empty() )
    {
        outcome.note = QStringLiteral( "faulted grid is empty — nothing to grade" ).toStdString();
        return outcome; // ran=false → the runner fails closed
    }

    QTemporaryDir dir;
    if ( !dir.isValid() )
    {
        outcome.note = QStringLiteral( "cannot create a verifier temp dir" ).toStdString();
        return outcome;
    }
    const QString target = dir.filePath( QStringLiteral( "faulted_artifact.tif" ) );

    try
    {
        std::vector<sicnu::geo::RasterBandSpec> bandSpecs;
        bandSpecs.reserve( grid.bands.size() );
        bool scaleIdentity = true;
        for ( const sicnu::faultlab::BandSpec &band : grid.bands )
        {
            sicnu::geo::RasterBandSpec bandSpec;
            bandSpec.dtype = "Float64"; // fault samples are doubles — no
                                        // precision slip between grid and grade
            bandSpec.role = band.role;
            bandSpec.hasNoData = grid.hasNoData;
            bandSpec.noDataValue = grid.noDataValue;
            // The fault grid's samples are PHYSICAL values; the writer's
            // contract separates stored from physical (scale/offset). Carry
            // the band's transform as metadata so a reader recovers the
            // same physical values the grader must judge (R1b P3). A
            // degenerate scale (0) would make the inverse unrecoverable —
            // the caller cannot honor the declaration, so it fails closed.
            if ( band.scale == 0.0 || !std::isfinite( band.scale ) ||
                 !std::isfinite( band.offset ) )
            {
                outcome.note = "faulted band '" + band.role +
                               "' carries a degenerate scale/offset transform";
                return outcome;
            }
            if ( band.scale != 1.0 || band.offset != 0.0 )
            {
                bandSpec.hasScale = true;
                bandSpec.scale = band.scale;
                bandSpec.hasOffset = true;
                bandSpec.offset = band.offset;
                scaleIdentity = false;
            }
            bandSpecs.push_back( bandSpec );
        }
        sicnu::geo::RasterWriteOptions writeOptions;
        writeOptions.overwrite = true; // temp dir is private; publish is ours
        sicnu::geo::RasterWriter writer =
            sicnu::geo::RasterWriter::create( target.toStdString(), grid.width, grid.height,
                                              bandSpecs, writeOptions );
        writer.setGeotransform( { grid.geoTransform[ 0 ], grid.geoTransform[ 1 ],
                                  grid.geoTransform[ 2 ], grid.geoTransform[ 3 ],
                                  grid.geoTransform[ 4 ], grid.geoTransform[ 5 ] } );
        writer.setCrs( sicnu::geo::Crs::fromAuthid( grid.crsId ) );
        const sicnu::geo::RasterWindow full{ 0, 0, grid.width, grid.height };
        std::vector<double> stored;
        for ( std::size_t band = 0; band < grid.bands.size(); ++band )
        {
            const std::vector<double> &samples = grid.bands[ band ].samples;
            if ( samples.size() != grid.sampleCount() )
            {
                outcome.note = "faulted band '" + grid.bands[ band ].role +
                               "' does not carry width*height samples";
                return outcome;
            }
            const sicnu::faultlab::BandSpec &spec = grid.bands[ band ];
            const double *values = samples.data();
            if ( !scaleIdentity )
            {
                // stored = (physical - offset) / scale, so reading the band
                // back (stored*scale + offset) yields the physical samples.
                stored.resize( samples.size() );
                for ( std::size_t i = 0; i < samples.size(); ++i )
                    stored[ i ] = ( samples[ i ] - spec.offset ) / spec.scale;
                values = stored.data();
            }
            writer.writeWindow( static_cast<int>( band ) + 1, full, values );
        }
        writer.finalize();
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        outcome.note = std::string( "cannot materialize the faulted artifact: " ) + e.what();
        return outcome;
    }

    const sicnu::agent::OutputVerifier verifier;
    const sicnu::agent::OutputVerifier::LabGradeResult graded =
        verifier.gradeForTeaching( QString::fromStdString( spec.rulesRef ), target );
    if ( !graded.graded )
    {
        outcome.note = "grading did not produce a verdict: " + graded.error.toStdString();
        return outcome; // ran=false → fail closed, never a guessed pass
    }

    outcome.ran = true;
    for ( const auto &deduction : graded.deductions )
        outcome.observedFailedAssertions.push_back( deduction.assertionId.toStdString() );
    outcome.mustFailSatisfied = true;
    std::string missing;
    for ( const std::string &mustFail : spec.mustFailAssertions )
    {
        const bool observed = std::find( outcome.observedFailedAssertions.begin(),
                                         outcome.observedFailedAssertions.end(),
                                         mustFail ) !=
                              outcome.observedFailedAssertions.end();
        if ( !observed )
        {
            outcome.mustFailSatisfied = false;
            if ( !missing.empty() )
                missing += ", ";
            missing += mustFail;
        }
    }
    if ( !outcome.mustFailSatisfied )
        outcome.note = "must-fail assertions the grader did not catch: " + missing;
    return outcome;
}

/// Bounded QJson → jsoncpp conversion — the mirror of the bridge's
/// jsonCppToQJson (same caps: ≤16 depth, ≤512 members/elements; overflow is
/// a typed refusal, never silent truncation). Fault scenario documents are
/// small teaching artifacts; the caps keep a hostile document from pinning
/// the UI thread.
bool qJsonToJsonCpp( const QJsonValue &value, int depth, Json::Value &out, QString *errorOut )
{
    if ( depth > 16 )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "scenario document nesting exceeds 16" );
        return false;
    }
    switch ( value.type() )
    {
        case QJsonValue::Null:
            out = Json::Value( Json::nullValue );
            return true;
        case QJsonValue::Bool:
            out = Json::Value( value.toBool() );
            return true;
        case QJsonValue::Double:
        {
            const double d = value.toDouble();
            if ( d == std::floor( d ) && d >= 0.0
                 && d <= static_cast<double>( std::numeric_limits<Json::Value::UInt64>::max() ) )
            {
                // Integral values become unsigned ints: faultlab scenario
                // fields (seeds, max_bytes) are validated as unsigned.
                out = Json::Value( static_cast<Json::Value::UInt64>( d ) );
            }
            else
            {
                out = Json::Value( d );
            }
            return true;
        }
        case QJsonValue::String:
            out = Json::Value( value.toString().toStdString() );
            return true;
        case QJsonValue::Array:
        {
            const QJsonArray array = value.toArray();
            if ( array.size() > 512 )
            {
                if ( errorOut )
                    *errorOut = QStringLiteral( "scenario array exceeds 512 elements" );
                return false;
            }
            out = Json::Value( Json::arrayValue );
            for ( const QJsonValue &item : array )
            {
                Json::Value converted;
                if ( !qJsonToJsonCpp( item, depth + 1, converted, errorOut ) )
                    return false;
                out.append( std::move( converted ) );
            }
            return true;
        }
        case QJsonValue::Object:
        {
            const QJsonObject object = value.toObject();
            if ( object.size() > 512 )
            {
                if ( errorOut )
                    *errorOut = QStringLiteral( "scenario object exceeds 512 members" );
                return false;
            }
            out = Json::Value( Json::objectValue );
            for ( auto it = object.begin(); it != object.end(); ++it )
            {
                Json::Value converted;
                if ( !qJsonToJsonCpp( it.value(), depth + 1, converted, errorOut ) )
                    return false;
                out[it.key().toStdString()] = std::move( converted );
            }
            return true;
        }
        case QJsonValue::Undefined:
        default:
            if ( errorOut )
                *errorOut = QStringLiteral( "scenario document holds an undefined value" );
            return false;
    }
}

/// Bounded jsoncpp → QJson conversion (same caps as the bridge's
/// jsonCppToQJson: ≤16 depth, ≤512 members/elements; overflow is marked
/// with an explicit entry, never silent). Used to carry the canonical
/// fault report into the teaching VM's evidence block.
QJsonValue jsonCppToQJsonValue( const Json::Value &value, int depth = 0 )
{
    if ( depth > 16 )
        return QStringLiteral( "…truncated" );
    switch ( static_cast<Json::ValueType>( value.type() ) )
    {
        case Json::nullValue:
            return QJsonValue( QJsonValue::Null );
        case Json::booleanValue:
            return value.asBool();
        case Json::intValue:
            return static_cast<double>( value.asInt64() );
        case Json::uintValue:
            return static_cast<double>( value.asUInt64() );
        case Json::realValue:
            return value.asDouble();
        case Json::stringValue:
            return QString::fromStdString( value.asString() );
        case Json::arrayValue:
        {
            if ( value.size() > 512 )
                return QStringLiteral( "…truncated" );
            QJsonArray array;
            for ( const auto &item : value )
                array.append( jsonCppToQJsonValue( item, depth + 1 ) );
            return array;
        }
        case Json::objectValue:
        default:
        {
            QJsonObject object;
            int count = 0;
            for ( const auto &key : value.getMemberNames() )
            {
                if ( ++count > 512 )
                {
                    object.insert( QStringLiteral( "…truncated" ), true );
                    break;
                }
                object.insert( QString::fromStdString( key ),
                               jsonCppToQJsonValue( value[key], depth + 1 ) );
            }
            return object;
        }
    }
}

Diagnostic scenarioInvalid( const QString &detail )
{
    return studioError( QStringLiteral( "experiment_studio.fault_scenario_invalid" ), detail );
}

QString studioIssueFromDiagnostic( const sicnu::faultlab::FaultDiagnostic &diagnostic )
{
    return QStringLiteral( "faultlab.%1: %2" )
        .arg( QString::fromStdString( diagnostic.code ),
              QString::fromStdString( diagnostic.message ) );
}

} // namespace

Result<study::StudyRunSummary> runStudy( experiment::ExperimentStore &store,
                                         experiment::MatrixLedger &ledger,
                                         const study::ParameterStudySpec &spec,
                                         study::IStudyExecutionBackend &backend,
                                         const QString &studyOutputDir,
                                         const std::atomic<bool> &cancelFlag )
{
    study::StudyRunner runner( store, ledger, backend );
    return runner.run( spec, cancelFlag, studyOutputDir );
}

study::StudyReport reportFromStore( experiment::ExperimentStore &store,
                                    experiment::MatrixLedger &ledger,
                                    const study::ParameterStudySpec &spec,
                                    const QVector<study::StudyPoint> &points,
                                    const QVector<study::SpatialDifferenceSummary> &spatialSummaries,
                                    const study::StudyRunSummary *runnerSummary )
{
    return study::buildStudyReport( store, ledger, spec, points, spatialSummaries,
                                    runnerSummary );
}

Result<QString> committedOutputPath( const QString &studyOutputDir, const QString &pointId )
{
    if ( studyOutputDir.trimmed().isEmpty() || pointId.trimmed().isEmpty() )
        return Result<QString>::failure( studioError(
            QStringLiteral( "experiment_studio.output_missing" ),
            QStringLiteral( "no live study output directory or point id given" ) ) );
    const QString path = studyOutputDir + QStringLiteral( "/" ) + pointId
                         + QStringLiteral( "/output.tif" );
    if ( !QFile::exists( path ) )
    {
        return Result<QString>::failure(
            studioError( QStringLiteral( "experiment_studio.output_missing" ),
                         QStringLiteral( "committed study output not found: %1" ).arg( path ) ) );
    }
    return Result<QString>::success( path );
}

Result<SpatialCompareViewModel> compareRecordedPoints( const QString &studyOutputDir,
                                                       const QString &leftPointId,
                                                       const QString &rightPointId, double epsilon,
                                                       SpatialCompareMode mode,
                                                       const StudioResourcePolicy &policy )
{
    const auto left = committedOutputPath( studyOutputDir, leftPointId );
    if ( !left )
        return Result<SpatialCompareViewModel>::failure( left.diagnostics() );
    const auto right = committedOutputPath( studyOutputDir, rightPointId );
    if ( !right )
        return Result<SpatialCompareViewModel>::failure( right.diagnostics() );

    const GdalRasterDifferenceSummarizer summarizer( epsilon );
    return compareSpatialOutputs( leftPointId, rightPointId, left.value(), right.value(),
                                  summarizer, mode, policy );
}

Result<QJsonObject> firstDivergenceReport( experiment::ExperimentStore *store,
                                           const QString &runDirectory,
                                           const QString &referenceRunId,
                                           const QString &studentRunId )
{
    sicnu::experiment::debugger::DirectoryEvidenceSource source( store, runDirectory );
    sicnu::experiment::debugger::RunSnapshotBuilder builder( source );

    const auto referenceRecord = source.run( referenceRunId );
    if ( !referenceRecord )
        return Result<QJsonObject>::failure( referenceRecord.diagnostics() );
    const auto studentRecord = source.run( studentRunId );
    if ( !studentRecord )
        return Result<QJsonObject>::failure( studentRecord.diagnostics() );

    const auto referenceSnapshot = builder.build( referenceRunId );
    if ( !referenceSnapshot )
        return Result<QJsonObject>::failure( referenceSnapshot.diagnostics() );
    const auto studentSnapshot = builder.build( studentRunId );
    if ( !studentSnapshot )
        return Result<QJsonObject>::failure( studentSnapshot.diagnostics() );

    const auto analysis = sicnu::experiment::debugger::FirstDivergenceAnalyzer::analyze(
        referenceRecord.value(), studentRecord.value(), referenceSnapshot.value(),
        studentSnapshot.value() );
    if ( !analysis )
        return Result<QJsonObject>::failure( analysis.diagnostics() );
    return Result<QJsonObject>::success( analysis.value().toJson() );
}

Result<FaultTeachingViewModel> runFaultScenarioTeaching( const QJsonObject &scenarioJson,
                                                         const QString &studentPrediction,
                                                         const QString &sandboxRoot )
{
    Json::Value scenarioDoc;
    QString conversionError;
    if ( !qJsonToJsonCpp( scenarioJson, 0, scenarioDoc, &conversionError ) )
        return Result<FaultTeachingViewModel>::failure( scenarioInvalid( conversionError ) );

    const auto scenario = sicnu::faultlab::loadFaultScenario( scenarioDoc );
    if ( !scenario.ok )
    {
        const QString detail = scenario.diagnostics.empty()
                                   ? QStringLiteral( "scenario refused by the faultlab loader" )
                                   : QString::fromStdString( scenario.diagnostics.front().message );
        return Result<FaultTeachingViewModel>::failure( scenarioInvalid( detail ) );
    }

    FaultRunOptions options;
    options.sandboxRoot = sandboxRoot.toStdString();
    // R21-E (#1613): the production verifier hook — a scenario declaring
    // expected.verifier gets its referenced rules really run over the
    // faulted artifact (fail-closed through the whole chain).
    options.verifierHook = &labFaultVerifierHook;
    const auto run = sicnu::faultlab::runFaultScenario( scenario.value, options );
    if ( !run.ok )
    {
        // The scenario was VALID — the failure is the pipeline start (e.g.
        // sandbox creation), a different family from an invalid document.
        return Result<FaultTeachingViewModel>::failure(
            studioError( QStringLiteral( "experiment_studio.fault_pipeline_failed" ),
                         QStringLiteral( "fault pipeline could not start" ) ) );
    }
    const FaultRunReport &report = run.value;

    const auto prior = projectFaultScenarioPredict( scenarioJson );
    const QString sandboxPath = options.sandboxRoot.empty()
                                    ? QStringLiteral( "<system temp>" )
                                    : QString::fromStdString( options.sandboxRoot );
    FaultTeachingViewModel vm = projectFaultDiagnosis(
        prior, studentPrediction, QString::fromStdString( report.actualDiagnosisSignature ),
        jsonCppToQJsonValue( report.toJson() ).toObject(), sandboxPath,
        QString::fromStdString( report.sourceDigestBefore ),
        QString::fromStdString( report.sourceDigestAfter ) );

    // Honest surfacing of the runner's own diagnostics — a refused fault, a
    // failed expectation or a mutated source is never presented as success.
    for ( const auto &diagnostic : report.diagnostics )
        vm.issues.append( studioIssueFromDiagnostic( diagnostic ) );
    if ( !report.passed )
    {
        vm.issues.append( QStringLiteral( "experiment_studio.fault_scenario_failed:"
                                          " the scenario did not pass (see evidence)" ) );
    }
    return Result<FaultTeachingViewModel>::success( vm );
}

Result<StudioCapsuleRef> exportRunCapsule( const experiment::ExperimentStore &store,
                                           const dataset::DatasetStore &datasets,
                                           const QString &runId, const QString &outputPath,
                                           const QString &workspaceRoot,
                                           const QString &createdUtc )
{
    sicnu::experiment::capsule::CapsuleBuilder builder( store, datasets );
    sicnu::experiment::capsule::CapsuleOptions options;
    options.workspaceRoot = workspaceRoot;
    if ( !createdUtc.isEmpty() )
        options.createdUtc = createdUtc;

    const auto built = builder.build( runId, options );
    if ( !built )
        return Result<StudioCapsuleRef>::failure( built.diagnostics() );

    const auto exported =
        sicnu::experiment::capsule::CapsuleIO::exportCapsule( built.value(), outputPath );
    if ( !exported )
        return Result<StudioCapsuleRef>::failure( exported.diagnostics() );

    // Reload through the real load gates: the ref the Studio stores must
    // point at an artifact that loads, not one that merely was written.
    const auto reloaded = sicnu::experiment::capsule::CapsuleIO::loadCapsule( exported->path );
    if ( !reloaded )
        return Result<StudioCapsuleRef>::failure( reloaded.diagnostics() );

    StudioCapsuleRef ref;
    ref.runId = runId;
    ref.path = exported->path;
    ref.bytes = exported->bytes;
    return Result<StudioCapsuleRef>::success( ref );
}

Result<QJsonObject> capsuleReloadReadiness( const QString &capsulePath,
                                            const dataset::DatasetStore *datasets )
{
    const auto doc = sicnu::experiment::capsule::CapsuleIO::loadCapsule( capsulePath );
    if ( !doc )
        return Result<QJsonObject>::failure( doc.diagnostics() );

    // Unwired hooks: readiness answers Unknown where it cannot know — the
    // Studio never fabricates an Exact.
    const sicnu::experiment::capsule::CapsuleHooks hooks;
    const sicnu::experiment::capsule::CapsuleReadinessHooks readinessHooks;
    const auto readiness = sicnu::experiment::capsule::CapsuleReadiness::assess(
        doc.value(), datasets, hooks, readinessHooks );

    QJsonObject out;
    out.insert( QStringLiteral( "capsule_path" ), capsulePath );
    out.insert( QStringLiteral( "capsule_id" ), doc->capsuleId() );
    out.insert( QStringLiteral( "digest_ok" ), doc->digestValid() );
    out.insert( QStringLiteral( "readiness" ), readiness.toJson() );
    return Result<QJsonObject>::success( out );
}

} // namespace sicnu::experiment_studio::live
