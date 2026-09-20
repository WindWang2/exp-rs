// reproduction_bundle_import.cpp — offline bundle import (12.0).
//
// Integrity first, exactly like validateBundle: checksums.txt is verified
// against the bundle contents BEFORE any document is trusted. Import
// installs the recorded run as evidence under status Created — a store that
// never observed the execution never fabricates a terminal lifecycle.
#include "reproduction_bundle_import.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

namespace sicnu::experiment
{

namespace
{

Diagnostic importDiag( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, DiagnosticSeverity::Error };
}

/// Loads one bundle JSON document; nullopt (with @p ok=false) when missing
/// or unparsable.
std::optional<QJsonObject> loadJson( const QDir &dir, const QString &name,
                                     QStringList *warnings )
{
    QFile file( dir.filePath( name ) );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        warnings->append( QStringLiteral( "%1 missing" ).arg( name ) );
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson( file.readAll() );
    if ( !document.isObject() )
    {
        warnings->append( QStringLiteral( "%1 is not a JSON object" ).arg( name ) );
        return std::nullopt;
    }
    return document.object();
}

/// Documents the importer actually parses; every one of them must be
/// covered by checksums.txt — a manifest that omits the files it wants the
/// consumer to trust defeats the integrity gate.
const char *kRequiredBundleMembers[] = {
    "manifest.json", "run_config.json", "environment.json",
};

/// Verifies checksums.txt against the bundle contents (the same
/// "integrity first" rule as validateBundle) AND that every required member
/// is listed in it.
bool verifyChecksums( const QDir &dir, QStringList *reasons )
{
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
            continue;
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
    reasons->append( QStringLiteral( "bundle checksums verified" ) );
    return true;
}

} // namespace

QJsonObject ReproductionBundleImportReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "ok" ), ok );
    json.insert( QStringLiteral( "run_id" ), runId );
    json.insert( QStringLiteral( "original_run_id" ), originalRunId );
    json.insert( QStringLiteral( "execution_fingerprint" ), executionFingerprint );
    json.insert( QStringLiteral( "result_fingerprint" ), resultFingerprint );
    json.insert( QStringLiteral( "already_present" ), alreadyPresent );
    json.insert( QStringLiteral( "relocated_files" ), relocatedFiles );
    json.insert( QStringLiteral( "warnings" ), QJsonArray::fromStringList( warnings ) );
    return json;
}

ReproductionBundleImporter::ReproductionBundleImporter( ExperimentStore &experimentStore )
    : m_experimentStore( &experimentStore )
{
}

ReproductionBundleImportReport ReproductionBundleImporter::importRun(
    const ReproductionBundleImportOptions &options, const ReproductionHooks &hooks ) const
{
    ReproductionBundleImportReport report;
    if ( !m_experimentStore || !m_experimentStore->isOpen() )
    {
        report.warnings.append( QStringLiteral( "experiment store is not open" ) );
        return report;
    }
    const QDir dir( options.bundleDir );
    if ( !dir.exists() )
    {
        report.warnings.append( QStringLiteral( "bundle directory missing" ) );
        return report;
    }

    // 1. Integrity gate: tampered bundles never reach the store.
    {
        QStringList integrityReasons;
        if ( !verifyChecksums( dir, &integrityReasons ) )
        {
            report.warnings.append( integrityReasons );
            return report;
        }
    }

    // 2. Schema gate.
    QStringList warnings;
    const auto manifest = loadJson( dir, QStringLiteral( "manifest.json" ), &warnings );
    if ( !manifest )
    {
        report.warnings.append( warnings );
        return report;
    }
    warnings.clear();
    if ( manifest->value( QStringLiteral( "schema_version" ) ).toString() !=
         QLatin1String( kReproductionBundleSchemaVersion ) )
    {
        report.warnings.append( QStringLiteral(
            "bundle schema %1 is not supported (expected %2)" )
                                    .arg( manifest->value( QStringLiteral( "schema_version" ) )
                                              .toString(),
                                          QLatin1String( kReproductionBundleSchemaVersion ) ) );
        return report;
    }
    report.originalRunId = manifest->value( QStringLiteral( "run_id" ) ).toString();

    // 3. Run document.
    const auto runConfig = loadJson( dir, QStringLiteral( "run_config.json" ), &warnings );
    if ( !runConfig )
    {
        report.warnings.append( warnings );
        return report;
    }

    ExperimentRun run;
    report.executionFingerprint =
        runConfig->value( QStringLiteral( "execution_fingerprint" ) ).toString();
    report.resultFingerprint =
        runConfig->value( QStringLiteral( "result_fingerprint" ) ).toString();
    run.setAlgorithmId( runConfig->value( QStringLiteral( "algorithm_id" ) ).toString() );
    run.setAlgorithmVersion(
        runConfig->value( QStringLiteral( "algorithm_version" ) ).toString() );
    run.setParameters( runConfig->value( QStringLiteral( "parameters" ) ).toObject() );
    run.setDatasetVersionId(
        runConfig->value( QStringLiteral( "dataset_version_id" ) ).toString() );
    run.setDatasetFingerprint(
        runConfig->value( QStringLiteral( "dataset_fingerprint" ) ).toString() );
    run.setSplitManifestId(
        runConfig->value( QStringLiteral( "split_manifest_id" ) ).toString() );
    run.setSplitFingerprint(
        runConfig->value( QStringLiteral( "split_fingerprint" ) ).toString() );
    run.setSeed( static_cast<quint64>(
        runConfig->value( QStringLiteral( "seed" ) ).toDouble( 0 ) ) );
    if ( runConfig->contains( QStringLiteral( "determinism_note" ) ) )
        run.setDeterminismNote(
            runConfig->value( QStringLiteral( "determinism_note" ) ).toString() );

    // Environment + software re-parsed through the strict readers; a foreign
    // document fails the import instead of half-populating the record.
    if ( const auto environment = loadJson( dir, QStringLiteral( "environment.json" ), &warnings ) )
    {
        const auto parsed = RunEnvironment::fromJson( *environment );
        if ( !parsed )
        {
            report.warnings.append( QStringLiteral( "environment.json does not parse" ) );
            return report;
        }
        run.setEnvironment( parsed.value() );
    }
    else
        report.warnings.append( warnings );
    warnings.clear();

    // Model pin (empty document = the run had no model identity).
    if ( const auto modelRefs = loadJson( dir, QStringLiteral( "model_refs.json" ), &warnings ) )
    {
        run.setModelId( modelRefs->value( QStringLiteral( "model_id" ) ).toString() );
        run.setModelDigest( modelRefs->value( QStringLiteral( "model_digest" ) ).toString() );
    }
    else
        report.warnings.append( warnings );
    warnings.clear();

    // 4. Identity must be self-consistent: the recorded execution
    // fingerprint must match what the pins hash to now. A bundle whose pins
    // were tampered (checksums forged wholesale) still cannot disagree with
    // its own hash.
    const QString computedFingerprint = runExecutionFingerprint( run.executionIdentity() );
    if ( !report.executionFingerprint.isEmpty() &&
         computedFingerprint != report.executionFingerprint )
    {
        report.warnings.append(
            QStringLiteral( "identity pins do not reproduce the recorded execution"
                            " fingerprint" ) );
        return report;
    }
    report.executionFingerprint = computedFingerprint;

    // 5. Target experiment must exist (a run never floats without a home).
    if ( options.targetExperimentId.isEmpty() )
    {
        report.warnings.append( QStringLiteral( "target experiment id is required" ) );
        return report;
    }
    if ( !m_experimentStore->experimentById( options.targetExperimentId ) )
    {
        report.warnings.append(
            QStringLiteral( "target experiment %1 does not exist" )
                .arg( options.targetExperimentId ) );
        return report;
    }

    // 6. Duplicate-ingest guard: a run with this execution fingerprint may
    // already carry this bundle (idempotent re-import).
    const QStringList twins =
        m_experimentStore->runIdsByExecutionFingerprint( computedFingerprint, 1 );
    if ( !twins.isEmpty() )
    {
        report.ok = true;
        report.alreadyPresent = true;
        report.runId = twins.first();
        report.warnings.append(
            QStringLiteral( "bundle already imported as run %1" ).arg( twins.first() ) );
        return report;
    }

    run.setExperimentId( options.targetExperimentId );
    run.setExecutionRef( report.originalRunId.isEmpty()
                             ? QString()
                             : QStringLiteral( "bundle:%1" ).arg( report.originalRunId ) );
    run.setStatus( RunStatus::Created ); // evidence import, never a fake lifecycle
    if ( options.keepOriginalRunId && !report.originalRunId.isEmpty() )
    {
        run.setRunId( report.originalRunId );
        // A DIFFERENT recorded run already owns the requested id: refuse.
        // (Created-status runs may legally change identity pins on upsert,
        // so an unchecked overwrite would silently destroy that run.)
        if ( m_experimentStore->runById( report.originalRunId ) )
        {
            report.warnings.append(
                QStringLiteral( "run id %1 already exists with a different identity" )
                    .arg( report.originalRunId ) );
            return report;
        }
    }
    else
        run.setRunId( QUuid::createUuid().toString( QUuid::WithoutBraces ) );

    const auto installed = m_experimentStore->upsertRun( run );
    if ( !installed )
    {
        report.warnings.append(
            QStringLiteral( "run install failed: %1" )
                .arg( installed.diagnostics().isEmpty()
                          ? QStringLiteral( "unknown error" )
                          : installed.diagnostics().first().message ) );
        return report;
    }

    // 7. Metrics evidence (protocol + recorded numbers).
    if ( const auto metrics = loadJson( dir, QStringLiteral( "metrics.json" ), &warnings ) )
    {
        if ( !metrics->isEmpty() )
        {
            QJsonObject metricJson = *metrics;
            metricJson.insert( QStringLiteral( "run_id" ), run.runId() );
            const auto record = MetricRecord::fromJson( metricJson );
            if ( !record )
            {
                report.warnings.append( QStringLiteral( "metrics.json does not parse as a"
                                                        " metric record" ) );
            }
            else
            {
                const auto saved = m_experimentStore->saveMetricRecord( record.value() );
                if ( !saved )
                    report.warnings.append(
                        QStringLiteral( "metric record install failed: %1" )
                            .arg( saved.diagnostics().isEmpty()
                                      ? QStringLiteral( "unknown error" )
                                      : saved.diagnostics().first().message ) );
            }
        }
    }
    else
        report.warnings.append( warnings );
    warnings.clear();

    // 8. Portable payload relocation (best-effort; the bundle stays
    // authoritative if the target is unusable).
    const QDir dataDir( dir.filePath( QStringLiteral( "data" ) ) );
    if ( dataDir.exists() )
    {
        if ( options.relocateDataDir.isEmpty() )
        {
            report.warnings.append(
                QStringLiteral( "bundle carries data/ but no relocation target was set" ) );
        }
        else
        {
            const QDir target( options.relocateDataDir );
            target.mkpath( QStringLiteral( "." ) );
            for ( const QFileInfo &entry :
                  dataDir.entryInfoList( QDir::Files ) )
            {
                const QString destination = target.filePath( entry.fileName() );
                QFile::remove( destination ); // relocation is last-writer-wins
                if ( QFile::copy( entry.absoluteFilePath(), destination ) )
                    ++report.relocatedFiles;
                else
                    report.warnings.append(
                        QStringLiteral( "cannot relocate %1" ).arg( entry.fileName() ) );
            }
        }
    }

    // 9. Best-effort availability notes (never verdicts, never blockers).
    if ( !run.modelId().isEmpty() && hooks.modelAvailable &&
         !hooks.modelAvailable( run.modelId(), run.modelDigest() ) )
        report.warnings.append(
            QStringLiteral( "model %1 is not resolvable on this host" ).arg( run.modelId() ) );
    if ( !run.algorithmId().isEmpty() && hooks.algorithmAvailable &&
         !hooks.algorithmAvailable( run.algorithmId() ) )
        report.warnings.append(
            QStringLiteral( "algorithm %1 is not available on this host" )
                .arg( run.algorithmId() ) );

    report.runId = run.runId();
    report.ok = true;
    return report;
}

} // namespace sicnu::experiment
