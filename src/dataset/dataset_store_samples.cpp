// dataset_store_samples.cpp — sample/annotation/label-schema persistence for
// the dataset store (ADR 0135). Split from dataset_store.cpp to keep each
// concern readable; shares the private Impl/StoreStmt plumbing.
//
// Mutability contract: samples and annotations attach to a dataset VERSION
// and may only change while that version is Draft. Committed/deprecated
// versions refuse writes with `dataset.not_draft` — the frozen-state rule
// that makes version references meaningful.
#include "dataset_store_impl.h"

#include "annotation.h"
#include "label_schema.h"
#include "sample.h"

#include <QCryptographicHash>

namespace sicnu::dataset
{

namespace
{

/// Loads the version status while the caller holds the mutex; nullopt when
/// the version row does not exist (or is corrupt — fail-conservative).
std::optional<DatasetVersionStatus> versionStatusLocked( sqlite3 *db,
                                                         const QString &versionId )
{
    StoreStmt stmt( db, QStringLiteral(
        "SELECT status FROM dataset_versions WHERE id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, versionId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    return datasetVersionStatusFromString( stmt.text( 0 ) );
}

Diagnostic notDraft( const QString &versionId )
{
    return storeDiag( QStringLiteral( "dataset.not_draft" ),
                      QStringLiteral( "version %1 is not a draft; samples and annotations"
                                      " are frozen once committed" )
                          .arg( versionId ) );
}

} // namespace

// --- label schemas --------------------------------------------------------------

sicnu::data::Result<void> DatasetStore::saveLabelSchema( const LabelSchema &schema )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    const auto validated = schema.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );

    const QString json = jsonToText( schema.toJson() );
    // Published vocabularies are immutable: identical re-save is idempotent,
    // a differing re-save of the same (id, version) is a conflict.
    {
        StoreStmt existing( m_impl->db, QStringLiteral(
            "SELECT json FROM label_schemas WHERE schema_id=? AND version=?" ) );
        if ( !existing )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               existing.error( m_impl->db ) ) );
        existing.bind( 1, schema.schemaId() );
        existing.bind( 2, qint64( schema.version() ) );
        if ( existing.stepRow() )
        {
            if ( existing.text( 0 ) == json )
                return Result::success();
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.conflict" ),
                QStringLiteral( "label schema %1@%2 already exists with different content" )
                    .arg( schema.schemaId() )
                    .arg( schema.version() ) ) );
        }
    }
    StoreStmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO label_schemas(schema_id, version, json, created_ms) VALUES(?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, schema.schemaId() );
    insert.bind( 2, qint64( schema.version() ) );
    insert.bind( 3, json );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success();
}

std::optional<LabelSchema> DatasetStore::labelSchema( const QString &schemaId,
                                                      quint64 version ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM label_schemas WHERE schema_id=? AND version=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, schemaId );
    stmt.bind( 2, qint64( version ) );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = LabelSchema::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<LabelSchema>( parsed.value() ) : std::nullopt;
}

QVector<QPair<quint64, QString>> DatasetStore::labelSchemaVersions( const QString &schemaId ) const
{
    QVector<QPair<quint64, QString>> versions;
    if ( !m_impl )
        return versions;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT version, json FROM label_schemas WHERE schema_id=? ORDER BY version" ) );
    if ( !stmt )
        return versions;
    stmt.bind( 1, schemaId );
    while ( stmt.stepRow() )
    {
        // Content digest of the stored document (change detection without a
        // dedicated column; the document is immutable per (id, version)).
        const QString json = stmt.text( 1 );
        const QString digest = QString::fromUtf8(
            QCryptographicHash::hash( json.toUtf8(), QCryptographicHash::Sha256 ).toHex() );
        versions.append( qMakePair( quint64( stmt.i64( 0 ) ), digest ) );
    }
    return versions;
}

// --- samples --------------------------------------------------------------------

sicnu::data::Result<void> DatasetStore::addSamples( const QVector<SampleRecord> &samples )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    if ( samples.isEmpty() )
        return Result::success();

    // All samples in one batch must target the same draft version.
    const QString versionId = samples.first().datasetVersionId();
    for ( const SampleRecord &sample : samples )
    {
        if ( sample.datasetVersionId() != versionId )
            return Result::failure( storeDiag( QStringLiteral( "dataset.invalid_argument" ),
                                               QStringLiteral( "batch mixes dataset versions" ) ) );
        const auto validated = validateSample( sample );
        if ( !validated )
            return Result::failure( validated.diagnostics() );
    }
    const auto status = versionStatusLocked( m_impl->db, versionId );
    if ( !status )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" ).arg( versionId ) ) );
    if ( *status != DatasetVersionStatus::Draft )
        return Result::failure( notDraft( versionId ) );

    if ( !m_impl->begin( nullptr ) )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "cannot begin transaction" ) ) );
    qint64 nextOrder = 0;
    {
        StoreStmt maxOrder( m_impl->db, QStringLiteral(
            "SELECT COALESCE(MAX(roword),0) FROM samples WHERE dataset_version_id=?" ) );
        if ( maxOrder )
        {
            maxOrder.bind( 1, versionId );
            if ( maxOrder.stepRow() )
                nextOrder = maxOrder.i64( 0 ) + 1;
        }
    }
    StoreStmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO samples(dataset_version_id, sample_id, kind, group_id, time_ms,"
        " weight, json, roword) VALUES(?,?,?,?,?,?,?,?)" ) );
    if ( !insert )
    {
        m_impl->rollback();
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    }
    for ( const SampleRecord &sample : samples )
    {
        insert.reset();
        insert.bind( 1, versionId );
        insert.bind( 2, sample.sampleId() );
        insert.bind( 3, sampleKindToString( sample.kind() ) );
        insert.bind( 4, sample.groupId() );
        insert.bind( 5, sample.timeUtc().isValid() ? sample.timeUtc().toMSecsSinceEpoch()
                                                : qint64( 0 ) );
        insert.bind( 6, sample.weight() );
        insert.bind( 7, jsonToText( sample.toJson() ) );
        insert.bind( 8, nextOrder++ );
        if ( !insert.step() )
        {
            m_impl->rollback();
            // The PRIMARY KEY(version, sample_id) makes duplicates loud: a
            // re-ingest must never silently overwrite a sample.
            return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                               QStringLiteral( "sample %1 rejected: %2" )
                                                   .arg( sample.sampleId(),
                                                         insert.error( m_impl->db ) ) ) );
        }
    }
    if ( !m_impl->commit( nullptr ) )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           QStringLiteral( "commit failed" ) ) );
    return Result::success();
}

std::optional<SampleRecord> DatasetStore::sampleById( const DatasetVersionId &versionId,
                                                      const SampleId &sampleId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM samples WHERE dataset_version_id=? AND sample_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, sampleId.toString() );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = SampleRecord::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<SampleRecord>( parsed.value() ) : std::nullopt;
}

sicnu::data::Result<QPair<qint64, QVector<SampleRecord>>> DatasetStore::samplesPage(
    const DatasetVersionId &versionId, qint64 offset, qint64 limit ) const
{
    using Result = sicnu::data::Result<QPair<qint64, QVector<SampleRecord>>>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );

    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        StoreStmt count( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM samples WHERE dataset_version_id=?" ) );
        if ( count )
        {
            count.bind( 1, versionId.toString() );
            if ( count.stepRow() )
                total = count.i64( 0 );
        }
    }
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM samples WHERE dataset_version_id=? ORDER BY roword LIMIT ? OFFSET ?" ) );
    if ( !stmt )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           stmt.error( m_impl->db ) ) );
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, limit );
    stmt.bind( 3, offset );
    QVector<SampleRecord> records;
    while ( stmt.stepRow() )
    {
        // A corrupt sample row fails the page loudly: primary data must not
        // silently shrink (fail-conservative, ADR 0130).
        auto parsed = SampleRecord::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( !parsed )
            return Result::failure( storeDiag( QStringLiteral( "dataset.corrupt_sample" ),
                                               parsed.diagnostics().first().message ) );
        records.append( parsed.value() );
    }
    return Result::success( qMakePair( total, records ) );
}

qint64 DatasetStore::sampleCount( const DatasetVersionId &versionId ) const
{
    if ( !m_impl )
        return 0;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt count( m_impl->db, QStringLiteral(
        "SELECT COUNT(*) FROM samples WHERE dataset_version_id=?" ) );
    if ( !count )
        return 0;
    count.bind( 1, versionId.toString() );
    if ( count.stepRow() )
        return count.i64( 0 );
    return 0;
}

QVector<QString> DatasetStore::sampleGroupIds( const DatasetVersionId &versionId,
                                               qint64 limit ) const
{
    QVector<QString> groups;
    if ( !m_impl )
        return groups;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT DISTINCT group_id FROM samples WHERE dataset_version_id=? AND group_id!=''"
        " ORDER BY group_id LIMIT ?" ) );
    if ( !stmt )
        return groups;
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, qBound<qint64>( qint64( 1 ), limit, 100000 ) );
    while ( stmt.stepRow() )
        groups.append( stmt.text( 0 ) );
    return groups;
}

sicnu::data::Result<void> DatasetStore::removeSample( const DatasetVersionId &versionId,
                                                      const SampleId &sampleId )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    const auto status = versionStatusLocked( m_impl->db, versionId.toString() );
    if ( !status )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( versionId.toString() ) ) );
    if ( *status != DatasetVersionStatus::Draft )
        return Result::failure( notDraft( versionId.toString() ) );

    StoreStmt stmt( m_impl->db, QStringLiteral(
        "DELETE FROM samples WHERE dataset_version_id=? AND sample_id=?" ) );
    if ( !stmt )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           stmt.error( m_impl->db ) ) );
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, sampleId.toString() );
    if ( !stmt.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           stmt.error( m_impl->db ) ) );
    if ( stmt.changes( m_impl->db ) != 1 )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "sample %1 not in version %2" )
                                               .arg( sampleId.toString(), versionId.toString() ) ) );
    return Result::success();
}

// --- annotations ----------------------------------------------------------------

sicnu::data::Result<void> DatasetStore::addAnnotation( const AnnotationRecord &annotation )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    const auto validated = validateAnnotation( annotation );
    if ( !validated )
        return Result::failure( validated.diagnostics() );

    const auto status = versionStatusLocked( m_impl->db, annotation.datasetVersionId() );
    if ( !status )
        return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( annotation.datasetVersionId() ) ) );
    if ( *status != DatasetVersionStatus::Draft )
        return Result::failure( notDraft( annotation.datasetVersionId() ) );

    if ( annotation.revision() == 1 )
    {
        // First revision must not collide with an existing chain head.
        StoreStmt existing( m_impl->db, QStringLiteral(
            "SELECT 1 FROM annotations WHERE annotation_id=? AND revision=1" ) );
        if ( !existing )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               existing.error( m_impl->db ) ) );
        existing.bind( 1, annotation.annotationId() );
        if ( existing.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.conflict" ),
                                               QStringLiteral( "annotation %1 already exists" )
                                                   .arg( annotation.annotationId() ) ) );
    }
    else
    {
        // Chain integrity: revision n must continue the stored n-1 record.
        StoreStmt previous( m_impl->db, QStringLiteral(
            "SELECT json FROM annotations WHERE annotation_id=? AND revision=?" ) );
        if ( !previous )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               previous.error( m_impl->db ) ) );
        previous.bind( 1, annotation.annotationId() );
        previous.bind( 2, annotation.parentRevision() );
        if ( !previous.stepRow() )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.annotation_chain_broken" ),
                QStringLiteral( "revision %1 claims parent %2 which is not stored" )
                    .arg( annotation.revision() )
                    .arg( annotation.parentRevision() ) ) );
        const auto parent = AnnotationRecord::fromJson( textToJson( previous.text( 0 ) ) );
        if ( !parent || !isContinuationOf( annotation, parent.value() ) )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.annotation_chain_broken" ),
                QStringLiteral( "revision %1 does not continue the stored chain" )
                    .arg( annotation.revision() ) ) );
    }

    StoreStmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO annotations(annotation_id, revision, target_sample_id,"
        " dataset_version_id, json, created_ms) VALUES(?,?,?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, annotation.annotationId() );
    insert.bind( 2, annotation.revision() );
    insert.bind( 3, annotation.targetSampleId() );
    insert.bind( 4, annotation.datasetVersionId() );
    insert.bind( 5, jsonToText( annotation.toJson() ) );
    insert.bind( 6, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success();
}

std::optional<AnnotationRecord> DatasetStore::annotationTip( const QString &annotationId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM annotations WHERE annotation_id=? ORDER BY revision DESC LIMIT 1" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, annotationId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = AnnotationRecord::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<AnnotationRecord>( parsed.value() ) : std::nullopt;
}

QVector<AnnotationRecord> DatasetStore::annotationHistory( const QString &annotationId ) const
{
    QVector<AnnotationRecord> history;
    if ( !m_impl )
        return history;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM annotations WHERE annotation_id=? ORDER BY revision" ) );
    if ( !stmt )
        return history;
    stmt.bind( 1, annotationId );
    while ( stmt.stepRow() )
    {
        auto record = AnnotationRecord::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( record )
            history.append( record.value() );
    }
    return history;
}

QVector<AnnotationRecord> DatasetStore::annotationsOfSample( const QString &sampleId,
                                                             qint64 limit ) const
{
    QVector<AnnotationRecord> tips;
    if ( !m_impl )
        return tips;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM annotations AS a WHERE target_sample_id=? AND revision ="
        " (SELECT MAX(revision) FROM annotations AS b WHERE b.annotation_id = a.annotation_id)"
        " ORDER BY created_ms, annotation_id LIMIT ?" ) );
    if ( !stmt )
        return tips;
    stmt.bind( 1, sampleId );
    stmt.bind( 2, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
    while ( stmt.stepRow() )
    {
        auto record = AnnotationRecord::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( record )
            tips.append( record.value() );
    }
    return tips;
}

} // namespace sicnu::dataset
