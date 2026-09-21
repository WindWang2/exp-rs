// test_experiment_capsule.cpp — RS14-17 ReproducibilityCapsule.
//
// Slice A: schema contract + deterministic canonicalization + self digest.
// The capsule is a PROJECTION of recorded experiment truth; these tests pin
// the document contract (canonical bytes, digest semantics, shape gates)
// before any builder exists.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "experiment/capsule/capsule_builder.h"
#include "experiment/capsule/capsule_diff.h"
#include "experiment/capsule/capsule_document.h"
#include "experiment/capsule/capsule_io.h"
#include "experiment/capsule/capsule_portability.h"
#include "experiment/capsule/capsule_readiness.h"
#include "experiment/evidence.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

namespace
{

using sicnu::dataset::DatasetId;
using sicnu::dataset::DatasetManifest;
using sicnu::dataset::DatasetStore;
using sicnu::dataset::DatasetVersionId;
using sicnu::experiment::EvidenceProjector;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRun;
using sicnu::experiment::ExperimentStore;
using sicnu::experiment::RunEnvironment;
using sicnu::experiment::capsule::CapsuleBuilder;
using sicnu::experiment::capsule::CapsuleDiffReport;
using sicnu::experiment::capsule::CapsuleDocument;
using sicnu::experiment::capsule::CapsuleIO;
using sicnu::experiment::capsule::CapsuleReadiness;
using sicnu::experiment::capsule::CapsuleReadinessHooks;
using sicnu::experiment::capsule::CapsuleHooks;
using sicnu::experiment::capsule::CapsuleOptions;
using sicnu::experiment::capsule::CapsuleValidation;
using sicnu::experiment::capsule::capsuleDigest;
using sicnu::experiment::capsule::capsuleSha256Hex;
using sicnu::experiment::capsule::capsuleDigestBody;
using sicnu::experiment::capsule::validateShape;

/// A minimal but well-formed capsule payload (no digest section yet).
QJsonObject minimalPayload( const QString &capsuleId = QStringLiteral( "capsule-run-1" ) )
{
    QJsonObject payload;
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 1 );
    payload.insert( QStringLiteral( "schema" ), schema );
    payload.insert( QStringLiteral( "capsule_id" ), capsuleId );
    payload.insert( QStringLiteral( "created_utc" ), QStringLiteral( "2026-09-21T00:00:00Z" ) );
    QJsonObject goal;
    goal.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-1" ) );
    payload.insert( QStringLiteral( "goal" ), goal );
    payload.insert( QStringLiteral( "software" ), QJsonObject{} );
    payload.insert( QStringLiteral( "capabilities" ), QJsonArray{} );
    payload.insert( QStringLiteral( "inputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "parameters" ), QJsonObject{} );
    payload.insert( QStringLiteral( "plan" ), QJsonObject{} );
    payload.insert( QStringLiteral( "environment" ), QJsonObject{} );
    payload.insert( QStringLiteral( "outputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "evidence" ), QJsonObject{} );
    payload.insert( QStringLiteral( "provenance" ), QJsonObject{} );
    return payload;
}

/// Same content as minimalPayload but a DIFFERENT key insertion order — the
/// digest must not be able to tell the difference.
QJsonObject minimalPayloadReordered()
{
    QJsonObject payload;
    payload.insert( QStringLiteral( "parameters" ), QJsonObject{} );
    payload.insert( QStringLiteral( "provenance" ), QJsonObject{} );
    payload.insert( QStringLiteral( "schema" ), [&] {
        QJsonObject s;
        s.insert( QStringLiteral( "version" ), 1 );
        s.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
        return s;
    }() );
    payload.insert( QStringLiteral( "outputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "capsule_id" ), QStringLiteral( "capsule-run-1" ) );
    payload.insert( QStringLiteral( "evidence" ), QJsonObject{} );
    payload.insert( QStringLiteral( "created_utc" ), QStringLiteral( "2026-09-21T00:00:00Z" ) );
    payload.insert( QStringLiteral( "software" ), QJsonObject{} );
    payload.insert( QStringLiteral( "inputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "plan" ), QJsonObject{} );
    payload.insert( QStringLiteral( "environment" ), QJsonObject{} );
    payload.insert( QStringLiteral( "goal" ), [&] {
        QJsonObject g;
        g.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-1" ) );
        return g;
    }() );
    payload.insert( QStringLiteral( "capabilities" ), QJsonArray{} );
    return payload;
}

} // namespace

TEST_CASE( "capsule digest is stable under key insertion order", "[capsule][digest]" )
{
    const QString digestA = capsuleDigest( capsuleDigestBody( minimalPayload() ) );
    const QString digestB = capsuleDigest( capsuleDigestBody( minimalPayloadReordered() ) );
    REQUIRE( !digestA.isEmpty() );
    CHECK( digestA == digestB );
}

TEST_CASE( "capsule digest changes when recorded content changes", "[capsule][digest]" )
{
    QJsonObject changed = minimalPayload();
    QJsonObject goal = changed.value( QStringLiteral( "goal" ) ).toObject();
    goal.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-OTHER" ) );
    changed.insert( QStringLiteral( "goal" ), goal );

    const QString digestA = capsuleDigest( capsuleDigestBody( minimalPayload() ) );
    const QString digestB = capsuleDigest( capsuleDigestBody( changed ) );
    CHECK( digestA != digestB );
}

TEST_CASE( "finalize stamps a self digest that verifies and detects tampering",
           "[capsule][digest]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleDocument doc = finalized.take();
    CHECK( doc.digestValid() );

    // Tamper with recorded content: the recorded digest no longer verifies.
    QJsonObject tamperedRoot = doc.root();
    tamperedRoot.insert( QStringLiteral( "capsule_id" ), QStringLiteral( "capsule-run-1-TAMPERED" ) );
    CapsuleDocument tampered = CapsuleDocument::fromRoot( tamperedRoot );
    CHECK( !tampered.digestValid() );
}

TEST_CASE( "digest body excludes the digest section itself", "[capsule][digest]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleDocument doc = finalized.take();

    // Re-deriving the digest body must not recurse into the digest section.
    const QString recomputed = capsuleDigest( capsuleDigestBody( doc.root() ) );
    CHECK( recomputed == doc.digestValue() );
}

TEST_CASE( "canonical bytes are identical for semantically identical documents",
           "[capsule][canonical]" )
{
    auto a = CapsuleDocument::finalize( minimalPayload() );
    auto b = CapsuleDocument::finalize( minimalPayloadReordered() );
    REQUIRE( a.has_value() );
    REQUIRE( b.has_value() );
    CHECK( a->canonicalBytes() == b->canonicalBytes() );
    CHECK( a->root().value( QStringLiteral( "digest" ) ).toObject()
               .value( QStringLiteral( "value" ) ).toString()
           == b->root().value( QStringLiteral( "digest" ) ).toObject()
                  .value( QStringLiteral( "value" ) ).toString() );
}

TEST_CASE( "finalize refuses payloads that already carry a digest section",
           "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject digest;
    digest.insert( QStringLiteral( "algorithm" ), QStringLiteral( "sha256-canonical-json" ) );
    digest.insert( QStringLiteral( "value" ), QStringLiteral( "00" ) );
    payload.insert( QStringLiteral( "digest" ), digest );

    auto refused = CapsuleDocument::finalize( payload );
    REQUIRE( !refused.has_value() );
    bool sawCode = false;
    for ( const auto &diagnostic : refused.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.digest-present" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: missing schema block is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    payload.remove( QStringLiteral( "schema" ) );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-missing" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unknown schema id is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "some.other.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 1 );
    payload.insert( QStringLiteral( "schema" ), schema );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-unknown" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unsupported schema version is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 99 );
    payload.insert( QStringLiteral( "schema" ), schema );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-unsupported" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: missing capsule id is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    payload.remove( QStringLiteral( "capsule_id" ) );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.identity-missing" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: digest mismatch is refused", "[capsule][schema]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    QJsonObject tamperedRoot = finalized->root();
    QJsonObject digest = tamperedRoot.value( QStringLiteral( "digest" ) ).toObject();
    digest.insert( QStringLiteral( "value" ), QStringLiteral( "deadbeef" ) );
    tamperedRoot.insert( QStringLiteral( "digest" ), digest );

    const CapsuleValidation validation = validateShape( tamperedRoot );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.digest-mismatch" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unknown digest algorithm is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    // Build a complete document, then swap the algorithm without fixing the
    // value: the digest gate must catch the un-verifiable algorithm first.
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    QJsonObject tamperedRoot = finalized->root();
    QJsonObject digest = tamperedRoot.value( QStringLiteral( "digest" ) ).toObject();
    digest.insert( QStringLiteral( "algorithm" ), QStringLiteral( "md5" ) );
    tamperedRoot.insert( QStringLiteral( "digest" ), digest );

    const CapsuleValidation validation = validateShape( tamperedRoot );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.digest-algorithm-unknown" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: a finalized minimal document passes", "[capsule][schema]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    if ( !validation.ok )
    {
        for ( const auto &issue : validation.issues )
            WARN( issue.code.toStdString() << ": " << issue.message.toStdString() );
    }
    CHECK( validation.ok );
    CHECK( validation.issues.empty() );
}

// --- Slice B: builder projections ------------------------------------------

namespace
{

/// Two open stores + one committed dataset version — the minimal recorded
/// truth a capsule builds from.
struct StoreFixture
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    QString versionId;
    QString fingerprint;
    QString manifestJson;
    QString splitFingerprint = QStringLiteral( "sf1" );

    bool open()
    {
        if ( !datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) )
            return false;
        if ( !experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) )
            return false;
        const DatasetId datasetId = DatasetId::generate();
        if ( !datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() )
            return false;
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        const auto draft = datasets.createDraftVersion( manifest );
        if ( !draft.has_value() )
            return false;
        const auto versionIdParsed =
            DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
        if ( !datasets.stageVersion( versionIdParsed ).has_value() )
            return false;
        const auto committed = datasets.commitVersion( versionIdParsed );
        if ( !committed.has_value() )
            return false;
        versionId = committed->versionId();
        fingerprint = committed->fingerprint();
        const auto version =
            datasets.versionById(
                DatasetVersionId::fromString( versionId ).value_or( DatasetVersionId{} ) );
        if ( !version.has_value() )
            return false;
        manifestJson = version->manifestJson();
        return true;
    }

    Experiment addExperiment( const QString &id = QStringLiteral( "exp-1" ) )
    {
        Experiment experiment;
        experiment.setExperimentId( id );
        experiment.setName( QStringLiteral( "land cover mapping" ) );
        experiment.setObjective( QStringLiteral( "Map land cover for the study area" ) );
        (void)experiments.upsertExperiment( experiment );
        return experiment;
    }

    ExperimentRun addRun( const QString &runId = QStringLiteral( "run-1" ),
                          const QString &experimentId = QStringLiteral( "exp-1" ),
                          const QString &algorithmId = QStringLiteral( "rs:classify" ),
                          const QString &datasetVersionOverride = QString(),
                          const QString &datasetFingerprintOverride = QString(),
                          const QVector<ExperimentRun::Artifact> &artifacts = {},
                          const QString &platform = QStringLiteral( "linux" ) )
    {
        ExperimentRun run;
        run.setRunId( runId );
        run.setExperimentId( experimentId );
        run.setAlgorithmId( algorithmId );
        run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
        QJsonObject parameters;
        parameters.insert( QStringLiteral( "bands" ), QJsonArray{ 1, 2, 3 } );
        parameters.insert( QStringLiteral( "model" ), QStringLiteral( "rf" ) );
        run.setParameters( parameters );
        run.setDatasetVersionId( datasetVersionOverride.isEmpty() ? versionId
                                                                  : datasetVersionOverride );
        run.setDatasetFingerprint( datasetFingerprintOverride.isEmpty()
                                       ? fingerprint
                                       : datasetFingerprintOverride );
        run.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
        run.setSplitFingerprint( splitFingerprint );
        run.setSeed( 42 );
        run.setSoftwareRevision( QStringLiteral( "rev-123" ) );
        QJsonObject envFields;
        envFields.insert( QStringLiteral( "platform" ), platform );
        envFields.insert( QStringLiteral( "qt_version" ), QStringLiteral( "6.8" ) );
        run.setEnvironment( RunEnvironment::fromFields( envFields ) );
        run.artifacts() = artifacts;
        // The store only accepts truthful lifecycle transitions: record the
        // run as Created first, then complete it (same pattern as the
        // reproduction-bundle tests).
        run.setStatus( sicnu::dataset::RunStatus::Created );
        if ( !experiments.upsertRun( run ).has_value() )
            return run;
        run.setStatus( sicnu::dataset::RunStatus::Running );
        if ( !experiments.upsertRun( run ).has_value() )
            return run;
        run.setStatus( sicnu::dataset::RunStatus::Completed );
        (void)experiments.upsertRun( run );
        return run;
    }
};

CapsuleOptions fixedOptions()
{
    CapsuleOptions options;
    options.createdUtc = QStringLiteral( "2026-09-21T00:00:00Z" );
    return options;
}

} // namespace

TEST_CASE( "builder projects a recorded run into a shape-valid capsule",
           "[capsule][builder]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions() );
    REQUIRE( built.has_value() );
    const CapsuleDocument doc = built.take();

    const CapsuleValidation validation = validateShape( doc.root() );
    if ( !validation.ok )
    {
        for ( const auto &issue : validation.issues )
            WARN( issue.code.toStdString() << ": " << issue.message.toStdString() );
    }
    CHECK( validation.ok );

    const QJsonObject goal = doc.root().value( QStringLiteral( "goal" ) ).toObject();
    CHECK( goal.value( QStringLiteral( "experiment_id" ) ).toString()
           == QLatin1String( "exp-1" ) );
    // Teaching labs join by id: lab_id mirrors experiment_id (lab_report.h).
    CHECK( goal.value( QStringLiteral( "lab_id" ) ).toString() == QLatin1String( "exp-1" ) );
    CHECK( goal.value( QStringLiteral( "objective" ) ).toString()
           == QLatin1String( "Map land cover for the study area" ) );

    CHECK( doc.root().value( QStringLiteral( "parameters" ) ) == run.parameters() );

    const QJsonObject software = doc.root().value( QStringLiteral( "software" ) ).toObject();
    CHECK( software.value( QStringLiteral( "revision" ) ).toString() == QLatin1String( "rev-123" ) );
    CHECK( software.value( QStringLiteral( "platform" ) ).toString() == QLatin1String( "linux" ) );

    CHECK( doc.capsuleId() == QLatin1String( "capsule-run-1" ) );
    CHECK( doc.root().value( QStringLiteral( "created_utc" ) ).toString()
           == QLatin1String( "2026-09-21T00:00:00Z" ) );
}

TEST_CASE( "builder refuses unknown runs with a typed error", "[capsule][builder]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( QStringLiteral( "nope" ), fixedOptions() );
    REQUIRE( !built.has_value() );
    bool sawCode = false;
    for ( const auto &diagnostic : built.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.run-missing" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "wired capability hook pins the descriptor digest; changes move the capsule",
           "[capsule][builder][capability]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    QJsonObject descriptor;
    descriptor.insert( QStringLiteral( "id" ), QStringLiteral( "rs:classify" ) );
    descriptor.insert( QStringLiteral( "family" ), QStringLiteral( "classify" ) );

    CapsuleHooks hooks;
    hooks.capabilityDescriptor = [&descriptor]( const QString & ) { return descriptor; };

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions(), hooks );
    REQUIRE( built.has_value() );
    const CapsuleDocument doc = built.take();

    const QJsonArray capabilities =
        doc.root().value( QStringLiteral( "capabilities" ) ).toArray();
    REQUIRE( capabilities.size() == 1 );
    const QJsonObject capability = capabilities.at( 0 ).toObject();
    CHECK( capability.value( QStringLiteral( "id" ) ).toString() == QLatin1String( "rs:classify" ) );
    CHECK( capability.value( QStringLiteral( "version" ) ).toString() == QLatin1String( "1.0" ) );
    CHECK( capability.value( QStringLiteral( "source" ) ).toString() == QLatin1String( "hook" ) );
    CHECK( capability.value( QStringLiteral( "digest" ) ).toString() == capsuleDigest( descriptor ) );

    // A different installed capability ⇒ a different capsule identity.
    QJsonObject changed = descriptor;
    changed.insert( QStringLiteral( "family" ), QStringLiteral( "spectral" ) );
    CapsuleHooks changedHooks;
    changedHooks.capabilityDescriptor = [changed]( const QString & ) { return changed; };
    auto rebuilt = builder.build( run.runId(), fixedOptions(), changedHooks );
    REQUIRE( rebuilt.has_value() );
    CHECK( rebuilt->digestValue() != doc.digestValue() );
}

TEST_CASE( "unwired capability hook records source=record and never fabricates a digest",
           "[capsule][builder][capability]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions() );
    REQUIRE( built.has_value() );

    const QJsonArray capabilities =
        built->root().value( QStringLiteral( "capabilities" ) ).toArray();
    REQUIRE( capabilities.size() == 1 );
    const QJsonObject capability = capabilities.at( 0 ).toObject();
    CHECK( capability.value( QStringLiteral( "source" ) ).toString() == QLatin1String( "record" ) );
    CHECK( capability.value( QStringLiteral( "digest" ) ).toString().isEmpty() );
}

TEST_CASE( "wired plan hook pins the workflow definition digest", "[capsule][builder][plan]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun( QStringLiteral( "run-1" ),
                                              QStringLiteral( "exp-1" ),
                                              QStringLiteral( "wf:landcover" ) );

    CapsuleHooks hooks;
    hooks.planDefinitionDigest = []( const QString &id ) {
        return id == QLatin1String( "wf:landcover" ) ? QLatin1String( "plan-digest-abc" )
                                                     : QString();
    };

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions(), hooks );
    REQUIRE( built.has_value() );
    const QJsonObject plan = built->root().value( QStringLiteral( "plan" ) ).toObject();
    CHECK( plan.value( QStringLiteral( "algorithm_id" ) ).toString()
           == QLatin1String( "wf:landcover" ) );
    CHECK( plan.value( QStringLiteral( "definition_digest" ) ).toString()
           == QLatin1String( "plan-digest-abc" ) );
}

TEST_CASE( "dataset pin resolves the version and pins the manifest digest",
           "[capsule][builder][inputs]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions() );
    REQUIRE( built.has_value() );

    const QJsonArray inputs = built->root().value( QStringLiteral( "inputs" ) ).toArray();
    REQUIRE( inputs.size() >= 1 );
    const QJsonObject datasetPin = inputs.at( 0 ).toObject();
    CHECK( datasetPin.value( QStringLiteral( "kind" ) ).toString()
           == QLatin1String( "dataset_version" ) );
    CHECK( datasetPin.value( QStringLiteral( "id" ) ).toString() == fixture.versionId );
    CHECK( datasetPin.value( QStringLiteral( "digest" ) ).toString() == fixture.fingerprint );
    CHECK( datasetPin.value( QStringLiteral( "manifest_digest" ) ).toString()
           == capsuleSha256Hex( fixture.manifestJson.toUtf8() ) );
    CHECK( !datasetPin.value( QStringLiteral( "state" ) ).toString().isEmpty() );
}

TEST_CASE( "unresolvable dataset version is recorded as unresolved, never dropped",
           "[capsule][builder][inputs]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QStringLiteral( "33333333-3333-4333-8333-333333333333" ), QStringLiteral( "ghost-fp" ) );

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions() );
    REQUIRE( built.has_value() );

    const QJsonArray inputs = built->root().value( QStringLiteral( "inputs" ) ).toArray();
    REQUIRE( inputs.size() >= 1 );
    const QJsonObject datasetPin = inputs.at( 0 ).toObject();
    CHECK( datasetPin.value( QStringLiteral( "state" ) ).toString() == QLatin1String( "unresolved" ) );

    bool sawWarning = false;
    for ( const auto &diagnostic : built.diagnostics() )
        if ( diagnostic.severity == sicnu::dataset::DiagnosticSeverity::Warning )
            sawWarning = true;
    CHECK( sawWarning );
}

TEST_CASE( "identical store content builds byte-identical capsules",
           "[capsule][builder][determinism]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto first = builder.build( run.runId(), fixedOptions() );
    auto second = builder.build( run.runId(), fixedOptions() );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    CHECK( first->canonicalBytes() == second->canonicalBytes() );
}

// --- Slice C: outputs / evidence / provenance -------------------------------

namespace
{

QVector<ExperimentRun::Artifact> twoArtifacts( const QString &insidePath,
                                               const QString &outsidePath )
{
    ExperimentRun::Artifact primary;
    primary.path = insidePath;
    primary.role = QStringLiteral( "primary" );
    primary.digest = QStringLiteral( "digest-primary" );
    primary.sizeBytes = 100;
    ExperimentRun::Artifact sidecar;
    sidecar.path = outsidePath;
    sidecar.role = QStringLiteral( "sidecar" );
    sidecar.digest = QStringLiteral( "digest-sidecar" );
    sidecar.sizeBytes = 5;
    return { primary, sidecar };
}

} // namespace

TEST_CASE( "outputs project recorded artifacts as digest-pinned portable refs",
           "[capsule][builder][outputs]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const QString inside = fixture.dir.filePath( QStringLiteral( "out/raster.tif" ) );
    const QString outside = QStringLiteral( "/mnt/external_drive/other.tif" );
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QString(), QString(), twoArtifacts( inside, outside ) );

    CapsuleOptions options = fixedOptions();
    options.workspaceRoot = fixture.dir.path();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), options );
    REQUIRE( built.has_value() );

    const QJsonArray outputs = built->root().value( QStringLiteral( "outputs" ) ).toArray();
    REQUIRE( outputs.size() == 2 );
    const QJsonObject primary = outputs.at( 0 ).toObject();
    CHECK( primary.value( QStringLiteral( "portable_ref" ) ).toString()
           == QLatin1String( "workspace:out/raster.tif" ) );
    CHECK( primary.value( QStringLiteral( "digest" ) ).toString()
           == QLatin1String( "digest-primary" ) );
    CHECK( primary.value( QStringLiteral( "size_bytes" ) ).toInt() == 100 );
    CHECK( primary.value( QStringLiteral( "role" ) ).toString() == QLatin1String( "primary" ) );
    CHECK( primary.value( QStringLiteral( "state" ) ).toString() == QLatin1String( "pinned" ) );

    const QJsonObject sidecar = outputs.at( 1 ).toObject();
    CHECK( sidecar.value( QStringLiteral( "portable_ref" ) ).toString()
           == QLatin1String( "external:other.tif" ) );

    // No absolute path — inside or outside the workspace — may appear
    // anywhere in the document.
    const QByteArray bytes = built->canonicalBytes();
    CHECK( !bytes.contains( fixture.dir.path().toUtf8() ) );
    CHECK( !bytes.contains( "/mnt/external_drive" ) );
}

TEST_CASE( "artifact without a recorded digest is labeled no-digest, never fabricated",
           "[capsule][builder][outputs]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    ExperimentRun::Artifact naked;
    naked.path = fixture.dir.filePath( QStringLiteral( "out/plot.png" ) );
    naked.role = QStringLiteral( "report" );
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QString(), QString(), { naked } );

    CapsuleOptions options = fixedOptions();
    options.workspaceRoot = fixture.dir.path();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), options );
    REQUIRE( built.has_value() );

    const QJsonArray outputs = built->root().value( QStringLiteral( "outputs" ) ).toArray();
    REQUIRE( outputs.size() == 1 );
    const QJsonObject output = outputs.at( 0 ).toObject();
    CHECK( output.value( QStringLiteral( "state" ) ).toString() == QLatin1String( "no-digest" ) );
    CHECK( output.value( QStringLiteral( "digest" ) ).toString().isEmpty() );
}

TEST_CASE( "evidence mirrors the projector completeness and never leaks raw paths",
           "[capsule][builder][evidence]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const QString inside = fixture.dir.filePath( QStringLiteral( "out/raster.tif" ) );
    ExperimentRun::Artifact primary;
    primary.path = inside;
    primary.role = QStringLiteral( "primary" );
    primary.digest = QStringLiteral( "digest-primary" );
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QString(), QString(), QVector<ExperimentRun::Artifact>{ primary } );
    sicnu::experiment::MetricRecord record;
    record.runId = run.runId();
    record.protocol.setDatasetVersionId( run.datasetVersionId() );
    record.protocol.setSplitManifestId( run.splitManifestId() );
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), 0.85 } };
    REQUIRE( fixture.experiments.saveMetricRecord( record ).has_value() );

    CapsuleOptions options = fixedOptions();
    options.workspaceRoot = fixture.dir.path();

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), options );
    REQUIRE( built.has_value() );

    const QJsonObject evidence = built->root().value( QStringLiteral( "evidence" ) ).toObject();
    CHECK( evidence.value( QStringLiteral( "schema_version" ) ).toInt()
           == sicnu::experiment::kEvidenceSchemaVersion );
    CHECK( evidence.contains( QStringLiteral( "completeness" ) ) );
    CHECK( !evidence.contains( QStringLiteral( "artifacts" ) ) );

    // Same completeness verdict as the projector for the same run.
    EvidenceProjector::Input input;
    input.run = run;
    input.metricRecord = fixture.experiments.metricRecordForRun( run.runId() );
    auto summary = EvidenceProjector::summarize( input );
    REQUIRE( summary.has_value() );
    CHECK( evidence.value( QStringLiteral( "completeness" ) )
           == summary->value( QStringLiteral( "completeness" ) ) );

    CHECK( !built->canonicalBytes().contains( fixture.dir.path().toUtf8() ) );
}

TEST_CASE( "verifier hook summary is embedded verbatim; unwired stays empty",
           "[capsule][builder][evidence]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();

    CapsuleHooks hooks;
    hooks.verifierSummary = []( const QString & ) {
        return QJsonObject{ { QStringLiteral( "verdict" ), QStringLiteral( "pass" ) },
                            { QStringLiteral( "score" ), 0.9 } };
    };

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto wired = builder.build( run.runId(), fixedOptions(), hooks );
    auto unwired = builder.build( run.runId(), fixedOptions() );
    REQUIRE( wired.has_value() );
    REQUIRE( unwired.has_value() );

    const QJsonObject wiredEvidence =
        wired->root().value( QStringLiteral( "evidence" ) ).toObject();
    CHECK( wiredEvidence.value( QStringLiteral( "verifier" ) ).toObject()
               .value( QStringLiteral( "verdict" ) ).toString() == QLatin1String( "pass" ) );
    CHECK( wiredEvidence.value( QStringLiteral( "verifier" ) ).toObject()
               .value( QStringLiteral( "score" ) ).toDouble() == 0.9 );

    const QJsonObject unwiredEvidence =
        unwired->root().value( QStringLiteral( "evidence" ) ).toObject();
    CHECK( unwiredEvidence.value( QStringLiteral( "verifier" ) ).toObject().isEmpty() );
}

TEST_CASE( "provenance projects the lineage slice and pins its digest",
           "[capsule][builder][provenance]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();
    // The auto-recording bridge (ADR 0143) records lineage edges; plain
    // store inserts do not — mirror the bridge here.
    REQUIRE( fixture.experiments
                 .addLineageEdge( QStringLiteral( "experiment" ),
                                  QStringLiteral( "exp-1" ),
                                  QStringLiteral( "produced" ),
                                  QStringLiteral( "run" ), run.runId() )
                 .has_value() );

    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), fixedOptions() );
    REQUIRE( built.has_value() );

    const QJsonObject provenance =
        built->root().value( QStringLiteral( "provenance" ) ).toObject();
    const QJsonArray edges = provenance.value( QStringLiteral( "run_edges" ) ).toArray();
    REQUIRE( !edges.isEmpty() );
    bool sawExperimentEdge = false;
    for ( const auto &edge : edges )
        if ( edge.toObject().value( QStringLiteral( "other_id" ) ).toString()
             == QLatin1String( "exp-1" ) )
            sawExperimentEdge = true;
    CHECK( sawExperimentEdge );

    QJsonObject slice;
    slice.insert( QStringLiteral( "run_edges" ), edges );
    CHECK( provenance.value( QStringLiteral( "slice_digest" ) ).toString()
           == capsuleDigest( slice ) );
}

// --- Slice D: export / load / validate --------------------------------------

namespace
{

/// Builds a small valid capsule from a fresh fixture, with the workspace
/// root set so the artifact paths are portable.
struct BuiltCapsule
{
    StoreFixture fixture;
    sicnu::experiment::capsule::CapsuleDocument doc;

    bool build()
    {
        if ( !fixture.open() )
            return false;
        fixture.addExperiment();
        ExperimentRun::Artifact artifact;
        artifact.path = fixture.dir.filePath( QStringLiteral( "out/raster.tif" ) );
        artifact.role = QStringLiteral( "primary" );
        artifact.digest = QStringLiteral( "digest-primary" );
        const ExperimentRun run = fixture.addRun(
            QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
            QString(), QString(), QVector<ExperimentRun::Artifact>{ artifact } );
        CapsuleOptions options = fixedOptions();
        options.workspaceRoot = fixture.dir.path();
        const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
        auto built = builder.build( run.runId(), options );
        if ( !built.has_value() )
            return false;
        doc = built.take();
        return true;
    }
};

} // namespace

TEST_CASE( "export/load round-trips the canonical bytes and digest",
           "[capsule][io]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "sub/capsule.json" ) );

    const auto exported = CapsuleIO::exportCapsule( built.doc, path );
    REQUIRE( exported.has_value() );
    CHECK( exported->bytes == built.doc.canonicalBytes().size() );

    const auto loaded = CapsuleIO::loadCapsule( path );
    REQUIRE( loaded.has_value() );
    CHECK( loaded->digestValue() == built.doc.digestValue() );
    CHECK( loaded->canonicalBytes() == built.doc.canonicalBytes() );
}

TEST_CASE( "load refuses a semantically identical but reformatted file",
           "[capsule][io][canonical]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "capsule.json" ) );

    // Pretty-print the same document — semantically identical, not canonical.
    const QJsonDocument pretty( built.doc.root() );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( pretty.toJson( QJsonDocument::Indented ) );
    file.close();

    auto refused = CapsuleIO::loadCapsule( path );
    REQUIRE( !refused.has_value() );
    bool sawCode = false;
    for ( const auto &diagnostic : refused.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.not-canonical" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "load refuses digest tampering", "[capsule][io][tamper]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QJsonObject root = built.doc.root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonObject parameters = root.value( QStringLiteral( "parameters" ) ).toObject();
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "swapped" ) );
    root.insert( QStringLiteral( "parameters" ), parameters );
    auto refinalized = CapsuleDocument::finalize( root );
    REQUIRE( refinalized.has_value() );

    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "capsule.json" ) );
    REQUIRE( CapsuleIO::exportCapsule( refinalized.take(), path ).has_value() );

    // Now tamper on disk: rewrite one canonical member value in place.
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    QByteArray bytes = file.readAll();
    file.close();
    QByteArray tampered = bytes;
    tampered.replace( "\"swapped\"", "\"forged\"" );
    REQUIRE( tampered != bytes );
    QFile out( path );
    REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    out.write( tampered );
    out.close();

    auto refused = CapsuleIO::loadCapsule( path );
    REQUIRE( !refused.has_value() );
    bool sawMismatch = false;
    for ( const auto &diagnostic : refused.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.digest-mismatch" ) )
            sawMismatch = true;
    CHECK( sawMismatch );
}

TEST_CASE( "export refuses a document whose self digest does not verify",
           "[capsule][io]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QJsonObject root = built.doc.root();
    root.insert( QStringLiteral( "capsule_id" ), QStringLiteral( "capsule-forged" ) );
    const CapsuleDocument forged = CapsuleDocument::fromRoot( root );
    CHECK( !forged.digestValid() );

    QTemporaryDir dir;
    auto refused = CapsuleIO::exportCapsule( forged, dir.filePath( QStringLiteral( "c.json" ) ) );
    REQUIRE( !refused.has_value() );
    CHECK( refused.diagnostics().first().code == QLatin1String( "capsule.digest-mismatch" ) );
}

TEST_CASE( "validate refuses secret-shaped members and values (fail closed)",
           "[capsule][io][secrets]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );

    // Plant a credential-shaped KEY into parameters (post-build — the
    // builder itself would have masked it) and re-stamp a valid digest so
    // only the CONTENT gate can catch it.
    QJsonObject root = built.doc.root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonObject parameters = root.value( QStringLiteral( "parameters" ) ).toObject();
    parameters.insert( QStringLiteral( "api_token" ), QStringLiteral( "hunter2" ) );
    root.insert( QStringLiteral( "parameters" ), parameters );
    auto replanted = CapsuleDocument::finalize( root );
    REQUIRE( replanted.has_value() );

    const CapsuleValidation validation = CapsuleIO::validate( replanted.take() );
    CHECK( !validation.ok );
    bool sawSecret = false;
    for ( const auto &issue : validation.issues )
    {
        INFO( issue.code.toStdString() );
        if ( issue.code == QLatin1String( "capsule.secret-detected" ) )
            sawSecret = true;
    }
    CHECK( sawSecret );
}

TEST_CASE( "validate refuses credential-shaped VALUES, not just keys",
           "[capsule][io][secrets]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QJsonObject root = built.doc.root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonObject parameters = root.value( QStringLiteral( "parameters" ) ).toObject();
    parameters.insert( QStringLiteral( "note" ),
                       QStringLiteral( "fallback sk-abcdefghijklmnopqrst" ) );
    root.insert( QStringLiteral( "parameters" ), parameters );
    auto replanted = CapsuleDocument::finalize( root );
    REQUIRE( replanted.has_value() );
    const CapsuleValidation validation = CapsuleIO::validate( replanted.take() );
    CHECK( !validation.ok );
    bool sawSecretValue = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.secret-detected" ) )
            sawSecretValue = true;
    CHECK( sawSecretValue );
}

TEST_CASE( "validate refuses absolute-path values anywhere in the document",
           "[capsule][io][paths]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    QJsonObject root = built.doc.root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonObject goal = root.value( QStringLiteral( "goal" ) ).toObject();
    goal.insert( QStringLiteral( "data_root" ), QStringLiteral( "/home/student/data" ) );
    root.insert( QStringLiteral( "goal" ), goal );
    auto replanted = CapsuleDocument::finalize( root );
    REQUIRE( replanted.has_value() );

    const CapsuleValidation validation = CapsuleIO::validate( replanted.take() );
    CHECK( !validation.ok );
    bool sawPath = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.absolute-path" ) )
            sawPath = true;
    CHECK( sawPath );
}

TEST_CASE( "validate passes a clean capsule with per-gate evidence",
           "[capsule][io]" )
{
    BuiltCapsule built;
    REQUIRE( built.build() );
    const CapsuleValidation validation = CapsuleIO::validate( built.doc );
    if ( !validation.ok )
    {
        for ( const auto &issue : validation.issues )
            WARN( issue.code.toStdString() << ": " << issue.message.toStdString() );
    }
    CHECK( validation.ok );
    CHECK( !validation.checks.isEmpty() );
}

// --- Slice E: replay readiness from a capsule --------------------------------

namespace
{

/// A fixture whose run carries a REAL split manifest (config + assignments —
/// SplitManifest::fromJson refuses degenerate documents) and whose capsule
/// pins capability descriptor + plan definition digests, so a fully wired
/// readiness assessment can reach Exact.
struct ReadyFixture
{
    StoreFixture fixture;
    sicnu::experiment::capsule::CapsuleDocument doc;
    QJsonObject descriptor;

    bool build()
    {
        if ( !fixture.open() )
            return false;
        fixture.addExperiment();

        // A split manifest that round-trips, saved first so the run can pin
        // the STORE-DERIVED fingerprint.
        sicnu::dataset::SplitManifest manifest;
        manifest.setManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
        manifest.setDatasetVersionId( fixture.versionId );
        sicnu::dataset::SplitConfig config;
        config.method = sicnu::dataset::SplitMethod::Random;
        config.seed = 7;
        manifest.setConfig( config );
        sicnu::dataset::SplitAssignment assignment;
        assignment.sampleId = QStringLiteral( "sample-1" );
        assignment.role = sicnu::dataset::SplitRole::Train;
        manifest.assignments().append( assignment );
        if ( !fixture.datasets.saveSplitManifest( manifest ).has_value() )
            return false;
        const auto saved = fixture.datasets.splitManifestById( manifest.manifestId() );
        if ( !saved.has_value() )
            return false;
        fixture.splitFingerprint = saved->fingerprint();

        const ExperimentRun run = fixture.addRun();

        // Build with hooks pinning the capability descriptor + plan digest.
        descriptor.insert( QStringLiteral( "id" ), QStringLiteral( "rs:classify" ) );
        descriptor.insert( QStringLiteral( "family" ), QStringLiteral( "classify" ) );
        CapsuleHooks buildHooks;
        buildHooks.capabilityDescriptor = [this]( const QString & ) { return descriptor; };
        buildHooks.planDefinitionDigest = []( const QString & ) {
            return QLatin1String( "plan-digest-abc" );
        };
        CapsuleOptions options = fixedOptions();
        options.workspaceRoot = fixture.dir.path();
        const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
        auto built = builder.build( run.runId(), options, buildHooks );
        if ( !built.has_value() )
            return false;
        doc = built.take();
        return true;
    }
};

/// Hooks matching what the fixture pinned — the "same machine" case.
CapsuleHooks matchingHooks( const QJsonObject &descriptor )
{
    CapsuleHooks hooks;
    hooks.capabilityDescriptor = [descriptor]( const QString & ) { return descriptor; };
    hooks.planDefinitionDigest = []( const QString & ) {
        return QLatin1String( "plan-digest-abc" );
    };
    return hooks;
}

CapsuleReadinessHooks matchingReadinessHooks()
{
    CapsuleReadinessHooks hooks;
    hooks.currentSoftwareRevision = [] { return QStringLiteral( "rev-123" ); };
    hooks.outputAvailable = []( const QString &, const QString &, qint64 ) { return true; };
    return hooks;
}

} // namespace

TEST_CASE( "fully resolvable capsule assesses Exact", "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );

    const auto report = CapsuleReadiness::assess( ready.doc, &ready.fixture.datasets,
                                                  matchingHooks( ready.descriptor ),
                                                  matchingReadinessHooks() );
    if ( report.level != sicnu::dataset::ReproductionLevel::Exact )
    {
        for ( const auto &check : report.checks )
            WARN( check.dependency.toStdString() << " -> "
                  << replayCheckStatusToString( check.status ).toStdString() << " ("
                  << check.detail.toStdString() << ")" );
    }
    CHECK( report.level == sicnu::dataset::ReproductionLevel::Exact );
    CHECK( report.missingDependencyDiagnostics().isEmpty() );
}

TEST_CASE( "missing dataset version blocks replay with a diagnostic",
           "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );
    StoreFixture emptyFixture;
    REQUIRE( emptyFixture.open() );

    const auto report = CapsuleReadiness::assess( ready.doc, &emptyFixture.datasets,
                                                  matchingHooks( ready.descriptor ),
                                                  matchingReadinessHooks() );
    CHECK( report.level == sicnu::dataset::ReproductionLevel::Impossible );
    CHECK( !report.missingDependencyDiagnostics().isEmpty() );
}

TEST_CASE( "dataset fingerprint drift is a Mismatched, not a silent pass",
           "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );
    // A different store whose version carries DIFFERENT content under the
    // id the capsule will be pointed at.
    StoreFixture drifted;
    REQUIRE( drifted.open() );
    drifted.addExperiment();
    sicnu::dataset::SplitManifest manifest;
    manifest.setManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    manifest.setDatasetVersionId( drifted.versionId );
    sicnu::dataset::SplitConfig config;
    config.method = sicnu::dataset::SplitMethod::Random;
    config.seed = 7;
    manifest.setConfig( config );
    sicnu::dataset::SplitAssignment assignment;
    assignment.sampleId = QStringLiteral( "sample-1" );
    assignment.role = sicnu::dataset::SplitRole::Train;
    manifest.assignments().append( assignment );
    REQUIRE( drifted.datasets.saveSplitManifest( manifest ).has_value() );
    // Point the capsule's dataset pin at the drifted store's version id but
    // keep the ORIGINAL fingerprint: same id, changed content.
    QJsonObject root = ready.doc.root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonArray inputs = root.value( QStringLiteral( "inputs" ) ).toArray();
    QJsonObject pin = inputs.at( 0 ).toObject();
    pin.insert( QStringLiteral( "id" ), drifted.versionId );
    inputs.replace( 0, pin );
    root.insert( QStringLiteral( "inputs" ), inputs );
    auto replanted = CapsuleDocument::finalize( root );
    REQUIRE( replanted.has_value() );

    const auto report = CapsuleReadiness::assess( replanted.take(), &drifted.datasets,
                                                  matchingHooks( ready.descriptor ),
                                                  matchingReadinessHooks() );
    bool sawMismatch = false;
    for ( const auto &check : report.checks )
        if ( check.dependency == QLatin1String( "dataset_version" )
             && check.status == sicnu::experiment::ReplayCheckStatus::Mismatched )
            sawMismatch = true;
    CHECK( sawMismatch );
    CHECK( report.level == sicnu::dataset::ReproductionLevel::Impossible );
}

TEST_CASE( "unwired hooks downgrade to BestEffort, never a fake Exact",
           "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );

    const auto report = CapsuleReadiness::assess( ready.doc, &ready.fixture.datasets, {}, {} );
    CHECK( report.level == sicnu::dataset::ReproductionLevel::BestEffort );
    bool sawUnknown = false;
    for ( const auto &check : report.checks )
        if ( check.status == sicnu::experiment::ReplayCheckStatus::Unknown )
            sawUnknown = true;
    CHECK( sawUnknown );
}

TEST_CASE( "capability descriptor changed since capture is a Mismatched",
           "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );
    QJsonObject changed = ready.descriptor;
    changed.insert( QStringLiteral( "family" ), QStringLiteral( "spectral" ) );
    CapsuleHooks hooks = matchingHooks( changed );

    const auto report = CapsuleReadiness::assess( ready.doc, &ready.fixture.datasets, hooks,
                                                  matchingReadinessHooks() );
    CHECK( report.level == sicnu::dataset::ReproductionLevel::Impossible );
    bool sawCapabilityMismatch = false;
    for ( const auto &check : report.checks )
        if ( check.dependency.startsWith( QLatin1String( "capability:" ) )
             && check.status == sicnu::experiment::ReplayCheckStatus::Mismatched )
            sawCapabilityMismatch = true;
    CHECK( sawCapabilityMismatch );
}

TEST_CASE( "software revision drift is reported as a Mismatched pin",
           "[capsule][readiness]" )
{
    ReadyFixture ready;
    REQUIRE( ready.build() );
    CapsuleReadinessHooks hooks = matchingReadinessHooks();
    hooks.currentSoftwareRevision = [] { return QStringLiteral( "OTHER-REV" ); };

    const auto report = CapsuleReadiness::assess( ready.doc, &ready.fixture.datasets,
                                                  matchingHooks( ready.descriptor ), hooks );
    CHECK( report.level == sicnu::dataset::ReproductionLevel::Impossible );
    bool sawSoftwareMismatch = false;
    for ( const auto &check : report.checks )
        if ( check.dependency == QLatin1String( "software_revision" )
             && check.status == sicnu::experiment::ReplayCheckStatus::Mismatched )
            sawSoftwareMismatch = true;
    CHECK( sawSoftwareMismatch );
}

// --- Slice F: capsule diff ----------------------------------------------------

TEST_CASE( "diff of two builds of the same recorded content is Identical",
           "[capsule][diff]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto first = builder.build( run.runId(), fixedOptions() );
    auto second = builder.build( run.runId(), fixedOptions() );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );

    const CapsuleDiffReport report = CapsuleDiffReport::diff( first.take(), second.take() );
    CHECK( report.level == CapsuleDiffReport::Level::Identical );
    CHECK( report.sections.isEmpty() );
}

TEST_CASE( "parameter change is an IdentityBreak naming the section",
           "[capsule][diff]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto first = builder.build( run.runId(), fixedOptions() );
    auto second = builder.build( run.runId(), fixedOptions() );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    CapsuleDocument unchanged = first.take();

    // Change a recorded parameter of the second document (post-build,
    // re-stamped — diff works on documents, not stores).
    QJsonObject root = second->root();
    root.remove( QStringLiteral( "digest" ) );
    QJsonObject parameters = root.value( QStringLiteral( "parameters" ) ).toObject();
    parameters.insert( QStringLiteral( "model" ), QStringLiteral( "svm" ) );
    root.insert( QStringLiteral( "parameters" ), parameters );
    auto changed = CapsuleDocument::finalize( root );
    REQUIRE( changed.has_value() );

    const CapsuleDiffReport report = CapsuleDiffReport::diff( unchanged, changed.take() );
    CHECK( report.level == CapsuleDiffReport::Level::IdentityBreak );
    bool sawParameterPath = false;
    for ( const auto &section : report.sections )
        if ( section.section.startsWith( QLatin1String( "parameters." ) )
             && section.kind == QLatin1String( "identity" ) )
            sawParameterPath = true;
    CHECK( sawParameterPath );
}

TEST_CASE( "environment-only drift is EquivalentRerun — reported, not punished",
           "[capsule][diff]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun linuxRun = fixture.addRun( QStringLiteral( "run-1" ) );
    const ExperimentRun windowsRun =
        fixture.addRun( QStringLiteral( "run-2" ), QStringLiteral( "exp-1" ),
                        QStringLiteral( "rs:classify" ), QString(), QString(), {},
                        QStringLiteral( "windows" ) );
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto linuxCapsule = builder.build( linuxRun.runId(), fixedOptions() );
    auto windowsCapsule = builder.build( windowsRun.runId(), fixedOptions() );
    REQUIRE( linuxCapsule.has_value() );
    REQUIRE( windowsCapsule.has_value() );

    const CapsuleDiffReport report =
        CapsuleDiffReport::diff( linuxCapsule.take(), windowsCapsule.take() );
    CHECK( report.level == CapsuleDiffReport::Level::EquivalentRerun );
    REQUIRE( !report.sections.isEmpty() );
    for ( const auto &section : report.sections )
    {
        INFO( section.section.toStdString() );
        CHECK( section.kind == QLatin1String( "reported" ) );
    }
    bool sawEnvironment = false;
    for ( const auto &section : report.sections )
        if ( section.section.startsWith( QLatin1String( "environment" ) ) )
            sawEnvironment = true;
    CHECK( sawEnvironment );
}

TEST_CASE( "creation instant drift alone is EquivalentRerun", "[capsule][diff]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun();
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    CapsuleOptions earlyOptions = fixedOptions();
    CapsuleOptions lateOptions = fixedOptions();
    lateOptions.createdUtc = QStringLiteral( "2026-09-22T00:00:00Z" );
    auto early = builder.build( run.runId(), earlyOptions );
    auto late = builder.build( run.runId(), lateOptions );
    REQUIRE( early.has_value() );
    REQUIRE( late.has_value() );

    const CapsuleDiffReport report = CapsuleDiffReport::diff( early.take(), late.take() );
    CHECK( report.level == CapsuleDiffReport::Level::EquivalentRerun );
    bool sawCreated = false;
    for ( const auto &section : report.sections )
        if ( section.section == QLatin1String( "created_utc" ) )
            sawCreated = true;
    CHECK( sawCreated );
}

TEST_CASE( "diff direction only swaps left/right, never the verdict",
           "[capsule][diff]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    fixture.addExperiment();
    const ExperimentRun linuxRun = fixture.addRun( QStringLiteral( "run-1" ) );
    const ExperimentRun windowsRun =
        fixture.addRun( QStringLiteral( "run-2" ), QStringLiteral( "exp-1" ),
                        QStringLiteral( "rs:classify" ), QString(), QString(), {},
                        QStringLiteral( "windows" ) );
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto linuxCapsule = builder.build( linuxRun.runId(), fixedOptions() );
    auto windowsCapsule = builder.build( windowsRun.runId(), fixedOptions() );
    REQUIRE( linuxCapsule.has_value() );
    REQUIRE( windowsCapsule.has_value() );
    CapsuleDocument linuxDoc = linuxCapsule.take();
    CapsuleDocument windowsDoc = windowsCapsule.take();

    const CapsuleDiffReport forward = CapsuleDiffReport::diff( linuxDoc, windowsDoc );
    const CapsuleDiffReport reverse = CapsuleDiffReport::diff( windowsDoc, linuxDoc );
    CHECK( forward.level == reverse.level );
    CHECK( forward.sections.size() == reverse.sections.size() );
}

// --- Slice G: cross-machine path normalization --------------------------------

namespace
{

/// Logical content shared by two "machines": fixed dataset/version ids so
/// the derived fingerprints match across stores.
struct RelocationIds
{
    QString datasetId = QStringLiteral( "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" );
    QString versionId = QStringLiteral( "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" );
};

/// Fills a fixture with the SAME logical content (ids, experiment, run,
/// split manifest) and records one workspace artifact at @p artifactPath.
bool fillRelocatableFixture( StoreFixture &fixture, const RelocationIds &ids,
                             const QString &artifactPath )
{
    if ( !fixture.open() )
        return false;
    const auto datasetId = DatasetId::fromString( ids.datasetId ).value_or( DatasetId{} );
    if ( !fixture.datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() )
        return false;
    DatasetManifest manifest;
    manifest.setDatasetId( ids.datasetId );
    manifest.setVersionId( ids.versionId );
    const auto draft = fixture.datasets.createDraftVersion( manifest );
    if ( !draft.has_value() )
        return false;
    if ( draft->versionId() != ids.versionId )
        return false;
    const auto versionIdParsed =
        DatasetVersionId::fromString( ids.versionId ).value_or( DatasetVersionId{} );
    if ( !fixture.datasets.stageVersion( versionIdParsed ).has_value() )
        return false;
    if ( !fixture.datasets.commitVersion( versionIdParsed ).has_value() )
        return false;
    const auto version = fixture.datasets.versionById( versionIdParsed );
    if ( !version.has_value() )
        return false;
    fixture.versionId = version->versionId();
    fixture.fingerprint = version->fingerprint();
    fixture.manifestJson = version->manifestJson();

    fixture.addExperiment();

    sicnu::dataset::SplitManifest split;
    split.setManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    split.setDatasetVersionId( ids.versionId );
    sicnu::dataset::SplitConfig config;
    config.method = sicnu::dataset::SplitMethod::Random;
    config.seed = 7;
    split.setConfig( config );
    sicnu::dataset::SplitAssignment assignment;
    assignment.sampleId = QStringLiteral( "sample-1" );
    assignment.role = sicnu::dataset::SplitRole::Train;
    split.assignments().append( assignment );
    if ( !fixture.datasets.saveSplitManifest( split ).has_value() )
        return false;
    const auto saved = fixture.datasets.splitManifestById( split.manifestId() );
    if ( !saved.has_value() )
        return false;
    fixture.splitFingerprint = saved->fingerprint();

    ExperimentRun::Artifact artifact;
    artifact.path = artifactPath;
    artifact.role = QStringLiteral( "primary" );
    artifact.digest = QStringLiteral( "digest-primary" );
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QString(), QString(), QVector<ExperimentRun::Artifact>{ artifact } );
    return run.runId() == QLatin1String( "run-1" );
}

} // namespace

TEST_CASE( "a relocated workspace builds a byte-identical capsule",
           "[capsule][portability]" )
{
    const RelocationIds ids;
    StoreFixture machineA;
    REQUIRE( machineA.open() );
    REQUIRE( fillRelocatableFixture( machineA, ids,
                                     machineA.dir.filePath( QStringLiteral( "out/raster.tif" ) ) ) );
    StoreFixture machineB;
    REQUIRE( machineB.open() );
    REQUIRE( fillRelocatableFixture( machineB, ids,
                                     machineB.dir.filePath( QStringLiteral( "out/raster.tif" ) ) ) );

    const CapsuleBuilder builderA( machineA.experiments, machineA.datasets );
    CapsuleOptions optionsA = fixedOptions();
    optionsA.workspaceRoot = machineA.dir.path();
    auto capsuleA = builderA.build( QStringLiteral( "run-1" ), optionsA );
    REQUIRE( capsuleA.has_value() );

    const CapsuleBuilder builderB( machineB.experiments, machineB.datasets );
    CapsuleOptions optionsB = fixedOptions();
    optionsB.workspaceRoot = machineB.dir.path();
    auto capsuleB = builderB.build( QStringLiteral( "run-1" ), optionsB );
    REQUIRE( capsuleB.has_value() );

    CHECK( capsuleA->digestValue() == capsuleB->digestValue() );
    CHECK( capsuleA->canonicalBytes() == capsuleB->canonicalBytes() );
    CHECK( capsuleA->root().value( QStringLiteral( "outputs" ) ).toArray().at( 0 )
               .toObject().value( QStringLiteral( "portable_ref" ) ).toString()
           == QLatin1String( "workspace:out/raster.tif" ) );
}

TEST_CASE( "portable refs survive separators, drive letters and root slashes",
           "[capsule][portability]" )
{
    using sicnu::experiment::capsule::toPortableRef;
    CHECK( toPortableRef( QStringLiteral( "C:\\ws\\out\\a.tif" ), QStringLiteral( "C:\\ws" ) )
           == QLatin1String( "workspace:out/a.tif" ) );
    CHECK( toPortableRef( QStringLiteral( "/ws/out/a.tif" ), QStringLiteral( "/ws/" ) )
           == QLatin1String( "workspace:out/a.tif" ) );
    CHECK( toPortableRef( QStringLiteral( "/ws/out/a.tif" ), QStringLiteral( "/ws/out" ) )
           == QLatin1String( "workspace:a.tif" ) );
    CHECK( toPortableRef( QStringLiteral( "/elsewhere/a.tif" ), QStringLiteral( "/ws" ) )
           == QLatin1String( "external:a.tif" ) );
}

TEST_CASE( "an empty workspace root marks everything external", "[capsule][portability]" )
{
    using sicnu::experiment::capsule::toPortableRef;
    CHECK( toPortableRef( QStringLiteral( "/x/y/z.tif" ), QString() )
           == QLatin1String( "external:z.tif" ) );
}

TEST_CASE( "a non-canonical workspace root is announced and never leaks paths",
           "[capsule][portability]" )
{
    StoreFixture fixture;
    REQUIRE( fixture.open() );
    // Artifact under a symlinked subdirectory of the workspace.
    QDir( fixture.dir.path() ).mkpath( QStringLiteral( "real/out" ) );
    const QString linkPath = fixture.dir.filePath( QStringLiteral( "linked" ) );
    QFile::link( fixture.dir.filePath( QStringLiteral( "real" ) ), linkPath );

    ExperimentRun::Artifact artifact;
    artifact.path = linkPath + QStringLiteral( "/out/raster.tif" );
    artifact.role = QStringLiteral( "primary" );
    artifact.digest = QStringLiteral( "digest-primary" );
    fixture.addExperiment();
    const ExperimentRun run = fixture.addRun(
        QStringLiteral( "run-1" ), QStringLiteral( "exp-1" ), QStringLiteral( "rs:classify" ),
        QString(), QString(), QVector<ExperimentRun::Artifact>{ artifact } );

    CapsuleOptions options = fixedOptions();
    options.workspaceRoot = linkPath;
    const CapsuleBuilder builder( fixture.experiments, fixture.datasets );
    auto built = builder.build( run.runId(), options );
    REQUIRE( built.has_value() );

    // The non-canonical root is announced, not silently accepted.
    bool sawWarning = false;
    for ( const auto &diagnostic : built.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.workspace-root-noncanonical" ) )
            sawWarning = true;
    CHECK( sawWarning );

    // And no absolute path enters the document regardless.
    CHECK( !built->canonicalBytes().contains( fixture.dir.path().toUtf8() ) );
}
