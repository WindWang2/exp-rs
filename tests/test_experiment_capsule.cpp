// test_experiment_capsule.cpp — RS14-17 ReproducibilityCapsule.
//
// Slice A: schema contract + deterministic canonicalization + self digest.
// The capsule is a PROJECTION of recorded experiment truth; these tests pin
// the document contract (canonical bytes, digest semantics, shape gates)
// before any builder exists.
#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "experiment/capsule/capsule_builder.h"
#include "experiment/capsule/capsule_document.h"
#include "experiment/capsule/capsule_io.h"
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
using sicnu::experiment::capsule::CapsuleDocument;
using sicnu::experiment::capsule::CapsuleIO;
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
                          const QVector<ExperimentRun::Artifact> &artifacts = {} )
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
        run.setSplitFingerprint( QStringLiteral( "sf1" ) );
        run.setSeed( 42 );
        run.setSoftwareRevision( QStringLiteral( "rev-123" ) );
        QJsonObject envFields;
        envFields.insert( QStringLiteral( "platform" ), QStringLiteral( "linux" ) );
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
