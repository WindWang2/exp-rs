// data_platform_tools.cpp — implementations for the dataset:/experiment:/
// reproducibility: MCP surface. See data_platform_tools.h for the contract.
#include "data_platform_tools.h"

#include "dataset/annotation.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_version.h"
#include "dataset/label_schema.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample.h"
#include "dataset/split.h"
#include "dataset/wkt.h"
#include "experiment/comparison_ext.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_readiness.h"
#include "experiment/reproduction_bundle.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace sicnu::agent
{

using namespace sicnu::dataset;

namespace
{

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 500;
constexpr int kMaxGroupBuckets = 100;   // dataset:stats by_group cap
constexpr int kMaxVersionsListed = 1000;

QVariantMap toVariant( const QJsonObject &json ) { return json.toVariantMap(); }

int pageLimit( const QVariant &value )
{
    bool ok = false;
    int limit = value.toInt( &ok );
    if ( !ok || limit <= 0 )
        limit = kDefaultLimit;
    if ( limit <= 0 )
        limit = kDefaultLimit;
    return qMin( limit, kMaxLimit );
}

qint64 pageCursor( const QVariant &value )
{
    const qint64 cursor = value.toLongLong();
    return cursor > 0 ? cursor : 0;
}

[[noreturn]] void fail( const QString &message ) { throw std::runtime_error( message.toStdString() ); }

std::unique_ptr<DatasetStore> openDatasetStore( const QVariantMap &args )
{
    const QString dbPath = args.value( QStringLiteral( "dataset_db" ) ).toString();
    if ( dbPath.isEmpty() )
        fail( QStringLiteral( "dataset_db is required" ) );
    auto store = std::make_unique<DatasetStore>();
    QString error;
    if ( !store->open( dbPath, &error ) )
        fail( QStringLiteral( "cannot open dataset_db at '%1'%2" )
                  .arg( dbPath, error.isEmpty() ? QString() : QStringLiteral( ": " ) + error ) );
    return store;
}

std::unique_ptr<sicnu::experiment::ExperimentStore> openExperimentStore( const QVariantMap &args )
{
    const QString dbPath = args.value( QStringLiteral( "experiment_db" ) ).toString();
    if ( dbPath.isEmpty() )
        fail( QStringLiteral( "experiment_db is required" ) );
    auto store = std::make_unique<sicnu::experiment::ExperimentStore>();
    QString error;
    if ( !store->open( dbPath, &error ) )
        fail( QStringLiteral( "cannot open experiment_db at '%1'%2" )
                  .arg( dbPath, error.isEmpty() ? QString() : QStringLiteral( ": " ) + error ) );
    return store;
}

DatasetVersionId parseVersionId( const QVariantMap &args, const char *key = "version" )
{
    const QString text = args.value( QLatin1String( key ) ).toString();
    const auto id = DatasetVersionId::fromString( text );
    if ( !id )
        fail( QStringLiteral( "%1 is not a valid dataset version id: '%2'" ).arg( QLatin1String( key ), text ) );
    return id.value();
}

QJsonArray diagnosticsArray( const QVector<sicnu::data::Diagnostic> &diagnostics )
{
    QJsonArray array;
    for ( const auto &diagnostic : diagnostics )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "code" ), diagnostic.code );
        item.insert( QStringLiteral( "message" ), diagnostic.message );
        item.insert( QStringLiteral( "severity" ),
                     diagnostic.severity == sicnu::data::DiagnosticSeverity::Error
                         ? QStringLiteral( "error" )
                         : ( diagnostic.severity == sicnu::data::DiagnosticSeverity::Warning
                                 ? QStringLiteral( "warning" )
                                 : QStringLiteral( "info" ) ) );
        array.append( item );
    }
    return array;
}

QJsonObject versionSummary( const DatasetVersionRecord &record )
{
    QJsonObject json;
    json.insert( QStringLiteral( "version_id" ), record.versionId() );
    json.insert( QStringLiteral( "dataset_id" ), record.datasetId() );
    json.insert( QStringLiteral( "parent_version_id" ), record.parentVersionId() );
    json.insert( QStringLiteral( "status" ), datasetVersionStatusToString( record.status() ) );
    json.insert( QStringLiteral( "quality_level" ),
                 datasetQualityLevelToString( record.qualityLevel() ) );
    json.insert( QStringLiteral( "fingerprint" ), record.fingerprint() );
    json.insert( QStringLiteral( "note" ), record.note() );
    json.insert( QStringLiteral( "created_at_utc" ), record.createdAtUtc().toString( Qt::ISODate ) );
    if ( record.committedAtUtc().isValid() )
        json.insert( QStringLiteral( "committed_at_utc" ),
                     record.committedAtUtc().toString( Qt::ISODate ) );
    return json;
}

QJsonObject runSummary( const sicnu::experiment::ExperimentRun &run )
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_id" ), run.runId() );
    json.insert( QStringLiteral( "experiment_id" ), run.experimentId() );
    json.insert( QStringLiteral( "status" ), runStatusToString( run.status() ) );
    json.insert( QStringLiteral( "algorithm_id" ), run.algorithmId() );
    json.insert( QStringLiteral( "algorithm_version" ), run.algorithmVersion() );
    json.insert( QStringLiteral( "dataset_version_id" ), run.datasetVersionId() );
    json.insert( QStringLiteral( "dataset_fingerprint" ), run.datasetFingerprint() );
    json.insert( QStringLiteral( "split_manifest_id" ), run.splitManifestId() );
    json.insert( QStringLiteral( "split_fingerprint" ), run.splitFingerprint() );
    json.insert( QStringLiteral( "model_id" ), run.modelId() );
    json.insert( QStringLiteral( "model_digest" ), run.modelDigest() );
    json.insert( QStringLiteral( "seed" ), qint64( run.seed() ) );
    json.insert( QStringLiteral( "determinism" ), determinismGradeToString( run.determinism() ) );
    json.insert( QStringLiteral( "created_at_utc" ), run.createdAtUtc().toString( Qt::ISODate ) );
    json.insert( QStringLiteral( "finished_at_utc" ), run.finishedAtUtc().toString( Qt::ISODate ) );
    return json;
}

QJsonObject runDetail( const sicnu::experiment::ExperimentRun &run )
{
    QJsonObject json = runSummary( run );
    // Full identity detail for an inspected run.
    json.insert( QStringLiteral( "parameters" ),
                 sicnu::experiment::RunEnvironment::redactSecretKeys( run.parameters() ) );
    json.insert( QStringLiteral( "config_hash" ), run.configHash() );
    json.insert( QStringLiteral( "execution_fingerprint" ),
                 sicnu::experiment::runExecutionFingerprint( run.executionIdentity() ) );
    json.insert( QStringLiteral( "result_fingerprint" ), run.resultFingerprint() );
    json.insert( QStringLiteral( "determinism_note" ), run.determinismNote() );
    json.insert( QStringLiteral( "software_revision" ), run.softwareRevision() );
    json.insert( QStringLiteral( "execution_ref" ), run.executionRef() );
    json.insert( QStringLiteral( "started_at_utc" ), run.startedAtUtc().toString( Qt::ISODate ) );
    json.insert( QStringLiteral( "metrics" ),
                 sicnu::experiment::RunEnvironment::redactSecretKeys( run.metrics() ) );
    json.insert( QStringLiteral( "environment" ),
                 sicnu::experiment::RunEnvironment::redactSecretKeys( run.environment().redacted().toJson() ) );
    QJsonArray artifacts;
    for ( const auto &artifact : run.artifacts() )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "path" ), artifact.path );
        item.insert( QStringLiteral( "role" ), artifact.role );
        item.insert( QStringLiteral( "digest" ), artifact.digest );
        item.insert( QStringLiteral( "size_bytes" ), artifact.sizeBytes );
        artifacts.append( item );
    }
    json.insert( QStringLiteral( "artifacts" ), artifacts );
    return json;
}

QVariantMap finishPage( QJsonObject data, qint64 total, qint64 offset, int limit )
{
    const qint64 next = offset + limit;
    data.insert( QStringLiteral( "total" ), total );
    data.insert( QStringLiteral( "next_cursor" ), next < total ? next : -1 );
    return toVariant( data );
}

// --- dataset:* -----------------------------------------------------------

QVariantMap datasetList( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const int limit = pageLimit( args.value( QStringLiteral( "limit" ) ) );
    const qint64 cursor = pageCursor( args.value( QStringLiteral( "cursor" ) ) );
    const auto page = store->listDatasets( cursor, limit );
    if ( !page )
        fail( QStringLiteral( "dataset listing failed: %1" )
                  .arg( page.diagnostics().isEmpty()
                            ? QStringLiteral( "unknown error" )
                            : page.diagnostics().first().message ) );
    QJsonArray rows;
    for ( const QVariantMap &row : page.value().second )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "id" ), row.value( QStringLiteral( "id" ) ).toString() );
        item.insert( QStringLiteral( "name" ), row.value( QStringLiteral( "name" ) ).toString() );
        item.insert( QStringLiteral( "description" ),
                     row.value( QStringLiteral( "description" ) ).toString() );
        rows.append( item );
    }
    QJsonObject data;
    data.insert( QStringLiteral( "datasets" ), rows );
    return finishPage( data, page.value().first, cursor, limit );
}

QJsonObject manifestOfVersion( DatasetStore &store, const DatasetVersionId &versionId )
{
    const auto record = store.versionById( versionId );
    if ( !record )
        fail( QStringLiteral( "dataset version not found: %1" ).arg( versionId.toString() ) );
    const auto manifest =
        DatasetManifest::fromJson( QJsonDocument::fromJson( record->manifestJson().toUtf8() ).object() );
    if ( !manifest )
        fail( QStringLiteral( "stored manifest of %1 does not parse" ).arg( versionId.toString() ) );
    return manifest->toJson();
}

QVariantMap datasetInspect( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const QString datasetText = args.value( QStringLiteral( "dataset" ) ).toString();
    const bool hasVersion = args.contains( QStringLiteral( "version" ) ) &&
                            !args.value( QStringLiteral( "version" ) ).toString().isEmpty();
    if ( hasVersion )
    {
        const auto versionId = parseVersionId( args );
        const auto record = store->versionById( versionId );
        if ( !record )
            fail( QStringLiteral( "dataset version not found: %1" ).arg( versionId.toString() ) );
        QJsonObject data = versionSummary( record.value() );
        data.insert( QStringLiteral( "manifest" ), manifestOfVersion( *store, versionId ) );
        data.insert( QStringLiteral( "sample_count" ), store->sampleCount( versionId ) );
        QJsonArray splits;
        for ( const auto &manifest : store->splitManifestsForVersion( versionId ) )
        {
            QJsonObject item;
            item.insert( QStringLiteral( "manifest_id" ), manifest.manifestId() );
            item.insert( QStringLiteral( "method" ),
                         splitMethodToString( manifest.config().method ) );
            item.insert( QStringLiteral( "fingerprint" ), manifest.fingerprint() );
            splits.append( item );
        }
        data.insert( QStringLiteral( "split_manifests" ), splits );
        return toVariant( data );
    }
    if ( datasetText.isEmpty() )
        fail( QStringLiteral( "dataset or version is required" ) );
    const auto datasetId = DatasetId::fromString( datasetText );
    if ( !datasetId )
        fail( QStringLiteral( "dataset is not a valid id: '%1'" ).arg( datasetText ) );
    const auto record = store->datasetById( datasetId.value() );
    if ( !record )
        fail( QStringLiteral( "dataset not found: %1" ).arg( datasetText ) );
    QJsonObject data = QJsonObject::fromVariantMap( record.value() );
    const auto versions = store->versionsOfDataset( datasetId.value() );
    QJsonArray rows;
    int count = 0;
    for ( const auto &entry : versions )
    {
        if ( ++count > kMaxVersionsListed )
            break;
        rows.append( versionSummary( entry ) );
    }
    data.insert( QStringLiteral( "versions" ), rows );
    data.insert( QStringLiteral( "versions_truncated" ), versions.size() > kMaxVersionsListed );
    return toVariant( data );
}

QVariantMap datasetVersionList( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const QString datasetText = args.value( QStringLiteral( "dataset" ) ).toString();
    const auto datasetId = DatasetId::fromString( datasetText );
    if ( !datasetId )
        fail( QStringLiteral( "dataset is not a valid id: '%1'" ).arg( datasetText ) );
    const auto versions = store->versionsOfDataset( datasetId.value() );
    QJsonArray rows;
    int count = 0;
    for ( const auto &entry : versions )
    {
        if ( ++count > kMaxVersionsListed )
            break;
        rows.append( versionSummary( entry ) );
    }
    QJsonObject data;
    data.insert( QStringLiteral( "dataset_id" ), datasetText );
    data.insert( QStringLiteral( "versions" ), rows );
    data.insert( QStringLiteral( "total" ), qint64( versions.size() ) );
    data.insert( QStringLiteral( "truncated" ), versions.size() > kMaxVersionsListed );
    return toVariant( data );
}

QVariantMap datasetDiff( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const auto load = [&]( const char *key ) -> DatasetManifest {
        const auto versionId = parseVersionId( args, key );
        const auto record = store->versionById( versionId );
        if ( !record )
            fail( QStringLiteral( "dataset version not found: %1" ).arg( versionId.toString() ) );
        const auto manifest = DatasetManifest::fromJson(
            QJsonDocument::fromJson( record->manifestJson().toUtf8() ).object() );
        if ( !manifest )
            fail( QStringLiteral( "stored manifest of %1 does not parse" ).arg( versionId.toString() ) );
        return manifest.value();
    };
    const auto from = load( "from" );
    const auto to = load( "to" );
    const auto diff = diffManifests( from, to );
    if ( !diff )
        fail( QStringLiteral( "diff failed: %1" )
                  .arg( diff.diagnostics().isEmpty() ? QStringLiteral( "unknown error" )
                                                     : diff.diagnostics().first().message ) );
    QJsonObject data;
    data.insert( QStringLiteral( "diff" ), diff.value().toJson() );
    return toVariant( data );
}

QVariantMap datasetStats( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const auto versionId = parseVersionId( args );
    QHash<QString, qint64> byKind;
    QHash<QString, qint64> byGroup;
    qint64 total = 0;
    bool groupsTruncated = false;
    qint64 offset = 0;
    while ( true )
    {
        const auto page = store->samplesPage( versionId, offset, DatasetStore::kMaxPageSize );
        if ( !page )
            fail( QStringLiteral( "sample page failed" ) );
        if ( page.value().second.isEmpty() )
            break;
        for ( const auto &sample : page.value().second )
        {
            ++total;
            ++byKind[sampleKindToString( sample.kind() )];
            const QString group = sample.groupId().isEmpty() ? QStringLiteral( "(ungrouped)" )
                                                             : sample.groupId();
            if ( byGroup.contains( group ) )
                ++byGroup[group];
            else if ( byGroup.size() < kMaxGroupBuckets )
                byGroup[group] = 1;
            else
                groupsTruncated = true;
        }
        offset += page.value().second.size();
    }
    QJsonObject byKindJson;
    for ( auto it = byKind.constBegin(); it != byKind.constEnd(); ++it )
        byKindJson.insert( it.key(), it.value() );
    // Buckets sorted by descending count, ties by name — deterministic output.
    QList<QPair<QString, qint64>> groupBuckets;
    for ( auto it = byGroup.constBegin(); it != byGroup.constEnd(); ++it )
        groupBuckets.append( qMakePair( it.key(), it.value() ) );
    std::sort( groupBuckets.begin(), groupBuckets.end(),
               []( const QPair<QString, qint64> &a, const QPair<QString, qint64> &b ) {
                   if ( a.second != b.second )
                       return a.second > b.second;
                   return a.first < b.first;
               } );
    QJsonObject byGroupJson;
    for ( const auto &bucket : groupBuckets )
        byGroupJson.insert( bucket.first, bucket.second );
    QJsonObject data;
    data.insert( QStringLiteral( "version" ), args.value( QStringLiteral( "version" ) ).toString() );
    data.insert( QStringLiteral( "sample_count" ), total );
    data.insert( QStringLiteral( "by_kind" ), byKindJson );
    data.insert( QStringLiteral( "by_group" ), byGroupJson );
    data.insert( QStringLiteral( "by_group_truncated" ), groupsTruncated );
    return toVariant( data );
}

QVariantMap datasetValidate( const QVariantMap &args )
{
    // READ-ONLY validation: the stored manifest is checked under exactly the
    // strict reader contract consumers use. No staging (staging is a state
    // change and belongs to the management CLI's `dataset validate`).
    auto store = openDatasetStore( args );
    const auto versionId = parseVersionId( args );
    const auto record = store->versionById( versionId );
    if ( !record )
        fail( QStringLiteral( "dataset version not found: %1" ).arg( versionId.toString() ) );
    const auto parsed = DatasetManifest::fromJson(
        QJsonDocument::fromJson( record->manifestJson().toUtf8() ).object() );
    QJsonObject data;
    data.insert( QStringLiteral( "version" ), versionId.toString() );
    data.insert( QStringLiteral( "status" ),
                 datasetVersionStatusToString( record->status() ) );
    data.insert( QStringLiteral( "valid" ), parsed.has_value() );
    if ( parsed )
        data.insert( QStringLiteral( "fingerprint" ), record->fingerprint() );
    else
        data.insert( QStringLiteral( "diagnostics" ),
                     diagnosticsArray( parsed.diagnostics() ) );
    // Truthful: an invalid manifest is a completed inspection with a false
    // flag, NOT a thrown tool error — the caller asked "is this valid?".
    return toVariant( data );
}

QVariantMap datasetLabelSchema( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const QString schemaId = args.value( QStringLiteral( "schema_id" ) ).toString();
    if ( schemaId.isEmpty() )
        fail( QStringLiteral( "schema_id is required" ) );
    QJsonObject data;
    if ( args.contains( QStringLiteral( "schema_version" ) ) )
    {
        const quint64 version = args.value( QStringLiteral( "schema_version" ) ).toULongLong();
        const auto schema = store->labelSchema( schemaId, version );
        if ( !schema )
            fail( QStringLiteral( "label schema not found: %1@%2" ).arg( schemaId ).arg( version ) );
        data.insert( QStringLiteral( "schema" ), schema->toJson() );
    }
    else
    {
        const auto versions = store->labelSchemaVersions( schemaId );
        QJsonArray rows;
        for ( const auto &entry : versions )
        {
            QJsonObject item;
            item.insert( QStringLiteral( "version" ), qint64( entry.first ) );
            item.insert( QStringLiteral( "fingerprint" ), entry.second );
            rows.append( item );
        }
        data.insert( QStringLiteral( "schema_id" ), schemaId );
        data.insert( QStringLiteral( "versions" ), rows );
    }
    return toVariant( data );
}

QJsonObject splitSummaryJson( const SplitManifest &manifest )
{
    QJsonObject json;
    json.insert( QStringLiteral( "manifest_id" ), manifest.manifestId() );
    json.insert( QStringLiteral( "dataset_version_id" ), manifest.datasetVersionId() );
    json.insert( QStringLiteral( "config" ), manifest.config().toJson() );
    json.insert( QStringLiteral( "determinism" ), determinismGradeToString( manifest.determinism() ) );
    if ( !manifest.determinismNote().isEmpty() )
        json.insert( QStringLiteral( "determinism_note" ), manifest.determinismNote() );
    json.insert( QStringLiteral( "fingerprint" ), manifest.fingerprint() );
    json.insert( QStringLiteral( "note" ), manifest.note() );
    json.insert( QStringLiteral( "created_at_utc" ),
                 manifest.createdAtUtc().toString( Qt::ISODate ) );
    json.insert( QStringLiteral( "leakage_summary" ), manifest.leakageSummary() );
    // Role/fold distribution — the shape a consumer needs before materializing.
    QHash<QString, qint64> roleCounts;
    QHash<int, qint64> foldCounts;
    for ( const auto &assignment : manifest.assignments() )
    {
        ++roleCounts[splitRoleToString( assignment.role )];
        if ( assignment.fold >= 0 )
            ++foldCounts[assignment.fold];
    }
    QJsonObject roles;
    for ( auto it = roleCounts.constBegin(); it != roleCounts.constEnd(); ++it )
        roles.insert( it.key(), it.value() );
    json.insert( QStringLiteral( "role_counts" ), roles );
    if ( !foldCounts.isEmpty() )
    {
        QJsonObject folds;
        for ( auto it = foldCounts.constBegin(); it != foldCounts.constEnd(); ++it )
            folds.insert( QString::number( it.key() ), it.value() );
        json.insert( QStringLiteral( "fold_counts" ), folds );
    }
    return json;
}

QVariantMap splitInspect( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const QString manifestId = args.value( QStringLiteral( "split_manifest_id" ) ).toString();
    if ( manifestId.isEmpty() )
    {
        const auto versionId = parseVersionId( args );
        const auto manifests = store->splitManifestsForVersion( versionId );
        QJsonArray rows;
        for ( const auto &manifest : manifests )
            rows.append( splitSummaryJson( manifest ) );
        QJsonObject data;
        data.insert( QStringLiteral( "manifests" ), rows );
        data.insert( QStringLiteral( "total" ), qint64( manifests.size() ) );
        return toVariant( data );
    }
    const auto manifest = store->splitManifestById( manifestId );
    if ( !manifest )
        fail( QStringLiteral( "split manifest not found: %1" ).arg( manifestId ) );
    QJsonObject data = splitSummaryJson( manifest.value() );
    // Optional assignments page (bounded; the full manifest may be 100k+ rows).
    if ( args.contains( QStringLiteral( "limit" ) ) )
    {
        const int limit = pageLimit( args.value( QStringLiteral( "limit" ) ) );
        const qint64 cursor = pageCursor( args.value( QStringLiteral( "cursor" ) ) );
        const auto &assignments = manifest->assignments();
        QJsonArray rows;
        const qint64 end = qMin( qint64( assignments.size() ), cursor + limit );
        for ( qint64 i = cursor; i < end; ++i )
        {
            QJsonObject item;
            item.insert( QStringLiteral( "sample_id" ), assignments[int( i )].sampleId );
            item.insert( QStringLiteral( "role" ),
                         splitRoleToString( assignments[int( i )].role ) );
            item.insert( QStringLiteral( "fold" ), assignments[int( i )].fold );
            rows.append( item );
        }
        data.insert( QStringLiteral( "assignments" ), rows );
        data.insert( QStringLiteral( "assignment_total" ), qint64( assignments.size() ) );
        data.insert( QStringLiteral( "next_cursor" ),
                     end < qint64( assignments.size() ) ? end : qint64( -1 ) );
    }
    return toVariant( data );
}

/// Assembles the flat audit view for one version from paged samples + tip
/// annotations. Evidence keys on the sample provenance document (see header).
/// Returns (samples, boundsUnknownCount).
QVector<AuditSample> assembleAuditSamples( DatasetStore &store,
                                           const DatasetVersionId &versionId,
                                           const SplitManifest &manifest,
                                           int *boundsUnknownOut )
{
    // Tip class per sample: latest annotation revision wins.
    QHash<QString, QString> classBySample;
    QVector<SampleRecord> samples;
    qint64 offset = 0;
    while ( true )
    {
        const auto page = store.samplesPage( versionId, offset, DatasetStore::kMaxPageSize );
        if ( !page )
            fail( QStringLiteral( "sample page failed" ) );
        if ( page.value().second.isEmpty() )
            break;
        for ( const auto &sample : page.value().second )
        {
            samples.append( sample );
            const auto annotations = store.annotationsOfSample( sample.sampleId() );
            for ( const auto &annotation : annotations )
            {
                if ( !annotation.classCode().isEmpty() )
                    classBySample[sample.sampleId()] = annotation.classCode();
            }
        }
        offset += page.value().second.size();
    }

    // Fold placements by sample id (fold manifests carry role Unassigned).
    QHash<QString, QPair<SplitRole, int>> placement;
    for ( const auto &assignment : manifest.assignments() )
        placement[assignment.sampleId] = qMakePair( assignment.role, assignment.fold );

    QVector<AuditSample> audit;
    audit.reserve( samples.size() );
    int boundsUnknown = 0;
    for ( const auto &sample : samples )
    {
        AuditSample item;
        item.input.sampleId = sample.sampleId();
        item.input.groupId = sample.groupId();
        item.input.classCode = classBySample.value( sample.sampleId() );
        item.input.timeMs = sample.timeUtc().isValid() ? sample.timeUtc().toMSecsSinceEpoch() : 0;
        const auto placementIt = placement.constFind( sample.sampleId() );
        if ( placementIt != placement.constEnd() )
        {
            item.role = placementIt->first;
            item.fold = placementIt->second;
        }
        const QJsonObject provenance = sample.provenance();
        item.sceneId = provenance.value( QStringLiteral( "scene_id" ) ).toString();
        if ( item.sceneId.isEmpty() )
            item.sceneId = sample.groupId(); // split.h fallback contract
        item.input.sceneId = item.sceneId;
        item.parentPolygonId = provenance.value( QStringLiteral( "parent_polygon_id" ) ).toString();
        item.sourceObjectId = provenance.value( QStringLiteral( "source_object_id" ) ).toString();
        item.parentSampleId = provenance.value( QStringLiteral( "parent_sample_id" ) ).toString();
        item.pseudoDerived = provenance.value( QStringLiteral( "pseudo_derived" ) ).toBool();
        item.eventGroup = provenance.value( QStringLiteral( "event_group" ) ).toString();
        item.input.eventGroup = item.eventGroup;
        item.contentDigest = provenance.value( QStringLiteral( "content_digest" ) ).toString();
        item.input.year = provenance.value( QStringLiteral( "year" ) ).toInt();

        // Bounds from the payload footprint (window/patch stored WKT, point or
        // pixel coordinates). Unknown bounds stay unknown — the audit's
        // spatial checks then exclude the sample and the gap is quantified.
        QString footprint;
        double width = 0.0;
        double height = 0.0;
        struct PayloadVisitor
        {
            QString *footprint;
            double *width;
            double *height;
            void operator()( const PointSample &point ) const
            {
                *footprint = QStringLiteral( "POINT(%1 %2)" ).arg( point.x ).arg( point.y );
            }
            void operator()( const PixelSample &pixel ) const
            {
                *footprint = QStringLiteral( "POINT(%1 %2)" ).arg( pixel.column ).arg( pixel.row );
            }
            void operator()( const WindowSample &window )
            {
                if ( !window.groundFootprintWkt.isEmpty() )
                    *footprint = window.groundFootprintWkt;
                *width = window.window.width;
                *height = window.window.height;
            }
            void operator()( const PatchSample &patch )
            {
                if ( !patch.groundFootprintWkt.isEmpty() )
                    *footprint = patch.groundFootprintWkt;
                *width = patch.window.width;
                *height = patch.window.height;
            }
            void operator()( const PolygonSample &polygon ) { *footprint = polygon.wkt; }
            void operator()( const ObjectSample & ) {}
            void operator()( const PairSample & ) {}
            void operator()( const TemporalSample & ) {}
            void operator()( const MultiModalSample & ) {}
            void operator()( const std::monostate & ) {}
        };
        std::visit( PayloadVisitor{ &footprint, &width, &height }, sample.payload() );
        item.windowWidth = width;
        item.windowHeight = height;
        if ( !footprint.isEmpty() )
        {
            const auto bounds = parseWktBounds( footprint );
            if ( bounds && bounds.value().size() == 4 )
            {
                item.input.minX = bounds.value()[0];
                item.input.minY = bounds.value()[1];
                item.input.maxX = bounds.value()[2];
                item.input.maxY = bounds.value()[3];
                item.input.validBounds = true;
            }
        }
        if ( !item.input.validBounds )
            ++boundsUnknown;
        audit.append( item );
    }
    if ( boundsUnknownOut )
        *boundsUnknownOut = boundsUnknown;
    return audit;
}

QVariantMap leakageAudit( const QVariantMap &args )
{
    auto store = openDatasetStore( args );
    const QString manifestId = args.value( QStringLiteral( "split_manifest_id" ) ).toString();
    if ( manifestId.isEmpty() )
        fail( QStringLiteral( "split_manifest_id is required" ) );
    const QString mode = args.value( QStringLiteral( "mode" ) ).toString().isEmpty()
                             ? QStringLiteral( "stored" )
                             : args.value( QStringLiteral( "mode" ) ).toString();

    if ( mode == QLatin1String( "stored" ) )
    {
        const auto report = store->latestLeakageReport( manifestId );
        if ( !report )
            fail( QStringLiteral( "no stored leakage report for split manifest %1" )
                      .arg( manifestId ) );
        QJsonObject data;
        data.insert( QStringLiteral( "mode" ), QStringLiteral( "stored" ) );
        data.insert( QStringLiteral( "report" ), report->toJson() );
        return toVariant( data );
    }
    if ( mode != QLatin1String( "run" ) )
        fail( QStringLiteral( "mode must be 'stored' or 'run'" ) );

    const auto manifest = store->splitManifestById( manifestId );
    if ( !manifest )
        fail( QStringLiteral( "split manifest not found: %1" ).arg( manifestId ) );

    LeakageAuditConfig config;
    if ( args.contains( QStringLiteral( "checks" ) ) )
    {
        const QStringList checks =
            args.value( QStringLiteral( "checks" ) ).toString().split( ',', Qt::SkipEmptyParts );
        for ( QString check : checks )
            config.checks.append( check.trimmed() );
    }
    if ( args.contains( QStringLiteral( "distance_threshold" ) ) )
        config.distanceThreshold = args.value( QStringLiteral( "distance_threshold" ) ).toDouble();
    if ( args.contains( QStringLiteral( "overlap_fraction_threshold" ) ) )
        config.overlapFractionThreshold =
            args.value( QStringLiteral( "overlap_fraction_threshold" ) ).toDouble();
    if ( args.contains( QStringLiteral( "buffer_distance" ) ) )
        config.bufferDistance = args.value( QStringLiteral( "buffer_distance" ) ).toDouble();

    int boundsUnknown = 0;
    const auto auditVersionId =
        DatasetVersionId::fromString( manifest->datasetVersionId() );
    if ( !auditVersionId )
        fail( QStringLiteral( "split manifest carries an invalid dataset version id" ) );
    const auto samples =
        assembleAuditSamples( *store, auditVersionId.value(), manifest.value(), &boundsUnknown );
    const auto report =
        LeakageAuditor::audit( manifest->datasetVersionId(), manifestId, samples, config );
    if ( !report )
        fail( QStringLiteral( "audit failed: %1" )
                  .arg( report.diagnostics().isEmpty() ? QStringLiteral( "unknown error" )
                                                       : report.diagnostics().first().message ) );
    // Persist the evidence the report cites (append-only; see store
    // contract). A persist failure FAILS the tool: an unpersisted report
    // would cite evidence the store cannot resolve.
    const auto saved = store->saveLeakageReport( report.value() );
    if ( !saved )
        fail( QStringLiteral( "audit evidence persistence failed: %1" )
                  .arg( saved.diagnostics().isEmpty()
                            ? QStringLiteral( "unknown error" )
                            : saved.diagnostics().first().message ) );

    QJsonObject data;
    data.insert( QStringLiteral( "mode" ), QStringLiteral( "run" ) );
    data.insert( QStringLiteral( "report" ), report.value().toJson() );
    data.insert( QStringLiteral( "bounds_unknown_count" ), boundsUnknown );
    data.insert( QStringLiteral( "persisted" ), true );
    return toVariant( data );
}

// --- experiment:* -----------------------------------------------------------

QVariantMap experimentList( const QVariantMap &args )
{
    auto store = openExperimentStore( args );
    const int limit = pageLimit( args.value( QStringLiteral( "limit" ) ) );
    const qint64 cursor = pageCursor( args.value( QStringLiteral( "cursor" ) ) );
    const auto page = store->listExperiments( cursor, limit );
    if ( !page )
        fail( QStringLiteral( "experiment listing failed" ) );
    QJsonArray rows;
    for ( const auto &experiment : page.value().second )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "experiment_id" ), experiment.experimentId() );
        item.insert( QStringLiteral( "name" ), experiment.name() );
        item.insert( QStringLiteral( "objective" ), experiment.objective() );
        item.insert( QStringLiteral( "tags" ),
                     QJsonArray::fromStringList( experiment.tags() ) );
        item.insert( QStringLiteral( "run_count" ), qint64( experiment.runIds().size() ) );
        item.insert( QStringLiteral( "created_at_utc" ),
                     experiment.createdAtUtc().toString( Qt::ISODate ) );
        rows.append( item );
    }
    QJsonObject data;
    data.insert( QStringLiteral( "experiments" ), rows );
    return finishPage( data, page.value().first, cursor, limit );
}

QVariantMap experimentInspect( const QVariantMap &args )
{
    auto store = openExperimentStore( args );
    const QString runId = args.value( QStringLiteral( "run" ) ).toString();
    if ( !runId.isEmpty() )
    {
        const auto run = store->runById( runId );
        if ( !run )
            fail( QStringLiteral( "run not found: %1" ).arg( runId ) );
        QJsonObject data = runDetail( run.value() );
        const auto metricRecord = store->metricRecordForRun( runId );
        if ( metricRecord )
        {
            data.insert( QStringLiteral( "protocol" ), metricRecord->protocol.toJson() );
            data.insert( QStringLiteral( "metrics_record" ),
                         sicnu::experiment::RunEnvironment::redactSecretKeys( metricRecord->metrics ) );
        }
        return toVariant( data );
    }
    const QString experimentId = args.value( QStringLiteral( "experiment" ) ).toString();
    if ( experimentId.isEmpty() )
        fail( QStringLiteral( "experiment or run is required" ) );
    const auto experiment = store->experimentById( experimentId );
    if ( !experiment )
        fail( QStringLiteral( "experiment not found: %1" ).arg( experimentId ) );
    QJsonObject data = experiment->toJson();
    const auto runPage = store->listRuns( experimentId, QString(), QString(), 0,
                                         sicnu::experiment::ExperimentStore::kMaxPageSize );
    QJsonArray rows;
    if ( runPage )
    {
        for ( const auto &run : runPage.value().second )
            rows.append( runSummary( run ) );
        data.insert( QStringLiteral( "runs" ), rows );
        data.insert( QStringLiteral( "run_total" ), runPage.value().first );
    }
    return toVariant( data );
}

QVariantMap experimentCompare( const QVariantMap &args )
{
    auto store = openExperimentStore( args );
    const QString runA = args.value( QStringLiteral( "a" ) ).toString();
    const QString runB = args.value( QStringLiteral( "b" ) ).toString();
    if ( runA.isEmpty() || runB.isEmpty() )
        fail( QStringLiteral( "a and b (run ids) are required" ) );
    const auto a = store->runById( runA );
    const auto b = store->runById( runB );
    if ( !a || !b )
        fail( QStringLiteral( "run not found: %1" ).arg( !a ? runA : runB ) );
    const auto comparison = sicnu::experiment::RunComparison::compare( a.value(), b.value() );
    QJsonObject data = comparison.toJson();
    data.insert( QStringLiteral( "metric_diff" ), comparison.metricDiff( a.value(), b.value() ) );

    // Protocol compatibility from the stored metric records (when both runs
    // were evaluated); class-schema compatibility when the dataset pins
    // resolve against dataset_db.
    const auto metricA = store->metricRecordForRun( runA );
    const auto metricB = store->metricRecordForRun( runB );
    if ( metricA && metricB )
    {
        const auto protocolCompat =
            sicnu::experiment::compareProtocols( metricA->protocol, metricB->protocol );
        data.insert( QStringLiteral( "protocol_compatibility" ), protocolCompat.toJson() );
        const QString datasetDb = args.value( QStringLiteral( "dataset_db" ) ).toString();
        if ( !datasetDb.isEmpty() )
        {
            auto schemaStore = openDatasetStore( args );
            const auto loadSchema = [ & ]( const QString &versionText )
                -> std::optional<sicnu::dataset::LabelSchema> {
                const auto versionId =
                    sicnu::dataset::DatasetVersionId::fromString( versionText );
                if ( !versionId )
                    return std::nullopt;
                const auto record = schemaStore->versionById( versionId.value() );
                if ( !record )
                    return std::nullopt;
                const auto manifest = sicnu::dataset::DatasetManifest::fromJson(
                    QJsonDocument::fromJson( record->manifestJson().toUtf8() ).object() );
                if ( !manifest || manifest.value().labelSchema().isNull() )
                    return std::nullopt;
                const auto schemaRef = manifest.value().labelSchema();
                return schemaStore->labelSchema( schemaRef.schemaId, schemaRef.version );
            };
            const auto schemaA = loadSchema( a->datasetVersionId() );
            const auto schemaB = loadSchema( b->datasetVersionId() );
            if ( schemaA && schemaB )
            {
                const auto schemaCompat = sicnu::experiment::compareLabelSchemas(
                    schemaA.value(), schemaB.value() );
                data.insert( QStringLiteral( "schema_compatibility" ), schemaCompat.toJson() );
                const auto paired = sicnu::experiment::pairedRunComparison(
                    metricA.value(), metricB.value(), &schemaCompat );
                data.insert( QStringLiteral( "paired_summary" ), paired.toJson() );
            }
        }
    }
    return toVariant( data );
}

// --- reproducibility:* --------------------------------------------------------

QVariantMap reproducibilityInspect( const QVariantMap &args )
{
    auto datasetStore = openDatasetStore( args );
    auto experimentStore = openExperimentStore( args );
    const QString runId = args.value( QStringLiteral( "run" ) ).toString();
    if ( runId.isEmpty() )
        fail( QStringLiteral( "run is required" ) );
    const auto run = experimentStore->runById( runId );
    if ( !run )
        fail( QStringLiteral( "run not found: %1" ).arg( runId ) );

    sicnu::experiment::ReproductionHooks hooks;
    hooks.artifactAvailable = []( const QString &path, qint64 sizeBytes ) {
        const QFileInfo info( path );
        return info.exists() && ( sizeBytes <= 0 || info.size() == sizeBytes );
    };
    auto report = sicnu::experiment::ReplayReadiness::assess( run.value(), datasetStore.get(), hooks );
    const auto fingerprint =
        sicnu::experiment::runExecutionFingerprint( run->executionIdentity() );
    const auto equivalent = sicnu::experiment::ReplayReadiness::equivalentRuns(
        *experimentStore, fingerprint, runId );
    QJsonObject data = report.toJson();
    data.insert( QStringLiteral( "run_id" ), runId );
    data.insert( QStringLiteral( "execution_fingerprint" ), fingerprint );
    data.insert( QStringLiteral( "equivalent_runs" ),
                 QJsonArray::fromStringList( equivalent ) );
    data.insert( QStringLiteral( "software_revision_recorded" ), run->softwareRevision() );
    data.insert( QStringLiteral( "determinism" ),
                 determinismGradeToString( run->determinism() ) );
    return toVariant( data );
}

QVariantMap reproducibilityExport( const QVariantMap &args )
{
    auto datasetStore = openDatasetStore( args );
    auto experimentStore = openExperimentStore( args );
    const QString runId = args.value( QStringLiteral( "run" ) ).toString();
    const QString outputDir = args.value( QStringLiteral( "out" ) ).toString();
    if ( runId.isEmpty() || outputDir.isEmpty() )
        fail( QStringLiteral( "run and out are required" ) );
    sicnu::experiment::ReproductionBundleExporter exporter( *experimentStore, *datasetStore );
    sicnu::experiment::ReproductionBundleOptions options;
    options.outputDir = outputDir;
    const QString portable = args.value( QStringLiteral( "mode" ) ).toString();
    if ( portable == QLatin1String( "portable" ) )
        options.mode = sicnu::experiment::ReproductionBundleOptions::Mode::Portable;
    options.currentSoftwareRevision =
        qEnvironmentVariable( "SICNU_SOFTWARE_REVISION" );
    const auto report = exporter.exportRun( runId, options );
    QJsonObject data = report.toJson();
    if ( !report.ok && report.fileCount == 0 )
        fail( QStringLiteral( "bundle export failed for run %1" ).arg( runId ) );
    return toVariant( data );
}

QVariantMap reproducibilityValidate( const QVariantMap &args )
{
    auto datasetStore = openDatasetStore( args );
    auto experimentStore = openExperimentStore( args );
    const QString bundleDir = args.value( QStringLiteral( "bundle" ) ).toString();
    if ( bundleDir.isEmpty() )
        fail( QStringLiteral( "bundle is required" ) );
    sicnu::experiment::ReproductionBundleExporter exporter( *experimentStore, *datasetStore );
    sicnu::experiment::ReproductionHooks hooks;
    // Explicit caller-declared availability (headless MCP has no registries).
    // The VALUE matters: false wires a hook that answers false - declaring a
    // dependency unavailable can never be read as available.
    if ( args.contains( QStringLiteral( "model_available" ) ) )
    {
        const bool available = args.value( QStringLiteral( "model_available" ) ).toBool();
        hooks.modelAvailable = [ available ]( const QString &, const QString & ) { return available; };
    }
    if ( args.contains( QStringLiteral( "algorithm_available" ) ) )
    {
        const bool available = args.value( QStringLiteral( "algorithm_available" ) ).toBool();
        hooks.algorithmAvailable = [ available ]( const QString & ) { return available; };
    }
    hooks.artifactAvailable = []( const QString &path, qint64 sizeBytes ) {
        const QFileInfo info( path );
        return info.exists() && ( sizeBytes <= 0 || info.size() == sizeBytes );
    };
    const auto validation = exporter.validateBundle( bundleDir, hooks );
    QJsonObject data;
    data.insert( QStringLiteral( "level" ),
                 reproductionLevelToString( validation.level ) );
    data.insert( QStringLiteral( "reasons" ),
                 QJsonArray::fromStringList( validation.reasons ) );
    return toVariant( data );
}

} // namespace

const QList<DataPlatformToolDef> &dataPlatformToolDefs()
{
    static const QList<DataPlatformToolDef> defs = {
        { "dataset:list",
          "List datasets in the authoritative dataset store (paginated).",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "limit", "integer", "Page size 1-500 (default 50)", false },
            { "cursor", "integer", "Offset from the previous next_cursor", false } } },
        { "dataset:inspect",
          "Inspect one dataset version (manifest, sample count, splits) or one dataset with its versions.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "dataset", "string", "Dataset id (when no version given)", false },
            { "version", "string", "Dataset version id", false } } },
        { "dataset:version",
          "List versions of one dataset, oldest first.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "dataset", "string", "Dataset id", true } } },
        { "dataset:diff",
          "Semantic diff between two versions of one dataset.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "from", "string", "Source version id", true },
            { "to", "string", "Target version id", true } } },
        { "dataset:stats",
          "Composition statistics of one version: sample count, kind and group distribution (bounded).",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "version", "string", "Dataset version id", true } } },
        { "dataset:validate",
          "Read-only validation of one version's stored manifest under the strict reader contract. valid=false means INVALID. (Staging - a state change - remains a management-CLI operation.)",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "version", "string", "Dataset version id", true } } },
        { "dataset:label_schema",
          "Read a label schema document, or list its stored versions.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "schema_id", "string", "Label schema id", true },
            { "schema_version", "integer", "Exact schema version (omit to list versions)", false } } },
        { "dataset:split_inspect",
          "Inspect a stored split manifest (config, determinism, role/fold counts, leakage summary, optional assignment page) or list manifests of a version.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "split_manifest_id", "string", "Split manifest id", false },
            { "version", "string", "Dataset version id (list its manifests)", false },
            { "limit", "integer", "Assignment page size 1-500 (omit = no assignments)", false },
            { "cursor", "integer", "Assignment page offset", false } } },
        { "dataset:leakage_audit",
          "Read the stored leakage report of a split (mode=stored) or re-run the audit over store content and persist it (mode=run).",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "split_manifest_id", "string", "Split manifest id", true },
            { "mode", "string", "'stored' (default) or 'run'", false },
            { "checks", "string", "Comma-separated check names (run mode; default all)", false },
            { "distance_threshold", "number", "Center-distance threshold in CRS units (0=off)", false },
            { "overlap_fraction_threshold", "number", "Window overlap fraction threshold", false },
            { "buffer_distance", "number", "Buffer-expansion distance for buffer_overlap", false } } },
        { "experiment:list",
          "List experiments in the authoritative experiment store (paginated).",
          { { "experiment_db", "string", "Path to the experiment store database", true },
            { "limit", "integer", "Page size 1-500 (default 50)", false },
            { "cursor", "integer", "Offset from the previous next_cursor", false } } },
        { "experiment:inspect",
          "Inspect an experiment (with run summaries) or one run (identity pins, artifacts, redacted environment, metrics, protocol).",
          { { "experiment_db", "string", "Path to the experiment store database", true },
            { "experiment", "string", "Experiment id", false },
            { "run", "string", "Run id (takes precedence)", false } } },
        { "experiment:compare",
          "Structural comparability verdict of two runs plus metric deltas (only meaningful within the verdict).",
          { { "experiment_db", "string", "Path to the experiment store database", true },
            { "a", "string", "First run id", true },
            { "b", "string", "Second run id", true } } },
        { "reproducibility:inspect",
          "Replay-readiness of one run: per-dependency status (dataset version, split, artifacts; model/algorithm stay unknown without registry hooks) and an overall Exact/BestEffort/Impossible level.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "experiment_db", "string", "Path to the experiment store database", true },
            { "run", "string", "Run id", true } } },
        { "reproducibility:export",
          "Export a reproduction bundle for one run (reference or portable mode; checksums + secret filtering included).",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "experiment_db", "string", "Path to the experiment store database", true },
            { "run", "string", "Run id", true },
            { "out", "string", "Output directory", true },
            { "mode", "string", "'reference' (default) or 'portable'", false } } },
        { "reproducibility:validate",
          "Validate an exported bundle directory; availability of model/algorithm may be declared by the caller.",
          { { "dataset_db", "string", "Path to the dataset store database", true },
            { "experiment_db", "string", "Path to the experiment store database", true },
            { "bundle", "string", "Bundle directory", true },
            { "model_available", "boolean", "Declare the recorded model resolvable", false },
            { "algorithm_available", "boolean", "Declare the recorded algorithm executable", false } } },
    };
    return defs;
}

bool isDataPlatformTool( const QString &toolId )
{
    return toolId.startsWith( QLatin1String( "dataset:" ) ) ||
           toolId.startsWith( QLatin1String( "experiment:" ) ) ||
           toolId.startsWith( QLatin1String( "reproducibility:" ) );
}

QVariantMap handleDataPlatformTool( const QString &toolId, const QVariantMap &arguments )
{
    if ( toolId == QLatin1String( "dataset:list" ) )
        return datasetList( arguments );
    if ( toolId == QLatin1String( "dataset:inspect" ) )
        return datasetInspect( arguments );
    if ( toolId == QLatin1String( "dataset:version" ) )
        return datasetVersionList( arguments );
    if ( toolId == QLatin1String( "dataset:diff" ) )
        return datasetDiff( arguments );
    if ( toolId == QLatin1String( "dataset:stats" ) )
        return datasetStats( arguments );
    if ( toolId == QLatin1String( "dataset:validate" ) )
        return datasetValidate( arguments );
    if ( toolId == QLatin1String( "dataset:label_schema" ) )
        return datasetLabelSchema( arguments );
    if ( toolId == QLatin1String( "dataset:split_inspect" ) )
        return splitInspect( arguments );
    if ( toolId == QLatin1String( "dataset:leakage_audit" ) )
        return leakageAudit( arguments );
    if ( toolId == QLatin1String( "experiment:list" ) )
        return experimentList( arguments );
    if ( toolId == QLatin1String( "experiment:inspect" ) )
        return experimentInspect( arguments );
    if ( toolId == QLatin1String( "experiment:compare" ) )
        return experimentCompare( arguments );
    if ( toolId == QLatin1String( "reproducibility:inspect" ) )
        return reproducibilityInspect( arguments );
    if ( toolId == QLatin1String( "reproducibility:export" ) )
        return reproducibilityExport( arguments );
    if ( toolId == QLatin1String( "reproducibility:validate" ) )
        return reproducibilityValidate( arguments );
    fail( QStringLiteral( "unknown data-platform tool: %1" ).arg( toolId ) );
}

} // namespace sicnu::agent
