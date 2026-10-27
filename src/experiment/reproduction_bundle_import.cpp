// reproduction_bundle_import.cpp — offline bundle import (12.0).
//
// Integrity first, exactly like validateBundle: checksums.txt is verified
// against the bundle contents BEFORE any document is trusted. Import
// installs the recorded run as evidence under status Created — a store that
// never observed the execution never fabricates a terminal lifecycle.
#include "reproduction_bundle_import.h"

#include <cmath>

#include "experiment_ids.h"
#include "experiment_types.h"

#include "dataset/dataset_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
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
    json.insert( QStringLiteral( "lineage_edges_reattached" ), lineageEdgesReattached );
    json.insert( QStringLiteral( "warnings" ), QJsonArray::fromStringList( warnings ) );
    return json;
}

ReproductionBundleImporter::ReproductionBundleImporter( ExperimentStore &experimentStore )
    : m_experimentStore( &experimentStore )
{
}

ReproductionBundleImporter::ReproductionBundleImporter(
    ExperimentStore &experimentStore, sicnu::dataset::DatasetStore &datasetStore )
    : m_experimentStore( &experimentStore ), m_datasetStore( &datasetStore )
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
    {
        // Legacy decimal seed (bundles written before the hex pin existed,
        // #1333 item 8): a JSON number survives a double round-trip exactly
        // only as a non-negative integer below 2^53. Anything else — huge
        // seeds, negative JSON numbers, non-numeric values — cannot restore
        // the recorded seed, and importing a silently wrong one forges
        // reproducibility. Refused, never guessed.
        static constexpr double kMaxLosslessSeed = 9007199254740992.0; // 2^53
        const QJsonValue seedValue = runConfig->value( QStringLiteral( "seed" ) );
        const double seedDouble = seedValue.toDouble( -1.0 );
        const bool integral = seedDouble == std::floor( seedDouble );
        if ( !seedValue.isDouble() || !integral || seedDouble < 0.0 ||
             seedDouble >= kMaxLosslessSeed )
        {
            report.warnings.append( QStringLiteral(
                "run_config.json seed is not losslessly restorable (legacy decimal"
                " seeds must be integers in [0, 2^53); re-export the bundle with a"
                " seed_hex pin) — import refused rather than guessing" ) );
            return report;
        }
        run.setSeed( static_cast<quint64>( seedDouble ) );
    }
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

    // The empty target-experiment-id refusal lives at step 5 (before any
    // store mutation). The A5 protection installed below is the COMPENSATION
    // leg: the metrics record is written before the run by design, and a
    // failed run install deletes it again — no orphan metric row survives.
    run.setExperimentId( options.targetExperimentId );
    run.setExecutionRef( report.originalRunId.isEmpty()
                             ? QString()
                             : QStringLiteral( "bundle:%1" ).arg( report.originalRunId ) );
    run.setStatus( RunStatus::Created ); // evidence import, never a fake lifecycle
    if ( options.keepOriginalRunId && !report.originalRunId.isEmpty() )
    {
        // The manifest is bundle-supplied, and a self-consistent
        // checksums.txt only proves the bundle matches ITSELF — never that
        // the recorded run id is well-formed. RunId::fromString is the
        // boundary contract (experiment_ids.h: "invalid run ids are
        // refused, never sanitized"): an unchecked id here becomes a store
        // identity, and store identities name capsule/checkpoint
        // directories downstream, so a path separator, a leading dot or a
        // 65-char blob must never reach the store. Refuse, never sanitize.
        const auto requestedRunId = RunId::fromString( report.originalRunId );
        if ( !requestedRunId )
        {
            report.warnings.append(
                QStringLiteral( "manifest run_id %1 is not a valid run id"
                                " (filename-safe, at most 64 chars,"
                                " [A-Za-z0-9._-], no leading dot) — import"
                                " refused rather than sanitized" )
                    .arg( report.originalRunId ) );
            return report;
        }
        run.setRunId( requestedRunId->toString() );
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
        // Fresh id through the RunId authority, so an imported run carries
        // the same id shape RunId::generate() produces everywhere else.
        run.setRunId( RunId::generate().toString() );

    // 6b. Metrics evidence — installed BEFORE the run so a refusal here
    // leaves zero partial state (no half-installed run without its numbers) (protocol + recorded numbers).
    bool metricsInstalled = false;
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
                metricsInstalled = true;
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
        // The metrics record above was installed before the run: without
        // compensation a failed run install strands an orphan metric row
        // keyed to a run id that does not exist (risk A5). Undo it now.
        if ( metricsInstalled )
        {
            const auto undone = m_experimentStore->deleteMetricRecord( run.runId() );
            if ( !undone )
                report.warnings.append(
                    QStringLiteral( "compensation failed: the metrics record for run %1"
                                    " could not be removed after run install failure —"
                                    " manual cleanup required" )
                        .arg( run.runId() ) );
        }
        report.warnings.append(
            QStringLiteral( "run install failed: %1" )
                .arg( installed.diagnostics().isEmpty()
                          ? QStringLiteral( "unknown error" )
                          : installed.diagnostics().first().message ) );
        return report;
    }

    // 7. Lineage slice (provenance.json). R24-B (#1581 residual): the
    // exporter now writes DIRECTION-PRESERVING items
    // ({edge,from_kind,from_id,to_kind,to_id}); those are re-attached here,
    // with the run endpoint rewritten to the installed run id (a fresh-id
    // import would otherwise attach edges naming an id the store does not
    // hold). Endpoints from the bundle are VALIDATED, never trusted: a
    // self-consistent bundle can still lie about lineage (checksums prove
    // self-consistency only). Legacy 3-key items keep the historical
    // named-gap warning; dataset-side endpoints need the optional
    // DatasetStore — without it that half stays a named gap.
    {
        QStringList sliceWarnings;
        const auto provenance =
            loadJson( dir, QStringLiteral( "provenance.json" ), &sliceWarnings );
        if ( !provenance )
        {
            report.warnings.append( sliceWarnings );
        }
        else
        {
            const QJsonValue edgesValue =
                provenance->value( QStringLiteral( "run_edges" ) );
            if ( !edgesValue.isArray() )
            {
                report.warnings.append( QStringLiteral(
                    "provenance.json does not record a run_edges array" ) );
            }
            else
            {
                const QJsonArray edges = edgesValue.toArray();
                int reattached = 0;
                int legacyItems = 0;
                int datasetSideSkipped = 0;
                int refused = 0;

                // Validation vocabulary (lineage.h ADR 0138 node kinds).
                const QSet<QString> kNodeKinds{
                    QStringLiteral( "asset" ),      QStringLiteral( "dataset" ),
                    QStringLiteral( "dataset_version" ), QStringLiteral( "sample" ),
                    QStringLiteral( "annotation" ), QStringLiteral( "split" ),
                    QStringLiteral( "experiment" ), QStringLiteral( "run" ),
                    QStringLiteral( "artifact" ),   QStringLiteral( "metric" ),
                };
                const QSet<QString> kDatasetSideKinds{
                    QStringLiteral( "asset" ),           QStringLiteral( "dataset" ),
                    QStringLiteral( "dataset_version" ), QStringLiteral( "sample" ),
                    QStringLiteral( "annotation" ),      QStringLiteral( "split" ),
                };
                // Same spirit as the RunId authority (experiment_ids.h):
                // bounded length, conservative charset — an endpoint id names
                // store rows and (for run/artifact) directories downstream.
                auto validEndpointId = []( const QString &id )
                {
                    if ( id.isEmpty() || id.size() > 256 )
                        return false;
                    for ( const QChar c : id )
                    {
                        const bool okChar = c.isLetterOrNumber() || c == QLatin1Char( '.' )
                                            || c == QLatin1Char( '_' ) || c == QLatin1Char( '-' )
                                            || c == QLatin1Char( ':' );
                        if ( !okChar )
                            return false;
                    }
                    return true;
                };

                constexpr int kMaxReattachEdges = 256;
                for ( qsizetype i = 0; i < edges.size(); ++i )
                {
                    const QJsonObject item = edges.at( i ).toObject();
                    const QString fromKind = item.value( QStringLiteral( "from_kind" ) ).toString();
                    const QString fromId = item.value( QStringLiteral( "from_id" ) ).toString();
                    const QString toKind = item.value( QStringLiteral( "to_kind" ) ).toString();
                    const QString toId = item.value( QStringLiteral( "to_id" ) ).toString();
                    const QString edgeKind = item.value( QStringLiteral( "edge" ) ).toString();

                    if ( fromKind.isEmpty() && toKind.isEmpty()
                         && item.contains( QStringLiteral( "other_kind" ) ) )
                    {
                        // Legacy shape (other_kind/other_id only): the
                        // direction was lost at export; keep the named-gap
                        // warning verbatim for these. Items without either
                        // shape fall through to the refused path below —
                        // garbage is never mislabeled as old-export (R1).
                        ++legacyItems;
                        continue;
                    }
                    if ( reattached + 1 > kMaxReattachEdges )
                    {
                        report.warnings.append(
                            QStringLiteral( "provenance.json records more than %1"
                                            " re-attachable lineage edges — the rest are"
                                            " skipped" )
                                .arg( kMaxReattachEdges ) );
                        refused += static_cast<int>( edges.size() ) - static_cast<int>( i );
                        break;
                    }
                    const QString index = QString::number( i );
                    const bool shapeOk = kNodeKinds.contains( fromKind )
                                         && kNodeKinds.contains( toKind )
                                         && validEndpointId( fromId )
                                         && validEndpointId( toId ) && !edgeKind.isEmpty()
                                         && edgeKind.size() <= 64
                                         && !( fromKind == toKind && fromId == toId );
                    if ( !shapeOk )
                    {
                        ++refused;
                        report.warnings.append(
                            QStringLiteral( "provenance.json run_edges[%1] is not a"
                                            " well-formed lineage edge — skipped" )
                                .arg( index ) );
                        continue;
                    }
                    // Exactly one endpoint must be the recorded run; the
                    // other endpoint must not silently claim a DIFFERENT run
                    // (that would fabricate third-party provenance).
                    const bool fromIsRun =
                        fromKind == QLatin1String( "run" ) && fromId == report.originalRunId;
                    const bool toIsRun =
                        toKind == QLatin1String( "run" ) && toId == report.originalRunId;
                    const bool otherIsForeignRun =
                        ( fromIsRun ? toKind : fromKind ) == QLatin1String( "run" );
                    if ( fromIsRun == toIsRun )
                    {
                        ++refused;
                        report.warnings.append(
                            QStringLiteral( "provenance.json run_edges[%1] must name the"
                                            " recorded run as exactly one endpoint —"
                                            " skipped" )
                                .arg( index ) );
                        continue;
                    }
                    if ( otherIsForeignRun )
                    {
                        ++refused;
                        report.warnings.append(
                            QStringLiteral( "provenance.json run_edges[%1] links the"
                                            " recorded run to a different run — import"
                                            " refuses to fabricate third-party"
                                            " provenance" )
                                .arg( index ) );
                        continue;
                    }

                    // R1 review: report.runId is only assigned AFTER this
                    // block — the rewrite source must be the upserted run's
                    // id (identical to report.runId for keepOriginalRunId,
                    // the fresh generated id otherwise).
                    const QString finalRunId = run.runId();
                    const bool datasetSide = fromIsRun
                                                 ? kDatasetSideKinds.contains( toKind )
                                                 : kDatasetSideKinds.contains( fromKind );
                    sicnu::data::Result<void> inserted = sicnu::data::Result<void>::failure(
                        Diagnostic{} );
                    if ( datasetSide )
                    {
                        if ( !m_datasetStore )
                        {
                            ++datasetSideSkipped;
                            continue;
                        }
                        inserted = m_datasetStore->addLineageEdge(
                            fromIsRun ? QStringLiteral( "run" ) : fromKind,
                            fromIsRun ? finalRunId : fromId, edgeKind,
                            fromIsRun ? toKind : QStringLiteral( "run" ),
                            fromIsRun ? toId : finalRunId );
                    }
                    else
                    {
                        inserted = m_experimentStore->addLineageEdge(
                            fromIsRun ? QStringLiteral( "run" ) : fromKind,
                            fromIsRun ? finalRunId : fromId, edgeKind,
                            fromIsRun ? toKind : QStringLiteral( "run" ),
                            fromIsRun ? toId : finalRunId );
                    }
                    if ( !inserted )
                    {
                        report.warnings.append(
                            QStringLiteral( "lineage edge re-attach failed at"
                                            " run_edges[%1]: %2" )
                                .arg( index,
                                      inserted.diagnostics().isEmpty()
                                          ? QStringLiteral( "unknown error" )
                                          : inserted.diagnostics().first().message ) );
                        continue;
                    }
                    ++reattached;
                }

                report.lineageEdgesReattached = reattached;
                if ( legacyItems > 0 )
                {
                    report.warnings.append(
                        QStringLiteral( "provenance.json records %1 legacy lineage"
                                        " item(s) without edge direction (old export);"
                                        " those cannot be re-attached" )
                            .arg( legacyItems ) );
                }
                if ( datasetSideSkipped > 0 )
                {
                    report.warnings.append(
                        QStringLiteral( "provenance.json records %1 dataset-side lineage"
                                        " edge(s) that import cannot re-attach without a"
                                        " DatasetStore; the imported run's dataset-side"
                                        " lineage stays unlinked" )
                            .arg( datasetSideSkipped ) );
                }
                if ( reattached == 0 && legacyItems == 0 && datasetSideSkipped == 0
                     && refused == 0 && !edges.isEmpty() )
                {
                    // Empty-ish arrays with zero work still deserve a note so
                    // "no edges" stays distinguishable from "silently none".
                    report.warnings.append(
                        QStringLiteral( "provenance.json records no re-attachable lineage"
                                        " edges around the recorded run" ) );
                }
            }
        }
    }

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
            // R15-B (B12): only payloads the integrity gate actually
            // verified may leave the bundle. verifyBundleChecksums hashes
            // every LISTED member but its coverage sweep is top-level only,
            // so data/ entries are trusted solely when checksums.txt lists
            // "data/<name>". A symlink (or anything resolving outside data/)
            // would make the copy read an arbitrary host file into the
            // relocation target — refused, never followed.
            QSet<QString> verifiedMembers;
            {
                QFile checksumFile( dir.filePath( QStringLiteral( "checksums.txt" ) ) );
                if ( checksumFile.open( QIODevice::ReadOnly ) )
                {
                    const QStringList lines = QString::fromUtf8( checksumFile.readAll() )
                                                  .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
                    for ( QString line : lines )
                    {
                        // R15-B (item 7): same CRLF tolerance as
                        // verifyBundleChecksums, so a CRLF table that passed
                        // the gate does not silently refuse every relocation.
                        if ( line.endsWith( QLatin1Char( '\r' ) ) )
                            line.chop( 1 );
                        if ( line.size() > 66 && line.indexOf( QStringLiteral( "  " ) ) == 64 )
                            verifiedMembers.insert( line.mid( 66 ) );
                    }
                }
            }
            const QString canonicalDataDir = QFileInfo( dataDir.absolutePath() ).canonicalFilePath();
            const QDir target( options.relocateDataDir );
            target.mkpath( QStringLiteral( "." ) );
            for ( const QFileInfo &entry :
                  dataDir.entryInfoList( QDir::Files | QDir::System | QDir::Hidden ) )
            {
                const QString memberName = QStringLiteral( "data/" ) + entry.fileName();
                const QString canonicalEntry = entry.canonicalFilePath();
                QString refusal;
                if ( entry.isSymLink() )
                    refusal = QStringLiteral( "is a symbolic link" );
                else if ( !entry.isFile() )
                    refusal = QStringLiteral( "is not a regular file" );
                else if ( canonicalDataDir.isEmpty() || canonicalEntry.isEmpty() ||
                          QFileInfo( canonicalEntry ).absolutePath() != canonicalDataDir )
                    refusal = QStringLiteral( "resolves outside the bundle data/ directory" );
                else if ( !verifiedMembers.contains( memberName ) )
                    refusal = QStringLiteral( "is not covered by checksums.txt" );
                if ( !refusal.isEmpty() )
                {
                    report.warnings.append( QStringLiteral( "refused to relocate %1: %2" )
                                                .arg( entry.fileName(), refusal ) );
                    continue;
                }
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
