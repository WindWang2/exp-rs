// dataset_store_splits.cpp — split manifest + leakage report persistence for
// the dataset store (goal §18/§19). Split from dataset_store.cpp to keep each
// concern readable; shares the private Impl/StoreStmt plumbing.
//
// Mutability contract: a split manifest id is written once — re-saving the
// same id with different content is a `dataset.conflict` (ids are never
// re-pointed). Leakage reports are an append-only audit history keyed by
// content digest; identical content re-runs are idempotent. Split manifests
// may attach to any version status (they are derived evidence ABOUT a
// version, not mutations of it).
#include "dataset_store_impl.h"
#include <QJsonArray>

#include "leakage_audit.h"
#include "split.h"

#include <QCryptographicHash>
#include <QDateTime>

namespace sicnu::dataset
{

namespace
{

QString contentDigest( const QString &canonicalJson )
{
    return QString::fromLatin1(
        QCryptographicHash::hash( canonicalJson.toUtf8(), QCryptographicHash::Sha256 ).toHex() );
}

} // namespace

// --- split manifests ---------------------------------------------------------

sicnu::data::Result<void> DatasetStore::saveSplitManifest( const SplitManifest &manifest )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    if ( manifest.manifestId().isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.split_invalid" ),
                                           QStringLiteral( "manifest id is empty" ) ) );
    if ( manifest.datasetVersionId().isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.split_invalid" ),
                                           QStringLiteral( "manifest carries no dataset version" ) ) );
    {
        // Dangling manifests are refused: a stored split always resolves to
        // a version (derived evidence about nothing is not evidence).
        StoreStmt version( m_impl->db, QStringLiteral(
            "SELECT 1 FROM dataset_versions WHERE id=?" ) );
        if ( !version )
            return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                               version.error( m_impl->db ) ) );
        version.bind( 1, manifest.datasetVersionId() );
        if ( !version.stepRow() )
            return Result::failure( storeDiag( QStringLiteral( "dataset.not_found" ),
                                               QStringLiteral( "version %1 does not exist" )
                                                   .arg( manifest.datasetVersionId() ) ) );
    }

    const QString fingerprint = splitManifestFingerprint( manifest );
    const QString json = jsonToText( manifest.toJson() );

    StoreStmt existing( m_impl->db, QStringLiteral(
        "SELECT fingerprint FROM split_manifests WHERE manifest_id=?" ) );
    if ( !existing )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           existing.error( m_impl->db ) ) );
    existing.bind( 1, manifest.manifestId() );
    if ( existing.stepRow() )
    {
        if ( existing.text( 0 ) != fingerprint )
            return Result::failure( storeDiag(
                QStringLiteral( "dataset.conflict" ),
                QStringLiteral( "split manifest %1 already exists with different content" )
                    .arg( manifest.manifestId() ) ) );
        return Result::success(); // idempotent re-save of identical content
    }

    StoreStmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO split_manifests(manifest_id, dataset_version_id, fingerprint,"
        " json, created_ms) VALUES(?,?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, manifest.manifestId() );
    insert.bind( 2, manifest.datasetVersionId() );
    insert.bind( 3, fingerprint );
    insert.bind( 4, json );
    insert.bind( 5, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success();
}

std::optional<SplitManifest> DatasetStore::splitManifestById( const QString &manifestId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM split_manifests WHERE manifest_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, manifestId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto manifest = SplitManifest::fromJson( textToJson( stmt.text( 0 ) ) );
    if ( !manifest )
        return std::nullopt; // corrupt row reads as absent — fail-conservative
    return manifest.value();
}

QVector<SplitManifest> DatasetStore::splitManifestsForVersion(
    const DatasetVersionId &versionId ) const
{
    QVector<SplitManifest> manifests;
    if ( !m_impl )
        return manifests;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM split_manifests WHERE dataset_version_id=?"
        " ORDER BY created_ms, manifest_id LIMIT ?" ) );
    if ( !stmt )
        return manifests;
    stmt.bind( 1, versionId.toString() );
    stmt.bind( 2, qint64( 10000 ) );
    while ( stmt.stepRow() )
    {
        const auto manifest = SplitManifest::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( manifest )
            manifests.append( manifest.value() );
    }
    return manifests;
}

// --- leakage reports -----------------------------------------------------------

sicnu::data::Result<void> DatasetStore::saveLeakageReport( const LeakageReport &report )
{
    using Result = sicnu::data::Result<void>;
    if ( !m_impl )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_read_only" ),
                                           QStringLiteral( "store is read-only" ) ) );
    if ( report.splitManifestId().isEmpty() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.leakage_report_invalid" ),
                                           QStringLiteral( "report carries no split manifest id" ) ) );

    StoreStmt split( m_impl->db, QStringLiteral(
        "SELECT 1 FROM split_manifests WHERE manifest_id=?" ) );
    if ( !split )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           split.error( m_impl->db ) ) );
    split.bind( 1, report.splitManifestId() );
    if ( !split.stepRow() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.split_not_found" ),
                                           QStringLiteral( "split manifest %1 is not stored" )
                                               .arg( report.splitManifestId() ) ) );

    const QString json = jsonToText( report.toJson() );
    const QString digest = contentDigest( json );

    StoreStmt insert( m_impl->db, QStringLiteral(
        "INSERT OR IGNORE INTO leakage_reports(split_manifest_id, report_digest,"
        " json, created_ms) VALUES(?,?,?,?)" ) );
    if ( !insert )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_query_failed" ),
                                           insert.error( m_impl->db ) ) );
    insert.bind( 1, report.splitManifestId() );
    insert.bind( 2, digest );
    insert.bind( 3, json );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return Result::failure( storeDiag( QStringLiteral( "dataset.store_write_failed" ),
                                           insert.error( m_impl->db ) ) );
    return Result::success();
}

std::optional<LeakageReport> DatasetStore::latestLeakageReport(
    const QString &splitManifestId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM leakage_reports WHERE split_manifest_id=?"
        " ORDER BY created_ms DESC, rowid DESC LIMIT 1" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, splitManifestId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto report = LeakageReport::fromJson( textToJson( stmt.text( 0 ) ) );
    if ( !report )
        return std::nullopt;
    return report.value();
}

QVector<LeakageReport> DatasetStore::leakageReportsForSplit( const QString &splitManifestId,
                                                             qint64 limit ) const
{
    QVector<LeakageReport> reports;
    if ( !m_impl )
        return reports;
    QMutexLocker lock( &m_impl->mutex );
    StoreStmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM leakage_reports WHERE split_manifest_id=?"
        " ORDER BY created_ms, rowid LIMIT ?" ) );
    if ( !stmt )
        return reports;
    stmt.bind( 1, splitManifestId );
    stmt.bind( 2, qBound<qint64>( qint64( 1 ), limit, qint64( 1000 ) ) );
    while ( stmt.stepRow() )
    {
        const auto report = LeakageReport::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( report )
            reports.append( report.value() );
    }
    return reports;
}

} // namespace sicnu::dataset
