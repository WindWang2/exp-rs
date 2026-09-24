// reproduction_bundle_import.cpp — offline bundle import (12.0).
//
// Integrity first, exactly like validateBundle: checksums.txt is verified
// against the bundle contents BEFORE any document is trusted. Import
// installs the recorded run as evidence under status Created — a store that
// never observed the execution never fabricates a terminal lifecycle.
#include "reproduction_bundle_import.h"

#include "experiment_types.h"

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

/// Self-consistent tamper gate: the metrics-hash gate only proves the record
/// matches its own embedded hash, so an attacker who re-derives the hash can
/// smuggle secret material that the exporter's ingestion redaction would
/// have masked. A record exported by the real pipeline never carries a
/// secret-named key with a value other than the "***" ingestion mask, nor a
/// credential-shaped string value — refuse those instead of installing them
/// into a store whose lab reports ship records byte-identical. Returns the
/// offending key, or empty when clean.
QString unmaskedSecretKeyInMetrics( const QJsonValue &value, const QString &key )
{
    if ( value.isObject() )
    {
        const QJsonObject object = value.toObject();
        for ( auto it = object.begin(); it != object.end(); ++it )
        {
            const QString found = unmaskedSecretKeyInMetrics( it.value(), it.key() );
            if ( !found.isEmpty() )
                return found;
        }
        return QString();
    }
    if ( value.isArray() )
    {
        const QJsonArray array = value.toArray();
        for ( const QJsonValue &item : array )
        {
            const QString found = unmaskedSecretKeyInMetrics( item, key );
            if ( !found.isEmpty() )
                return found;
        }
        return QString();
    }
    if ( !value.isString() )
        return QString();
    const QString text = value.toString();
    if ( text == QStringLiteral( "***" ) )
        return QString(); // already masked at ingestion — legal
    if ( RunEnvironment::nameLooksSecret( key ) || RunEnvironment::valueLooksSecret( text ) )
        return key;
    return QString();
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

    // 1. Integrity gate: tampered bundles never reach the store. Shared
    // with validateBundle — one verifier, one required-members contract.
    {
        QStringList integrityReasons;
        if ( !verifyBundleChecksums( options.bundleDir, &integrityReasons ) )
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
    // The identity gate below only means something when the bundle states
    // the fingerprint it claims. A bundle that "forgot" the key must not
    // skip the gate — key deletion is exactly what a wholesale checksum
    // re-forgery looks like. Missing key = refused, not waived.
    if ( report.executionFingerprint.isEmpty() )
    {
        report.warnings.append( QStringLiteral(
            "run_config.json states no execution_fingerprint; the identity gate"
            " cannot be evaluated, so the bundle is refused" ) );
        return report;
    }
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
    // Seeds are 64-bit; JSON numbers survive a double round-trip exactly
    // only below 2^53. The exporter writes a lossless seed_hex pin (same
    // contract as the run-bridge RunPins); the decimal key stays supported
    // for bundles written before the hex pin existed.
    const QString seedHex = runConfig->value( QStringLiteral( "seed_hex" ) ).toString();
    if ( !seedHex.isEmpty() )
    {
        bool seedOk = false;
        run.setSeed( seedHex.toULongLong( &seedOk, 16 ) );
        if ( !seedOk )
        {
            report.warnings.append(
                QStringLiteral( "run_config.json seed_hex is not valid hexadecimal" ) );
            return report;
        }
    }
    else
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
    {
        // The exporter ALWAYS writes environment.json; a bundle without a
        // parsable one is incomplete or tampered. Importing with an empty
        // environment would forge a false "no environment" negative in the
        // evidence completeness projection downstream.
        report.warnings.append( warnings );
        return report;
    }
    warnings.clear();

    // Model pin (empty document = the run had no model identity).
    if ( const auto modelRefs = loadJson( dir, QStringLiteral( "model_refs.json" ), &warnings ) )
    {
        run.setModelId( modelRefs->value( QStringLiteral( "model_id" ) ).toString() );
        run.setModelDigest( modelRefs->value( QStringLiteral( "model_digest" ) ).toString() );
    }
    else
    {
        // Same contract as environment.json: always written by the exporter
        // (possibly empty), so absence is a bundle defect, not a shrug.
        report.warnings.append( warnings );
        return report;
    }
    warnings.clear();

    // 4. Identity must be self-consistent: the recorded execution
    // fingerprint must match what the pins hash to now. A bundle whose pins
    // were tampered (checksums forged wholesale) still cannot disagree with
    // its own hash.
    const QString computedFingerprint = runExecutionFingerprint( run.executionIdentity() );
    if ( computedFingerprint != report.executionFingerprint )
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
    const auto twinsLookup =
        m_experimentStore->runIdsByExecutionFingerprint( computedFingerprint, 1 );
    if ( !twinsLookup )
    {
        report.warnings.append(
            QStringLiteral( "duplicate-ingest guard could not scan the store: %1" )
                .arg( twinsLookup.diagnostics().isEmpty()
                          ? QStringLiteral( "unknown store error" )
                          : twinsLookup.diagnostics().first().message ) );
        return report;
    }
    const QStringList twins = twinsLookup.value();
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

    // 6b. Metrics evidence — installed BEFORE the run so a refusal here
    // leaves zero partial state (no half-installed run without its numbers) (protocol + recorded numbers).
    if ( const auto metrics = loadJson( dir, QStringLiteral( "metrics.json" ), &warnings ) )
    {
        if ( !metrics->isEmpty() )
        {
            QJsonObject metricJson = *metrics;
            metricJson.insert( QStringLiteral( "run_id" ), run.runId() );
            const auto record = MetricRecord::fromJson( metricJson );
            if ( !record )
            {
                // A metrics document that exists but cannot install would
                // silently DROP the run's only recorded numbers while the
                // import still reports ok — evidence loss disguised as
                // success. Refuse the import instead.
                report.warnings.append( QStringLiteral( "metrics.json does not parse as a"
                                                        " metric record" ) );
                return report;
            }
            const QString secretKey =
                unmaskedSecretKeyInMetrics( record->metrics, QStringLiteral( "metrics" ) );
            if ( !secretKey.isEmpty() )
            {
                // Self-consistent tamper: a re-signed metrics_hash makes the
                // record provably "its own", not trustworthy. A real export
                // never carries unmasked secret material — the ingestion
                // redaction masked it before the record was hash-committed.
                report.warnings.append(
                    QStringLiteral( "metrics.json carries unmasked secret-shaped"
                                    " material under '%1' — refused" )
                        .arg( secretKey ) );
                return report;
            }
            else
            {
                const auto saved = m_experimentStore->saveMetricRecord( record.value() );
                if ( !saved )
                {
                    // Includes the metrics-hash mismatch gate: a bundle whose
                    // embedded hash disagrees with its own content is tamper
                    // evidence. Installing the run while dropping its only
                    // recorded numbers would be evidence loss dressed as
                    // success — refuse the import instead.
                    report.warnings.append(
                        QStringLiteral( "metric record install failed: %1" )
                            .arg( saved.diagnostics().isEmpty()
                                      ? QStringLiteral( "unknown error" )
                                      : saved.diagnostics().first().message ) );
                    return report;
                }
            }
        }
    }
    else
    {
        report.warnings.append( warnings );
        return report;
    }
    warnings.clear();

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

    // 7. Portable payload relocation (best-effort; the bundle stays
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

    // 8. Best-effort availability notes (never verdicts, never blockers).
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
