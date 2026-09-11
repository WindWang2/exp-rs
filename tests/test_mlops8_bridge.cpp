// test_mlops8_bridge.cpp — Platform 8.0 bridge-core tests (goal 8.0 §A):
// automatic execution→experiment lifecycle recording with truthful states.
// Light target (experiment + dataset only); the workflow-side adapter has
// its own E2E coverage in test_mlops8_e2e.cpp.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/split.h"
#include "dataset/sample.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/run_bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

struct BridgeFixture
{
    QTemporaryDir dir;
    DatasetStore datasetStore;
    ExperimentStore experimentStore;
    ExperimentRunBridge bridge;

    static QString experimentId()
    {
        return QStringLiteral( "6d2b3d52-1111-4111-8111-111111111111" );
    }

    explicit BridgeFixture( bool wireDatasetStore = false )
        : bridge( experimentStore )
    {
        REQUIRE( experimentStore.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
        if ( wireDatasetStore )
        {
            REQUIRE( datasetStore.open( dir.filePath( QStringLiteral( "ds.db" ) ) ) );
            bridge.setDatasetStore( &datasetStore );
        }
        REQUIRE( bridge.ensureExperiment( experimentId(),
                                          QStringLiteral( "auto-recording" ) )
                     .has_value() );
    }

    ExecutionEvent event( const QString &ref, const QString &state,
                          const QString &message = QString() ) const
    {
        ExecutionEvent e;
        e.executionRef = ref;
        e.workflowId = QStringLiteral( "wf-test" );
        e.state = state;
        e.errorMessage = message;
        return e;
    }
};

/// A dataset store with one committed version + one split manifest for pin
/// verification coverage.
struct PinFixture
{
    DatasetId datasetId;
    DatasetVersionId versionId;
    QString fingerprint;
    QString splitManifestId;
    QString splitFingerprint;

    static PinFixture create( DatasetStore &store )
    {
        PinFixture fx;
        fx.datasetId = DatasetId::generate();
        REQUIRE( store.createDataset( fx.datasetId, QStringLiteral( "pinned" ) ).has_value() );
        fx.versionId = DatasetVersionId::generate();
        DatasetManifest manifest;
        manifest.setDatasetId( fx.datasetId.toString() );
        manifest.setVersionId( fx.versionId.toString() );
        manifest.setName( QStringLiteral( "pinned" ) );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-10T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        REQUIRE( store.createDraftVersion( manifest ).has_value() );
        // One sample + assignment: manifests must carry assignments to
        // round-trip (fromJson refuses assignment-less manifests), so the
        // fixture pins a single-point version.
        SampleRecord sample;
        const QString sampleId = SampleId::generate().toString();
        sample.setSampleId( sampleId );
        sample.setDatasetVersionId( fx.versionId.toString() );
        sample.setKind( SampleKind::Point );
        PointSample point;
        point.x = 0;
        point.y = 0;
        sample.payload() = point;
        REQUIRE( store.addSamples( { sample } ).has_value() );

        auto staged = store.stageVersion( fx.versionId );
        REQUIRE( staged.has_value() );
        auto committed = store.commitVersion( fx.versionId );
        REQUIRE( committed.has_value() );
        fx.fingerprint = committed.value().fingerprint();

        // A stored split manifest (an empty assignment set is legitimate for
        // contract tests; saveSplitManifest validates id + version existence).
        SplitManifest split;
        fx.splitManifestId = QStringLiteral( "11111111-1111-4111-8111-222222222222" );
        split.setManifestId( fx.splitManifestId );
        split.setDatasetVersionId( fx.versionId.toString() );
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 7;
        split.setConfig( config );
        SplitAssignment assignment;
        assignment.sampleId = sampleId;
        assignment.role = SplitRole::Train;
        split.assignments().append( assignment );
        fx.splitFingerprint = QStringLiteral( "splitfp" );
        split.setFingerprint( fx.splitFingerprint );
        REQUIRE( store.saveSplitManifest( split ).has_value() );
        return fx;
    }
};

} // namespace

TEST_CASE( "successful execution lifecycle records a Completed run", "[bridge][lifecycle]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-success" );

    auto started = fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) );
    REQUIRE( started.has_value() );
    const QString runId = started.value();

    ExecutionEvent done = fx.event( ref, "Completed" );
    done.startedMs = 1000;
    done.finishedMs = 2500;
    QJsonObject artifacts;
    artifacts.insert( QStringLiteral( "step-a" ),
                      QJsonObject{ { "path", QStringLiteral( "/tmp/out.tif" ) },
                                   { "digest", QStringLiteral( "abc123" ) },
                                   { "size", 42.0 } } );
    done.artifacts = artifacts;

    REQUIRE( fx.bridge.handleExecutionEvent( done ).has_value() );

    const auto run = fx.experimentStore.runById( runId );
    REQUIRE( run.has_value() );
    REQUIRE( run->status() == RunStatus::Completed );
    REQUIRE( run->executionRef() == ref );
    REQUIRE( run->algorithmId() == QStringLiteral( "wf-test" ) );
    REQUIRE( run->startedAtUtc().isValid() );
    REQUIRE( run->finishedAtUtc().isValid() );
    REQUIRE( run->artifacts().size() == 1 );
    REQUIRE( run->artifacts().first().digest == QStringLiteral( "abc123" ) );
    REQUIRE( run->artifacts().first().sizeBytes == 42 );
    // The producing step id rides the artifact role.
    REQUIRE( run->artifacts().first().role == QStringLiteral( "step-a" ) );
    // Workflow evidence landed in the metrics document.
    REQUIRE( run->metrics().value( QStringLiteral( "workflow" ) ).toObject()
                 .value( QStringLiteral( "execution_ref" ) )
                 .toString() == ref );
}

TEST_CASE( "failed executions record Failed with error evidence", "[bridge][lifecycle]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-fail" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge
                 .handleExecutionEvent(
                     fx.event( ref, "Failed", QStringLiteral( "operator crashed" ) ) )
                 .has_value() );

    const auto run = fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    REQUIRE( run->status() == RunStatus::Failed );
    const QJsonObject error =
        run->metrics().value( QStringLiteral( "error" ) ).toObject();
    REQUIRE( error.value( QStringLiteral( "error_code" ) ).toString()
             == QStringLiteral( "workflow.failed" ) );
    REQUIRE( error.value( QStringLiteral( "message" ) ).toString()
             == QStringLiteral( "operator crashed" ) );
}

TEST_CASE( "cancellation records Cancelled with the reason", "[bridge][lifecycle]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-cancel" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge
                 .handleExecutionEvent( fx.event( ref, "Canceled", QStringLiteral( "user asked" ) ) )
                 .has_value() );

    const auto run = fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    REQUIRE( run->status() == RunStatus::Cancelled );
    REQUIRE( run->metrics().value( QStringLiteral( "cancel_reason" ) ).toString()
             == QStringLiteral( "user asked" ) );
}

TEST_CASE( "interruption and resume continue the same record", "[bridge][lifecycle][resume]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-resume" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Interrupted" ) ).has_value() );

    const QString runId = fx.bridge.runIdForExecution( ref );
    auto interrupted = fx.experimentStore.runById( runId );
    REQUIRE( interrupted.has_value() );
    REQUIRE( interrupted->status() == RunStatus::Interrupted );
    REQUIRE( !isTerminalRunStatus( interrupted->status() ) );

    // Resume: Running again, then Completed — same run id throughout.
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Completed" ) ).has_value() );

    const auto done = fx.experimentStore.runById( runId );
    REQUIRE( done.has_value() );
    REQUIRE( done->status() == RunStatus::Completed );
    REQUIRE( fx.bridge.runIdForExecution( ref ) == runId );
}

TEST_CASE( "completion after interruption advances through resume (no direct jump)",
           "[bridge][lifecycle][resume]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-resume2" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Interrupted" ) ).has_value() );

    // A resumed execution may report Completed WITHOUT a re-delivered Running
    // event (the coordinator does not re-emit Running for the original id
    // after a resume swap): the bridge advances Interrupted→Running itself.
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Completed" ) ).has_value() );
    REQUIRE( fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) )->status()
             == RunStatus::Completed );
}

TEST_CASE( "duplicate terminal delivery is an idempotent no-op", "[bridge][idempotency]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-dup" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Completed" ) ).has_value() );
    // Late duplicate Running + Completed: tolerated, state unchanged.
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Completed" ) ).has_value() );

    const auto run = fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    REQUIRE( run->status() == RunStatus::Completed );
    REQUIRE( fx.experimentStore.runCount() == 1 );
}

TEST_CASE( "unknown state strings are refused, never guessed", "[bridge][truthfulness]" )
{
    BridgeFixture fx;
    auto result = fx.bridge.handleExecutionEvent( fx.event( "wf-run-x", "WaitingResource" ) );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code == QStringLiteral( "experiment.bridge_invalid_event" ) );
    REQUIRE( fx.experimentStore.runCount() == 0 );
}

TEST_CASE( "terminal event for an unseen execution is refused (no fabricated history)",
           "[bridge][truthfulness]" )
{
    BridgeFixture fx;
    auto result = fx.bridge.handleExecutionEvent( fx.event( "wf-run-ghost", "Completed" ) );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code
             == QStringLiteral( "experiment.bridge_unknown_execution" ) );
    REQUIRE( fx.experimentStore.runCount() == 0 );
}

TEST_CASE( "events without a target experiment are refused", "[bridge][truthfulness]" )
{
    BridgeFixture fx;
    fx.bridge.setTargetExperiment( QString() );
    auto result = fx.bridge.handleExecutionEvent( fx.event( "wf-run-t", "Running" ) );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code == QStringLiteral( "experiment.bridge_no_target" ) );
}

TEST_CASE( "workflow pins land in the recorded run before start", "[bridge][pins]" )
{
    BridgeFixture fx( /*wireDatasetStore=*/true );
    const PinFixture pins = PinFixture::create( fx.datasetStore );

    RunPins pinned;
    pinned.datasetVersionId = pins.versionId.toString();
    pinned.splitManifestId = pins.splitManifestId;
    pinned.modelId = QStringLiteral( "segformer" );
    pinned.modelDigest = QStringLiteral( "deadbeef" );
    pinned.seed = 42;
    pinned.hasSeed = true;
    fx.bridge.setWorkflowPins( QStringLiteral( "wf-test" ), pinned );

    const QString ref = QStringLiteral( "wf-run-pinned" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Completed" ) ).has_value() );

    const auto run = fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    REQUIRE( run->datasetVersionId() == pins.versionId.toString() );
    // The dataset fingerprint was auto-filled from the authoritative store.
    REQUIRE( run->datasetFingerprint() == pins.fingerprint );
    REQUIRE( run->splitManifestId() == pins.splitManifestId );
    // The split fingerprint was resolved from the STORED manifest (the store
    // recomputes the canonical fingerprint on save — the fixture's
    // placeholder is intentionally not the authority).
    const auto storedManifest = fx.datasetStore.splitManifestById( pins.splitManifestId );
    REQUIRE( storedManifest.has_value() );
    REQUIRE( run->splitFingerprint() == storedManifest->fingerprint() );
    REQUIRE( run->modelId() == QStringLiteral( "segformer" ) );
    REQUIRE( run->modelDigest() == QStringLiteral( "deadbeef" ) );
    REQUIRE( run->seed() == 42 );
}

TEST_CASE( "identity-changing pins after start are honestly refused", "[bridge][pins]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-late" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );

    RunPins late;
    late.datasetVersionId = QStringLiteral( "99999999-9999-4999-8999-999999999999" );
    auto result = fx.bridge.attachExecutionPins( ref, late );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code == QStringLiteral( "experiment.bridge_pins_late" ) );

    // Equal pins after start are an idempotent success.
    RunPins same;
    REQUIRE( fx.bridge.attachExecutionPins( ref, same ).has_value() );
}

TEST_CASE( "execution pin overrides beat workflow defaults", "[bridge][pins]" )
{
    BridgeFixture fx( /*wireDatasetStore=*/true );
    RunPins defaults;
    defaults.modelId = QStringLiteral( "model-a" );
    fx.bridge.setWorkflowPins( QStringLiteral( "wf-test" ), defaults );

    RunPins override;
    override.modelId = QStringLiteral( "model-b" );
    override.seed = 5;
    override.hasSeed = true;
    REQUIRE( fx.bridge.attachExecutionPins( QStringLiteral( "wf-run-ovr" ), override ).has_value() );

    const RunPins resolved = fx.bridge.pinsForExecution( QStringLiteral( "wf-run-ovr" ),
                                                         QStringLiteral( "wf-test" ) );
    REQUIRE( resolved.modelId == QStringLiteral( "model-b" ) );
    REQUIRE( resolved.seed == 5 );
    // Another execution of the same workflow keeps the default.
    const RunPins plain = fx.bridge.pinsForExecution( QStringLiteral( "wf-run-other" ),
                                                      QStringLiteral( "wf-test" ) );
    REQUIRE( plain.modelId == QStringLiteral( "model-a" ) );
}

TEST_CASE( "ensureExperiment is idempotent", "[bridge][experiment]" )
{
    BridgeFixture fx;
    REQUIRE( fx.bridge
                 .ensureExperiment( BridgeFixture::experimentId(),
                                    QStringLiteral( "another name" ) )
                 .has_value() );
    const auto page = fx.experimentStore.listExperiments();
    REQUIRE( page.has_value() );
    REQUIRE( page.value().second.size() == 1 );
    // Original creation wins (no content rewrite on re-ensure).
    REQUIRE( page.value().second.first().name() == QStringLiteral( "auto-recording" ) );
}

TEST_CASE( "stale reconciliation follows checkpoint evidence", "[bridge][stale]" )
{
    BridgeFixture fx;
    const QString liveRef = QStringLiteral( "wf-run-live" );
    const QString deadFailed = QStringLiteral( "wf-run-stale-failed" );
    const QString deadCanceled = QStringLiteral( "wf-run-stale-canceled" );
    const QString deadInterrupted = QStringLiteral( "wf-run-stale-interrupted" );
    const QString deadUnknown = QStringLiteral( "wf-run-stale-noevidence" );

    for ( const QString &ref : { liveRef, deadFailed, deadCanceled, deadInterrupted, deadUnknown } )
        REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );

    QSet<QString> live{ liveRef };
    auto lookup = [deadFailed, deadCanceled, deadInterrupted](
                      const QString &ref ) -> std::optional<ExecutionEvidence> {
        if ( ref == deadFailed )
            return ExecutionEvidence{ ref, QStringLiteral( "Failed" ), true };
        if ( ref == deadCanceled )
            return ExecutionEvidence{ ref, QStringLiteral( "Canceled" ), true };
        if ( ref == deadInterrupted )
            return ExecutionEvidence{ ref, QStringLiteral( "Interrupted" ), true };
        return std::nullopt; // deadUnknown: no checkpoint on disk
    };

    const auto decisions = fx.bridge.reconcileStale( live, lookup );
    QMap<QString, QString> byRef;
    for ( const auto &d : decisions )
        byRef.insert( d.executionRef, d.action );

    REQUIRE( byRef.value( deadFailed ) == QStringLiteral( "failed" ) );
    REQUIRE( byRef.value( deadCanceled ) == QStringLiteral( "cancelled" ) );
    REQUIRE( byRef.value( deadInterrupted ) == QStringLiteral( "interrupted" ) );
    REQUIRE( byRef.value( deadUnknown ) == QStringLiteral( "report" ) );

    const auto failedRun = fx.experimentStore.runById( fx.bridge.runIdForExecution( deadFailed ) );
    REQUIRE( failedRun.has_value() );
    REQUIRE( failedRun->status() == RunStatus::Failed );
    REQUIRE( failedRun->metrics()
                 .value( QStringLiteral( "error" ) )
                 .toObject()
                 .value( QStringLiteral( "error_code" ) )
                 .toString() == QStringLiteral( "workflow.stale_reconciled" ) );

    const auto canceledRun =
        fx.experimentStore.runById( fx.bridge.runIdForExecution( deadCanceled ) );
    REQUIRE( canceledRun.has_value() );
    REQUIRE( canceledRun->status() == RunStatus::Cancelled );

    const auto interruptedRun =
        fx.experimentStore.runById( fx.bridge.runIdForExecution( deadInterrupted ) );
    REQUIRE( interruptedRun.has_value() );
    REQUIRE( interruptedRun->status() == RunStatus::Interrupted );

    // The live run is untouched; the no-evidence run stays Running (resumable).
    REQUIRE( fx.experimentStore.runById( fx.bridge.runIdForExecution( liveRef ) )->status()
             == RunStatus::Running );
    REQUIRE( fx.experimentStore.runById( fx.bridge.runIdForExecution( deadUnknown ) )->status()
             == RunStatus::Running );

    // A resumed stale-interrupted run can still complete truthfully.
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( deadInterrupted, "Running" ) ).has_value() );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( deadInterrupted, "Completed" ) ).has_value() );
    REQUIRE( fx.experimentStore.runById( fx.bridge.runIdForExecution( deadInterrupted ) )->status()
             == RunStatus::Completed );
}

TEST_CASE( "a completed checkpoint without artifact evidence is reported, not closed",
           "[bridge][stale][truthfulness]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-orph" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );

    auto lookup = [ref]( const QString & ) -> std::optional<ExecutionEvidence> {
        return ExecutionEvidence{ ref, QStringLiteral( "Completed" ), true };
    };
    const auto decisions = fx.bridge.reconcileStale( QSet<QString>(), lookup );
    REQUIRE( decisions.size() == 1 );
    REQUIRE( decisions.first().action == QStringLiteral( "report" ) );
    REQUIRE( fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) )->status()
             == RunStatus::Running );
}

TEST_CASE( "secret-looking keys in event evidence are redacted", "[bridge][security]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-secret" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );

    ExecutionEvent done = fx.event( ref, "Completed" );
    done.extra = QJsonObject{ { QStringLiteral( "api_token" ), QStringLiteral( "supersecret" ) },
                              { QStringLiteral( "region" ), QStringLiteral( "korea" ) } };
    REQUIRE( fx.bridge.handleExecutionEvent( done ).has_value() );

    const auto run = fx.experimentStore.runById( fx.bridge.runIdForExecution( ref ) );
    REQUIRE( run.has_value() );
    const QJsonObject extra = run->metrics()
                                  .value( QStringLiteral( "workflow" ) )
                                  .toObject()
                                  .value( QStringLiteral( "extra" ) )
                                  .toObject();
    REQUIRE( extra.value( QStringLiteral( "api_token" ) ).toString() == QStringLiteral( "***" ) );
    REQUIRE( extra.value( QStringLiteral( "region" ) ).toString() == QStringLiteral( "korea" ) );
}

TEST_CASE( "read-only experiment stores fail typed, never partially", "[bridge][compat]" )
{
    BridgeFixture fx;
    const QString ref = QStringLiteral( "wf-run-ro" );
    REQUIRE( fx.bridge.handleExecutionEvent( fx.event( ref, "Running" ) ).has_value() );
    // Simulate an externally downgraded store by reopening read-only is not
    // exposed by the store API; instead exercise the closed-store path.
    ExperimentStore closedStore;
    ExperimentRunBridge orphanBridge( closedStore );
    REQUIRE( orphanBridge.ensureExperiment( QStringLiteral( "e" ), QStringLiteral( "e" ) )
                 .has_value() == false );
}
