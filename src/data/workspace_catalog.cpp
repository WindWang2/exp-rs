// workspace_catalog.cpp — see workspace_catalog.h for the contract.
#include "workspace_catalog.h"

#include <QDateTime>
#include <QDir>

#include <sqlite3.h>

#include "runtime/observability/fault_point.h"

#include <algorithm>
#include <mutex>

namespace sicnu::data
{
namespace
{
#define SICNU_CATALOG_SCHEMA_VERSION "1"
#define SICNU_CATALOG_ASSET_COLS \
    "asset_id, source_key, canonical_source, kind, state, persistence," \
    " display_name, parent_collection_id, acquisition_ms, revision, metadata_json"

Diagnostic catalogDiag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

class Stmt
{
  public:
    Stmt( sqlite3 *db, const char *sql )
        : m_db( db )
    {
        if ( sqlite3_prepare_v2( db, sql, -1, &m_stmt, nullptr ) != SQLITE_OK )
            m_stmt = nullptr;
    }
    ~Stmt() { if ( m_stmt ) sqlite3_finalize( m_stmt ); }
    explicit operator bool() const { return m_stmt != nullptr; }
    sqlite3_stmt *get() const { return m_stmt; }
    bool step() const { return sqlite3_step( m_stmt ) == SQLITE_DONE; }
    bool stepRow() const { return sqlite3_step( m_stmt ) == SQLITE_ROW; }
    void reset() { sqlite3_reset( m_stmt ); sqlite3_clear_bindings( m_stmt ); }
    void bind( int idx, const QString &v ) const
    {
        sqlite3_bind_text( m_stmt, idx, v.toUtf8().constData(), -1, SQLITE_TRANSIENT );
    }
    void bind( int idx, qint64 v ) const { sqlite3_bind_int64( m_stmt, idx, v ); }
    void bindEmptyNull( int idx ) const { sqlite3_bind_null( m_stmt, idx ); }
    QString text( int col ) const
    {
        const unsigned char *p = sqlite3_column_text( m_stmt, col );
        return p ? QString::fromUtf8( reinterpret_cast<const char *>( p ) ) : QString();
    }
    qint64 i64( int col ) const { return sqlite3_column_int64( m_stmt, col ); }

  private:
    sqlite3 *m_db = nullptr;
    sqlite3_stmt *m_stmt = nullptr;
};
} // namespace

struct WorkspaceCatalog::Impl
{
    sqlite3 *db = nullptr;

    mutable std::mutex mutex;
    QString schemaVersion;
    // Text of the last failing sqlite call (empty when nothing failed); lets
    // writers build actionable diagnostics instead of opaque failures.
    mutable QString lastError;

    bool exec( const char *sql ) const
    {
        char *err = nullptr;
        if ( sqlite3_exec( db, sql, nullptr, nullptr, &err ) != SQLITE_OK )
        {
            lastError = err ? QString::fromUtf8( err ) : QStringLiteral( "unknown sqlite error" );
            sqlite3_free( err );
            return false;
        }
        lastError.clear();
        return true;
    }

    // Checked COMMIT (issue #1045): a discarded commit result used to report
    // success for a non-durable batch and leak the open transaction into the
    // next call, whose ROLLBACK then silently undid this call's "successful"
    // writes. A failed commit now rolls back and reports failure.
    bool commit() const
    {
        if ( exec( "COMMIT" ) )
            return true;
        exec( "ROLLBACK" );
        return false;
    }

    // sqlite3_errmsg is connection-scoped and stays current after a failed
    // step(), unlike lastError which only exec() refreshes.
    QString stepError() const { return QString::fromUtf8( sqlite3_errmsg( db ) ); }

    // Roll back the failed transaction and return the failing statement's
    // error text, captured BEFORE the rollback replaces the connection's
    // error state.
    QString rollbackMsg() const
    {
        const QString msg = stepError();
        exec( "ROLLBACK" );
        return msg;
    }

    bool createSchema()
    {
        const char *schema =
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=NORMAL;"
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS catalog_meta("
            "  key TEXT PRIMARY KEY, value TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS assets("
            "  asset_id TEXT PRIMARY KEY,"
            "  source_key TEXT NOT NULL DEFAULT '',"
            "  canonical_source TEXT NOT NULL DEFAULT '',"
            "  kind TEXT NOT NULL DEFAULT '',"
            "  state TEXT NOT NULL DEFAULT '',"
            "  persistence TEXT NOT NULL DEFAULT '',"
            "  display_name TEXT NOT NULL DEFAULT '',"
            "  parent_collection_id TEXT NOT NULL DEFAULT '',"
            "  acquisition_ms INTEGER NOT NULL DEFAULT 0,"
            "  revision INTEGER NOT NULL DEFAULT 1,"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS idx_assets_kind_state ON assets(kind, state);"
            "CREATE INDEX IF NOT EXISTS idx_assets_collection ON assets(parent_collection_id);"
            "CREATE INDEX IF NOT EXISTS idx_assets_name ON assets(display_name);"
            "CREATE TABLE IF NOT EXISTS aliases("
            "  path TEXT PRIMARY KEY,"
            "  asset_id TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS idx_aliases_asset ON aliases(asset_id);"
            "CREATE TABLE IF NOT EXISTS tags("
            "  asset_id TEXT NOT NULL,"
            "  tag TEXT NOT NULL,"
            "  PRIMARY KEY(asset_id, tag));"
            "CREATE INDEX IF NOT EXISTS idx_tags_tag ON tags(tag);"
            "INSERT OR REPLACE INTO catalog_meta(key,value)"
            " VALUES('schema_version','" SICNU_CATALOG_SCHEMA_VERSION "');"
            "COMMIT;";
        return exec( schema );
    }

    CatalogAsset readAsset( const Stmt &s )
    {
        CatalogAsset a;
        a.assetId = s.text( 0 );
        a.sourceKey = s.text( 1 );
        a.canonicalSource = s.text( 2 );
        a.kind = s.text( 3 );
        a.state = s.text( 4 );
        a.persistence = s.text( 5 );
        a.displayName = s.text( 6 );
        a.parentCollectionId = s.text( 7 );
        a.acquisitionMs = s.i64( 8 );
        a.revision = static_cast<quint64>( s.i64( 9 ) );
        a.metadataJson = s.text( 10 );
        // Hydrate aliases and tags (small indexed lookups per asset).
        {
            Stmt al( db, "SELECT path FROM aliases WHERE asset_id=?" );
            al.bind( 1, a.assetId );
            while ( al.stepRow() )
                a.aliases.append( al.text( 0 ) );
            Stmt tg( db, "SELECT tag FROM tags WHERE asset_id=?" );
            tg.bind( 1, a.assetId );
            while ( tg.stepRow() )
                a.tags.append( tg.text( 0 ) );
        }
        return a;
    }

};

WorkspaceCatalog::~WorkspaceCatalog()
{
    close();
}

bool WorkspaceCatalog::open( const QString &dbPath, QString *errorOut )
{
    close();
    m_impl = new Impl;
    if ( sqlite3_open_v2( dbPath.toUtf8().constData(), &m_impl->db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr ) != SQLITE_OK )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot open catalog %1" ).arg( dbPath );
        close();
        return false;
    }
    // Forward-compat guard like the ArtifactStore.
    {
        Stmt v( m_impl->db, "SELECT value FROM catalog_meta WHERE key='schema_version'" );
        if ( v && v.stepRow() && v.text( 0 ).toInt() > 1 )
        {
            if ( errorOut )
                *errorOut = QStringLiteral( "catalog schema %1 is newer than supported" )
                                .arg( v.text( 0 ) );
            close();
            return false;
        }
    }
    if ( !m_impl->createSchema() )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot create catalog schema" );
        close();
        return false;
    }
    m_impl->schemaVersion = QStringLiteral( "1" );
    return true;
}

void WorkspaceCatalog::close()
{
    if ( !m_impl )
        return;
    {
        std::lock_guard<std::mutex> lock( m_impl->mutex );
        if ( m_impl->db )
            sqlite3_close_v2( m_impl->db );
    }
    delete m_impl;
    m_impl = nullptr;
}

Result<void> WorkspaceCatalog::upsertAsset( const CatalogAsset &asset )
{
    return upsertAssets( { asset } );
}

Result<void> WorkspaceCatalog::upsertAssets( const QVector<CatalogAsset> &assets )
{
    if ( !m_impl )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.closed" ),
                                                   QStringLiteral( "catalog not open" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    // Checked BEGIN (issue #1045): on SQLITE_BUSY the whole batch used to run
    // in autocommit — a mid-batch step failure then no-op'd the ROLLBACK while
    // earlier assets were already durable.
    if ( !m_impl->exec( "BEGIN IMMEDIATE" ) )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.begin" ),
                                                   QStringLiteral( "cannot begin transaction: %1" ).arg( m_impl->lastError ) ) );
    {
        Stmt up( m_impl->db,
            "INSERT INTO assets(asset_id, source_key, canonical_source, kind, state,"
            " persistence, display_name, parent_collection_id, acquisition_ms, revision,"
            " metadata_json, updated_ms) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(asset_id) DO UPDATE SET"
            "  source_key=excluded.source_key, canonical_source=excluded.canonical_source,"
            "  kind=excluded.kind, state=excluded.state, persistence=excluded.persistence,"
            "  display_name=excluded.display_name,"
            "  parent_collection_id=excluded.parent_collection_id,"
            "  acquisition_ms=excluded.acquisition_ms, revision=excluded.revision,"
            "  metadata_json=excluded.metadata_json, updated_ms=excluded.updated_ms" );
        Stmt delAlias( m_impl->db, "DELETE FROM aliases WHERE asset_id=?" );
        Stmt insAlias( m_impl->db, "INSERT OR REPLACE INTO aliases(path, asset_id) VALUES(?,?)" );
        Stmt delTag( m_impl->db, "DELETE FROM tags WHERE asset_id=?" );
        Stmt insTag( m_impl->db, "INSERT OR IGNORE INTO tags(asset_id, tag) VALUES(?,?)" );
        if ( !up || !delAlias || !insAlias || !delTag || !insTag )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.prepare" ),
                                                       QStringLiteral( "statement prepare failed" ) ) );
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for ( const CatalogAsset &asset : assets )
        {
            up.reset();
            up.bind( 1, asset.assetId );
            up.bind( 2, asset.sourceKey );
            up.bind( 3, asset.canonicalSource );
            up.bind( 4, asset.kind );
            up.bind( 5, asset.state );
            up.bind( 6, asset.persistence );
            up.bind( 7, asset.displayName );
            up.bind( 8, asset.parentCollectionId );
            up.bind( 9, asset.acquisitionMs );
            up.bind( 10, static_cast<qint64>( asset.revision ) );
            up.bind( 11, asset.metadataJson );
            up.bind( 12, now );
            if ( SICNU_FAULT_POINT( "workspace_catalog.step" ) || !up.step() )
            {
                // Roll back on the FIRST failure — the batch is atomic, so no
                // asset row may survive a later asset's failed write.
                const QString stepErr = m_impl->rollbackMsg();
                return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.upsert" ),
                                                           QStringLiteral( "asset %1 failed: %2" )
                                                               .arg( asset.assetId, stepErr ) ) );
            }
            delAlias.reset();
            delAlias.bind( 1, asset.assetId );
            if ( SICNU_FAULT_POINT( "workspace_catalog.step" ) || !delAlias.step() )
            {
                const QString stepErr = m_impl->rollbackMsg();
                return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.alias_write" ),
                                                           QStringLiteral( "asset %1 alias cleanup failed: %2" )
                                                               .arg( asset.assetId, stepErr ) ) );
            }
            const auto writeAlias = [ & ]( const QString &path ) -> bool {
                insAlias.reset();
                insAlias.bind( 1, path );
                insAlias.bind( 2, asset.assetId );
                return insAlias.step();
            };
            if ( !writeAlias( asset.canonicalSource ) )
            {
                const QString stepErr = m_impl->rollbackMsg();
                return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.alias_write" ),
                                                           QStringLiteral( "asset %1 alias write failed: %2" )
                                                               .arg( asset.assetId, stepErr ) ) );
            }
            for ( const QString &alias : asset.aliases )
            {
                if ( alias == asset.canonicalSource )
                    continue;
                if ( !writeAlias( alias ) )
                {
                    const QString stepErr = m_impl->rollbackMsg();
                    return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.alias_write" ),
                                                               QStringLiteral( "asset %1 alias write failed: %2" )
                                                                   .arg( asset.assetId, stepErr ) ) );
                }
            }
            delTag.reset();
            delTag.bind( 1, asset.assetId );
            if ( !delTag.step() )
            {
                const QString stepErr = m_impl->rollbackMsg();
                return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.tag_write" ),
                                                           QStringLiteral( "asset %1 tag cleanup failed: %2" )
                                                               .arg( asset.assetId, stepErr ) ) );
            }
            for ( const QString &tag : asset.tags )
            {
                insTag.reset();
                insTag.bind( 1, asset.assetId );
                insTag.bind( 2, tag );
                if ( !insTag.step() )
                {
                    const QString stepErr = m_impl->rollbackMsg();
                    return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.tag_write" ),
                                                               QStringLiteral( "asset %1 tag write failed: %2" )
                                                                   .arg( asset.assetId, stepErr ) ) );
                }
            }
        }
    }
    if ( SICNU_FAULT_POINT( "workspace_catalog.commit" ) )
    {
        // Injected commit failure (fault matrix, test-only arming): take
        // exactly the real commit-failure branch so the batch rolls back
        // instead of reporting success for a non-durable write.
        m_impl->exec( "ROLLBACK" );
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.commit" ),
                                                   QStringLiteral( "transaction commit failed" ) ) );
    }
    if ( !m_impl->commit() )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.commit" ),
                                                   m_impl->lastError.isEmpty() ? QStringLiteral( "transaction commit failed" )
                                                                               : m_impl->lastError ) );
    return Result<void>::success();
}

Result<void> WorkspaceCatalog::removeAsset( const QString &assetId )
{
    if ( !m_impl )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.closed" ),
                                                   QStringLiteral( "catalog not open" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    if ( !m_impl->exec( "BEGIN IMMEDIATE" ) )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.begin" ),
                                                   QStringLiteral( "cannot begin transaction: %1" ).arg( m_impl->lastError ) ) );
    {
        Stmt a( m_impl->db, "DELETE FROM assets WHERE asset_id=?" );
        Stmt al( m_impl->db, "DELETE FROM aliases WHERE asset_id=?" );
        Stmt t( m_impl->db, "DELETE FROM tags WHERE asset_id=?" );
        if ( !a || !al || !t )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.prepare" ),
                                                       QStringLiteral( "prepare failed" ) ) );
        }
        a.bind( 1, assetId );
        if ( SICNU_FAULT_POINT( "workspace_catalog.step" ) || !a.step() )
        {
            const QString stepErr = m_impl->rollbackMsg();
            return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.remove" ),
                                                       QStringLiteral( "asset %1 delete failed: %2" )
                                                           .arg( assetId, stepErr ) ) );
        }
        // The not-found verdict is reached INSIDE the transaction so a miss
        // rolls back instead of committing a partial cleanup (issue #1045).
        const bool removed = sqlite3_changes( m_impl->db ) > 0;
        al.bind( 1, assetId );
        t.bind( 1, assetId );
        if ( !al.step() || !t.step() )
        {
            const QString stepErr = m_impl->rollbackMsg();
            return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.remove" ),
                                                       QStringLiteral( "asset %1 cleanup failed: %2" )
                                                           .arg( assetId, stepErr ) ) );
        }
        if ( !removed )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.unknown" ),
                                                       QStringLiteral( "no asset %1" ).arg( assetId ) ) );
        }
    }
    if ( SICNU_FAULT_POINT( "workspace_catalog.commit" ) )
    {
        // Injected commit failure (fault matrix, test-only arming): same
        // branch as a real commit error — roll back, report failure.
        m_impl->exec( "ROLLBACK" );
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.commit" ),
                                                   QStringLiteral( "transaction commit failed" ) ) );
    }
    if ( !m_impl->commit() )
        return Result<void>::failure( catalogDiag( QStringLiteral( "catalog.commit" ),
                                                   m_impl->lastError.isEmpty() ? QStringLiteral( "transaction commit failed" )
                                                                               : m_impl->lastError ) );
    return Result<void>::success();
}

std::optional<CatalogAsset> WorkspaceCatalog::byId( const QString &assetId ) const
{
    if ( !m_impl || assetId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT " SICNU_CATALOG_ASSET_COLS " FROM assets WHERE asset_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, assetId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readAsset( s );
}

std::optional<CatalogAsset> WorkspaceCatalog::byPath( const QString &path ) const
{
    if ( !m_impl || path.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    // Qualified columns: asset_id exists in BOTH joined tables, so the
    // unqualified SELECT list of SICNU_CATALOG_ASSET_COLS would be ambiguous.
    Stmt s( m_impl->db,
            "SELECT a.asset_id, a.source_key, a.canonical_source, a.kind, a.state,"
            " a.persistence, a.display_name, a.parent_collection_id, a.acquisition_ms,"
            " a.revision, a.metadata_json FROM assets a"
            " JOIN aliases al ON al.asset_id=a.asset_id WHERE al.path=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, path );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readAsset( s );
}

CatalogPage WorkspaceCatalog::page( const CatalogQuery &query, qint64 offset, qint64 limit ) const
{
    CatalogPage out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );

    QString where = " WHERE 1=1";
    if ( !query.kind.isEmpty() )
        where += " AND kind=?";
    if ( !query.state.isEmpty() )
        where += " AND state=?";
    if ( !query.parentCollectionId.isEmpty() )
        where += " AND parent_collection_id=?";
    if ( !query.textPrefix.isEmpty() )
        where += " AND (display_name LIKE ? ESCAPE '\\' OR asset_id IN (SELECT asset_id FROM aliases WHERE path LIKE ? ESCAPE '\\'))";

    // Build bound LIKE prefixes safely (escape % and _).
    const QString prefix = query.textPrefix.isEmpty()
                               ? QString()
                               : QString( query.textPrefix )
                                     .replace( QLatin1Char( '\\' ), QLatin1String( "\\\\" ) )
                                     .replace( QLatin1Char( '%' ), QLatin1String( "\\%" ) )
                                     .replace( QLatin1Char( '_' ), QLatin1String( "\\_" ) ) + "%";

    const qint64 clampedLimit = std::clamp<qint64>( limit, 1, kMaxPageSize );
    const qint64 clampedOffset = std::max<qint64>( offset, 0 );

    // Total count (for paging UI).
    {
        Stmt s( m_impl->db, ( "SELECT COUNT(*) FROM assets" + where ).toUtf8().constData() );
        if ( !s )
            return out;
        int idx = 1;
        if ( !query.kind.isEmpty() ) s.bind( idx++, query.kind );
        if ( !query.state.isEmpty() ) s.bind( idx++, query.state );
        if ( !query.parentCollectionId.isEmpty() ) s.bind( idx++, query.parentCollectionId );
        if ( !query.textPrefix.isEmpty() ) { s.bind( idx++, prefix ); s.bind( idx++, prefix ); }
        if ( s.stepRow() )
            out.total = s.i64( 0 );
    }
    {
        Stmt s( m_impl->db, ( QString( "SELECT " SICNU_CATALOG_ASSET_COLS " FROM assets" ) + where +
                              " ORDER BY updated_ms DESC, asset_id LIMIT ? OFFSET ?" ).toUtf8().constData() );
        if ( !s )
            return out;
        int idx = 1;
        if ( !query.kind.isEmpty() ) s.bind( idx++, query.kind );
        if ( !query.state.isEmpty() ) s.bind( idx++, query.state );
        if ( !query.parentCollectionId.isEmpty() ) s.bind( idx++, query.parentCollectionId );
        if ( !query.textPrefix.isEmpty() ) { s.bind( idx++, prefix ); s.bind( idx++, prefix ); }
        s.bind( idx++, clampedLimit );
        s.bind( idx++, clampedOffset );
        while ( s.stepRow() )
            out.items.append( m_impl->readAsset( s ) );
    }
    return out;
}

qint64 WorkspaceCatalog::count() const
{
    if ( !m_impl )
        return 0;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT COUNT(*) FROM assets" );
    return s.stepRow() ? s.i64( 0 ) : 0;
}

QString WorkspaceCatalog::schemaVersion() const
{
    return m_impl ? m_impl->schemaVersion : QString();
}

Result<void> DataManagerCatalogBridge::mirrorUpsert( const CatalogAsset &asset )
{
    return m_catalog.upsertAsset( asset );
}

Result<void> DataManagerCatalogBridge::mirrorRemove( const QString &assetId )
{
    return m_catalog.removeAsset( assetId );
}

} // namespace sicnu::data
