// dataset_store_facets.cpp — SQL-side facet distribution + quality-summary
// cache for the dataset store (goal 7.0 §E). Shares the private Impl/StoreStmt
// plumbing.
//
// Scale contract: facet questions are answered by GROUP BY queries whose
// RESULT is bounded (maxValues/maxCells); the store never walks sample rows
// into C++ memory to count buckets. Facet writes attach to DRAFT versions
// only — facets describe content and committed versions are frozen.
#include "dataset_store_impl.h"
#include <QJsonArray>

#include <QDateTime>

namespace sicnu::dataset
{

namespace
{

Diagnostic facetDiag( QString code, QString message )
{
    return storeDiag( std::move( code ), std::move( message ) );
}

Diagnostic notDraft( const QString &versionId )
{
    return facetDiag( QStringLiteral( "dataset.not_draft" ),
                      QStringLiteral( "version %1 is not a draft; sample content and facets"
                                      " are frozen once committed" )
                          .arg( versionId ) );
}

} // namespace

sicnu::data::Result<void> DatasetStore::setSampleFacets( const DatasetVersionId &versionId,
                                                         const SampleId &sampleId,
                                                         const QVector<FacetEntry> &entries )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );

    for ( const auto &entry : entries )
    {
        if ( entry.first.isEmpty() )
            return Result::failure( facetDiag( QStringLiteral( "dataset.facet_invalid" ),
                                               QStringLiteral( "facet name is empty" ) ) );
    }

    // Replace is atomic per sample: wipe then insert inside one transaction.
    // (#811 playbook: the draft/sample checks run INSIDE the BEGIN IMMEDIATE
    // transaction so a concurrent commit cannot slip between check and write.)
    if ( !m_impl->begin( nullptr ) )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_transaction" ),
                                           QStringLiteral( "cannot begin facet transaction" ) ) );
    StoreStmt status( m_impl->db, QStringLiteral( "SELECT status FROM dataset_versions WHERE id=?" ) );
    if ( !status )
    {
        m_impl->rollback();
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           status.error( m_impl->db ) ) );
    }
    status.bind( 1, versionId.toString() );
    if ( !status.stepRow() )
    {
        m_impl->rollback();
        return Result::failure( facetDiag( QStringLiteral( "dataset.not_found" ),
                                           QStringLiteral( "version %1 does not exist" )
                                               .arg( versionId.toString() ) ) );
    }
    const auto versionStatus = datasetVersionStatusFromString( status.text( 0 ) );
    if ( !versionStatus || *versionStatus != DatasetVersionStatus::Draft )
    {
        m_impl->rollback();
        return Result::failure( notDraft( versionId.toString() ) );
    }
    {
        // The facet target must be a real sample of the version: orphan rows
        // would silently count in facet distributions.
        StoreStmt sample( m_impl->db, QStringLiteral(
            "SELECT 1 FROM samples WHERE dataset_version_id=? AND sample_id=?" ) );
        if ( !sample )
        {
            m_impl->rollback();
            return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               sample.error( m_impl->db ) ) );
        }
        sample.bind( 1, versionId.toString() );
        sample.bind( 2, sampleId.toString() );
        if ( !sample.stepRow() )
        {
            m_impl->rollback();
            return Result::failure( facetDiag( QStringLiteral( "dataset.sample_not_found" ),
                                               QStringLiteral( "sample %1 is not in version %2" )
                                                   .arg( sampleId.toString(), versionId.toString() ) ) );
        }
    }
    {
        StoreStmt wipe( m_impl->db, QStringLiteral(
            "DELETE FROM sample_facets WHERE dataset_version_id=? AND sample_id=?" ) );
        if ( !wipe )
        {
            m_impl->rollback();
            return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               wipe.error( m_impl->db ) ) );
        }
        wipe.bind( 1, versionId.toString() );
        wipe.bind( 2, sampleId.toString() );
        if ( !wipe.step() )
        {
            m_impl->rollback();
            return Result::failure( facetDiag( QStringLiteral( "dataset.store_write_failed" ),
                                               wipe.error( m_impl->db ) ) );
        }
        for ( const auto &entry : entries )
        {
            StoreStmt insert( m_impl->db, QStringLiteral(
                "INSERT OR IGNORE INTO sample_facets(dataset_version_id, sample_id,"
                " facet, value) VALUES(?,?,?,?)" ) );
            if ( !insert )
            {
                m_impl->rollback();
                return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                                   insert.error( m_impl->db ) ) );
            }
            insert.bind( 1, versionId.toString() );
            insert.bind( 2, sampleId.toString() );
            insert.bind( 3, entry.first );
            insert.bind( 4, entry.second );
            if ( !insert.step() )
            {
                m_impl->rollback();
                return Result::failure( facetDiag( QStringLiteral( "dataset.store_write_failed" ),
                                                   insert.error( m_impl->db ) ) );
            }
        }
    }
    if ( !m_impl->commit( nullptr ) )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_transaction" ),
                                           QStringLiteral( "facet transaction failed to commit" ) ) );
    return Result::success();
}

sicnu::data::Result<DatasetStore::FacetDistribution> DatasetStore::facetDistribution(
    const DatasetVersionId &versionId, const QString &facet, int maxValues ) const
{
    using Result = sicnu::data::Result<FacetDistribution>;
    if ( !m_impl )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    FacetDistribution distribution;
    distribution.facet = facet;

    StoreStmt total( m_impl->db, QStringLiteral(
        "SELECT COUNT(*) FROM sample_facets WHERE dataset_version_id=? AND facet=?" ) );
    if ( !total )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           total.error( m_impl->db ) ) );
    total.bind( 1, versionId.toString() );
    total.bind( 2, facet );
    if ( !total.stepRow() )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           total.error( m_impl->db ) ) );
    distribution.total = total.i64( 0 );

    // The bounded tail collapses into a "(other)" bucket so the sum of the
    // reported values plus the bucket equals total (nothing hidden).
    StoreStmt values( m_impl->db, QStringLiteral(
        "SELECT value, COUNT(*) AS n FROM sample_facets"
        " WHERE dataset_version_id=? AND facet=?"
        " GROUP BY value ORDER BY n DESC, value ASC LIMIT ?" ) );
    if ( !values )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           values.error( m_impl->db ) ) );
    values.bind( 1, versionId.toString() );
    values.bind( 2, facet );
    values.bind( 3, qMax( 1, maxValues ) );
    qint64 reported = 0;
    while ( values.stepRow() )
    {
        distribution.values.append( qMakePair( values.text( 0 ), values.i64( 1 ) ) );
        reported += values.i64( 1 );
    }
    if ( reported < distribution.total )
        distribution.values.append(
            qMakePair( QStringLiteral( "(other)" ), distribution.total - reported ) );
    return Result::success( distribution );
}

sicnu::data::Result<QVector<QPair<QString, qint64>>> DatasetStore::facetCrossCounts(
    const DatasetVersionId &versionId, const QString &facetA, const QString &facetB,
    int maxCells ) const
{
    using Result = sicnu::data::Result<QVector<QPair<QString, qint64>>>;
    if ( !m_impl )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    // Join the facet table with itself on sample identity.
    StoreStmt cells( m_impl->db, QStringLiteral(
        "SELECT a.value, b.value, COUNT(*) AS n"
        " FROM sample_facets a JOIN sample_facets b"
        " ON a.dataset_version_id=b.dataset_version_id"
        " AND a.sample_id=b.sample_id"
        " WHERE a.dataset_version_id=? AND a.facet=? AND b.facet=?"
        " GROUP BY a.value, b.value ORDER BY n DESC, a.value ASC, b.value ASC LIMIT ?" ) );
    if ( !cells )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           cells.error( m_impl->db ) ) );
    cells.bind( 1, versionId.toString() );
    cells.bind( 2, facetA );
    cells.bind( 3, facetB );
    cells.bind( 4, qMax( 1, maxCells ) );
    QVector<QPair<QString, qint64>> out;
    while ( cells.stepRow() )
        out.append( qMakePair( cells.text( 0 ) + QChar( 0x001F ) + cells.text( 1 ),
                               cells.i64( 2 ) ) );
    return Result::success( out );
}

QVector<QString> DatasetStore::facetNames( const DatasetVersionId &versionId, qint64 limit ) const
{
    QVector<QString> names;
    if ( !m_impl )
        return names;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT DISTINCT facet FROM sample_facets WHERE dataset_version_id=?"
        " ORDER BY facet ASC LIMIT ?" ) );
    if ( !stmt )
        return names;
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, limit );
    while ( stmt.stepRow() )
        names.append( stmt.text( 0 ) );
    return names;
}

sicnu::data::Result<void> DatasetStore::saveQualitySummary( const DatasetVersionId &versionId,
                                                            qint64 sampleCount, qint64 maxRoword,
                                                            const QJsonObject &summary )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    {
        // The cache target must be a real version (cache-only content, but a
        // summary for a nonexistent version is a lie about coverage).
        StoreStmt status( m_impl->db, QStringLiteral(
            "SELECT 1 FROM dataset_versions WHERE id=?" ) );
        if ( !status )
            return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               status.error( m_impl->db ) ) );
        status.bind( 1, versionId.toString() );
        if ( !status.stepRow() )
            return Result::failure( facetDiag( QStringLiteral( "dataset.not_found" ),
                                               QStringLiteral( "version %1 does not exist" )
                                                   .arg( versionId.toString() ) ) );
    }
    StoreStmt upsert( m_impl->db, QStringLiteral(
        "INSERT OR REPLACE INTO quality_summaries(dataset_version_id, sample_count,"
        " max_roword, json, updated_ms) VALUES(?,?,?,?,?)" ) );
    if ( !upsert )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           upsert.error( m_impl->db ) ) );
    upsert.bind( 1, versionId.toString() );
    upsert.bind( 2, sampleCount );
    upsert.bind( 3, maxRoword );
    upsert.bind( 4, jsonToText( summary ) );
    upsert.bind( 5, QDateTime::currentMSecsSinceEpoch() );
    if ( !upsert.step() )
        return Result::failure( facetDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           upsert.error( m_impl->db ) ) );
    return Result::success();
}

std::optional<DatasetStore::QualitySummaryRecord> DatasetStore::qualitySummary(
    const DatasetVersionId &versionId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT sample_count, max_roword, json, updated_ms FROM quality_summaries"
        " WHERE dataset_version_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, versionId.toString() );
    if ( !stmt.stepRow() )
        return std::nullopt;
    QualitySummaryRecord record;
    record.sampleCount = stmt.i64( 0 );
    record.maxRoword = stmt.i64( 1 );
    record.summary = textToJson( stmt.text( 2 ) );
    record.updatedAtUtc = QDateTime::fromMSecsSinceEpoch( stmt.i64( 3 ) );
    return record;
}

QPair<qint64, qint64> DatasetStore::sampleContentStamp( const DatasetVersionId &versionId ) const
{
    QPair<qint64, qint64> stamp{ 0, 0 };
    if ( !m_impl )
        return stamp;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT COUNT(*), COALESCE(MAX(roword),0) FROM samples"
        " WHERE dataset_version_id=?" ) );
    if ( !stmt )
        return stamp;
    stmt.bind( 1, versionId.toString() );
    if ( stmt.stepRow() )
        stamp = qMakePair( stmt.i64( 0 ), stmt.i64( 1 ) );
    return stamp;
}

} // namespace sicnu::dataset
