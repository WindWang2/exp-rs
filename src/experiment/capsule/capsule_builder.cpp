// capsule_builder.cpp — projects one recorded ExperimentRun into a
// sicnu.capsule v1 document. Every section is a projection of recorded
// truth; unresolvable pins are recorded as unresolved (with a warning
// diagnostic), never dropped or fabricated.
#include "capsule_builder.h"

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_types.h"

#include <QDateTime>
#include <QJsonArray>

namespace sicnu::experiment::capsule
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::DiagnosticSeverity;

namespace
{

Diagnostic warning( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, DiagnosticSeverity::Warning };
}

QJsonObject inputPin( const QString &kind, const QString &id, const QString &digest,
                      const QString &state )
{
    QJsonObject pin;
    pin.insert( QStringLiteral( "kind" ), kind );
    pin.insert( QStringLiteral( "id" ), id );
    pin.insert( QStringLiteral( "digest" ), digest );
    pin.insert( QStringLiteral( "state" ), state );
    return pin;
}

/// Dataset version pin: identity + recorded state + manifest content digest.
/// The manifest itself is payload — the capsule carries its DIGEST, not the
/// bytes (no data copying).
QJsonObject datasetPin( const dataset::DatasetStore &store, const ExperimentRun &run,
                        QVector<Diagnostic> *diagnostics )
{
    QJsonObject pin = inputPin( QStringLiteral( "dataset_version" ), run.datasetVersionId(),
                                run.datasetFingerprint(), QString() );
    const auto version = store.versionById(
        dataset::DatasetVersionId::fromString( run.datasetVersionId() )
            .value_or( dataset::DatasetVersionId{} ) );
    if ( !version )
    {
        pin.insert( QStringLiteral( "state" ), QStringLiteral( "unresolved" ) );
        if ( diagnostics )
            diagnostics->append( warning(
                QStringLiteral( "capsule.dataset-unresolved" ),
                QStringLiteral( "dataset version %1 not resolvable in store; recorded as unresolved" )
                    .arg( run.datasetVersionId() ) ) );
        return pin;
    }
    pin.insert( QStringLiteral( "state" ),
                dataset::datasetVersionStatusToString( version->status() ) );
    const QString manifestJson = version->manifestJson();
    pin.insert( QStringLiteral( "manifest_digest" ),
                capsuleSha256Hex( manifestJson.toUtf8() ) );
    const auto parsed = dataset::DatasetManifest::fromJson(
        QJsonDocument::fromJson( manifestJson.toUtf8() ).object() );
    if ( parsed.has_value() )
    {
        QJsonArray sourceAssets;
        for ( const dataset::SourceAssetRef &ref : parsed.value().sourceAssets() )
        {
            QJsonObject asset;
            asset.insert( QStringLiteral( "asset_id" ), ref.assetId );
            asset.insert( QStringLiteral( "revision" ), qint64( ref.revision ) );
            asset.insert( QStringLiteral( "role" ), ref.role );
            sourceAssets.append( asset );
        }
        pin.insert( QStringLiteral( "source_assets" ), sourceAssets );
    }
    return pin;
}

} // namespace

CapsuleBuilder::CapsuleBuilder( const ExperimentStore &experimentStore,
                                const dataset::DatasetStore &datasetStore )
    : m_experimentStore{ &experimentStore }
    , m_datasetStore{ &datasetStore }
{
}

Result<CapsuleDocument> CapsuleBuilder::build( const QString &runId,
                                               const CapsuleOptions &options,
                                               const CapsuleHooks &hooks ) const
{
    const auto runRecord = m_experimentStore->runById( runId );
    if ( !runRecord )
    {
        return Result<CapsuleDocument>::failure( Diagnostic{
            QStringLiteral( "capsule.run-missing" ),
            QStringLiteral( "run %1 not found; nothing to project" ).arg( runId ),
            DiagnosticSeverity::Error } );
    }
    const ExperimentRun run = *runRecord;
    QVector<Diagnostic> diagnostics;

    QJsonObject payload;

    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QString::fromLatin1( kCapsuleSchemaId ) );
    schema.insert( QStringLiteral( "version" ), kCapsuleSchemaVersion );
    payload.insert( QStringLiteral( "schema" ), schema );
    payload.insert( QStringLiteral( "capsule_id" ),
                    QStringLiteral( "capsule-%1" ).arg( run.runId() ) );
    payload.insert( QStringLiteral( "created_utc" ),
                    options.createdUtc.isEmpty()
                        ? QDateTime::currentDateTimeUtc().toString( Qt::ISODate )
                        : options.createdUtc );

    // Goal: the owning experiment's recorded question. Teaching labs join by
    // id (lab_report.h: labId == experimentId), so lab_id mirrors it.
    QJsonObject goal;
    goal.insert( QStringLiteral( "experiment_id" ), run.experimentId() );
    goal.insert( QStringLiteral( "lab_id" ), run.experimentId() );
    const auto experiment = m_experimentStore->experimentById( run.experimentId() );
    if ( experiment )
    {
        goal.insert( QStringLiteral( "name" ), experiment->name() );
        goal.insert( QStringLiteral( "objective" ), experiment->objective() );
    }
    payload.insert( QStringLiteral( "goal" ), goal );

    // Software: the run's recorded revision + allowlisted platform facts
    // projected from the captured environment (no live machine reads).
    const QJsonObject envFields = run.environment().redacted().fields();
    QJsonObject software;
    software.insert( QStringLiteral( "revision" ), run.softwareRevision() );
    software.insert( QStringLiteral( "platform" ),
                     envFields.value( QStringLiteral( "platform" ) ) );
    software.insert( QStringLiteral( "qt_version" ),
                     envFields.value( QStringLiteral( "qt_version" ) ) );
    software.insert( QStringLiteral( "build_abi" ),
                     envFields.value( QStringLiteral( "build_abi" ) ) );
    payload.insert( QStringLiteral( "software" ), software );

    // Capabilities: the executed algorithm identity is always recorded;
    // the descriptor in force is pinned by digest ONLY when a hook provided
    // one (source "hook"); unwired ⇒ source "record" and no digest.
    QJsonArray capabilities;
    {
        QJsonObject capability;
        capability.insert( QStringLiteral( "id" ), run.algorithmId() );
        capability.insert( QStringLiteral( "version" ), run.algorithmVersion() );
        QJsonObject descriptor;
        bool descriptorWired = false;
        if ( hooks.capabilityDescriptor )
        {
            descriptor = hooks.capabilityDescriptor( run.algorithmId() );
            descriptorWired = !descriptor.isEmpty();
        }
        if ( descriptorWired )
        {
            capability.insert( QStringLiteral( "digest" ), capsuleDigest( descriptor ) );
            capability.insert( QStringLiteral( "source" ), QStringLiteral( "hook" ) );
        }
        else
        {
            capability.insert( QStringLiteral( "digest" ), QString() );
            capability.insert( QStringLiteral( "source" ), QStringLiteral( "record" ) );
        }
        capabilities.append( capability );
    }
    payload.insert( QStringLiteral( "capabilities" ), capabilities );

    // Inputs: dataset version pin, split pin, model pin — fixed order, each
    // present only when the run records it.
    QJsonArray inputs;
    if ( !run.datasetVersionId().isEmpty() )
        inputs.append( datasetPin( *m_datasetStore, run, &diagnostics ) );
    if ( !run.splitManifestId().isEmpty() )
        inputs.append( inputPin( QStringLiteral( "split" ), run.splitManifestId(),
                                 run.splitFingerprint(), QStringLiteral( "record" ) ) );
    if ( !run.modelId().isEmpty() || !run.modelDigest().isEmpty() )
        inputs.append( inputPin( QStringLiteral( "model" ), run.modelId(), run.modelDigest(),
                                 QStringLiteral( "record" ) ) );
    payload.insert( QStringLiteral( "inputs" ), inputs );

    // Parameters through the same secret-key pass the bundle uses (#789):
    // a credential-shaped parameter key must not leak into the capsule.
    payload.insert( QStringLiteral( "parameters" ),
                    RunEnvironment::redactSecretKeys( run.parameters() ) );

    // Plan: what was executed. The definition digest pins the exact plan
    // snapshot ONLY when a hook supplies it.
    QJsonObject plan;
    plan.insert( QStringLiteral( "algorithm_id" ), run.algorithmId() );
    plan.insert( QStringLiteral( "algorithm_version" ), run.algorithmVersion() );
    plan.insert( QStringLiteral( "definition_digest" ),
                 hooks.planDefinitionDigest ? hooks.planDefinitionDigest( run.algorithmId() )
                                            : QString() );
    payload.insert( QStringLiteral( "plan" ), plan );

    // Environment: the recorded, re-redacted snapshot — never a live read.
    payload.insert( QStringLiteral( "environment" ), run.environment().redacted().toJson() );

    // Filled by later slices; present-but-empty keeps documents stable.
    payload.insert( QStringLiteral( "outputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "evidence" ), QJsonObject{} );
    payload.insert( QStringLiteral( "provenance" ), QJsonObject{} );

    auto finalized = CapsuleDocument::finalize( std::move( payload ) );
    if ( !finalized.has_value() )
        return finalized;
    return Result<CapsuleDocument>::success( finalized.take(), diagnostics );
}

} // namespace sicnu::experiment::capsule
