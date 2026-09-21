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
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

namespace
{

using sicnu::dataset::DatasetId;
using sicnu::dataset::DatasetManifest;
using sicnu::dataset::DatasetStore;
using sicnu::dataset::DatasetVersionId;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRun;
using sicnu::experiment::ExperimentStore;
using sicnu::experiment::RunEnvironment;
using sicnu::experiment::capsule::CapsuleBuilder;
using sicnu::experiment::capsule::CapsuleDocument;
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
                          const QString &datasetFingerprintOverride = QString() )
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
