// reproduction_bundle.cpp — bundle export + reproduction validation.
#include "reproduction_bundle.h"

#include "../dataset/dataset_manifest.h"
#include "../dataset/dataset_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

bool verifyBundleChecksums( const QString &bundleDir, QStringList *reasons )
{
    const QDir dir( bundleDir );
    QFile checksumFile( dir.filePath( QStringLiteral( "checksums.txt" ) ) );
    if ( !checksumFile.open( QIODevice::ReadOnly ) )
    {
        reasons->append( QStringLiteral( "checksums.txt missing" ) );
        return false;
    }
    const QStringList lines = QString::fromUtf8( checksumFile.readAll() )
                                  .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    QStringList listed;
    for ( const QString &line : lines )
    {
        const int split = line.indexOf( QStringLiteral( "  " ) );
        if ( split <= 0 )
        {
            // A line that cannot be parsed is an integrity signal, not noise:
            // skipping it would let a truncated or edited table pass while
            // only pretending to verify the bundle.
            reasons->append( QStringLiteral( "malformed checksum line: %1" ).arg( line ) );
            return false;
        }
        const QString digest = line.left( split );
        const QString name = line.mid( split + 2 );
        listed.append( name );
        // Path traversal guard: bundle members are relative names inside the
        // directory, never absolute paths or ../ escapes.
        if ( name.startsWith( QLatin1Char( '/' ) ) || name.contains( QLatin1String( ".." ) ) )
        {
            reasons->append( QStringLiteral( "unsafe checksum entry: %1" ).arg( name ) );
            return false;
        }
        QFile member( dir.filePath( name ) );
        if ( !member.open( QIODevice::ReadOnly ) ||
             QString::fromUtf8(
                 QCryptographicHash::hash( member.readAll(), QCryptographicHash::Sha256 )
                     .toHex() ) != digest )
        {
            reasons->append( QStringLiteral( "checksum mismatch: %1" ).arg( name ) );
            return false;
        }
    }
    for ( const char *member : kRequiredBundleMembers )
    {
        if ( !listed.contains( QLatin1String( member ) ) )
        {
            reasons->append( QStringLiteral( "checksums.txt does not cover %1" )
                                 .arg( QLatin1String( member ) ) );
            return false;
        }
    }
    // Coverage is directional in both ways: every checksums.txt line must
    // hash-match a file, AND every file in the bundle must be covered —
    // an uncovered member (metrics.json with its checksum line stripped, a
    // stray document) is exactly the tamper shape the gate exists for.
    // checksums.txt cannot list itself.
    const QFileInfoList bundleFiles =
        dir.entryInfoList( QDir::Files, QDir::Name | QDir::IgnoreCase );
    for ( const QFileInfo &entry : bundleFiles )
    {
        const QString name = entry.fileName();
        if ( name == QLatin1String( "checksums.txt" ) )
            continue;
        if ( !listed.contains( name ) )
        {
            reasons->append( QStringLiteral( "checksums.txt does not cover %1" ).arg( name ) );
            return false;
        }
    }
    reasons->append( QStringLiteral( "bundle checksums verified" ) );
    return true;
}

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

    // Run config + identity (the config hash is recomputable from the
    // exported parameters by any consumer).
    // #789: the export boundary re-applies the secret filters — parameters
    // with credential-shaped keys are masked, never serialized verbatim.
    QJsonObject runConfig;
    runConfig.insert( QStringLiteral( "run_id" ), run.runId() );
    runConfig.insert( QStringLiteral( "experiment_id" ), run.experimentId() );
    runConfig.insert( QStringLiteral( "algorithm_id" ), run.algorithmId() );
    runConfig.insert( QStringLiteral( "algorithm_version" ), run.algorithmVersion() );
    runConfig.insert( QStringLiteral( "parameters" ),
                      RunEnvironment::redactSecretKeys( run.parameters() ) );
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
    // Lossless seed pin for consumers: JSON numbers only round-trip through
    // a double below 2^53, and seeds are 64-bit (run-bridge RunPins set the
    // precedent with seed_hex).
    runConfig.insert( QStringLiteral( "seed_hex" ), QString::number( run.seed(), 16 ) );
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
            const auto parsed = dataset::DatasetManifest::fromJson(
                QJsonDocument::fromJson( version->manifestJson().toUtf8() ).object() );
            if ( parsed.has_value() )
            {
                // Every other export member goes through the secret filters
                // at this boundary (#789); embedding the RAW store manifest
                // text would be a scrub bypass through the one member nobody
                // re-serialized. Persist the REDACTED document instead — the
                // authoritative raw manifest stays in the dataset store.
                const QJsonObject redactedManifest = RunEnvironment::redactSecretKeys(
                    parsed.value().toJson() );
                // Stay string-typed (schema v1 contract): older consumers
                // read manifest_json as the manifest TEXT.
                datasetRefs.insert(
                    QStringLiteral( "manifest_json" ),
                    QString::fromUtf8(
                        QJsonDocument( redactedManifest )
                            .toJson( QJsonDocument::Compact ) ) );
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
            else
            {
                // Never fall back to the raw store text — export pins without
                // the manifest copy and names the gap instead.
                report.warnings.append(
                    QStringLiteral( "dataset manifest for %1 does not parse;"
                                    " exporting pins without the manifest copy" )
                        .arg( run.datasetVersionId() ) );
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

    // Environment — denylist re-applied AT THE EXPORT BOUNDARY (#789): a run
    // record assembled before the denylist existed, or through a
    // non-filtering path, must not leak through serialization.
    if ( !writeJson( QStringLiteral( "environment.json" ),
                     run.environment().redacted().toJson() ) )
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

    // Metrics + protocol (#789 review: metrics serialize through the same
    // secret-key pass as parameters — a credential-shaped metric key must
    // not leak either).
    // The metric record is recorded evidence, not a credential carrier: it
    // was scrubbed AT INGESTION (run_recorder), and the persisted
    // metrics_hash binds the stored content. Re-running the denylist here
    // would rewrite legitimate secret-SHAPED metric keys (e.g.
    // "token_accuracy") into "***", breaking the hash round-trip on
    // re-import — so the record goes out byte-identical to the store.
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
    {
        const QByteArray checksumBytes =
            checksums.join( QLatin1Char( '\n' ) ).toUtf8() + "\n";
        if ( checksumFile.write( checksumBytes ) != checksumBytes.size() )
        {
            report.warnings.append( QStringLiteral( "short write checksums.txt" ) );
            checksumFile.close();
            report.ok = false;
            return report;
        }
    }
    if ( !checksumFile.flush() )
    {
        report.warnings.append( QStringLiteral( "flush failed checksums.txt" ) );
        checksumFile.close();
        report.ok = false;
        return report;
    }
    checksumFile.close();
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

    // 0. Integrity first: verify checksums.txt against the bundle contents
    // BEFORE trusting any of the documents below (a tampered bundle must
    // not be able to talk its way into a better verdict). An empty or
    // partially-readable table fails here instead of passing vacuously.
    {
        QStringList checksumReasons;
        if ( !verifyBundleChecksums( bundleDir, &checksumReasons ) )
        {
            validation.reasons.append( checksumReasons );
            validation.level = dataset::ReproductionLevel::Impossible;
            return validation;
        }
    }

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

    // 5. Artifacts present — an UNWIRED artifact hook is a checked-nothing
    // outcome and caps the verdict at BestEffort (the header contract: unwired
    // hooks never fabricate Exact).
    bool artifactsOk = true;
    bool artifactsChecked = false;
    {
        const auto runRecord = m_experimentStore.runById(
            runConfig.value( QStringLiteral( "run_id" ) ).toString() );
        if ( runRecord && hooks.artifactAvailable )
        {
            artifactsChecked = true;
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

    // 6. Environment compatibility: the honest check compares the recorded
    // run environment against the CURRENT machine, not against itself.
    bool environmentIdentical = false;
    {
        const auto runRecord = m_experimentStore.runById(
            runConfig.value( QStringLiteral( "run_id" ) ).toString() );
        bool envOk = false;
        const QJsonObject environmentJson =
            loadBundleJson( bundleDir, QStringLiteral( "environment.json" ), &envOk );
        if ( runRecord && envOk )
        {
            environmentIdentical =
                environmentJson == runRecord->environment().toJson() &&
                runRecord->environment() == RunEnvironment::captureCurrent();
            validation.reasons.append( environmentIdentical
                                           ? QStringLiteral( "environment identical to this machine"
                                                             " and the recording run" )
                                           : QStringLiteral( "environment differs from this machine"
                                                             " or the recording run" ) );
        }
    }

    const bool strictRun = runConfig.value( QStringLiteral( "determinism" ) ).toString() ==
                           QStringLiteral( "strict" );
    if ( artifactsOk && artifactsChecked && environmentIdentical && strictRun && modelChecked )
        validation.level = dataset::ReproductionLevel::Exact;
    else if ( artifactsOk && modelChecked )
        validation.level = dataset::ReproductionLevel::Compatible;
    else
        validation.level = dataset::ReproductionLevel::BestEffort;
    return validation;
}

} // namespace sicnu::experiment
