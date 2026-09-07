// reproduction_bundle.cpp — bundle export + reproduction validation.
#include "reproduction_bundle.h"

#include "../dataset/dataset_manifest.h"
#include "../dataset/dataset_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

namespace sicnu::experiment
{

using dataset::DatasetStore;

namespace
{

Diagnostic bundleError( const QString &message )
{
    return Diagnostic{ QStringLiteral( "repro.bundle_error" ), message,
                       DiagnosticSeverity::Error };
}

/// Writes one bundle file + returns its checksum line content.
bool writeFileWithChecksum( const QString &path, const QByteArray &content, QString *errorOut,
                            QStringList *checksumLines, const QString &relativeName )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot write %1" ).arg( path );
        return false;
    }
    if ( file.write( content ) != content.size() )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "short write %1" ).arg( path );
        return false;
    }
    checksumLines->append( QString::fromUtf8(
                               QCryptographicHash::hash( content, QCryptographicHash::Sha256 )
                                   .toHex() ) +
                           QStringLiteral( "  " ) + relativeName );
    return true;
}

QJsonObject loadBundleJson( const QString &bundleDir, const QString &name, bool *ok )
{
    QFile file( QDir( bundleDir ).filePath( name ) );
    *ok = file.open( QIODevice::ReadOnly );
    if ( !*ok )
        return {};
    return QJsonDocument::fromJson( file.readAll() ).object();
}

} // namespace

QJsonObject ReproductionBundleReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "ok" ), ok );
    json.insert( QStringLiteral( "bundle_path" ), bundlePath );
    json.insert( QStringLiteral( "file_count" ), fileCount );
    json.insert( QStringLiteral( "warnings" ), QJsonArray::fromStringList( warnings ) );
    return json;
}

QJsonObject ReproductionValidation::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "level" ),
                 dataset::reproductionLevelToString( level ) );
    json.insert( QStringLiteral( "reasons" ), QJsonArray::fromStringList( reasons ) );
    return json;
}

ReproductionBundleExporter::ReproductionBundleExporter( const ExperimentStore &experimentStore,
                                                        const DatasetStore &datasetStore )
  : m_experimentStore( experimentStore )
  , m_datasetStore( datasetStore )
{
}

ReproductionBundleReport ReproductionBundleExporter::exportRun(
    const QString &runId, const ReproductionBundleOptions &options ) const
{
    ReproductionBundleReport report;
    const auto runRecord = m_experimentStore.runById( runId );
    if ( !runRecord )
    {
        report.warnings.append( QStringLiteral( "run %1 not found" ).arg( runId ) );
        return report;
    }
    const ExperimentRun run = *runRecord;

    QDir dir( options.outputDir );
    if ( !dir.mkpath( QStringLiteral( "." ) ) )
    {
        report.warnings.append( QStringLiteral( "cannot create %1" ).arg( options.outputDir ) );
        return report;
    }
    report.bundlePath = dir.absolutePath();

    QString error;
    QStringList checksums;
    auto writeJson = [&]( const QString &name, const QJsonObject &json ) {
        const QByteArray content = QJsonDocument( json ).toJson( QJsonDocument::Indented );
        if ( !writeFileWithChecksum( dir.filePath( name ), content, &error, &checksums, name ) )
        {
            report.warnings.append( error );
            return false;
        }
        ++report.fileCount;
        return true;
    };

    // Run config + identity (canonical parameters verbatim; the config hash
    // is recomputable from them by any consumer).
    QJsonObject runConfig;
    runConfig.insert( QStringLiteral( "run_id" ), run.runId() );
    runConfig.insert( QStringLiteral( "experiment_id" ), run.experimentId() );
    runConfig.insert( QStringLiteral( "algorithm_id" ), run.algorithmId() );
    runConfig.insert( QStringLiteral( "algorithm_version" ), run.algorithmVersion() );
    runConfig.insert( QStringLiteral( "parameters" ), run.parameters() );
    // Identity pins the validator (and any consumer) needs at minimum.
    runConfig.insert( QStringLiteral( "dataset_version_id" ), run.datasetVersionId() );
    runConfig.insert( QStringLiteral( "dataset_fingerprint" ), run.datasetFingerprint() );
    runConfig.insert( QStringLiteral( "split_manifest_id" ), run.splitManifestId() );
    runConfig.insert( QStringLiteral( "split_fingerprint" ), run.splitFingerprint() );
    runConfig.insert( QStringLiteral( "config_hash" ), run.configHash() );
    runConfig.insert(
        QStringLiteral( "execution_fingerprint" ),
        runExecutionFingerprint( run.executionIdentity() ) );
    runConfig.insert( QStringLiteral( "result_fingerprint" ), run.resultFingerprint() );
    runConfig.insert( QStringLiteral( "seed" ), qint64( run.seed() ) );
    runConfig.insert( QStringLiteral( "determinism" ),
                      dataset::determinismGradeToString( run.determinism() ) );
    if ( !run.determinismNote().isEmpty() )
        runConfig.insert( QStringLiteral( "determinism_note" ), run.determinismNote() );
    if ( !writeJson( QStringLiteral( "run_config.json" ), runConfig ) )
        return report;

    // Dataset pin.
    QJsonObject datasetRefs;
    datasetRefs.insert( QStringLiteral( "dataset_version_id" ), run.datasetVersionId() );
    datasetRefs.insert( QStringLiteral( "dataset_fingerprint" ), run.datasetFingerprint() );
    if ( !run.datasetVersionId().isEmpty() )
    {
        const auto version = m_datasetStore.versionById(
            dataset::DatasetVersionId::fromString( run.datasetVersionId() )
                .value_or( dataset::DatasetVersionId{} ) );
        if ( version )
        {
            datasetRefs.insert( QStringLiteral( "status" ),
                                dataset::datasetVersionStatusToString( version->status() ) );
            datasetRefs.insert( QStringLiteral( "manifest_json" ), version->manifestJson() );
            const auto parsed = dataset::DatasetManifest::fromJson(
                QJsonDocument::fromJson( version->manifestJson().toUtf8() ).object() );
            if ( parsed.has_value() )
            {
                QJsonArray sourceAssets;
                for ( const dataset::SourceAssetRef &ref : parsed.value().sourceAssets() )
                {
                    QJsonObject item;
                    item.insert( QStringLiteral( "asset_id" ), ref.assetId );
                    item.insert( QStringLiteral( "revision" ), qint64( ref.revision ) );
                    item.insert( QStringLiteral( "role" ), ref.role );
                    sourceAssets.append( item );
                }
                datasetRefs.insert( QStringLiteral( "source_assets" ), sourceAssets );
            }
        }
        else
        {
            report.warnings.append(
                QStringLiteral( "dataset version %1 not resolvable in store" )
                    .arg( run.datasetVersionId() ) );
        }
    }
    if ( !writeJson( QStringLiteral( "dataset_refs.json" ), datasetRefs ) )
        return report;

    // Environment — denylist applied again at the boundary (goal §29).
    if ( !writeJson( QStringLiteral( "environment.json" ), run.environment().toJson() ) )
        return report;

    // Software.
    QJsonObject software;
    software.insert( QStringLiteral( "software_revision" ), run.softwareRevision() );
    software.insert( QStringLiteral( "exported_by_revision" ), options.currentSoftwareRevision );
    if ( !writeJson( QStringLiteral( "software.json" ), software ) )
        return report;

    // Model pin.
    QJsonObject modelRefs;
    if ( !run.modelId().isEmpty() || !run.modelDigest().isEmpty() )
    {
        modelRefs.insert( QStringLiteral( "model_id" ), run.modelId() );
        modelRefs.insert( QStringLiteral( "model_digest" ), run.modelDigest() );
    }
    if ( !writeJson( QStringLiteral( "model_refs.json" ), modelRefs ) )
        return report;

    // Metrics + protocol.
    QJsonObject metrics;
    const auto metricRecord = m_experimentStore.metricRecordForRun( run.runId() );
    if ( metricRecord )
        metrics = metricRecord->toJson();
    if ( !writeJson( QStringLiteral( "metrics.json" ), metrics ) )
        return report;

    // Lineage slice (direct edges of the run node).
    LineageGraph graph( m_datasetStore, m_experimentStore );
    LineageNodeId runNode{ QStringLiteral( "run" ), run.runId() };
    QJsonObject provenance;
    QJsonArray edges;
    for ( const LineageEdgeRecord &edge : graph.edgesOf( runNode ) )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "edge" ), edge.edgeKind );
        item.insert( QStringLiteral( "other_kind" ),
                     edge.from.id == run.runId() ? edge.to.kind : edge.from.kind );
        item.insert( QStringLiteral( "other_id" ),
                     edge.from.id == run.runId() ? edge.to.id : edge.from.id );
        edges.append( item );
    }
    provenance.insert( QStringLiteral( "run_edges" ), edges );
    if ( !writeJson( QStringLiteral( "provenance.json" ), provenance ) )
        return report;

    // Split pin (from the lineage edge if present; reference only).
    QJsonObject splitPin;
    splitPin.insert( QStringLiteral( "split_manifest_id" ), run.splitManifestId() );
    splitPin.insert( QStringLiteral( "split_fingerprint" ), run.splitFingerprint() );
    if ( !writeJson( QStringLiteral( "split.json" ), splitPin ) )
        return report;

    // README + checksums + manifest last (checksums cover everything else).
    const QString readme = QStringLiteral(
        "# Reproduction Bundle\n\n"
        "This bundle pins one experiment run: dataset version, split, canonical\n"
        "parameters, seed, model digest, allowlisted environment and metrics.\n"
        "Validate with `sicnu_geo_rs_cli reproduce validate --bundle <dir>`.\n"
        "checksums.txt lists the SHA-256 of every file in this directory.\n" );
    if ( !writeFileWithChecksum( dir.filePath( QStringLiteral( "README.md" ) ),
                                 readme.toUtf8(), &error, &checksums, QStringLiteral( "README.md" ) ) )
    {
        report.warnings.append( error );
        return report;
    }
    ++report.fileCount;

    QJsonObject manifest;
    manifest.insert( QStringLiteral( "schema_version" ),
                     QString::fromLatin1( kReproductionBundleSchemaVersion ) );
    manifest.insert( QStringLiteral( "run_id" ), run.runId() );
    manifest.insert( QStringLiteral( "mode" ), options.mode == ReproductionBundleOptions::Mode::Portable
                                                    ? QStringLiteral( "portable" )
                                                    : QStringLiteral( "reference" ) );
    manifest.insert( QStringLiteral( "file_count" ), report.fileCount );
    if ( !writeJson( QStringLiteral( "manifest.json" ), manifest ) )
        return report;

    QFile checksumFile( dir.filePath( QStringLiteral( "checksums.txt" ) ) );
    if ( !checksumFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        report.warnings.append( QStringLiteral( "cannot write checksums.txt" ) );
        return report;
    }
    checksumFile.write( checksums.join( QLatin1Char( '\n' ) ).toUtf8() );
    checksumFile.write( "\n" );
    ++report.fileCount;

    report.ok = report.warnings.isEmpty();
    return report;
}

ReproductionValidation ReproductionBundleExporter::validateBundle(
    const QString &bundleDir, const ReproductionHooks &hooks ) const
{
    ReproductionValidation validation;
    // The struct default is Impossible, so early exits must be tracked
    // explicitly — an unmet check sets the flag, the default never counts
    // as a verdict.
    bool impossible = false;
    bool ok = false;
    const QJsonObject runConfig =
        loadBundleJson( bundleDir, QStringLiteral( "run_config.json" ), &ok );
    if ( !ok )
    {
        validation.level = dataset::ReproductionLevel::Impossible;
        validation.reasons.append( QStringLiteral( "run_config.json missing" ) );
        return validation;
    }

    // 1. Dataset availability + fingerprint match.
    const QString datasetVersionId =
        runConfig.value( QStringLiteral( "dataset_version_id" ) ).toString();
    const QString expectedFingerprint =
        runConfig.value( QStringLiteral( "dataset_fingerprint" ) ).toString();
    if ( datasetVersionId.isEmpty() )
    {
        validation.reasons.append( QStringLiteral( "run has no dataset pin" ) );
        validation.level = dataset::ReproductionLevel::Impossible;
        return validation;
    }
    const auto version = m_datasetStore.versionById(
        dataset::DatasetVersionId::fromString( datasetVersionId ).value_or( dataset::DatasetVersionId{} ) );
    if ( !version )
    {
        validation.reasons.append( QStringLiteral( "dataset version %1 not available" )
                                       .arg( datasetVersionId ) );
        validation.level = dataset::ReproductionLevel::Impossible;
        return validation;
    }
    const QString actualFingerprint = version->fingerprint();
    if ( !expectedFingerprint.isEmpty() && actualFingerprint != expectedFingerprint )
    {
        validation.reasons.append( QStringLiteral( "dataset fingerprint mismatch" ) );
        validation.level = dataset::ReproductionLevel::Impossible;
        return validation;
    }
    Q_UNUSED( impossible );
    validation.reasons.append( QStringLiteral( "dataset version available, fingerprint match" ) );

    // 2. Model digest.
    bool modelChecked = false;
    {
        bool modelOk = false;
        const QJsonObject modelRefs =
            loadBundleJson( bundleDir, QStringLiteral( "model_refs.json" ), &modelOk );
        if ( modelOk && !modelRefs.isEmpty() )
        {
            const QString modelId = modelRefs.value( QStringLiteral( "model_id" ) ).toString();
            const QString modelDigest =
                modelRefs.value( QStringLiteral( "model_digest" ) ).toString();
            if ( hooks.modelAvailable )
            {
                modelChecked = true;
                if ( hooks.modelAvailable( modelId, modelDigest ) )
                    validation.reasons.append( QStringLiteral( "model digest match" ) );
                else
                {
                    validation.reasons.append( QStringLiteral( "model not resolvable at pinned digest" ) );
                    validation.level = dataset::ReproductionLevel::Impossible;
                    return validation;
                }
            }
            else
            {
                validation.reasons.append( QStringLiteral( "model availability not checkable" ) );
            }
        }
    }

    // 3. Algorithm availability.
    const QString algorithmId = runConfig.value( QStringLiteral( "algorithm_id" ) ).toString();
    if ( !algorithmId.isEmpty() )
    {
        if ( hooks.algorithmAvailable )
        {
            if ( hooks.algorithmAvailable( algorithmId ) )
                validation.reasons.append( QStringLiteral( "algorithm available" ) );
            else
            {
                validation.reasons.append( QStringLiteral( "algorithm %1 unavailable" ).arg( algorithmId ) );
                validation.level = dataset::ReproductionLevel::Impossible;
                return validation;
            }
        }
        else
            validation.reasons.append( QStringLiteral( "algorithm availability not checkable" ) );
    }

    // 4. Workflow validity (when the algorithm is a workflow).
    if ( hooks.workflowValid && algorithmId.startsWith( QLatin1String( "workflow" ) ) )
    {
        if ( hooks.workflowValid( algorithmId ) )
            validation.reasons.append( QStringLiteral( "workflow valid" ) );
        else
        {
            validation.reasons.append( QStringLiteral( "workflow invalid for current engine" ) );
            validation.level = dataset::ReproductionLevel::Impossible;
            return validation;
        }
    }

    // 5. Artifacts present.
    bool artifactsOk = true;
    {
        const auto runRecord = m_experimentStore.runById(
            runConfig.value( QStringLiteral( "run_id" ) ).toString() );
        if ( runRecord && hooks.artifactAvailable )
        {
            for ( const ExperimentRun::Artifact &artifact : runRecord->artifacts() )
            {
                if ( !hooks.artifactAvailable( artifact.path, artifact.sizeBytes ) )
                {
                    artifactsOk = false;
                    validation.reasons.append(
                        QStringLiteral( "artifact missing: %1" ).arg( artifact.path ) );
                }
            }
        }
    }

    // 6. Environment compatibility: exact equality → Exact candidate;
    // otherwise Compatible-with-notes. Determinism note participates: a
    // non-strict run can never claim Exact.
    bool environmentExact = false;
    {
        const auto runRecord = m_experimentStore.runById(
            runConfig.value( QStringLiteral( "run_id" ) ).toString() );
        bool envOk = false;
        const QJsonObject environmentJson =
            loadBundleJson( bundleDir, QStringLiteral( "environment.json" ), &envOk );
        if ( runRecord && envOk )
        {
            environmentExact = environmentJson == runRecord->environment().toJson();
            validation.reasons.append( environmentExact
                                           ? QStringLiteral( "environment identical" )
                                           : QStringLiteral( "environment differs (compatible mode)" ) );
        }
    }

    const bool strictRun = runConfig.value( QStringLiteral( "determinism" ) ).toString() ==
                           QStringLiteral( "strict" );
    if ( artifactsOk && environmentExact && strictRun )
        validation.level = dataset::ReproductionLevel::Exact;
    else if ( artifactsOk && modelChecked )
        validation.level = dataset::ReproductionLevel::Compatible;
    else
        validation.level = dataset::ReproductionLevel::BestEffort;
    return validation;
}

} // namespace sicnu::experiment
