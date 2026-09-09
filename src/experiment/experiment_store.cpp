// experiment_store.cpp — SQLite implementation (mirrors dataset_store).
#include "experiment_store.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <sqlite3.h>

#include <QUuid>

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
         !m_impl->exec( "PRAGMA busy_timeout=5000", errorOut ) ||
         !m_impl->exec(
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

    QString existing;
    {
        QMutexLocker lock( &m_impl->mutex );
        Stmt stmt( db, QStringLiteral( "SELECT value FROM exp_meta WHERE key='schema_version'" ) );
        if ( stmt && stmt.stepRow() )
            existing = stmt.text( 0 );
    }
    if ( existing.isEmpty() )
    {
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
    else if ( existing != QLatin1String( kExperimentStoreSchemaVersion ) )
    {
        m_impl->readOnly = true;
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
    // Upsert semantics: an existing experiment's content is never silently
    // rewritten — name changes ride a new revision of the record via explicit
    // delete + create by the caller (experiments are immutable-ish registries).
    Stmt exists( m_impl->db, QStringLiteral( "SELECT json FROM experiments WHERE id=?" ) );
    if ( !exists )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            exists.error( m_impl->db ) ) );
    exists.bind( 1, experiment.experimentId() );
    if ( exists.stepRow() )
    {
        if ( exists.text( 0 ) == json )
            return ResultT::success();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.conflict" ),
                                            QStringLiteral( "experiment %1 exists with different content" )
                                                .arg( experiment.experimentId() ) ) );
    }
    Stmt insert( m_impl->db, QStringLiteral(
        "INSERT INTO experiments(id, name, json, created_ms) VALUES(?,?,?,?)" ) );
    if ( !insert )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            insert.error( m_impl->db ) ) );
    insert.bind( 1, experiment.experimentId() );
    insert.bind( 2, experiment.name() );
    insert.bind( 3, json );
    insert.bind( 4, QDateTime::currentMSecsSinceEpoch() );
    if ( !insert.step() )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            insert.error( m_impl->db ) ) );
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

    // An experiment with runs is never row-deleted.
    {
        Stmt runs( m_impl->db, QStringLiteral(
            "SELECT COUNT(*) FROM experiment_runs WHERE experiment_id=?" ) );
        if ( !runs )
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                runs.error( m_impl->db ) ) );
        runs.bind( 1, experimentId );
        if ( runs.stepRow() && runs.i64( 0 ) > 0 )
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.delete_refused" ),
                QStringLiteral( "experiment %1 still has runs" ).arg( experimentId ) ) );
    }
    Stmt stmt( m_impl->db, QStringLiteral( "DELETE FROM experiments WHERE id=?" ) );
    if ( !stmt )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            stmt.error( m_impl->db ) ) );
    stmt.bind( 1, experimentId );
    if ( !stmt.step() || stmt.changes( m_impl->db ) != 1 )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.not_found" ),
                                            QStringLiteral( "experiment %1 not found" ).arg( experimentId ) ) );
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

} // namespace

sicnu::data::Result<void> ExperimentStore::upsertRun( const ExperimentRun &run )
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

    if ( !m_impl->begin( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "cannot begin transaction" ) ) );

    const auto existing = loadRunLocked( m_impl->db, run.runId() );
    if ( existing && !isValidRunTransition( existing->status(), run.status() ) )
    {
        m_impl->rollback();
        return ResultT::failure( storeDiag(
            QStringLiteral( "experiment.bad_transition" ),
            QStringLiteral( "illegal status transition %1 → %2" )
                .arg( dataset::runStatusToString( existing->status() ),
                      dataset::runStatusToString( run.status() ) ) ) );
    }
    // Identity pins are immutable once the run started.
    if ( existing && existing->status() != RunStatus::Created )
    {
        if ( existing->executionIdentity() != run.executionIdentity() )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag(
                QStringLiteral( "experiment.identity_immutable" ),
                QStringLiteral( "run %1 already started with different identity pins" )
                    .arg( run.runId() ) ) );
        }
    }

    const QJsonObject json = run.toJson();
    const QString executionFingerprint =
        runExecutionFingerprint( run.executionIdentity() );
    const QString resultFingerprint = run.resultFingerprint();

    Stmt upsert( m_impl->db, QStringLiteral(
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
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                            upsert.error( m_impl->db ) ) );
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
        m_impl->rollback();
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            upsert.error( m_impl->db ) ) );
    }
    // Mirror the run row into the experiment's run list.
    {
        Stmt experimentExists( m_impl->db, QStringLiteral( "SELECT 1 FROM experiments WHERE id=?" ) );
        if ( !experimentExists )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_query_failed" ),
                                                experimentExists.error( m_impl->db ) ) );
        }
        experimentExists.bind( 1, run.experimentId() );
        if ( !experimentExists.stepRow() )
        {
            m_impl->rollback();
            return ResultT::failure( storeDiag( QStringLiteral( "experiment.not_found" ),
                                                QStringLiteral( "experiment %1 does not exist" )
                                                    .arg( run.experimentId() ) ) );
        }
    }
    if ( !m_impl->commit( nullptr ) )
        return ResultT::failure( storeDiag( QStringLiteral( "experiment.store_write_failed" ),
                                            QStringLiteral( "commit failed" ) ) );
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

qint64 ExperimentStore::runCount() const
{
    if ( !m_impl )
        return 0;
    QMutexLocker lock( &m_impl->mutex );
    Stmt count( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM experiment_runs" ) );
    if ( count && count.stepRow() )
        return count.i64( 0 );
    return 0;
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

} // namespace sicnu::experiment
