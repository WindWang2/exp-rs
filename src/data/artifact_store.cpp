// artifact_store.cpp — SQLite-backed implementation of artifact identity
// bookkeeping. See artifact_store.h for the contract.
#include "artifact_store.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <QVector>

#include <sqlite3.h>

#include <mutex>

namespace sicnu::data
{
namespace
{
// Macro (not constexpr pointer) so it can join with adjacent string literals.
#define SICNU_ARTIFACT_STORE_SCHEMA_VERSION "1"
constexpr const char *kCurrentSchemaVersion = "1";

Diagnostic diag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString jsonToCompact( const QJsonObject &obj )
{
    return obj.isEmpty() ? QString() : QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
}

QJsonObject jsonFromCompact( const QString &text )
{
    if ( text.isEmpty() )
        return QJsonObject{};
    return QJsonDocument::fromJson( text.toUtf8() ).object();
}

// Minimal sqlite3 helpers; every call site checks for SQLITE_OK / SQLITE_DONE.
class Stmt
{
  public:
    Stmt( sqlite3 *db, const char *sql, QString *errorOut = nullptr )
        : m_db( db )
    {
        if ( sqlite3_prepare_v2( db, sql, -1, &m_stmt, nullptr ) != SQLITE_OK )
        {
            if ( errorOut )
                *errorOut = QString::fromUtf8( sqlite3_errmsg( db ) );
            m_stmt = nullptr;
        }
    }
    ~Stmt()
    {
        if ( m_stmt )
            sqlite3_finalize( m_stmt );
    }
    explicit operator bool() const { return m_stmt != nullptr; }
    sqlite3_stmt *get() const { return m_stmt; }

    bool step() const { return sqlite3_step( m_stmt ) == SQLITE_DONE; }
    bool stepRow() const { return sqlite3_step( m_stmt ) == SQLITE_ROW; }
    void reset()
    {
        sqlite3_reset( m_stmt );
        sqlite3_clear_bindings( m_stmt );
    }

    void bind( int idx, const QString &v ) const
    {
        sqlite3_bind_text( m_stmt, idx, v.toUtf8().constData(), -1, SQLITE_TRANSIENT );
    }
    void bind( int idx, qint64 v ) const { sqlite3_bind_int64( m_stmt, idx, v ); }
    void bind( int idx, int v ) const { sqlite3_bind_int( m_stmt, idx, v ); }
    void bindOpt( int idx, const QString &v ) const
    {
        if ( v.isEmpty() )
            sqlite3_bind_null( m_stmt, idx );
        else
            sqlite3_bind_text( m_stmt, idx, v.toUtf8().constData(), -1, SQLITE_TRANSIENT );
    }

    QString text( int col ) const
    {
        const unsigned char *p = sqlite3_column_text( m_stmt, col );
        return p ? QString::fromUtf8( reinterpret_cast<const char *>( p ) ) : QString();
    }
    qint64 i64( int col ) const { return sqlite3_column_int64( m_stmt, col ); }
    int i( int col ) const { return sqlite3_column_int( m_stmt, col ); }
    bool isNull( int col ) const { return sqlite3_column_type( m_stmt, col ) == SQLITE_NULL; }

  private:
    sqlite3 *m_db = nullptr;
    sqlite3_stmt *m_stmt = nullptr;
};

QString lastError( sqlite3 *db )
{
    return QString::fromUtf8( sqlite3_errmsg( db ) );
}

// Column list for reading a full ArtifactRecord (order must match readRecord).
std::string recordSelectSql( const char *tail )
{
    return std::string( "SELECT artifact_id, logical_key, version, producer_fingerprint,"
                        " content_digest, storage_path, kind, size_bytes, mtime_ms,"
                        " metadata_json, lineage_json, state, created_at_ms, last_touch_ms"
                        " FROM artifacts " ) + tail;
}

} // namespace

QString artifactContentDigest( const QString &path, QString *errorOut )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot open %1 for digest" ).arg( path );
        return {};
    }
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    constexpr qint64 kChunk = 1 << 20;
    while ( !file.atEnd() )
        hash.addData( file.read( kChunk ) );
    return QString::fromLatin1( hash.result().toHex() );
}

struct ArtifactStore::Impl
{
    sqlite3 *db = nullptr;
    // Recursive: public entry points (registerArtifact → artifactById) nest
    // their lock acquisition.
    mutable std::recursive_mutex mutex;
    QString schemaVersion;

    bool exec( const char *sql, QString *errorOut ) const
    {
        char *err = nullptr;
        if ( sqlite3_exec( db, sql, nullptr, nullptr, &err ) != SQLITE_OK )
        {
            if ( errorOut )
                *errorOut = err ? QString::fromUtf8( err ) : lastError( db );
            sqlite3_free( err );
            return false;
        }
        return true;
    }

    bool createSchema( QString *errorOut ) const
    {
        const char *schema =
            "PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=NORMAL;"
            "PRAGMA foreign_keys=ON;"
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS store_meta("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS artifacts("
            "  artifact_id TEXT NOT NULL,"
            "  logical_key TEXT NOT NULL,"
            "  version INTEGER NOT NULL,"
            "  producer_fingerprint TEXT,"
            "  content_digest TEXT,"
            "  storage_path TEXT NOT NULL,"
            "  kind TEXT NOT NULL,"
            "  size_bytes INTEGER NOT NULL,"
            "  mtime_ms INTEGER NOT NULL,"
            "  metadata_json TEXT,"
            "  lineage_json TEXT,"
            "  state TEXT NOT NULL CHECK(state IN ('live','retained','trash')),"
            "  created_at_ms INTEGER NOT NULL,"
            "  last_touch_ms INTEGER NOT NULL,"
            "  PRIMARY KEY(logical_key, version));"
            "CREATE INDEX IF NOT EXISTS idx_artifacts_id ON artifacts(artifact_id);"
            "CREATE INDEX IF NOT EXISTS idx_artifacts_digest ON artifacts(content_digest);"
            "CREATE INDEX IF NOT EXISTS idx_artifacts_fp ON artifacts(producer_fingerprint);"
            "CREATE INDEX IF NOT EXISTS idx_artifacts_path ON artifacts(storage_path);"
            "CREATE TABLE IF NOT EXISTS artifact_refs("
            "  artifact_id TEXT NOT NULL,"
            "  ref_kind TEXT NOT NULL,"
            "  ref_id TEXT NOT NULL,"
            "  created_at_ms INTEGER NOT NULL,"
            "  PRIMARY KEY(artifact_id, ref_kind, ref_id));"
            "CREATE INDEX IF NOT EXISTS idx_refs_ref ON artifact_refs(ref_kind, ref_id);"
            "INSERT OR REPLACE INTO store_meta(key,value) VALUES('schema_version','" SICNU_ARTIFACT_STORE_SCHEMA_VERSION "');"
            "COMMIT;";
        return exec( schema, errorOut );
    }

    ArtifactRecord readRecord( const Stmt &s ) const
    {
        ArtifactRecord r;
        r.artifactId = s.text( 0 );
        r.logicalKey = s.text( 1 );
        r.version = s.i( 2 );
        r.producerFingerprint = s.isNull( 3 ) ? QString() : s.text( 3 );
        r.contentDigest = s.isNull( 4 ) ? QString() : s.text( 4 );
        r.storagePath = s.text( 5 );
        r.kind = s.text( 6 );
        r.sizeBytes = s.i64( 7 );
        r.mtimeMs = s.i64( 8 );
        r.metadata = jsonFromCompact( s.isNull( 9 ) ? QString() : s.text( 9 ) );
        r.lineage = jsonFromCompact( s.isNull( 10 ) ? QString() : s.text( 10 ) );
        r.state = s.text( 11 );
        r.createdAtMs = s.i64( 12 );
        r.lastTouchMs = s.i64( 13 );
        return r;
    }
};

ArtifactStore::~ArtifactStore()
{
    close();
}

bool ArtifactStore::open( const QString &dbPath, QString *errorOut )
{
    close();
    m_impl = new Impl;
    if ( sqlite3_open_v2( dbPath.toUtf8().constData(), &m_impl->db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr ) != SQLITE_OK )
    {
        if ( errorOut )
            *errorOut = m_impl->db ? lastError( m_impl->db ) : QStringLiteral( "cannot open %1" ).arg( dbPath );
        close();
        return false;
    }
    // Forward-compat guard: refuse a database written by a NEWER schema.
    {
        Stmt v( m_impl->db, "SELECT value FROM store_meta WHERE key='schema_version'", errorOut );
        if ( v && v.stepRow() && v.text( 0 ).toInt() > QString( kCurrentSchemaVersion ).toInt() )
        {
            if ( errorOut )
                *errorOut = QStringLiteral( "artifact store schema %1 is newer than supported %2" )
                                .arg( v.text( 0 ), QString( kCurrentSchemaVersion ) );
            close();
            return false;
        }
    }
    if ( !m_impl->createSchema( errorOut ) )
    {
        close();
        return false;
    }
    m_impl->schemaVersion = QString( kCurrentSchemaVersion );
    return true;
}

void ArtifactStore::close()
{
    if ( !m_impl )
        return;
    {
        std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
        if ( m_impl->db )
            sqlite3_close_v2( m_impl->db );
    }
    delete m_impl;
    m_impl = nullptr;
}

bool ArtifactStore::isOpen() const
{
    return m_impl && m_impl->db;
}

Result<ArtifactRecord> ArtifactStore::registerArtifact( const ArtifactRegistration &reg )
{
    if ( !isOpen() || reg.logicalKey.isEmpty() || reg.storagePath.isEmpty() || reg.kind.isEmpty() )
        return Result<ArtifactRecord>::failure( diag( QStringLiteral( "artifact.invalid" ),
                                                      QStringLiteral( "store closed or registration missing identity/payload/kind" ) ) );

    qint64 size = reg.sizeBytes;
    qint64 mtime = reg.mtimeMs;
    if ( size <= 0 || mtime <= 0 )
    {
        const QFileInfo info( reg.storagePath );
        if ( !info.exists() )
        {
            if ( reg.requireExistingPayload )
                return Result<ArtifactRecord>::failure(
                    diag( QStringLiteral( "artifact.payload_missing" ),
                          QStringLiteral( "payload %1 does not exist" ).arg( reg.storagePath ) ) );
            size = 0;
            mtime = 0;
        }
        else
        {
            if ( size <= 0 )
                size = info.size();
            if ( mtime <= 0 )
                mtime = info.lastModified().toMSecsSinceEpoch();
        }
    }

    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    QString *errorOut = nullptr;

    // Find the logical artifact's current head (artifact_id + version).
    QString artifactId;
    int nextVersion = 1;
    {
        Stmt s( m_impl->db,
                "SELECT artifact_id, MAX(version) FROM artifacts WHERE logical_key=?", errorOut );
        if ( !s )
            return Result<ArtifactRecord>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
        s.bind( 1, reg.logicalKey );
        if ( s.stepRow() && !s.isNull( 0 ) )
        {
            artifactId = s.text( 0 );
            nextVersion = s.i( 1 ) + 1;
        }
    }
    if ( artifactId.isEmpty() )
        artifactId = QUuid::createUuid().toString( QUuid::WithoutBraces );

    m_impl->exec( "BEGIN IMMEDIATE", nullptr );
    {
        Stmt s( m_impl->db,
                "INSERT INTO artifacts(artifact_id, logical_key, version, producer_fingerprint,"
                " content_digest, storage_path, kind, size_bytes, mtime_ms, metadata_json,"
                " lineage_json, state, created_at_ms, last_touch_ms)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                errorOut );
        if ( !s )
        {
            m_impl->exec( "ROLLBACK", nullptr );
            return Result<ArtifactRecord>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
        }
        const qint64 ts = nowMs();
        s.bind( 1, artifactId );
        s.bind( 2, reg.logicalKey );
        s.bind( 3, nextVersion );
        s.bindOpt( 4, reg.producerFingerprint );
        s.bindOpt( 5, reg.contentDigest );
        s.bind( 6, reg.storagePath );
        s.bind( 7, reg.kind );
        s.bind( 8, size );
        s.bind( 9, mtime );
        s.bindOpt( 10, jsonToCompact( reg.metadata ) );
        s.bindOpt( 11, jsonToCompact( reg.lineage ) );
        s.bind( 12, QStringLiteral( "live" ) );
        s.bind( 13, ts );
        s.bind( 14, ts );
        if ( !s.step() )
        {
            const QString err = lastError( m_impl->db );
            m_impl->exec( "ROLLBACK", nullptr );
            return Result<ArtifactRecord>::failure( diag( QStringLiteral( "artifact.db" ), err ) );
        }
    }
    m_impl->exec( "COMMIT", nullptr );

    // Fetch exactly the version just inserted — artifact_id is shared across
    // versions, so an id-only lookup may return an older row.
    std::optional<ArtifactRecord> rec;
    {
        Stmt s( m_impl->db,
                "SELECT artifact_id, logical_key, version, producer_fingerprint,"
                " content_digest, storage_path, kind, size_bytes, mtime_ms, metadata_json,"
                " lineage_json, state, created_at_ms, last_touch_ms FROM artifacts"
                " WHERE logical_key=? AND version=?" );
        if ( s )
        {
            s.bind( 1, reg.logicalKey );
            s.bind( 2, nextVersion );
            if ( s.stepRow() )
                rec = m_impl->readRecord( s );
        }
    }
    if ( !rec )
        return Result<ArtifactRecord>::failure( diag( QStringLiteral( "artifact.db" ),
                                                      QStringLiteral( "registered row vanished" ) ) );
    return Result<ArtifactRecord>::success( *rec );
}

Result<void> ArtifactStore::updateContentDigest( const QString &artifactId, const QString &digest )
{
    if ( !isOpen() || artifactId.isEmpty() || digest.isEmpty() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "missing id/digest" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "UPDATE artifacts SET content_digest=? WHERE artifact_id=?", nullptr );
    if ( !s )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    s.bind( 1, digest );
    s.bind( 2, artifactId );
    if ( !s.step() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    if ( sqlite3_changes( m_impl->db ) == 0 )
        return Result<void>::failure( diag( QStringLiteral( "artifact.unknown" ),
                                            QStringLiteral( "no artifact %1" ).arg( artifactId ) ) );
    return Result<void>::success();
}

std::optional<ArtifactRecord> ArtifactStore::artifactById( const QString &artifactId ) const
{
    if ( !isOpen() || artifactId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "WHERE artifact_id=?" ).c_str(), nullptr );
    if ( !s )
        return std::nullopt;
    s.bind( 1, artifactId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readRecord( s );
}

std::optional<ArtifactRecord> ArtifactStore::latestByLogicalKey( const QString &logicalKey ) const
{
    if ( !isOpen() || logicalKey.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "WHERE logical_key=? ORDER BY version DESC LIMIT 1" ).c_str(),
            nullptr );
    if ( !s )
        return std::nullopt;
    s.bind( 1, logicalKey );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readRecord( s );
}

std::optional<ArtifactRecord> ArtifactStore::latestByPath( const QString &storagePath ) const
{
    if ( !isOpen() || storagePath.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
            recordSelectSql( "WHERE storage_path=? ORDER BY created_at_ms DESC, version DESC LIMIT 1" )
                .c_str(),
            nullptr );
    if ( !s )
        return std::nullopt;
    s.bind( 1, storagePath );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readRecord( s );
}

QVector<ArtifactRecord> ArtifactStore::versionsByLogicalKey( const QString &logicalKey ) const
{
    QVector<ArtifactRecord> out;
    if ( !isOpen() || logicalKey.isEmpty() )
        return out;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "WHERE logical_key=? ORDER BY version DESC" ).c_str(),
            nullptr );
    if ( !s )
        return out;
    s.bind( 1, logicalKey );
    while ( s.stepRow() )
        out.append( m_impl->readRecord( s ) );
    return out;
}

QVector<ArtifactRecord> ArtifactStore::byProducerFingerprint( const QString &fingerprint ) const
{
    QVector<ArtifactRecord> out;
    if ( !isOpen() || fingerprint.isEmpty() )
        return out;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "WHERE producer_fingerprint=? AND state!='trash'"
                                        " ORDER BY version DESC" ).c_str(),
            nullptr );
    if ( !s )
        return out;
    s.bind( 1, fingerprint );
    while ( s.stepRow() )
        out.append( m_impl->readRecord( s ) );
    return out;
}

std::optional<ArtifactRecord> ArtifactStore::liveByContentDigest( const QString &digest ) const
{
    if ( !isOpen() || digest.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "WHERE content_digest=? AND state='live' LIMIT 1" ).c_str(),
            nullptr );
    if ( !s )
        return std::nullopt;
    s.bind( 1, digest );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readRecord( s );
}

Result<void> ArtifactStore::attachRef( const QString &artifactId, const QString &refKind,
                                       const QString &refId )
{
    if ( !isOpen() || artifactId.isEmpty() || refKind.isEmpty() || refId.isEmpty() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "missing ref identity" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "INSERT OR IGNORE INTO artifact_refs(artifact_id, ref_kind, ref_id, created_at_ms)"
                        " VALUES(?,?,?,?)",
            nullptr );
    if ( !s )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    s.bind( 1, artifactId );
    s.bind( 2, refKind );
    s.bind( 3, refId );
    s.bind( 4, nowMs() );
    if ( !s.step() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    if ( sqlite3_changes( m_impl->db ) == 0 && !artifactById( artifactId ) )
        return Result<void>::failure( diag( QStringLiteral( "artifact.unknown" ),
                                            QStringLiteral( "no artifact %1" ).arg( artifactId ) ) );
    return Result<void>::success();
}

Result<void> ArtifactStore::detachRef( const QString &artifactId, const QString &refKind,
                                       const QString &refId )
{
    if ( !isOpen() || artifactId.isEmpty() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "missing ref identity" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "DELETE FROM artifact_refs WHERE artifact_id=? AND ref_kind=? AND ref_id=?", nullptr );
    if ( !s )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    s.bind( 1, artifactId );
    s.bind( 2, refKind );
    s.bind( 3, refId );
    if ( !s.step() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    return Result<void>::success();
}

int ArtifactStore::refCount( const QString &artifactId ) const
{
    if ( !isOpen() || artifactId.isEmpty() )
        return 0;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT COUNT(*) FROM artifact_refs WHERE artifact_id=?", nullptr );
    if ( !s )
        return 0;
    s.bind( 1, artifactId );
    return s.stepRow() ? s.i( 0 ) : 0;
}

QVector<ArtifactRef> ArtifactStore::refsOf( const QString &artifactId ) const
{
    QVector<ArtifactRef> out;
    if ( !isOpen() || artifactId.isEmpty() )
        return out;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT artifact_id, ref_kind, ref_id, created_at_ms FROM artifact_refs"
                        " WHERE artifact_id=? ORDER BY created_at_ms",
            nullptr );
    if ( !s )
        return out;
    s.bind( 1, artifactId );
    while ( s.stepRow() )
    {
        ArtifactRef r;
        r.artifactId = s.text( 0 );
        r.refKind = s.text( 1 );
        r.refId = s.text( 2 );
        r.createdAtMs = s.i64( 3 );
        out.append( r );
    }
    return out;
}

Result<void> ArtifactStore::retain( const QString &artifactId )
{
    if ( !isOpen() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "store closed" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "UPDATE artifacts SET state='retained' WHERE artifact_id=? AND state='live'", nullptr );
    if ( !s )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    s.bind( 1, artifactId );
    if ( !s.step() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    if ( sqlite3_changes( m_impl->db ) == 0 )
        return Result<void>::failure( diag( QStringLiteral( "artifact.state" ),
                                            QStringLiteral( "artifact %1 is not live" ).arg( artifactId ) ) );
    return Result<void>::success();
}

Result<void> ArtifactStore::markTrash( const QString &artifactId )
{
    if ( !isOpen() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "store closed" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "UPDATE artifacts SET state='trash' WHERE artifact_id=?", nullptr );
    if ( !s )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    s.bind( 1, artifactId );
    if ( !s.step() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.db" ), lastError( m_impl->db ) ) );
    if ( sqlite3_changes( m_impl->db ) == 0 )
        return Result<void>::failure( diag( QStringLiteral( "artifact.unknown" ),
                                            QStringLiteral( "no artifact %1" ).arg( artifactId ) ) );
    return Result<void>::success();
}

bool ArtifactStore::touch( const QString &artifactId )
{
    if ( !isOpen() || artifactId.isEmpty() )
        return false;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "UPDATE artifacts SET last_touch_ms=? WHERE artifact_id=?", nullptr );
    if ( !s )
        return false;
    s.bind( 1, nowMs() );
    s.bind( 2, artifactId );
    return s.step() && sqlite3_changes( m_impl->db ) > 0;
}

QVector<ArtifactRecord> ArtifactStore::reapable( qint64 lastTouchCutoffMs ) const
{
    QVector<ArtifactRecord> out;
    if ( !isOpen() )
        return out;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, recordSelectSql( "a WHERE a.state='trash' AND a.last_touch_ms<?"
                                        " AND NOT EXISTS (SELECT 1 FROM artifact_refs r"
                                        "                 WHERE r.artifact_id=a.artifact_id)" ).c_str(),
            nullptr );
    if ( !s )
        return out;
    s.bind( 1, lastTouchCutoffMs );
    while ( s.stepRow() )
        out.append( m_impl->readRecord( s ) );
    return out;
}

Result<void> ArtifactStore::forget( const QString &artifactId )
{
    if ( !isOpen() || artifactId.isEmpty() )
        return Result<void>::failure( diag( QStringLiteral( "artifact.invalid" ), QStringLiteral( "missing id" ) ) );
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE", nullptr );
    {
        Stmt d( m_impl->db, "DELETE FROM artifact_refs WHERE artifact_id=?", nullptr );
        Stmt a( m_impl->db, "DELETE FROM artifacts WHERE artifact_id=?", nullptr );
        if ( d && a )
        {
            d.bind( 1, artifactId );
            a.bind( 1, artifactId );
            const bool ok = d.step() && a.step() && sqlite3_changes( m_impl->db ) > 0;
            if ( ok )
            {
                m_impl->exec( "COMMIT", nullptr );
                return Result<void>::success();
            }
        }
    }
    m_impl->exec( "ROLLBACK", nullptr );
    return Result<void>::failure( diag( QStringLiteral( "artifact.forget" ),
                                        QStringLiteral( "artifact %1 not removed" ).arg( artifactId ) ) );
}

qint64 ArtifactStore::count() const
{
    if ( !isOpen() )
        return 0;
    std::lock_guard<std::recursive_mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT COUNT(*) FROM artifacts", nullptr );
    return s.stepRow() ? s.i64( 0 ) : 0;
}

QString ArtifactStore::schemaVersion() const
{
    if ( !isOpen() )
        return QString();
    return m_impl->schemaVersion;
}

} // namespace sicnu::data
