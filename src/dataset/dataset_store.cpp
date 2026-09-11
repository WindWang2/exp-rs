// dataset_store.cpp — SQLite implementation of the dataset store.
//
// Uses the SQLite C API directly, mirroring ArtifactStore/GovernanceStore
// (headers stay Qt-only, the store keeps one connection guarded by a mutex).
//
// Storage layout (schema version 1):
//   ds_meta(key TEXT PK, value TEXT)
//   datasets(id TEXT PK, name, description, created_ms)
//   dataset_versions(id TEXT PK, dataset_id, parent_version_id, status,
//                    quality_level, note, created_ms, committed_ms,
//                    manifest_json, fingerprint, staged)
//   dataset_lineage(from_kind, from_id, edge_kind, to_kind, to_id, created_ms)
//
// The `staged` column records "manifest validated + fingerprint stamped,
// commit not yet observed". commitVersion clears it in the SAME transaction
// that flips status to committed, so a crash can only leave a staged draft —
// which staleStagedDrafts() reports.
#include "dataset_store.h"

#include "dataset_fingerprint.h"
#include "dataset_store_impl.h"
#include "runtime/observability/fault_point.h"
#include "runtime/observability/trace.h"

#include <chrono>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <sqlite3.h>

#include <QUuid>

namespace sicnu::dataset
{

namespace
{

#define DATASET_VERSION_COLS     "id, dataset_id, parent_version_id, status, quality_level, note,"     " created_ms, committed_ms, manifest_json, fingerprint, staged"

#define DATASET_LINEAGE_COLS     "from_kind, from_id, edge_kind, to_kind, to_id"

// The shared statement helper now lives in dataset_store_impl.h.
using Stmt = StoreStmt;

/// Parses one row of DATASET_VERSION_COLS. nullopt on a corrupt row
/// (unparsable vocabulary) — corruption surfaces as "absent", never as a
/// fabricated draft.
std::optional<DatasetVersionRecord> recordFromRow( const Stmt &stmt )
{
    DatasetVersionRecord record;
    record.setVersionId( stmt.text( 0 ) );
    record.setDatasetId( stmt.text( 1 ) );
    record.setParentVersionId( stmt.text( 2 ) );
    const auto status = datasetVersionStatusFromString( stmt.text( 3 ) );
    if ( !status || record.versionId().isEmpty() )
        return std::nullopt;
    record.setStatus( *status );
    const auto quality = datasetQualityLevelFromString( stmt.text( 4 ) );
    if ( !quality )
        return std::nullopt;
    record.setQualityLevel( *quality );
    record.setNote( stmt.text( 5 ) );
    record.setCreatedAtUtc( QDateTime::fromMSecsSinceEpoch( stmt.i64( 6 ) ) );
    record.setCommittedAtUtc( QDateTime::fromMSecsSinceEpoch( stmt.i64( 7 ) ) );
    record.setManifestJson( stmt.text( 8 ) );
    record.setFingerprint( stmt.text( 9 ) );
    return record;
}

} // namespace

DatasetStore::~DatasetStore()
{
    close();
}

bool DatasetStore::open( const QString &dbPath, QString *errorOut )
{
    close();

    sqlite3 *db = nullptr;
    if ( sqlite3_open_v2( dbPath.toUtf8().constData(), &db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr ) != SQLITE_OK )
    {
        if ( errorOut )
            *errorOut = db ? QString::fromUtf8( sqlite3_errmsg( db ) )
                           : QStringLiteral( "cannot open %1" ).arg( dbPath );
        if ( db )
            sqlite3_close( db );
        return false;
    }

    m_impl = new Impl;
    m_impl->db = db;
    m_storePath = dbPath;

    if ( !m_impl->exec( "PRAGMA journal_mode=WAL", errorOut ) ||
         !m_impl->exec( "PRAGMA synchronous=NORMAL", errorOut ) ||
         !m_impl->exec( "PRAGMA busy_timeout=5000", errorOut ) )
    {
        close();
        return false;
    }

    // Safe creation: IF NOT EXISTS on every object; an existing schema stamp
    // is never rewritten by open (that would mask a foreign/older store).
    if ( !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS ds_meta("
             "key TEXT PRIMARY KEY, value TEXT NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS datasets("
             "id TEXT PRIMARY KEY, name TEXT NOT NULL,"
             "description TEXT NOT NULL DEFAULT '',"
             "created_ms INTEGER NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS dataset_versions("
             "id TEXT PRIMARY KEY, dataset_id TEXT NOT NULL,"
             "parent_version_id TEXT NOT NULL DEFAULT '',"
             "status TEXT NOT NULL, quality_level TEXT NOT NULL DEFAULT 'unassessed',"
             "note TEXT NOT NULL DEFAULT '',"
             "created_ms INTEGER NOT NULL, committed_ms INTEGER NOT NULL DEFAULT 0,"
             "manifest_json TEXT NOT NULL DEFAULT '',"
             "fingerprint TEXT NOT NULL DEFAULT '',"
             "staged INTEGER NOT NULL DEFAULT 0)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_versions_dataset"
             " ON dataset_versions(dataset_id, created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS dataset_lineage(" DATASET_LINEAGE_COLS
             ", created_ms INTEGER NOT NULL,"
             " PRIMARY KEY(from_kind, from_id, edge_kind, to_kind, to_id))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_lineage_from"
             " ON dataset_lineage(from_kind, from_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_lineage_to"
             " ON dataset_lineage(to_kind, to_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS label_schemas("
             "schema_id TEXT NOT NULL, version INTEGER NOT NULL,"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(schema_id, version))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS samples("
             "dataset_version_id TEXT NOT NULL, sample_id TEXT NOT NULL,"
             "kind TEXT NOT NULL, group_id TEXT NOT NULL DEFAULT '',"
             "time_ms INTEGER NOT NULL DEFAULT 0, weight REAL NOT NULL DEFAULT 1.0,"
             "json TEXT NOT NULL, roword INTEGER NOT NULL,"
             "PRIMARY KEY(dataset_version_id, sample_id))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_samples_page"
             " ON samples(dataset_version_id, roword)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_samples_group"
             " ON samples(group_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS annotations("
             "annotation_id TEXT NOT NULL, revision INTEGER NOT NULL,"
             "target_sample_id TEXT NOT NULL, dataset_version_id TEXT NOT NULL,"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(annotation_id, revision))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_annotations_sample"
             " ON annotations(target_sample_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_annotations_version"
             " ON annotations(dataset_version_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS split_manifests("
             "manifest_id TEXT NOT NULL, dataset_version_id TEXT NOT NULL,"
             "fingerprint TEXT NOT NULL, json TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(manifest_id))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_splits_version"
             " ON split_manifests(dataset_version_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS leakage_reports("
             "split_manifest_id TEXT NOT NULL, report_digest TEXT NOT NULL,"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(split_manifest_id, report_digest))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_leakage_split"
             " ON leakage_reports(split_manifest_id, created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS sample_facets("
             "dataset_version_id TEXT NOT NULL, sample_id TEXT NOT NULL,"
             "facet TEXT NOT NULL, value TEXT NOT NULL,"
             "PRIMARY KEY(dataset_version_id, sample_id, facet, value))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_facets_query"
             " ON sample_facets(dataset_version_id, facet, value)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS quality_summaries("
             "dataset_version_id TEXT NOT NULL PRIMARY KEY,"
             "sample_count INTEGER NOT NULL, max_roword INTEGER NOT NULL,"
             "json TEXT NOT NULL, updated_ms INTEGER NOT NULL)",
             errorOut ) )
    {
        close();
        return false;
    }

    QString existing;
    {
        QMutexLocker lock( &m_impl->mutex );
        Stmt stmt( db, QStringLiteral( "SELECT value FROM ds_meta WHERE key='schema_version'" ) );
        if ( stmt && stmt.stepRow() )
            existing = stmt.text( 0 );
    }
    if ( existing.isEmpty() )
    {
        if ( !m_impl->exec(
                 QStringLiteral(
                     "INSERT OR REPLACE INTO ds_meta(key,value) VALUES('schema_version','%1')" )
                     .arg( kDatasetStoreSchemaVersion )
                     .toUtf8()
                     .constData(),
                 errorOut ) )
        {
            close();
            return false;
        }
    }
    else if ( existing != QLatin1String( kDatasetStoreSchemaVersion ) )
    {
        // Forward tolerance: newer schema → read-only (writes fail).
        m_impl->readOnly = true;
    }
    return true;
}

void DatasetStore::close()
{
    if ( !m_impl )
        return;
    sqlite3_close( m_impl->db );
    delete m_impl;
    m_impl = nullptr;
    m_storePath.clear();
}

bool DatasetStore::isReadOnly() const
{
    return !m_impl || m_impl->readOnly;
}

QString DatasetStore::schemaVersion() const
{
    return m_impl ? m_impl->schemaVersion() : QString();
}

bool DatasetStore::checkpointForBackup()
{
    if ( !m_impl )
        return false;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "PRAGMA wal_checkpoint(TRUNCATE)" ) );
    // busy != 0 means the checkpoint did not complete — the file is not yet
    // safely copyable.
    return stmt && stmt.stepRow() && stmt.i64( 0 ) == 0;
}

// --- datasets ----------------------------------------------------------------

sicnu::data::Result<QString> DatasetStore::createDataset( const DatasetId &datasetId,
                                                          const QString &name,
                                                          const QString &description )
{
    using Result = sicnu::data::Result<QString>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    if ( datasetId.isNull() || name.isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.invalid_argument" ),
                                           QStringLiteral( "dataset id and name are required" ) ) );

    const QString id = datasetId.toString();
    {
        Stmt exists( m_impl->db, QStringLiteral( "SELECT 1 FROM datasets WHERE id=?" ) );
        if ( !exists )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               exists.error( m_impl->db ) ) );
        exists.bind( 1, id );
        if ( exists.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                               QStringLiteral( "dataset %1 already exists" ).arg( id ) ) );
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO datasets(id, name, description, created_ms) VALUES(?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, id );
    insert.bind( 2, name );
    insert.bind( 3, description );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success( id );
}

std::optional<QVariantMap> DatasetStore::datasetById( const DatasetId &datasetId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT id, name, description, created_ms FROM datasets WHERE id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, datasetId.toString() );
    if ( !stmt.stepRow() )
        return std::nullopt;
    QVariantMap row;
    row.insert( QStringLiteral( "id" ), stmt.text( 0 ) );
    row.insert( QStringLiteral( "name" ), stmt.text( 1 ) );
    row.insert( QStringLiteral( "description" ), stmt.text( 2 ) );
    row.insert( QStringLiteral( "created_ms" ), stmt.i64( 3 ) );
    return row;
}

sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>> DatasetStore::listDatasets(
    qint64 offset, qint64 limit ) const
{
    using Result = sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );

    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM datasets" ) );
        if ( count && count.stepRow() )
            total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT id, name, description, created_ms FROM datasets ORDER BY name, id"
        " LIMIT ? OFFSET ?" ) );
    if ( !stmt )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           stmt.error( m_impl->db ) ) );
    stmt.bind( 1, limit );
    stmt.bind( 2, offset );
    QVector<QVariantMap> rows;
    while ( stmt.stepRow() )
    {
        QVariantMap row;
        row.insert( QStringLiteral( "id" ), stmt.text( 0 ) );
        row.insert( QStringLiteral( "name" ), stmt.text( 1 ) );
        row.insert( QStringLiteral( "description" ), stmt.text( 2 ) );
        row.insert( QStringLiteral( "created_ms" ), stmt.i64( 3 ) );
        rows.append( row );
    }
    return Result::success( qMakePair( total, rows ) );
}

qint64 DatasetStore::datasetCount() const
{
    if ( !m_impl )
        return 0;
    QMutexLocker lock( &m_impl->mutex );
    Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM datasets" ) );
    if ( count && count.stepRow() )
        return count.i64( 0 );
    return 0;
}

sicnu::data::Result<void> DatasetStore::deleteDataset( const DatasetId &datasetId )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    {
        Stmt exists( m_impl->db, QStringLiteral( "SELECT 1 FROM datasets WHERE id=?" ) );
        if ( !exists )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               exists.error( m_impl->db ) ) );
        exists.bind( 1, datasetId.toString() );
        if ( !exists.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                               QStringLiteral( "dataset %1 does not exist" )
                                                   .arg( datasetId.toString() ) ) );
    }
    // A dataset with non-draft versions is never row-deleted: references from
    // runs/lineage outlive the caller's intention. Deprecate instead.
    {
        Stmt committed( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM dataset_versions WHERE dataset_id=? AND status!='draft'" ) );
        if ( !committed )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               committed.error( m_impl->db ) ) );
        committed.bind( 1, datasetId.toString() );
        if ( committed.stepRow() && committed.i64( 0 ) > 0 )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.delete_refused" ),
                QStringLiteral( "dataset %1 has non-draft versions; deprecate instead" )
                    .arg( datasetId.toString() ) ) );
    }

    if ( !m_impl->begin( nullptr ) )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "cannot begin transaction" ) ) );
    {
        // Draft rows of this dataset may carry samples and annotations; a
        // header-only delete would strand them forever (no FKs by design).
        Stmt samples( m_impl->db, QStringLiteral(
            "DELETE FROM samples WHERE dataset_version_id IN"
            " (SELECT id FROM dataset_versions WHERE dataset_id=?)" ) );
        Stmt annotations( m_impl->db, QStringLiteral(
            "DELETE FROM annotations WHERE dataset_version_id IN"
            " (SELECT id FROM dataset_versions WHERE dataset_id=?)" ) );
        Stmt versions( m_impl->db, QStringLiteral(
            "DELETE FROM dataset_versions WHERE dataset_id=?" ) );
        Stmt header( m_impl->db, QStringLiteral( "DELETE FROM datasets WHERE id=?" ) );
        if ( !samples || !annotations || !versions || !header )
        {
            m_impl->rollback();
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                               QStringLiteral( "statement prepare failed" ) ) );
        }
        samples.bind( 1, datasetId.toString() );
        annotations.bind( 1, datasetId.toString() );
        versions.bind( 1, datasetId.toString() );
        header.bind( 1, datasetId.toString() );
        if ( !samples.step() || !annotations.step() || !versions.step() || !header.step() )
        {
            m_impl->rollback();
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                               header.error( m_impl->db ) ) );
        }
    }
    // #774: the deletion only exists once the transaction commits — returning
    // success with the transaction still open leaked the SQLite write lock
    // and stranded every later writer.
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "cannot commit dataset deletion" ) ) );
    }
    return Result::success();
}

// --- versions ------------------------------------------------------------------

sicnu::data::Result<DatasetVersionRecord> DatasetStore::createDraftVersion(
    const DatasetManifest &manifest, const QString &note )
{
    using Result = sicnu::data::Result<DatasetVersionRecord>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    QString datasetId;
    QString versionId;
    QString parentId;
    if ( !canonicalId( manifest.datasetId(), &datasetId ) ||
         !canonicalId( manifest.versionId(), &versionId ) )
        return Result::failure( storeDiag( QStringLiteral( "dataset.manifest_invalid" ),
                                           QStringLiteral( "manifest requires valid ids" ) ) );
    const bool hasParent = canonicalId( manifest.parentVersionId(), &parentId );

    {
        Stmt exists( m_impl->db, QStringLiteral( "SELECT 1 FROM datasets WHERE id=?" ) );
        if ( !exists )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               exists.error( m_impl->db ) ) );
        exists.bind( 1, datasetId );
        if ( !exists.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                               QStringLiteral( "dataset %1 does not exist" ).arg( datasetId ) ) );
    }
    {
        Stmt duplicate( m_impl->db, QStringLiteral( "SELECT 1 FROM dataset_versions WHERE id=?" ) );
        if ( !duplicate )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               duplicate.error( m_impl->db ) ) );
        duplicate.bind( 1, versionId );
        if ( duplicate.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                               QStringLiteral( "version %1 already exists" ).arg( versionId ) ) );
    }
    if ( hasParent )
    {
        // M1 lineage contract: a dangling or cross-dataset parent link would
        // fork the DAG silently. The parent must exist inside THIS dataset,
        // and its own ancestry must terminate within the depth bound — a
        // loop in a corrupt store is refused at write time, never discovered
        // at query time.
        QString parentDatasetId;
        {
            Stmt parent( m_impl->db, QStringLiteral(
                "SELECT dataset_id FROM dataset_versions WHERE id=?" ) );
            if ( !parent )
                return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                                   parent.error( m_impl->db ) ) );
            parent.bind( 1, parentId );
            if ( !parent.stepRow() )
                return Result::failure( storeDiag(
                    QStringLiteral( "dataset.parent_not_found" ),
                    QStringLiteral( "parent version %1 does not exist" ).arg( parentId ) ) );
            parentDatasetId = parent.text( 0 );
        }
        if ( parentDatasetId != datasetId )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.parent_dataset_mismatch" ),
                QStringLiteral( "parent version %1 belongs to dataset %2, not %3" )
                    .arg( parentId, parentDatasetId, datasetId ) ) );
        QString cursor = parentId;
        QSet<QString> visited;
        while ( !cursor.isEmpty() )
        {
            if ( visited.contains( cursor ) || visited.size() > kMaxVersionLineageDepth )
                return Result::failure( storeDiag(
                    QStringLiteral( "dataset.version_cycle" ),
                    QStringLiteral( "parent lineage of %1 loops or exceeds %2 versions" )
                        .arg( parentId )
                        .arg( kMaxVersionLineageDepth ) ) );
            visited.insert( cursor );
            Stmt up( m_impl->db, QStringLiteral(
                "SELECT parent_version_id FROM dataset_versions WHERE id=?" ) );
            if ( !up )
                return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                                   up.error( m_impl->db ) ) );
            up.bind( 1, cursor );
            cursor = up.stepRow() ? up.text( 0 ) : QString();
        }
    }

    DatasetVersionRecord record;
    record.setVersionId( versionId );
    record.setDatasetId( datasetId );
    record.setParentVersionId( hasParent ? parentId : QString() );
    record.setStatus( DatasetVersionStatus::Draft );
    record.setQualityLevel( DatasetQualityLevel::Unassessed );
    record.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    record.setNote( note );
    record.setManifestJson( jsonToText( manifest.toJson() ) );

    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO dataset_versions(id, dataset_id, parent_version_id, status,"
        " quality_level, note, created_ms, manifest_json, fingerprint, staged)"
        " VALUES(?,?,?,?,?,?,?,?,?,0)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, record.versionId() );
    insert.bind( 2, record.datasetId() );
    insert.bind( 3, record.parentVersionId() );
    insert.bind( 4, datasetVersionStatusToString( record.status() ) );
    insert.bind( 5, datasetQualityLevelToString( record.qualityLevel() ) );
    insert.bind( 6, record.note() );
    insert.bind( 7, record.createdAtUtc().toMSecsSinceEpoch() );
    insert.bind( 8, record.manifestJson() );
    insert.bind( 9, QString() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success( record );
}

sicnu::data::Result<DatasetVersionRecord> DatasetStore::stageVersion(
    const DatasetVersionId &versionId )
{
    using Result = sicnu::data::Result<DatasetVersionRecord>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    std::optional<DatasetVersionRecord> current;
    {
        Stmt stmt( m_impl->db, QStringLiteral(
            "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE id=?" ) );
        if ( !stmt )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               stmt.error( m_impl->db ) ) );
        stmt.bind( 1, versionId.toString() );
        if ( stmt.stepRow() )
            current = recordFromRow( stmt );
    }
    if ( !current )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( versionId.toString() ) ) );
    if ( !current->isMutable() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_draft" ),
                                           QStringLiteral( "only draft versions can be staged" ) ) );

    // Validate via a full parse round-trip: the staged document must load
    // under exactly the reader contract consumers will use.
    const QJsonObject stored = textToJson( current->manifestJson() );
    const auto parsed = DatasetManifest::fromJson( stored );
    if ( !parsed )
        return Result::failure( parsed.diagnostics() );

    // Canonical staged document: fingerprint excluded (stamping happens at
    // commit; a staged draft makes no content promise).
    DatasetManifest staged = parsed.value();
    staged.setFingerprint( QString() );
    const QString stagedText = jsonToText( staged.toJson() );

    Stmt update( m_impl->db, QStringLiteral(
        "UPDATE dataset_versions SET manifest_json=?, staged=1"
        " WHERE id=? AND status='draft'" ) );
    if ( !update )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           update.error( m_impl->db ) ) );
    update.bind( 1, stagedText );
    update.bind( 2, current->versionId() );
    if ( !update.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           update.error( m_impl->db ) ) );
    if ( update.changes( m_impl->db ) != 1 )
        return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                           QStringLiteral( "version left draft state during staging" ) ) );

    auto record = *current;
    record.setManifestJson( stagedText );
    return Result::success( record );
}

sicnu::data::Result<DatasetVersionRecord> DatasetStore::commitVersion(
    const DatasetVersionId &versionId )
{
    // Unified-trace adapter (Verification Platform 8.0): the Dataset link of
    // the chain. One record per commit with the version id as the artifact
    // identity. Disabled path = one relaxed atomic load.
    if ( !sicnu::runtime::observability::trace::Trace::enabled() )
        return commitVersionImpl( versionId );
    const auto started = std::chrono::steady_clock::now();
    const auto result = commitVersionImpl( versionId );
    sicnu::runtime::observability::trace::TraceEvent trace;
    trace.event = "dataset_commit";
    trace.phase = "end";
    trace.artifact = versionId.toString().toStdString();
    trace.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started ).count();
    if ( result )
        trace.status = "ok";
    else
        trace.status = "error";
    sicnu::runtime::observability::trace::Trace::publish( trace );
    return result;
}

sicnu::data::Result<DatasetVersionRecord> DatasetStore::commitVersionImpl(
    const DatasetVersionId &versionId )
{
    using Result = sicnu::data::Result<DatasetVersionRecord>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    std::optional<DatasetVersionRecord> current;
    {
        Stmt stmt( m_impl->db, QStringLiteral(
            "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE id=?" ) );
        if ( !stmt )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               stmt.error( m_impl->db ) ) );
        stmt.bind( 1, versionId.toString() );
        if ( stmt.stepRow() )
            current = recordFromRow( stmt );
    }
    if ( !current )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( versionId.toString() ) ) );
    if ( current->status() != DatasetVersionStatus::Draft )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_draft" ),
                                           QStringLiteral( "only draft versions can be committed" ) ) );
    // Commit REQUIRES a staged draft: staging is where the manifest is
    // validated under exactly the reader contract consumers use. Committing
    // an unstaged (never-validated) document would freeze unreadable content
    // into an immutable version - a fake success.
    {
        Stmt stagedCheck( m_impl->db, QStringLiteral(
            "SELECT 1 FROM dataset_versions WHERE id=? AND staged=1 AND status='draft'" ) );
        if ( !stagedCheck )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               stagedCheck.error( m_impl->db ) ) );
        stagedCheck.bind( 1, current->versionId() );
        if ( !stagedCheck.stepRow() )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.not_staged" ),
                QStringLiteral( "version %1 must be staged (validated) before commit" )
                    .arg( current->versionId() ) ) );
    }

    // Second gate, defense-in-depth: the staged document must still parse
    // under the strict reader (a foreign writer could have touched the row).
    const QJsonObject stagedManifest = textToJson( current->manifestJson() );
    if ( stagedManifest.isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.manifest_invalid" ),
                                           QStringLiteral( "draft manifest is not staged JSON" ) ) );
    const auto validated = DatasetManifest::fromJson( stagedManifest );
    if ( !validated )
        return Result::failure( validated.diagnostics() );

    // Fingerprint covers the CANONICAL re-serialization of the validated
    // manifest (without the fingerprint field): the committed document is
    // byte-identical to what was validated.
    DatasetManifest canonical = validated.value();
    canonical.setFingerprint( QString() );
    const QJsonObject canonicalJson = canonical.toJson();
    const QString canonicalText = jsonToText( canonicalJson );
    const QString fingerprint = makeDatasetFingerprint( canonicalJson ).toHex();
    QJsonObject committedJson = canonicalJson;
    committedJson.insert( QStringLiteral( "fingerprint" ), fingerprint );
    const QString committedText = jsonToText( committedJson );

    if ( !m_impl->begin( nullptr ) )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "cannot begin transaction" ) ) );
    {
        Stmt update( m_impl->db, QStringLiteral(
            "UPDATE dataset_versions SET manifest_json=?, fingerprint=?,"
            " status='committed', staged=0, committed_ms=? WHERE id=? AND status='draft'" ) );
        if ( !update )
        {
            m_impl->rollback();
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                               update.error( m_impl->db ) ) );
        }
        update.bind( 1, committedText );
        update.bind( 2, fingerprint );
        update.bind( 3, QDateTime::currentMSecsSinceEpoch() );
        update.bind( 4, current->versionId() );
        if ( !update.step() || update.changes( m_impl->db ) != 1 )
        {
            m_impl->rollback();
            return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                               QStringLiteral( "version left draft state during commit" ) ) );
        }
    }
    if ( SICNU_FAULT_POINT( "dataset_store.commit" ) )
    {
        // Injected commit failure (Verification Platform 8.0 fault matrix,
        // test-only arming): take exactly the real commit-failure branch and
        // roll the transaction back so the store stays consistent.
        m_impl->rollback();
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "commit transaction failed" ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        // A failed COMMIT can leave the transaction active (e.g. SQLITE_BUSY):
        // roll back explicitly so the shared connection never leaks its write
        // lock (same convention as deleteDataset, #774).
        m_impl->rollback();
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "commit transaction failed" ) ) );
    }

    auto record = *current;
    record.setStatus( DatasetVersionStatus::Committed );
    record.setCommittedAtUtc( QDateTime::currentDateTimeUtc() );
    record.setManifestJson( committedText );
    record.setFingerprint( fingerprint );
    return Result::success( record );
}

sicnu::data::Result<DatasetVersionRecord> DatasetStore::deprecateVersion(
    const DatasetVersionId &versionId )
{
    using Result = sicnu::data::Result<DatasetVersionRecord>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    std::optional<DatasetVersionRecord> current;
    {
        Stmt stmt( m_impl->db, QStringLiteral(
            "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE id=?" ) );
        if ( !stmt )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               stmt.error( m_impl->db ) ) );
        stmt.bind( 1, versionId.toString() );
        if ( stmt.stepRow() )
            current = recordFromRow( stmt );
    }
    if ( !current )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( versionId.toString() ) ) );
    if ( current->status() != DatasetVersionStatus::Committed )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_committed" ),
                                           QStringLiteral( "only committed versions can be deprecated" ) ) );

    Stmt update( m_impl->db, QStringLiteral(
        "UPDATE dataset_versions SET status='deprecated' WHERE id=? AND status='committed'" ) );
    if ( !update )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           update.error( m_impl->db ) ) );
    update.bind( 1, current->versionId() );
    if ( !update.step() || update.changes( m_impl->db ) != 1 )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           update.error( m_impl->db ) ) );

    auto record = *current;
    record.setStatus( DatasetVersionStatus::Deprecated );
    return Result::success( record );
}

std::optional<DatasetVersionRecord> DatasetStore::versionById( const DatasetVersionId &versionId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, versionId.toString() );
    if ( !stmt.stepRow() )
        return std::nullopt;
    return recordFromRow( stmt );
}

QVector<DatasetVersionRecord> DatasetStore::versionsOfDataset( const DatasetId &datasetId ) const
{
    QVector<DatasetVersionRecord> records;
    if ( !m_impl )
        return records;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE dataset_id=?"
        " ORDER BY created_ms, id" ) );
    if ( !stmt )
        return records;
    stmt.bind( 1, datasetId.toString() );
    while ( stmt.stepRow() )
    {
        if ( auto record = recordFromRow( stmt ) )
            records.append( std::move( *record ) );
    }
    return records;
}

std::optional<DatasetVersionRecord> DatasetStore::latestCommittedVersion( const DatasetId &datasetId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_VERSION_COLS " FROM dataset_versions"
        " WHERE dataset_id=? AND status IN ('committed','deprecated')"
        " ORDER BY committed_ms DESC, id DESC LIMIT 1" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, datasetId.toString() );
    if ( !stmt.stepRow() )
        return std::nullopt;
    return recordFromRow( stmt );
}

QVector<DatasetVersionRecord> DatasetStore::staleStagedDrafts() const
{
    QVector<DatasetVersionRecord> records;
    if ( !m_impl )
        return records;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_VERSION_COLS " FROM dataset_versions"
        " WHERE staged=1 AND status='draft'" ) );
    if ( !stmt )
        return records;
    while ( stmt.stepRow() )
    {
        if ( auto record = recordFromRow( stmt ) )
            records.append( std::move( *record ) );
    }
    return records;
}

QVector<DatasetVersionRecord> DatasetStore::versionChildren( const DatasetVersionId &versionId,
                                                             qint64 limit ) const
{
    QVector<DatasetVersionRecord> records;
    if ( !m_impl )
        return records;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE parent_version_id=?"
        " ORDER BY created_ms, id LIMIT ?" ) );
    if ( !stmt )
        return records;
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, limit );
    while ( stmt.stepRow() )
    {
        if ( auto record = recordFromRow( stmt ) )
            records.append( std::move( *record ) );
    }
    return records;
}

sicnu::data::Result<QVector<DatasetVersionRecord>> DatasetStore::versionAncestors(
    const DatasetVersionId &versionId, qint64 maxDepth ) const
{
    using Result = sicnu::data::Result<QVector<DatasetVersionRecord>>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    if ( maxDepth < 1 )
        return Result::failure( storeDiag( QStringLiteral( "dataset.version_depth_invalid" ),
                                           QStringLiteral( "maxDepth must be >= 1" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    QVector<DatasetVersionRecord> chain;
    QSet<QString> visited;
    QString cursor = versionId.toString();
    while ( !cursor.isEmpty() )
    {
        if ( visited.contains( cursor ) || visited.size() > maxDepth )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.version_cycle" ),
                QStringLiteral( "lineage of %1 loops or exceeds %2 versions" )
                    .arg( versionId.toString() )
                    .arg( maxDepth ) ) );
        visited.insert( cursor );
        Stmt stmt( m_impl->db, QStringLiteral(
            "SELECT " DATASET_VERSION_COLS " FROM dataset_versions WHERE id=?" ) );
        if ( !stmt )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               stmt.error( m_impl->db ) ) );
        stmt.bind( 1, cursor );
        if ( !stmt.stepRow() )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.parent_not_found" ),
                chain.isEmpty()
                    ? QStringLiteral( "version %1 does not exist" ).arg( cursor )
                    : QStringLiteral( "ancestor %1 does not exist (dangling parent link)" )
                          .arg( cursor ) ) );
        const auto record = recordFromRow( stmt );
        if ( !record )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.version_row_corrupt" ),
                QStringLiteral( "version row %1 is corrupt" ).arg( cursor ) ) );
        cursor = record->parentVersionId();
        chain.append( std::move( *record ) );
    }
    return Result::success( chain );
}

sicnu::data::Result<DatasetVersionRecord> DatasetStore::createDerivedVersion(
    const DatasetVersionId &parentId, const QString &note )
{
    using Result = sicnu::data::Result<DatasetVersionRecord>;
    // The parent is read first without the write lock: committed (and
    // deprecated) manifests are immutable, so nothing can change between
    // the read and the createDraftVersion transaction below.
    const auto parent = versionById( parentId );
    if ( !parent )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( parentId.toString() ) ) );
    if ( parent->isMutable() )
        return Result::failure( storeDiag(
            QStringLiteral( "dataset.derive_requires_committed" ),
            QStringLiteral( "version %1 is still a draft; commit it before deriving" )
                .arg( parentId.toString() ) ) );

    const auto parsed = DatasetManifest::fromJson( textToJson( parent->manifestJson() ) );
    if ( !parsed )
        return Result::failure( storeDiag(
            QStringLiteral( "dataset.version_row_corrupt" ),
            QStringLiteral( "manifest of %1 does not parse: %2" )
                .arg( parentId.toString(),
                      parsed.diagnostics().isEmpty()
                          ? QStringLiteral( "unknown error" )
                          : parsed.diagnostics().first().message ) ) );

    DatasetManifest derived = parsed.value();
    derived.setVersionId( DatasetVersionId::generate().toString() );
    derived.setParentVersionId( parent->versionId() );
    derived.setFingerprint( QString() ); // stamped by the usual commit path
    return createDraftVersion( derived, note );
}

// --- lineage -----------------------------------------------------------------

sicnu::data::Result<void> DatasetStore::addLineageEdge( const QString &fromKind,
                                                        const QString &fromId,
                                                        const QString &edgeKind,
                                                        const QString &toKind,
                                                        const QString &toId )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    if ( fromKind.isEmpty() || fromId.isEmpty() || edgeKind.isEmpty() || toKind.isEmpty() ||
         toId.isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.invalid_argument" ),
                                           QStringLiteral( "lineage edges require kind+id on both ends" ) ) );

    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT OR IGNORE INTO dataset_lineage(" DATASET_LINEAGE_COLS ", created_ms)"
        " VALUES(?,?,?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, fromKind );
    insert.bind( 2, fromId );
    insert.bind( 3, edgeKind );
    insert.bind( 4, toKind );
    insert.bind( 5, toId );
    insert.bind( 6, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success();
}

namespace
{

QVector<DatasetStore::LineageEdge> queryEdgesLocked( sqlite3 *db, const QString &sql,
                                                     const QString &kind, const QString &id,
                                                     qint64 limit )
{
    QVector<DatasetStore::LineageEdge> edges;
    Stmt stmt( db, sql );
    if ( !stmt )
        return edges;
    stmt.bind( 1, kind );
    stmt.bind( 2, id );
    stmt.bind( 3, limit );
    while ( stmt.stepRow() )
    {
        DatasetStore::LineageEdge edge;
        edge.fromKind = stmt.text( 0 );
        edge.fromId = stmt.text( 1 );
        edge.edgeKind = stmt.text( 2 );
        edge.toKind = stmt.text( 3 );
        edge.toId = stmt.text( 4 );
        edges.append( edge );
    }
    return edges;
}

} // namespace

QVector<DatasetStore::LineageEdge> DatasetStore::outgoingEdges( const QString &kind,
                                                                const QString &id,
                                                                qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    return queryEdgesLocked(
        m_impl->db,
        QStringLiteral(
            "SELECT " DATASET_LINEAGE_COLS " FROM dataset_lineage"
            " WHERE from_kind=? AND from_id=? LIMIT ?" ),
        kind, id, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
}

QVector<DatasetStore::LineageEdge> DatasetStore::incomingEdges( const QString &kind,
                                                                const QString &id,
                                                                qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    return queryEdgesLocked(
        m_impl->db,
        QStringLiteral(
            "SELECT " DATASET_LINEAGE_COLS " FROM dataset_lineage"
            " WHERE to_kind=? AND to_id=? LIMIT ?" ),
        kind, id, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
}

QVector<DatasetStore::LineageEdge> DatasetStore::allLineageEdges( qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    QVector<LineageEdge> edges;
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT " DATASET_LINEAGE_COLS " FROM dataset_lineage LIMIT ?" ) );
    if ( !stmt )
        return edges;
    stmt.bind( 1, qBound<qint64>( qint64( 1 ), limit, qint64( 1000000 ) ) );
    while ( stmt.stepRow() )
    {
        LineageEdge edge;
        edge.fromKind = stmt.text( 0 );
        edge.fromId = stmt.text( 1 );
        edge.edgeKind = stmt.text( 2 );
        edge.toKind = stmt.text( 3 );
        edge.toId = stmt.text( 4 );
        edges.append( edge );
    }
    return edges;
}

qint64 DatasetStore::versionCount() const
{
    if ( !m_impl )
        return 0;
    QMutexLocker lock( &m_impl->mutex );
    Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM dataset_versions" ) );
    if ( count && count.stepRow() )
        return count.i64( 0 );
    return 0;
}

qint64 DatasetStore::lineageEdgeCount() const
{
    if ( !m_impl )
        return 0;
    QMutexLocker lock( &m_impl->mutex );
    Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM dataset_lineage" ) );
    if ( count && count.stepRow() )
        return count.i64( 0 );
    return 0;
}

} // namespace sicnu::dataset
