// experiment_store.cpp — SQLite implementation (mirrors dataset_store).
#include "experiment_store.h"

#include "../data/query_cursor.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <sqlite3.h>

#include <QUuid>

#include "runtime/observability/fault_point.h"
#include "runtime/observability/trace.h"

#include <chrono>

namespace sicnu::experiment
{

namespace
{

Diagnostic storeDiag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

QString jsonToText( const QJsonObject &object )
{
    return QJsonDocument( object ).toJson( QJsonDocument::Compact );
}

QJsonObject textToJson( const QString &text )
{
    if ( text.isEmpty() )
        return {};
    return QJsonDocument::fromJson( text.toUtf8() ).object();
}

class Stmt
{
  public:
    Stmt( sqlite3 *db, const QString &sql )
    {
        if ( sqlite3_prepare_v2( db, sql.toUtf8().constData(), -1, &m_stmt, nullptr ) != SQLITE_OK )
            m_stmt = nullptr;
    }
    ~Stmt() { if ( m_stmt ) sqlite3_finalize( m_stmt ); }
    Stmt( const Stmt & ) = delete;
    Stmt &operator=( const Stmt & ) = delete;
    explicit operator bool() const { return m_stmt != nullptr; }
    QString error( sqlite3 *db ) const { return QString::fromUtf8( sqlite3_errmsg( db ) ); }
    bool step() const { return sqlite3_step( m_stmt ) == SQLITE_DONE; }
    bool stepRow() const { return sqlite3_step( m_stmt ) == SQLITE_ROW; }
    int changes( sqlite3 *db ) const { return sqlite3_changes( db ); }
    void reset() { sqlite3_reset( m_stmt ); sqlite3_clear_bindings( m_stmt ); }
    void bind( int idx, const QString &v ) const
    {
        sqlite3_bind_text( m_stmt, idx, v.toUtf8().constData(), -1, SQLITE_TRANSIENT );
    }
    void bind( int idx, qint64 v ) const { sqlite3_bind_int64( m_stmt, idx, v ); }
    QString text( int col ) const
    {
        const unsigned char *p = sqlite3_column_text( m_stmt, col );
        return p ? QString::fromUtf8( reinterpret_cast<const char *>( p ) ) : QString();
    }
    qint64 i64( int col ) const { return sqlite3_column_int64( m_stmt, col ); }

  private:
    sqlite3_stmt *m_stmt = nullptr;
};

} // namespace

struct ExperimentStore::Impl
{
    sqlite3 *db = nullptr;
    mutable QMutex mutex;
    bool readOnly = false;

    bool exec( const char *sql, QString *errorOut )
    {
        char *message = nullptr;
        if ( sqlite3_exec( db, sql, nullptr, nullptr, &message ) != SQLITE_OK )
        {
            if ( errorOut )
                *errorOut = message ? QString::fromUtf8( message ) : QStringLiteral( "exec failed" );
            sqlite3_free( message );
            return false;
        }
        if ( message )
            sqlite3_free( message );
        return true;
    }
    bool begin( QString *errorOut ) { return exec( "BEGIN IMMEDIATE", errorOut ); }
    bool commit( QString *errorOut )
    {
        if ( !exec( "COMMIT", errorOut ) )
        {
            exec( "ROLLBACK", nullptr );
            return false;
        }
        return true;
    }
    void rollback() { exec( "ROLLBACK", nullptr ); }
};

ExperimentStore::~ExperimentStore()
{
    close();
}

bool ExperimentStore::open( const QString &dbPath, QString *errorOut )
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
    // Schema gate BEFORE DDL (read-only forward tolerance must not mutate a
    // newer store via CREATE TABLE IF NOT EXISTS / index rebuilds).
    {
        QMutexLocker lock( &m_impl->mutex );
        Stmt stmt( db, QStringLiteral( "SELECT value FROM exp_meta WHERE key='schema_version'" ) );
        if ( stmt && stmt.stepRow() )
        {
            const QString existing = stmt.text( 0 );
            if ( !existing.isEmpty()
                 && existing != QLatin1String( kExperimentStoreSchemaVersion ) )
            {
                m_impl->readOnly = true;
                if ( errorOut )
                    *errorOut = QStringLiteral(
                        "experiment schema %1 is newer than supported (%2); opened read-only" )
                                    .arg( existing, QLatin1String( kExperimentStoreSchemaVersion ) );
                return true;
            }
        }
    }
    if ( !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS exp_meta("
             "key TEXT PRIMARY KEY, value TEXT NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS experiments("
             "id TEXT PRIMARY KEY, name TEXT NOT NULL,"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS experiment_runs("
             "run_id TEXT PRIMARY KEY, experiment_id TEXT NOT NULL,"
             "status TEXT NOT NULL, algorithm_id TEXT NOT NULL DEFAULT '',"
             "dataset_version_id TEXT NOT NULL DEFAULT '',"
             "split_manifest_id TEXT NOT NULL DEFAULT '',"
             "seed INTEGER NOT NULL DEFAULT 0,"
             "config_hash TEXT NOT NULL DEFAULT '',"
             "execution_fingerprint TEXT NOT NULL DEFAULT '',"
             "result_fingerprint TEXT NOT NULL DEFAULT '',"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL, updated_ms INTEGER NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_runs_experiment"
             " ON experiment_runs(experiment_id, created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_runs_dataset"
             " ON experiment_runs(dataset_version_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_runs_execution_fp"
             " ON experiment_runs(execution_fingerprint)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_runs_created"
             " ON experiment_runs(created_ms, run_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_runs_dataset_created"
             " ON experiment_runs(dataset_version_id, created_ms, run_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS run_metrics("
             "run_id TEXT PRIMARY KEY, dataset_version_id TEXT NOT NULL DEFAULT '',"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS experiment_lineage("
             "from_kind TEXT NOT NULL, from_id TEXT NOT NULL, edge_kind TEXT NOT NULL,"
             "to_kind TEXT NOT NULL, to_id TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(from_kind, from_id, edge_kind, to_kind, to_id))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS model_promotions("
             "promotion_id TEXT PRIMARY KEY, run_id TEXT NOT NULL,"
             "model_id TEXT NOT NULL DEFAULT '', model_digest TEXT NOT NULL DEFAULT '',"
             "dataset_version_id TEXT NOT NULL DEFAULT '', verdict TEXT NOT NULL,"
             "decision TEXT NOT NULL DEFAULT 'pending', decided_by TEXT NOT NULL DEFAULT '',"
             "decided_at_ms INTEGER NOT NULL DEFAULT 0,"
             "created_ms INTEGER NOT NULL, json TEXT NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_promotions_model"
             " ON model_promotions(model_id, created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS benchmark_definitions("
             "benchmark_id TEXT NOT NULL, benchmark_version INTEGER NOT NULL,"
             "content_digest TEXT NOT NULL DEFAULT '',"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL,"
             "PRIMARY KEY(benchmark_id, benchmark_version))",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_benchmark_defs_created"
             " ON benchmark_definitions(created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE TABLE IF NOT EXISTS benchmark_results("
             "result_id TEXT PRIMARY KEY, benchmark_id TEXT NOT NULL,"
             "benchmark_version INTEGER NOT NULL DEFAULT 0,"
             "json TEXT NOT NULL, created_ms INTEGER NOT NULL)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_benchmark_results_bench"
             " ON benchmark_results(benchmark_id, created_ms)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_exp_lineage_from"
             " ON experiment_lineage(from_kind, from_id)",
             errorOut ) ||
         !m_impl->exec(
             "CREATE INDEX IF NOT EXISTS idx_exp_lineage_to"
             " ON experiment_lineage(to_kind, to_id)",
             errorOut ) )
    {
        close();
        return false;
    }

    // Stamp schema version when missing (fresh / pre-version stores only —
    // newer schemas already returned read-only above).
    {
        QMutexLocker lock( &m_impl->mutex );
        Stmt stmt( db, QStringLiteral( "SELECT value FROM exp_meta WHERE key='schema_version'" ) );
        const bool have = stmt && stmt.stepRow() && !stmt.text( 0 ).isEmpty();
        if ( !have )
        {
            lock.unlock();
            if ( !m_impl->exec(
                     QStringLiteral(
                         "INSERT OR REPLACE INTO exp_meta(key,value) VALUES('schema_version','%1')" )
                         .arg( kExperimentStoreSchemaVersion )
                         .toUtf8()
                         .constData(),
                     errorOut ) )
            {
                close();
                return false;
            }
        }
    }
    return true;
}

void ExperimentStore::close()
{
    if ( !m_impl )
        return;
    sqlite3_close( m_impl->db );
    delete m_impl;
    m_impl = nullptr;
    m_storePath.clear();
}

bool ExperimentStore::isReadOnly() const
{
    return !m_impl || m_impl->readOnly;
}

QString ExperimentStore::schemaVersion() const
{
    if ( !m_impl )
        return QString();
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "SELECT value FROM exp_meta WHERE key='schema_version'" ) );
    if ( stmt && stmt.stepRow() )
        return stmt.text( 0 );
    return QString();
}

bool ExperimentStore::checkpointForBackup()
{
    if ( !m_impl )
        return false;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "PRAGMA wal_checkpoint(TRUNCATE)" ) );
    return stmt && stmt.stepRow() && stmt.i64( 0 ) == 0;
}

// --- experiments -----------------------------------------------------------------

sicnu::data::Result<void> ExperimentStore::upsertExperiment( const Experiment &experiment )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( experiment.experimentId().isEmpty() || experiment.name().isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.invalid" ),
                                            QStringLiteral( "experiment requires id + name" ) ) );

    const QString json = jsonToText( experiment.toJson() );
    // #1173: BEGIN IMMEDIATE around the conflict check + insert so a concurrent
    // connection cannot race a second insert past the SELECT and degrade the
    // UNIQUE failure into a generic store_write_failed (must stay experiment.conflict).
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );
    Stmt exists( m_impl->db, QStringLiteral( "SELECT json FROM experiments WHERE id=?" ) );
    if ( !exists )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            exists.error( m_impl->db ) ) );
    }
    exists.bind( 1, experiment.experimentId() );
    if ( exists.stepRow() )
    {
        const bool same = exists.text( 0 ) == json;
        m_impl->rollback();
        if ( same )
            return ResultT::success();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.conflict" ),
                                            QStringLiteral( "experiment %1 exists with different content" )
                                                .arg( experiment.experimentId() ) ) );
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO experiments(id, name, json, created_ms) VALUES(?,?,?,?)" ) );
    if ( !insert )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    }
    insert.bind( 1, experiment.experimentId() );
    insert.bind( 2, experiment.name() );
    insert.bind( 3, json );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.conflict" ),
                                            QStringLiteral( "experiment %1 exists with different content" )
                                                .arg( experiment.experimentId() ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success();
}

std::optional<Experiment> ExperimentStore::experimentById( const QString &experimentId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "SELECT json FROM experiments WHERE id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, experimentId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = Experiment::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<Experiment>( parsed.value() ) : std::nullopt;
}

sicnu::data::Result<QPair<qint64, QVector<Experiment>>> ExperimentStore::listExperiments(
    qint64 offset, qint64 limit ) const
{
    using ResultT = sicnu::data::Result<QPair<qint64, QVector<Experiment>>>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );
    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM experiments" ) );
        if ( count && count.stepRow() )
            total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM experiments ORDER BY created_ms, id LIMIT ? OFFSET ?" ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    stmt.bind( 1, limit );
    stmt.bind( 2, offset );
    QVector<Experiment> rows;
    while ( stmt.stepRow() )
    {
        auto parsed = Experiment::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( !parsed )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.corrupt_record" ),
                                                parsed.diagnostics().first().message ) );
        rows.append( parsed.value() );
    }
    return ResultT::success( qMakePair( total, rows ) );
}

sicnu::data::Result<void> ExperimentStore::deleteExperiment( const QString &experimentId )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );

    // #1173: count-then-delete inside BEGIN IMMEDIATE so a concurrent run
    // insert cannot land between the guard and the DELETE (orphaning runs).
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );
    {
        Stmt runs( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM experiment_runs WHERE experiment_id=?" ) );
        if ( !runs )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                runs.error( m_impl->db ) ) );
        }
        runs.bind( 1, experimentId );
        if ( runs.stepRow() && runs.i64( 0 ) > 0 )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.delete_refused" ),
                QStringLiteral( "experiment %1 still has runs" ).arg( experimentId ) ) );
        }
    }
    {
        // Drop lineage edges that name this experiment so deleteExperiment
        // does not leave dangling experiment_lineage rows.
        Stmt edges( m_impl->db, QStringLiteral(
            "DELETE FROM experiment_lineage WHERE (from_kind='experiment' AND from_id=?)"
            " OR (to_kind='experiment' AND to_id=?)" ) );
        if ( !edges )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                edges.error( m_impl->db ) ) );
        }
        edges.bind( 1, experimentId );
        edges.bind( 2, experimentId );
        if ( !edges.step() )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                edges.error( m_impl->db ) ) );
        }
    }
    Stmt stmt( m_impl->db, QStringLiteral( "DELETE FROM experiments WHERE id=?" ) );
    if ( !stmt )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    }
    stmt.bind( 1, experimentId );
    if ( !stmt.step() || stmt.changes( m_impl->db ) != 1 )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.not_found" ),
                                            QStringLiteral( "experiment %1 not found" ).arg( experimentId ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success();
}

// --- runs ---------------------------------------------------------------------------

namespace
{

/// Loads the stored run JSON (status included) while the caller holds the
/// mutex; nullopt when absent or corrupt.
std::optional<ExperimentRun> loadRunLocked( sqlite3 *db, const QString &runId )
{
    Stmt stmt( db, QStringLiteral( "SELECT json FROM experiment_runs WHERE run_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, runId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = ExperimentRun::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<ExperimentRun>( parsed.value() ) : std::nullopt;
}

/// Tri-state view of the stored run row for the write path (#1056): absent
/// vs parsed vs corrupt. A parse failure must be observable by the writer —
/// an unparseable row is evidence to preserve, not a gap to overwrite.
struct ExistingRun
{
    std::optional<ExperimentRun> run;
    bool corrupt = false;
    bool queryFailed = false;
};

ExistingRun loadExistingRunLocked( sqlite3 *db, const QString &runId )
{
    ExistingRun out;
    Stmt stmt( db, QStringLiteral( "SELECT json FROM experiment_runs WHERE run_id=?" ) );
    if ( !stmt )
    {
        out.queryFailed = true;
        return out;
    }
    stmt.bind( 1, runId );
    if ( !stmt.stepRow() )
        return out; // absent
    const auto parsed = ExperimentRun::fromJson( textToJson( stmt.text( 0 ) ) );
    if ( !parsed )
    {
        out.corrupt = true;
        return out;
    }
    out.run = parsed.value();
    return out;
}

/// Writes one run row inside an ALREADY-OPEN transaction (12.0). Shared by
/// upsertRunImpl and upsertRunsBatch so the batch path validates exactly
/// like the single path: corrupt-row guard, status transition, identity
/// immutability, the upsert itself and the experiment-existence mirror
/// check. Never begins/commits — the caller owns the transaction and its
/// rollback, so a failure here leaves no partial batch behind.
sicnu::data::Result<void> writeRunInTxnLocked( sqlite3 *db, const ExperimentRun &run )
{
    using ResultT = sicnu::data::Result<void>;
    if ( run.runId().isEmpty() || run.experimentId().isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.invalid" ),
                                            QStringLiteral( "run requires run_id + experiment_id" ) ) );

    // #811: the existing run is read INSIDE the same transaction that
    // writes; validating before the transaction left a TOCTOU window where a
    // concurrent writer could change the run between the checks and the upsert.
    const auto existing = loadExistingRunLocked( db, run.runId() );
    if ( existing.corrupt )
    {
        // #1056: a row that exists but cannot be parsed is corrupt evidence,
        // not an absent run — the transition/identity checks cannot run on
        // it, and silently upserting over it would destroy the evidence.
        // Fail closed and leave the row untouched for inspection.
        return ResultT::failure( storeDiag(
            QStringLiteral( "experiment.corrupt_record" ),
            QStringLiteral( "run %1 exists but its stored record cannot be parsed;"
                            " refusing to overwrite corrupt evidence" )
                .arg( run.runId() ) ) );
    }
    if ( existing.queryFailed )
    {
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            QStringLiteral( "cannot read existing run %1" )
                                                .arg( run.runId() ) ) );
    }
    const std::optional<ExperimentRun> &parsed = existing.run;
    if ( parsed && !isValidRunTransition( parsed->status(), run.status() ) )
    {
        return ResultT::failure( storeDiag(
            QStringLiteral( "experiment.bad_transition" ),
            QStringLiteral( "illegal status transition %1 → %2" )
                .arg( dataset::runStatusToString( parsed->status() ),
                      dataset::runStatusToString( run.status() ) ) ) );
    }
    // Identity pins are immutable once the run started.
    if ( parsed && parsed->status() != RunStatus::Created )
    {
        if ( parsed->executionIdentity() != run.executionIdentity() )
        {
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.identity_immutable" ),
                QStringLiteral( "run %1 already started with different identity pins" )
                    .arg( run.runId() ) ) );
        }
    }

    const QJsonObject json = run.toJson();
    // Round-trip gate (12.0): a record the store cannot read back must never
    // be written — corruption must be refused at the source, not discovered
    // at read time (the read path fails closed on it, #1056 contract).
    if ( !ExperimentRun::fromJson( json ) )
    {
        return ResultT::failure( storeDiag(
            QStringLiteral( "experiment.invalid" ),
            QStringLiteral( "run %1 serialization does not round-trip; refusing to"
                            " store an unreadable record" )
                .arg( run.runId() ) ) );
    }
    const QString executionFingerprint =
        runExecutionFingerprint( run.executionIdentity() );
    const QString resultFingerprint = run.resultFingerprint();

    Stmt upsert( db, QStringLiteral(
        "INSERT INTO experiment_runs(run_id, experiment_id, status, algorithm_id,"
        " dataset_version_id, split_manifest_id, seed, config_hash,"
        " execution_fingerprint, result_fingerprint, json, created_ms, updated_ms)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(run_id) DO UPDATE SET status=excluded.status,"
        " execution_fingerprint=excluded.execution_fingerprint,"
        " result_fingerprint=excluded.result_fingerprint,"
        " json=excluded.json, updated_ms=excluded.updated_ms" ) );
    if ( !upsert )
    {
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            upsert.error( db ) ) );
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    upsert.bind( 1, run.runId() );
    upsert.bind( 2, run.experimentId() );
    upsert.bind( 3, dataset::runStatusToString( run.status() ) );
    upsert.bind( 4, run.algorithmId() );
    upsert.bind( 5, run.datasetVersionId() );
    upsert.bind( 6, run.splitManifestId() );
    upsert.bind( 7, qint64( run.seed() ) );
    upsert.bind( 8, run.configHash() );
    upsert.bind( 9, executionFingerprint );
    upsert.bind( 10, resultFingerprint );
    upsert.bind( 11, jsonToText( json ) );
    upsert.bind( 12, now );
    upsert.bind( 13, now );
    if ( !upsert.step() )
    {
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            upsert.error( db ) ) );
    }
    // Mirror the run row into the experiment's run list (#1172: the block
    // used to only CHECK existence — run_ids stayed empty forever, so every
    // surface reading run_count reported 0 and the prune's run-list rewrite
    // was dead code). Read-modify-write inside this same transaction.
    {
        Stmt experimentSelect( db, QStringLiteral( "SELECT json FROM experiments WHERE id=?" ) );
        if ( !experimentSelect )
        {
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                experimentSelect.error( db ) ) );
        }
        experimentSelect.bind( 1, run.experimentId() );
        if ( !experimentSelect.stepRow() )
        {
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.not_found" ),
                                                QStringLiteral( "experiment %1 does not exist" )
                                                    .arg( run.experimentId() ) ) );
        }
        const QString experimentJson = experimentSelect.text( 0 );
        QJsonDocument doc;
        {
            QJsonParseError parseError;
            doc = QJsonDocument::fromJson( experimentJson.toUtf8(), &parseError );
            if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
            {
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.corrupt_record" ),
                                                    QStringLiteral( "experiment %1 record cannot be parsed" )
                                                        .arg( run.experimentId() ) ) );
            }
        }
        QJsonObject root = doc.object();
        QJsonArray runIds = root.value( QStringLiteral( "run_ids" ) ).toArray();
        bool present = false;
        for ( const QJsonValue &v : runIds )
        {
            if ( v.toString() == run.runId() )
            {
                present = true;
                break;
            }
        }
        if ( !present )
        {
            runIds.append( run.runId() );
            root.insert( QStringLiteral( "run_ids" ), runIds );
            Stmt experimentUpdate( db, QStringLiteral(
                "UPDATE experiments SET json=? WHERE id=?" ) );
            if ( !experimentUpdate )
            {
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    experimentUpdate.error( db ) ) );
            }
            experimentUpdate.bind( 1,
                                   QString::fromUtf8( QJsonDocument( root ).toJson( QJsonDocument::Compact ) ) );
            experimentUpdate.bind( 2, run.experimentId() );
            if ( !experimentUpdate.step() )
            {
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                    experimentUpdate.error( db ) ) );
            }
        }
    }
    return ResultT::success();
}

} // namespace

sicnu::data::Result<void> ExperimentStore::upsertRun( const ExperimentRun &run )
{
    // Unified-trace adapter (Verification Platform 8.0): the Experiment link
    // of the chain — one record per persisted run transition (the recorder's
    // Created → Running → terminal lifecycle lands here). Disabled path =
    // one relaxed atomic load.
    if ( sicnu::runtime::observability::trace::Trace::enabled() )
    {
        const auto started = std::chrono::steady_clock::now();
        const auto result = upsertRunImpl( run );
        sicnu::runtime::observability::trace::TraceEvent trace;
        trace.event = "experiment_upsert";
        trace.phase = "end";
        trace.run = run.runId().toStdString();
        trace.artifact = run.experimentId().toStdString();
        trace.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started ).count();
        trace.status = result ? "ok" : "error";
        sicnu::runtime::observability::trace::Trace::publish( trace );
        return result;
    }
    return upsertRunImpl( run );
}

sicnu::data::Result<void> ExperimentStore::upsertRunImpl( const ExperimentRun &run )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( run.runId().isEmpty() || run.experimentId().isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.invalid" ),
                                            QStringLiteral( "run requires run_id + experiment_id" ) ) );

    // #811: BEGIN IMMEDIATE takes the write lock up front, and the existing
    // run is read INSIDE the same transaction that writes. Validating before
    // the transaction left a TOCTOU window where a concurrent writer could
    // change the run between the transition/identity check and the upsert.
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );

    const auto written = writeRunInTxnLocked( m_impl->db, run );
    if ( !written )
    {
        m_impl->rollback();
        return written;
    }
    if ( SICNU_FAULT_POINT( "experiment_store.commit" ) )
    {
        // Injected commit failure (Verification Platform 8.0 fault matrix,
        // test-only arming): take exactly the real commit-failure branch and
        // roll the transaction back so the store stays consistent.
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        // A failed COMMIT can leave the transaction active (e.g. SQLITE_BUSY):
        // roll back explicitly so the shared connection never leaks its write
        // lock (same convention as the dataset store, #774).
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success();
}

namespace
{

/// Batch sweep of the unified-trace chain (12.0): the single-run upsert
/// records one event per transition; a batch sweep records one event for
/// the whole sweep so the trace stays proportional to the number of CALLS,
/// not to the batch size.
class BatchTraceEvent
{
  public:
    BatchTraceEvent( int batchSize, QString experimentId )
        : m_started( std::chrono::steady_clock::now() )
        , m_batchSize( batchSize )
        , m_experimentId( std::move( experimentId ) )
    {
    }

    void finish( bool ok )
    {
        if ( !sicnu::runtime::observability::trace::Trace::enabled() )
            return;
        sicnu::runtime::observability::trace::TraceEvent trace;
        trace.event = "experiment_upsert_batch";
        trace.phase = "end";
        trace.run = QString::number( m_batchSize ).toStdString();
        trace.artifact = m_experimentId.toStdString();
        trace.durationUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_started ).count();
        trace.status = ok ? "ok" : "error";
        sicnu::runtime::observability::trace::Trace::publish( trace );
    }

  private:
    std::chrono::steady_clock::time_point m_started;
    int m_batchSize = 0;
    QString m_experimentId;
};

} // namespace

sicnu::data::Result<void> ExperimentStore::upsertRunsBatch(
    const QVector<ExperimentRun> &runs )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    if ( runs.isEmpty() )
        return ResultT::success();
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );
    BatchTraceEvent trace( runs.size(), runs.first().experimentId() );
    for ( const ExperimentRun &run : runs )
    {
        const auto written = writeRunInTxnLocked( m_impl->db, run );
        if ( !written )
        {
            // All-or-nothing: the first invalid roll fails the whole batch,
            // so callers can never strand half a sweep of run rows.
            m_impl->rollback();
            trace.finish( false );
            return written;
        }
    }
    if ( SICNU_FAULT_POINT( "experiment_store.batch_commit" ) )
    {
        // Injected batch-commit failure (same contract as the single-run
        // point): take exactly the real commit-failure branch and roll the
        // whole batch back.
        m_impl->rollback();
        trace.finish( false );
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        trace.finish( false );
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    trace.finish( true );
    return ResultT::success();
}

std::optional<ExperimentRun> ExperimentStore::runById( const QString &runId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    return loadRunLocked( m_impl->db, runId );
}

sicnu::data::Result<QPair<qint64, QVector<ExperimentRun>>> ExperimentStore::listRuns(
    const QString &experimentId, const QString &datasetVersionId, const QString &status,
    qint64 offset, qint64 limit ) const
{
    using ResultT = sicnu::data::Result<QPair<qint64, QVector<ExperimentRun>>>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );

    QStringList conditions;
    if ( !experimentId.isEmpty() )
        conditions.append( QStringLiteral( "experiment_id=?" ) );
    if ( !datasetVersionId.isEmpty() )
        conditions.append( QStringLiteral( "dataset_version_id=?" ) );
    if ( !status.isEmpty() )
        conditions.append( QStringLiteral( "status=?" ) );
    const QString where =
        conditions.isEmpty() ? QString() : QStringLiteral( " WHERE " ) + conditions.join( QStringLiteral( " AND " ) );

    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        Stmt count( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM experiment_runs%1" ).arg( where ) );
        int bindIndex = 1;
        if ( !experimentId.isEmpty() )
            count.bind( bindIndex++, experimentId );
        if ( !datasetVersionId.isEmpty() )
            count.bind( bindIndex++, datasetVersionId );
        if ( !status.isEmpty() )
            count.bind( bindIndex++, status );
        if ( count.stepRow() )
            total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM experiment_runs%1 ORDER BY created_ms, run_id LIMIT ? OFFSET ?" )
                   .arg( where ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    int bindIndex = 1;
    if ( !experimentId.isEmpty() )
        stmt.bind( bindIndex++, experimentId );
    if ( !datasetVersionId.isEmpty() )
        stmt.bind( bindIndex++, datasetVersionId );
    if ( !status.isEmpty() )
        stmt.bind( bindIndex++, status );
    stmt.bind( bindIndex++, limit );
    stmt.bind( bindIndex++, offset );
    QVector<ExperimentRun> rows;
    while ( stmt.stepRow() )
    {
        auto parsed = ExperimentRun::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( !parsed )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.corrupt_record" ),
                                                parsed.diagnostics().first().message ) );
        rows.append( parsed.value() );
    }
    return ResultT::success( qMakePair( total, rows ) );
}

sicnu::data::Result<ExperimentStore::RunCursorPage> ExperimentStore::listRunsByCursor(
    const QString &experimentId, const QString &datasetVersionId, const QString &status,
    const QString &cursor, qint64 limit ) const
{
    using ResultT = sicnu::data::Result<RunCursorPage>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );

    // Resume point + filter echo. The cursor was minted for one exact filter
    // triple; replaying it under another must fail typed, never silently
    // resume over a different slice of the runs table.
    qint64 afterCreated = -1;
    QString afterRunId;
    if ( !cursor.isEmpty() )
    {
        const auto decoded = sicnu::data::QueryCursor::decode( cursor );
        if ( !decoded )
            return ResultT::failure( decoded.diagnostics() );
        const QStringList parts = decoded.value();
        if ( parts.size() != 5 || parts.at( 0 ) != experimentId ||
             parts.at( 1 ) != datasetVersionId || parts.at( 2 ) != status )
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.cursor_mismatch" ),
                QStringLiteral( "cursor was issued for a different run filter" ) ) );
        bool createdOk = false;
        afterCreated = parts.at( 3 ).toLongLong( &createdOk );
        if ( !createdOk )
            return ResultT::failure( storeDiag( QStringLiteral( "data.cursor_invalid" ),
                                                QStringLiteral( "cursor keyset is not numeric" ) ) );
        afterRunId = parts.at( 4 );
    }

    QStringList conditions;
    if ( !experimentId.isEmpty() )
        conditions.append( QStringLiteral( "experiment_id=?" ) );
    if ( !datasetVersionId.isEmpty() )
        conditions.append( QStringLiteral( "dataset_version_id=?" ) );
    if ( !status.isEmpty() )
        conditions.append( QStringLiteral( "status=?" ) );
    // The count is filter-scoped only — the keyset predicate must NOT narrow
    // it, `total` stays the full match count independent of the cursor.
    const QString filterWhere =
        conditions.isEmpty() ? QString() : QStringLiteral( " WHERE " ) + conditions.join( QStringLiteral( " AND " ) );
    QStringList scanConditions = conditions;
    if ( !cursor.isEmpty() )
        scanConditions.append( QStringLiteral(
            "(created_ms > ? OR (created_ms = ? AND run_id > ?))" ) );
    const QString where = scanConditions.isEmpty()
        ? QString()
        : QStringLiteral( " WHERE " ) + scanConditions.join( QStringLiteral( " AND " ) );

    QMutexLocker lock( &m_impl->mutex );
    RunCursorPage page;
    {
        Stmt count( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM experiment_runs%1" ).arg( filterWhere ) );
        if ( !count )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                count.error( m_impl->db ) ) );
        int bindIndex = 1;
        if ( !experimentId.isEmpty() )
            count.bind( bindIndex++, experimentId );
        if ( !datasetVersionId.isEmpty() )
            count.bind( bindIndex++, datasetVersionId );
        if ( !status.isEmpty() )
            count.bind( bindIndex++, status );
        if ( count.stepRow() )
            page.total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT created_ms, run_id, json FROM experiment_runs%1"
        " ORDER BY created_ms, run_id LIMIT ?" ).arg( where ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    int bindIndex = 1;
    if ( !experimentId.isEmpty() )
        stmt.bind( bindIndex++, experimentId );
    if ( !datasetVersionId.isEmpty() )
        stmt.bind( bindIndex++, datasetVersionId );
    if ( !status.isEmpty() )
        stmt.bind( bindIndex++, status );
    if ( !cursor.isEmpty() )
    {
        stmt.bind( bindIndex++, afterCreated );
        stmt.bind( bindIndex++, afterCreated );
        stmt.bind( bindIndex++, afterRunId );
    }
    stmt.bind( bindIndex++, limit );
    while ( stmt.stepRow() )
    {
        const qint64 createdMs = stmt.i64( 0 );
        const QString runId = stmt.text( 1 );
        auto parsed = ExperimentRun::fromJson( textToJson( stmt.text( 2 ) ) );
        if ( !parsed )
        {
            // Same fail-closed rule as listRuns: one corrupt row fails the
            // page loudly instead of shrinking the result set.
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.corrupt_record" ),
                                                parsed.diagnostics().first().message ) );
        }
        page.runs.append( parsed.value() );
        page.nextCursor = sicnu::data::QueryCursor::encode(
            { experimentId, datasetVersionId, status, QString::number( createdMs ), runId } );
    }
    if ( int( page.runs.size() ) < limit )
        page.nextCursor.clear(); // exhausted: the walk terminates cleanly
    return ResultT::success( page );
}

qint64 ExperimentStore::runCount() const
{
    if ( !m_impl )
        return -1; // closed store: fail closed (was 0, which looked empty)
    QMutexLocker lock( &m_impl->mutex );
    Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM experiment_runs" ) );
    if ( count && count.stepRow() )
        return count.i64( 0 );
    return -1; // query error: fail closed, not "empty"
}

QStringList ExperimentStore::runIdsByExecutionRef( const QString &executionRef,
                                                   qint64 limit ) const
{
    // The execution ref lives inside the run JSON (no dedicated column), so
    // this is a bounded paged scan, not an index lookup: it exists for
    // restart-time reconciliation, not per-event hot paths. Callers that
    // track executions live should keep their own ref→runId map (the bridge
    // does) and treat this as the cold-path fallback.
    QStringList ids;
    if ( !m_impl || executionRef.isEmpty() )
        return ids;
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    QMutexLocker lock( &m_impl->mutex );
    // One deferred read transaction so OFFSET pages share a single snapshot
    // under concurrent writers (skip/dup risk with per-page implicit snapshots).
    m_impl->exec( "BEGIN", nullptr );
    constexpr qint64 kPage = 200;
    qint64 lastCreatedMs = 0;
    QString lastRunId;
    bool pastFirstPage = false;
    // Keyset pagination after the first page: (created_ms, run_id) avoids
    // OFFSET drift entirely under concurrent writers.
    while ( qint64( ids.size() ) < limit )
    {
        Stmt stmt( m_impl->db,
                   !pastFirstPage
                       ? QStringLiteral(
                             "SELECT run_id, json, created_ms FROM experiment_runs"
                             " ORDER BY created_ms, run_id LIMIT ?" )
                       : QStringLiteral(
                             "SELECT run_id, json, created_ms FROM experiment_runs"
                             " WHERE (created_ms > ?) OR (created_ms = ? AND run_id > ?)"
                             " ORDER BY created_ms, run_id LIMIT ?" ) );
        if ( !stmt )
            break;
        if ( !pastFirstPage )
        {
            stmt.bind( 1, kPage );
        }
        else
        {
            stmt.bind( 1, lastCreatedMs );
            stmt.bind( 2, lastCreatedMs );
            stmt.bind( 3, lastRunId );
            stmt.bind( 4, kPage );
        }
        bool pageEmpty = true;
        int rows = 0;
        while ( stmt.stepRow() )
        {
            pageEmpty = false;
            ++rows;
            lastRunId = stmt.text( 0 );
            lastCreatedMs = stmt.i64( 2 );
            const QJsonObject row = textToJson( stmt.text( 1 ) );
            if ( row.value( QStringLiteral( "execution_ref" ) ).toString() == executionRef )
                ids.append( lastRunId );
            if ( qint64( ids.size() ) >= limit )
                break;
        }
        if ( pageEmpty || rows < kPage )
            break;
        pastFirstPage = true;
    }
    m_impl->exec( "COMMIT", nullptr );
    return ids;
}

QStringList ExperimentStore::runIdsByExecutionFingerprint( const QString &fingerprint,
                                                           qint64 limit ) const
{
    // Indexed column lookup (12.0): execution_fingerprint is stamped on every
    // run row by upsertRun, so the repeat-execution classifier can find
    // identity twins without scanning run JSON.
    QStringList ids;
    if ( !m_impl || fingerprint.isEmpty() )
        return ids;
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT run_id FROM experiment_runs WHERE execution_fingerprint=?"
        " ORDER BY created_ms, run_id LIMIT ?" ) );
    if ( !stmt )
        return ids;
    stmt.bind( 1, fingerprint );
    stmt.bind( 2, limit );
    while ( stmt.stepRow() )
        ids.append( stmt.text( 0 ) );
    return ids;
}

// --- run retention / prune (12.0) -----------------------------------------------

namespace
{

QJsonObject prunePolicyToJson( const ExperimentStore::RunPrunePolicy &policy )
{
    QJsonObject json;
    json.insert( QStringLiteral( "collapse_identity_twins" ), policy.collapseIdentityTwins );
    json.insert( QStringLiteral( "older_than" ), policy.olderThan.isValid()
        ? policy.olderThan.toString( Qt::ISODateWithMs ) : QString() );
    json.insert( QStringLiteral( "keep_promoted" ), policy.keepPromoted );
    json.insert( QStringLiteral( "keep_with_run_lineage" ), policy.keepWithRunLineage );
    json.insert( QStringLiteral( "keep_with_benchmark_citation" ), policy.keepWithBenchmarkCitation );
    return json;
}

bool runPromotedLocked( sqlite3 *db, const QString &runId )
{
    Stmt stmt( db, QStringLiteral( "SELECT 1 FROM model_promotions WHERE run_id=? LIMIT 1" ) );
    if ( !stmt )
        return true; // fail-conservative: unreadable state protects the run
    stmt.bind( 1, runId );
    return stmt.stepRow();
}

bool runHasLineageLocked( sqlite3 *db, const QString &runId )
{
    Stmt stmt( db, QStringLiteral(
        "SELECT 1 FROM experiment_lineage WHERE (from_kind='run' AND from_id=?)"
        " OR (to_kind='run' AND to_id=?) LIMIT 1" ) );
    if ( !stmt )
        return true;
    stmt.bind( 1, runId );
    stmt.bind( 2, runId );
    return stmt.stepRow();
}

/// True when an immutable benchmark_results JSON cites @p runId via
/// experiment_run_id (#1173). Fail-conservative on unreadable state.
bool runCitedByBenchmarkLocked( sqlite3 *db, const QString &runId )
{
    if ( runId.isEmpty() )
        return false;
    // Match the exact JSON string value produced by BenchmarkResult::toJson.
    Stmt stmt( db, QStringLiteral(
        "SELECT 1 FROM benchmark_results WHERE json LIKE ? LIMIT 1" ) );
    if ( !stmt )
        return true;
    const QString needle = QLatin1String( "%\"experiment_run_id\":\"" ) + runId + QLatin1String( "\"%" );
    stmt.bind( 1, needle );
    return stmt.stepRow();
}

/// Whether the CURRENT store state makes @p runId removable under @p policy.
/// Shared verbatim by plan and execute — that shared derivation IS the
/// dry-run/execute equivalence contract (Oracle O4).
bool runPruneEligibleLocked( sqlite3 *db, const QString &runId,
                             const QString &executionFingerprint, qint64 createdMs,
                             const ExperimentStore::RunPrunePolicy &policy )
{
    if ( policy.keepPromoted && runPromotedLocked( db, runId ) )
        return false;
    if ( policy.keepWithRunLineage && runHasLineageLocked( db, runId ) )
        return false;
    if ( policy.keepWithBenchmarkCitation && runCitedByBenchmarkLocked( db, runId ) )
        return false;
    if ( policy.olderThan.isValid() &&
         createdMs >= policy.olderThan.toMSecsSinceEpoch() )
        return false;
    if ( policy.collapseIdentityTwins )
    {
        // The newest twin survives; the keeper is never removable.
        Stmt newest( db, QStringLiteral(
            "SELECT run_id FROM experiment_runs WHERE execution_fingerprint=?"
            " ORDER BY created_ms DESC, run_id DESC LIMIT 1" ) );
        if ( !newest )
            return false;
        newest.bind( 1, executionFingerprint );
        if ( newest.stepRow() && newest.text( 0 ) == runId )
            return false;
    }
    return true;
}

} // namespace

QJsonObject ExperimentStore::RunPrunePolicy::toJson() const
{
    return prunePolicyToJson( *this );
}

QJsonObject ExperimentStore::RunPrunePlan::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), 1 );
    json.insert( QStringLiteral( "policy" ), policy.toJson() );
    json.insert( QStringLiteral( "scanned_runs" ), static_cast<double>( scannedRuns ) );
    json.insert( QStringLiteral( "run_ids" ), QJsonArray::fromStringList( runIds ) );
    return json;
}

sicnu::data::Result<ExperimentStore::RunPrunePlan> ExperimentStore::planRunPrune(
    const RunPrunePolicy &policy ) const
{
    using ResultT = sicnu::data::Result<RunPrunePlan>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    RunPrunePlan plan;
    QMutexLocker lock( &m_impl->mutex );
    constexpr qint64 kPage = 500;
    qint64 offset = 0;
    while ( true )
    {
        Stmt stmt( m_impl->db, QStringLiteral(
            "SELECT run_id, execution_fingerprint, created_ms FROM experiment_runs"
            " ORDER BY created_ms, run_id LIMIT ? OFFSET ?" ) );
        if ( !stmt )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                stmt.error( m_impl->db ) ) );
        stmt.bind( 1, kPage );
        stmt.bind( 2, offset );
        int rows = 0;
        while ( stmt.stepRow() )
        {
            ++rows;
            ++plan.scannedRuns;
            if ( runPruneEligibleLocked( m_impl->db, stmt.text( 0 ), stmt.text( 1 ),
                                         stmt.i64( 2 ), policy ) )
                plan.runIds.append( stmt.text( 0 ) );
        }
        if ( rows < int( kPage ) )
            break;
        offset += kPage;
    }
    plan.runIds.sort();
    plan.policy = policy;
    return ResultT::success( plan );
}

sicnu::data::Result<qint64> ExperimentStore::executeRunPrune( const RunPrunePlan &plan )
{
    using ResultT = sicnu::data::Result<qint64>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );

    // Every id is re-derived against the CURRENT state under the write lock
    // (the same eligibility predicate planRunPrune used): a stale plan only
    // ever shrinks — promotion evidence that appeared, lineage that was
    // created or a newer twin that took over all protect the run now.
    qint64 removed = 0;
    QSet<QString> removedIds;
    QSet<QString> affectedExperiments;
    for ( const QString &runId : plan.runIds )
    {
        QString experimentId;
        QString fingerprint;
        qint64 createdMs = 0;
        {
            Stmt row( m_impl->db, QStringLiteral(
                "SELECT experiment_id, execution_fingerprint, created_ms"
                " FROM experiment_runs WHERE run_id=?" ) );
            if ( !row )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    row.error( m_impl->db ) ) );
            }
            row.bind( 1, runId );
            if ( !row.stepRow() )
                continue; // already gone (concurrent execution): shrink
            experimentId = row.text( 0 );
            fingerprint = row.text( 1 );
            createdMs = row.i64( 2 );
        }
        if ( !runPruneEligibleLocked( m_impl->db, runId, fingerprint, createdMs,
                                      plan.policy ) )
            continue;
        {
            Stmt del( m_impl->db, QStringLiteral(
                "DELETE FROM experiment_runs WHERE run_id=?" ) );
            if ( !del )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    del.error( m_impl->db ) ) );
            }
            del.bind( 1, runId );
            if ( !del.step() )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                    del.error( m_impl->db ) ) );
            }
        }
        {
            Stmt metrics( m_impl->db, QStringLiteral(
                "DELETE FROM run_metrics WHERE run_id=?" ) );
            if ( !metrics )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    metrics.error( m_impl->db ) ) );
            }
            metrics.bind( 1, runId );
            if ( !metrics.step() )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                    metrics.error( m_impl->db ) ) );
            }
        }
        if ( !plan.policy.keepWithRunLineage )
        {
            // Explicitly requested: lineage edges of the pruned run go with
            // it, inside the same transaction (no dangling endpoints).
            Stmt edges( m_impl->db, QStringLiteral(
                "DELETE FROM experiment_lineage WHERE (from_kind='run' AND from_id=?)"
                " OR (to_kind='run' AND to_id=?)" ) );
            if ( !edges )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    edges.error( m_impl->db ) ) );
            }
            edges.bind( 1, runId );
            edges.bind( 2, runId );
            if ( !edges.step() )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                    edges.error( m_impl->db ) ) );
            }
        }
        if ( !plan.policy.keepPromoted )
        {
            // Explicitly requested: the promotion evidence rows of the pruned
            // run are discarded with it (policy choice, not a side effect).
            Stmt promotions( m_impl->db, QStringLiteral(
                "DELETE FROM model_promotions WHERE run_id=?" ) );
            if ( !promotions )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    promotions.error( m_impl->db ) ) );
            }
            promotions.bind( 1, runId );
            if ( !promotions.step() )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                    promotions.error( m_impl->db ) ) );
            }
        }
        removedIds.insert( runId );
        if ( !experimentId.isEmpty() )
            affectedExperiments.insert( experimentId );
        ++removed;
    }

    // Rewrite the affected experiments' run_id lists inside the same
    // transaction so no experiment JSON keeps citing a pruned run (phantom
    // reference prevention, Oracle O3/O4).
    for ( const QString &experimentId : affectedExperiments )
    {
        QString json;
        {
            Stmt row( m_impl->db, QStringLiteral(
                "SELECT json FROM experiments WHERE id=?" ) );
            if ( !row )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                    row.error( m_impl->db ) ) );
            }
            row.bind( 1, experimentId );
            if ( !row.stepRow() )
                continue; // experiment vanished: nothing to rewrite
            json = row.text( 0 );
        }
        const auto experiment = Experiment::fromJson( textToJson( json ) );
        if ( !experiment )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.corrupt_record" ),
                QStringLiteral( "experiment %1 does not parse; prune rolled back" )
                    .arg( experimentId ) ) );
        }
        QStringList kept = experiment->runIds();
        kept.removeAll( QString() ); // defensive: drop blanks while filtering
        const int before = kept.size();
        for ( const QString &prunedId : removedIds )
            kept.removeAll( prunedId );
        if ( kept.size() == before )
            continue; // list did not cite the pruned run
        Experiment updated = *experiment;
        updated.runIds() = kept;
        Stmt write( m_impl->db, QStringLiteral(
            "UPDATE experiments SET json=? WHERE id=?" ) );
        if ( !write )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                write.error( m_impl->db ) ) );
        }
        write.bind( 1, jsonToText( updated.toJson() ) );
        write.bind( 2, experimentId );
        if ( !write.step() )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                write.error( m_impl->db ) ) );
        }
    }

    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success( removed );
}

// --- metric records ---------------------------------------------------------------

sicnu::data::Result<void> ExperimentStore::saveMetricRecord( const MetricRecord &record )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    const auto validated = record.protocol.validate();
    if ( !validated )
        return ResultT::failure( validated.diagnostics() );

    const QString json = jsonToText( record.toJson() );
    {
        Stmt existing( m_impl->db, QStringLiteral( "SELECT json FROM run_metrics WHERE run_id=?" ) );
        if ( !existing )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                existing.error( m_impl->db ) ) );
        existing.bind( 1, record.runId );
        if ( existing.stepRow() )
        {
            if ( existing.text( 0 ) == json )
                return ResultT::success();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.conflict" ),
                                                QStringLiteral( "metrics for run %1 exist with different content" )
                                                    .arg( record.runId ) ) );
        }
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO run_metrics(run_id, dataset_version_id, json, created_ms)"
        " VALUES(?,?,?,?)" ) );
    if ( !insert )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    insert.bind( 1, record.runId );
    insert.bind( 2, record.protocol.datasetVersionId() );
    insert.bind( 3, json );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            insert.error( m_impl->db ) ) );
    return ResultT::success();
}

sicnu::data::Result<void> ExperimentStore::saveMetricRecordsBatch(
    const QVector<MetricRecord> &records )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    if ( records.isEmpty() )
        return ResultT::success();
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );
    for ( const MetricRecord &record : records )
    {
        const auto validated = record.protocol.validate();
        if ( !validated )
        {
            m_impl->rollback();
            return ResultT::failure( validated.diagnostics() );
        }
        const QString json = jsonToText( record.toJson() );
        {
            Stmt existing( m_impl->db, QStringLiteral(
                "SELECT json FROM run_metrics WHERE run_id=?" ) );
            if ( !existing )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag(
                    QStringLiteral( "experiment.store_query_failed" ),
                    existing.error( m_impl->db ) ) );
            }
            existing.bind( 1, record.runId );
            if ( existing.stepRow() && existing.text( 0 ) != json )
            {
                m_impl->rollback();
                return ResultT::failure( storeDiag(
                    QStringLiteral( "experiment.conflict" ),
                    QStringLiteral( "metrics for run %1 exist with different content" )
                        .arg( record.runId ) ) );
            }
        }
        Stmt insert( m_impl->db, QStringLiteral(
            "INSERT INTO run_metrics(run_id, dataset_version_id, json, created_ms)"
            " VALUES(?,?,?,?)"
            " ON CONFLICT(run_id) DO NOTHING" ) );
        if ( !insert )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                insert.error( m_impl->db ) ) );
        }
        insert.bind( 1, record.runId );
        insert.bind( 2, record.protocol.datasetVersionId() );
        insert.bind( 3, json );
        insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
        if ( !insert.step() )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                                insert.error( m_impl->db ) ) );
        }
    }
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success();
}

std::optional<MetricRecord> ExperimentStore::metricRecordForRun( const QString &runId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "SELECT json FROM run_metrics WHERE run_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, runId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = MetricRecord::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<MetricRecord>( parsed.value() ) : std::nullopt;
}

sicnu::data::Result<QPair<qint64, QVector<MetricRecord>>> ExperimentStore::listMetricRecords(
    const QString &datasetVersionId, qint64 offset, qint64 limit ) const
{
    using ResultT = sicnu::data::Result<QPair<qint64, QVector<MetricRecord>>>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );
    const QString where = datasetVersionId.isEmpty()
                              ? QString()
                              : QStringLiteral( " WHERE dataset_version_id=?" );
    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM run_metrics%1" ).arg( where ) );
        if ( !datasetVersionId.isEmpty() )
            count.bind( 1, datasetVersionId );
        if ( count.stepRow() )
            total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM run_metrics%1 ORDER BY created_ms, run_id LIMIT ? OFFSET ?" )
                   .arg( where ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    int bindIndex = 1;
    if ( !datasetVersionId.isEmpty() )
        stmt.bind( bindIndex++, datasetVersionId );
    stmt.bind( bindIndex++, limit );
    stmt.bind( bindIndex++, offset );
    QVector<MetricRecord> rows;
    while ( stmt.stepRow() )
    {
        auto parsed = MetricRecord::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( !parsed )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.corrupt_record" ),
                                                parsed.diagnostics().first().message ) );
        rows.append( parsed.value() );
    }
    return ResultT::success( qMakePair( total, rows ) );
}

// --- lineage -----------------------------------------------------------------------

sicnu::data::Result<void> ExperimentStore::addLineageEdge( const QString &fromKind,
                                                           const QString &fromId,
                                                           const QString &edgeKind,
                                                           const QString &toKind,
                                                           const QString &toId )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( fromKind.isEmpty() || fromId.isEmpty() || edgeKind.isEmpty() || toKind.isEmpty() ||
         toId.isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.invalid_argument" ),
                                            QStringLiteral( "lineage edges require kind+id on both ends" ) ) );
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT OR IGNORE INTO experiment_lineage(from_kind, from_id, edge_kind, to_kind, to_id, created_ms)"
        " VALUES(?,?,?,?,?,?)" ) );
    if ( !insert )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    insert.bind( 1, fromKind );
    insert.bind( 2, fromId );
    insert.bind( 3, edgeKind );
    insert.bind( 4, toKind );
    insert.bind( 5, toId );
    insert.bind( 6, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            insert.error( m_impl->db ) ) );
    return ResultT::success();
}

namespace
{

QVector<ExperimentStore::LineageEdge> queryEdgesLocked( sqlite3 *db, const QString &sql,
                                                        const QString &kind,
                                                        const QString &id, qint64 limit )
{
    QVector<ExperimentStore::LineageEdge> edges;
    Stmt stmt( db, sql );
    if ( !stmt )
        return edges;
    stmt.bind( 1, kind );
    stmt.bind( 2, id );
    stmt.bind( 3, limit );
    while ( stmt.stepRow() )
    {
        ExperimentStore::LineageEdge edge;
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

QVector<ExperimentStore::LineageEdge> ExperimentStore::allLineageEdges( qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    QVector<LineageEdge> edges;
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT from_kind, from_id, edge_kind, to_kind, to_id"
        " FROM experiment_lineage LIMIT ?" ) );
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

QVector<ExperimentStore::LineageEdge> ExperimentStore::outgoingEdges( const QString &kind,
                                                                      const QString &id,
                                                                      qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    return queryEdgesLocked( m_impl->db,
                             QStringLiteral(
                                 "SELECT from_kind, from_id, edge_kind, to_kind, to_id"
                                 " FROM experiment_lineage WHERE from_kind=? AND from_id=? LIMIT ?" ),
                             kind, id, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
}

QVector<ExperimentStore::LineageEdge> ExperimentStore::incomingEdges( const QString &kind,
                                                                      const QString &id,
                                                                      qint64 limit ) const
{
    if ( !m_impl )
        return {};
    QMutexLocker lock( &m_impl->mutex );
    return queryEdgesLocked( m_impl->db,
                             QStringLiteral(
                                 "SELECT from_kind, from_id, edge_kind, to_kind, to_id"
                                 " FROM experiment_lineage WHERE to_kind=? AND to_id=? LIMIT ?" ),
                             kind, id, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
}

sicnu::data::Result<void> ExperimentStore::savePromotionRecord( const PromotionRecord &record )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                           QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( record.promotionId.isEmpty() || record.runId.isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.promotion_invalid" ),
                                            QStringLiteral( "promotion record requires promotion_id + run_id" ) ) );

    // #1173: conflict check + insert inside BEGIN IMMEDIATE. The previous
    // SELECT-then-INSERT OR REPLACE could silently REPLACE concurrent approval
    // evidence from another connection — exactly what promotion_conflict exists
    // to prevent. Idempotent same-content saves still succeed without rewriting.
    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );
    {
        Stmt existing( m_impl->db, QStringLiteral(
            "SELECT json FROM model_promotions WHERE promotion_id=?" ) );
        if ( !existing )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                existing.error( m_impl->db ) ) );
        }
        existing.bind( 1, record.promotionId );
        if ( existing.stepRow() )
        {
            const auto parsed = PromotionRecord::fromJson(
                QJsonDocument::fromJson( existing.text( 0 ).toUtf8() ).object() );
            m_impl->rollback();
            if ( parsed && parsed.value() == record )
                return ResultT::success();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.promotion_conflict" ),
                QStringLiteral( "promotion %1 already exists with different content" )
                    .arg( record.promotionId ) ) );
        }
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO model_promotions(promotion_id, run_id, model_id,"
        " model_digest, dataset_version_id, verdict, decision, decided_by,"
        " decided_at_ms, created_ms, json) VALUES(?,?,?,?,?,?,?,?,?,?,?)" ) );
    if ( !insert )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    }
    insert.bind( 1, record.promotionId );
    insert.bind( 2, record.runId );
    insert.bind( 3, record.modelId );
    insert.bind( 4, record.modelDigest );
    insert.bind( 5, record.datasetVersionId );
    insert.bind( 6, record.verdict );
    insert.bind( 7, record.decision );
    insert.bind( 8, record.decidedBy );
    insert.bind( 9, record.decidedAtUtc.isValid() ? record.decidedAtUtc.toMSecsSinceEpoch() : 0 );
    insert.bind( 10, record.createdAtUtc.isValid() ? record.createdAtUtc.toMSecsSinceEpoch() : nowMs );
    insert.bind( 11, QString::fromUtf8( QJsonDocument( record.toJson() ).toJson() ) );
    if ( !insert.step() )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag(
            QStringLiteral( "experiment.promotion_conflict" ),
            QStringLiteral( "promotion %1 already exists with different content" )
                .arg( record.promotionId ) ) );
    }
    if ( !m_impl->commit( nullptr ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
    }
    return ResultT::success();
}

std::optional<PromotionRecord> ExperimentStore::promotionById( const QString &promotionId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM model_promotions WHERE promotion_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, promotionId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = PromotionRecord::fromJson(
        QJsonDocument::fromJson( stmt.text( 0 ).toUtf8() ).object() );
    if ( !parsed )
        return std::nullopt;
    return parsed.value();
}

QVector<PromotionRecord> ExperimentStore::promotionsForModel( const QString &modelId,
                                                              qint64 limit ) const
{
    QVector<PromotionRecord> records;
    if ( !m_impl )
        return records;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM model_promotions WHERE model_id=?"
        " ORDER BY created_ms, promotion_id LIMIT ?" ) );
    if ( !stmt )
        return records;
    stmt.bind( 1, modelId );
    stmt.bind( 2, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
    while ( stmt.stepRow() )
    {
        const auto record = PromotionRecord::fromJson(
            QJsonDocument::fromJson( stmt.text( 0 ).toUtf8() ).object() );
        if ( record.has_value() )
            records.append( record.value() );
    }
    return records;
}


// --- benchmark definitions / results (D19) --------------------------------------

sicnu::data::Result<void> ExperimentStore::saveBenchmarkDefinition(
    const BenchmarkDefinition &definition )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    const auto validated = definition.validate();
    if ( !validated )
        return ResultT::failure( validated.diagnostics() );

    const QString digest = definition.contentDigest();
    const QString json = jsonToText( definition.toJson() );
    {
        Stmt existing( m_impl->db, QStringLiteral(
            "SELECT content_digest, json FROM benchmark_definitions"
            " WHERE benchmark_id=? AND benchmark_version=?" ) );
        if ( !existing )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                existing.error( m_impl->db ) ) );
        existing.bind( 1, definition.benchmarkId() );
        existing.bind( 2, qint64( definition.benchmarkVersion() ) );
        if ( existing.stepRow() )
        {
            if ( existing.text( 0 ) == digest && existing.text( 1 ) == json )
                return ResultT::success();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.benchmark_conflict" ),
                QStringLiteral( "benchmark %1@%2 already published with different content" )
                    .arg( definition.benchmarkId() )
                    .arg( definition.benchmarkVersion() ) ) );
        }
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO benchmark_definitions(benchmark_id, benchmark_version,"
        " content_digest, json, created_ms) VALUES(?,?,?,?,?)" ) );
    if ( !insert )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    insert.bind( 1, definition.benchmarkId() );
    insert.bind( 2, qint64( definition.benchmarkVersion() ) );
    insert.bind( 3, digest );
    insert.bind( 4, json );
    insert.bind( 5, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            insert.error( m_impl->db ) ) );
    return ResultT::success();
}

std::optional<BenchmarkDefinition> ExperimentStore::benchmarkDefinition(
    const QString &benchmarkId, quint64 version ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM benchmark_definitions"
        " WHERE benchmark_id=? AND benchmark_version=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, benchmarkId );
    stmt.bind( 2, qint64( version ) );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = BenchmarkDefinition::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<BenchmarkDefinition>( parsed.value() ) : std::nullopt;
}

sicnu::data::Result<QPair<qint64, QVector<BenchmarkDefinition>>>
ExperimentStore::listBenchmarkDefinitions( qint64 offset, qint64 limit ) const
{
    using ResultT = sicnu::data::Result<QPair<qint64, QVector<BenchmarkDefinition>>>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    limit = qBound<qint64>( qint64( 1 ), limit, kMaxPageSize );
    offset = qMax<qint64>( 0, offset );
    QMutexLocker lock( &m_impl->mutex );
    qint64 total = 0;
    {
        Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM benchmark_definitions" ) );
        if ( !count )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                count.error( m_impl->db ) ) );
        if ( count.stepRow() )
            total = count.i64( 0 );
    }
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM benchmark_definitions"
        " ORDER BY created_ms, benchmark_id, benchmark_version LIMIT ? OFFSET ?" ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    stmt.bind( 1, limit );
    stmt.bind( 2, offset );
    QVector<BenchmarkDefinition> rows;
    while ( stmt.stepRow() )
    {
        auto parsed = BenchmarkDefinition::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( parsed )
            rows.append( parsed.value() );
    }
    return ResultT::success( qMakePair( total, rows ) );
}

sicnu::data::Result<void> ExperimentStore::saveBenchmarkResult( const BenchmarkResult &result )
{
    using ResultT = sicnu::data::Result<void>;
    if ( !m_impl )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_closed" ),
                                            QStringLiteral( "store is not open" ) ) );
    QMutexLocker lock( &m_impl->mutex );
    if ( isReadOnly() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_read_only" ),
                                            QStringLiteral( "store is read-only" ) ) );
    if ( result.resultId().isEmpty() || result.benchmarkId().isEmpty() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.benchmark_result_invalid" ),
                                            QStringLiteral( "result_id and benchmark_id required" ) ) );

    const QString json = jsonToText( result.toJson() );
    {
        Stmt existing( m_impl->db,
                       QStringLiteral( "SELECT json FROM benchmark_results WHERE result_id=?" ) );
        if ( !existing )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                existing.error( m_impl->db ) ) );
        existing.bind( 1, result.resultId() );
        if ( existing.stepRow() )
        {
            if ( existing.text( 0 ) == json )
                return ResultT::success();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.conflict" ),
                QStringLiteral( "benchmark result %1 exists with different content" )
                    .arg( result.resultId() ) ) );
        }
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO benchmark_results(result_id, benchmark_id, benchmark_version,"
        " json, created_ms) VALUES(?,?,?,?,?)" ) );
    if ( !insert )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    insert.bind( 1, result.resultId() );
    insert.bind( 2, result.benchmarkId() );
    insert.bind( 3, qint64( result.benchmarkVersion() ) );
    insert.bind( 4, json );
    insert.bind( 5, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            insert.error( m_impl->db ) ) );
    return ResultT::success();
}

std::optional<BenchmarkResult> ExperimentStore::benchmarkResultById( const QString &resultId ) const
{
    if ( !m_impl )
        return std::nullopt;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral( "SELECT json FROM benchmark_results WHERE result_id=?" ) );
    if ( !stmt )
        return std::nullopt;
    stmt.bind( 1, resultId );
    if ( !stmt.stepRow() )
        return std::nullopt;
    const auto parsed = BenchmarkResult::fromJson( textToJson( stmt.text( 0 ) ) );
    return parsed ? std::optional<BenchmarkResult>( parsed.value() ) : std::nullopt;
}

QVector<BenchmarkResult> ExperimentStore::benchmarkResultsFor( const QString &benchmarkId,
                                                               qint64 limit ) const
{
    QVector<BenchmarkResult> rows;
    if ( !m_impl )
        return rows;
    QMutexLocker lock( &m_impl->mutex );
    Stmt stmt( m_impl->db, QStringLiteral(
        "SELECT json FROM benchmark_results WHERE benchmark_id=?"
        " ORDER BY created_ms, result_id LIMIT ?" ) );
    if ( !stmt )
        return rows;
    stmt.bind( 1, benchmarkId );
    stmt.bind( 2, qBound<qint64>( qint64( 1 ), limit, qint64( 10000 ) ) );
    while ( stmt.stepRow() )
    {
        const auto parsed = BenchmarkResult::fromJson( textToJson( stmt.text( 0 ) ) );
        if ( parsed )
            rows.append( parsed.value() );
    }
    return rows;
}

} // namespace sicnu::experiment
