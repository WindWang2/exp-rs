// governance_store.cpp — see governance_store.h for the contract.
#include "governance_store.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <sqlite3.h>

#include <QHash>

#include <algorithm>
#include <mutex>

namespace sicnu::workspace
{

namespace
{

#define SICNU_GOVERNANCE_SCHEMA_VERSION "1"

// Column lists kept in one place so SELECTs and UPSERTs never drift.
#define GOV_ASSET_COLS \
    "asset_id, source_key, canonical_source, kind, state, persistence, display_name," \
    " parent_collection_id, acquisition_ms, revision, metadata_json, content_fingerprint," \
    " size_bytes, mtime_ms, verified_ms, format, crs, band_count, band_roles, sensor," \
    " modality, availability, created_ms, updated_ms"

Diagnostic govDiag( QString code, QString message, DiagnosticSeverity severity = DiagnosticSeverity::Error )
{
    return Diagnostic{ std::move( code ), std::move( message ), severity };
}

QString jsonToText( const QJsonObject &object )
{
    return QJsonDocument( object ).toJson( QJsonDocument::Compact );
}

QJsonObject textToJson( const QString &text )
{
    if ( text.isEmpty() )
        return {};
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson( text.toUtf8(), &error );
    return doc.object();
}

class Stmt
{
  public:
    Stmt( sqlite3 *db, const QString &sql )
    {
        if ( sqlite3_prepare_v2( db, sql.toUtf8().constData(), -1, &m_stmt, nullptr ) != SQLITE_OK )
            m_stmt = nullptr;
    }
    explicit Stmt( sqlite3 *db, const char *sql )
    {
        if ( sqlite3_prepare_v2( db, sql, -1, &m_stmt, nullptr ) != SQLITE_OK )
            m_stmt = nullptr;
    }
    ~Stmt() { if ( m_stmt ) sqlite3_finalize( m_stmt ); }
    Stmt( const Stmt & ) = delete;
    Stmt &operator=( const Stmt & ) = delete;
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
    QString text( int col ) const
    {
        const unsigned char *p = sqlite3_column_text( m_stmt, col );
        return p ? QString::fromUtf8( reinterpret_cast<const char *>( p ) ) : QString();
    }
    qint64 i64( int col ) const { return sqlite3_column_int64( m_stmt, col ); }

  private:
    sqlite3_stmt *m_stmt = nullptr;
};

QString likeContains( const QString &raw )
{
    return ( QString( raw )
                 .replace( QLatin1Char( '\\' ), QLatin1String( "\\\\" ) )
                 .replace( QLatin1Char( '%' ), QLatin1String( "\\%" ) )
                 .replace( QLatin1Char( '_' ), QLatin1String( "\\_" ) ) );
}

QStringList splitRoles( const QString &joined )
{
    QStringList out;
    for ( const QStringView part : QStringView( joined ).split( u',' ) )
    {
        const QString trimmed = part.trimmed().toString();
        if ( !trimmed.isEmpty() )
            out.append( trimmed );
    }
    return out;
}

} // namespace

struct GovernanceStore::Impl
{
    sqlite3 *db = nullptr;
    mutable std::mutex mutex;
    QString schemaVersion;
    bool readOnly = false;

    bool exec( const char *sql ) const
    {
        char *err = nullptr;
        if ( sqlite3_exec( db, sql, nullptr, nullptr, &err ) != SQLITE_OK )
        {
            sqlite3_free( err );
            return false;
        }
        return true;
    }

    bool exec( const QString &sql ) const { return exec( sql.toUtf8().constData() ); }

    bool createSchema()
    {
        exec( "PRAGMA journal_mode=WAL" );
        exec( "PRAGMA synchronous=NORMAL" );
        exec( "PRAGMA busy_timeout=5000" );

        const char *schema =
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS gov_meta("
            "  key TEXT PRIMARY KEY, value TEXT NOT NULL);"
            // Asset mirror + governance enrichment.
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
            "  content_fingerprint TEXT NOT NULL DEFAULT '',"
            "  size_bytes INTEGER NOT NULL DEFAULT -1,"
            "  mtime_ms INTEGER NOT NULL DEFAULT 0,"
            "  verified_ms INTEGER NOT NULL DEFAULT 0,"
            "  format TEXT NOT NULL DEFAULT '',"
            "  crs TEXT NOT NULL DEFAULT '',"
            "  band_count INTEGER NOT NULL DEFAULT -1,"
            "  band_roles TEXT NOT NULL DEFAULT '',"
            "  sensor TEXT NOT NULL DEFAULT '',"
            "  modality TEXT NOT NULL DEFAULT '',"
            "  availability TEXT NOT NULL DEFAULT 'unverified',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_kind_state ON assets(kind, state);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_sensor ON assets(sensor);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_modality ON assets(modality);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_acquisition ON assets(acquisition_ms);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_crs ON assets(crs);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_source ON assets(canonical_source);"
            "CREATE INDEX IF NOT EXISTS idx_gov_assets_fingerprint ON assets(content_fingerprint);"
            "CREATE TABLE IF NOT EXISTS aliases("
            "  path TEXT PRIMARY KEY,"
            "  asset_id TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS idx_gov_aliases_asset ON aliases(asset_id);"
            // Tags for ANY governed entity kind.
            "CREATE TABLE IF NOT EXISTS tags("
            "  entity_kind TEXT NOT NULL,"
            "  entity_id TEXT NOT NULL,"
            "  tag TEXT NOT NULL,"
            "  PRIMARY KEY(entity_kind, entity_id, tag));"
            "CREATE INDEX IF NOT EXISTS idx_gov_tags_tag ON tags(tag);"
            // Datasets (user/domain datasets; collections stay in DataManager).
            "CREATE TABLE IF NOT EXISTS datasets("
            "  dataset_id TEXT PRIMARY KEY,"
            "  kind TEXT NOT NULL DEFAULT 'group',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  revision INTEGER NOT NULL DEFAULT 1,"
            "  status TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS dataset_members("
            "  dataset_id TEXT NOT NULL,"
            "  asset_id TEXT NOT NULL,"
            "  position INTEGER NOT NULL DEFAULT 0,"
            "  PRIMARY KEY(dataset_id, asset_id));"
            // Results + lifecycle.
            "CREATE TABLE IF NOT EXISTS results("
            "  result_id TEXT PRIMARY KEY,"
            "  semantic_type TEXT NOT NULL DEFAULT 'other',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL DEFAULT 'draft',"
            "  revision INTEGER NOT NULL DEFAULT 1,"
            "  producer_json TEXT NOT NULL DEFAULT '',"
            "  run_id TEXT NOT NULL DEFAULT '',"
            "  metrics_json TEXT NOT NULL DEFAULT '',"
            "  quality_json TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  tags_json TEXT NOT NULL DEFAULT '',"
            "  superseded_by TEXT NOT NULL DEFAULT '',"
            "  validation_notes TEXT NOT NULL DEFAULT '',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS idx_gov_results_status ON results(status);"
            "CREATE INDEX IF NOT EXISTS idx_gov_results_semantic ON results(semantic_type);"
            "CREATE INDEX IF NOT EXISTS idx_gov_results_run ON results(run_id);"
            "CREATE TABLE IF NOT EXISTS result_inputs("
            "  result_id TEXT NOT NULL,"
            "  asset_id TEXT NOT NULL,"
            "  revision INTEGER NOT NULL DEFAULT 0,"
            "  role TEXT NOT NULL DEFAULT 'input',"
            "  PRIMARY KEY(result_id, asset_id, role));"
            "CREATE INDEX IF NOT EXISTS idx_gov_result_inputs_asset ON result_inputs(asset_id);"
            "CREATE TABLE IF NOT EXISTS result_artifacts("
            "  result_id TEXT NOT NULL,"
            "  path TEXT NOT NULL,"
            "  role TEXT NOT NULL DEFAULT 'primary',"
            "  digest TEXT NOT NULL DEFAULT '',"
            "  size_bytes INTEGER NOT NULL DEFAULT -1,"
            "  PRIMARY KEY(result_id, path, role));"
            // Run index (mirror of workflow checkpoints for queryability).
            "CREATE TABLE IF NOT EXISTS runs("
            "  run_id TEXT PRIMARY KEY,"
            "  workflow_id TEXT NOT NULL DEFAULT '',"
            "  state TEXT NOT NULL DEFAULT '',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  started_ms INTEGER NOT NULL DEFAULT 0,"
            "  finished_ms INTEGER NOT NULL DEFAULT 0,"
            "  definition_json TEXT NOT NULL DEFAULT '',"
            "  summary_json TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  tags_json TEXT NOT NULL DEFAULT '',"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS idx_gov_runs_workflow ON runs(workflow_id);"
            "CREATE TABLE IF NOT EXISTS run_outputs("
            "  run_id TEXT NOT NULL,"
            "  asset_id TEXT NOT NULL,"
            "  PRIMARY KEY(run_id, asset_id));"
            // Experiments.
            "CREATE TABLE IF NOT EXISTS experiments("
            "  experiment_id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL DEFAULT '',"
            "  objective TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  tags_json TEXT NOT NULL DEFAULT '',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS experiment_variants("
            "  experiment_id TEXT NOT NULL,"
            "  variant_key TEXT NOT NULL,"
            "  variant_json TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY(experiment_id, variant_key));"
            "CREATE TABLE IF NOT EXISTS experiment_runs("
            "  experiment_id TEXT NOT NULL,"
            "  run_id TEXT NOT NULL,"
            "  PRIMARY KEY(experiment_id, run_id));"
            // Queryable lineage graph.
            "CREATE TABLE IF NOT EXISTS lineage_edges("
            "  output_asset_id TEXT NOT NULL,"
            "  input_asset_id TEXT NOT NULL,"
            "  input_revision INTEGER NOT NULL DEFAULT 0,"
            "  operator_id TEXT NOT NULL DEFAULT '',"
            "  run_id TEXT NOT NULL DEFAULT '',"
            "  step_id TEXT NOT NULL DEFAULT '',"
            "  fingerprint TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY(output_asset_id, input_asset_id, operator_id, run_id, step_id));"
            "CREATE INDEX IF NOT EXISTS idx_gov_lineage_input ON lineage_edges(input_asset_id);"
            // Smart collections (dynamic predicates, no copied membership).
            "CREATE TABLE IF NOT EXISTS smart_collections("
            "  collection_id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL DEFAULT '',"
            "  predicate_json TEXT NOT NULL DEFAULT '[]',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            // Exports and relocation mappings.
            "CREATE TABLE IF NOT EXISTS exports("
            "  export_id TEXT PRIMARY KEY,"
            "  kind TEXT NOT NULL DEFAULT '',"
            "  target TEXT NOT NULL DEFAULT '',"
            "  result_id TEXT NOT NULL DEFAULT '',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '',"
            "  created_ms INTEGER NOT NULL DEFAULT 0,"
            "  updated_ms INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS path_mappings("
            "  kind TEXT NOT NULL,"
            "  from_path TEXT NOT NULL,"
            "  to_path TEXT NOT NULL,"
            "  PRIMARY KEY(kind, from_path));"
            // Audit log (append-only).
            "CREATE TABLE IF NOT EXISTS audit_log("
            "  seq INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts_ms INTEGER NOT NULL,"
            "  actor TEXT NOT NULL DEFAULT '',"
            "  action TEXT NOT NULL DEFAULT '',"
            "  entity_kind TEXT NOT NULL DEFAULT '',"
            "  entity_id TEXT NOT NULL DEFAULT '',"
            "  detail_json TEXT NOT NULL DEFAULT '');"
            "CREATE INDEX IF NOT EXISTS idx_gov_audit_entity ON audit_log(entity_kind, entity_id);"
            "INSERT OR REPLACE INTO gov_meta(key,value) VALUES('schema_version','"
            SICNU_GOVERNANCE_SCHEMA_VERSION "');"
            "COMMIT;";
        return exec( schema );
    }

    // --- readers ------------------------------------------------------------
    GovernedAsset readAsset( const Stmt &s ) const
    {
        GovernedAsset a;
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
        a.metadata = textToJson( s.text( 10 ) );
        a.contentFingerprint = s.text( 11 );
        a.sizeBytes = s.i64( 12 );
        a.mtimeMs = s.i64( 13 );
        a.verifiedMs = s.i64( 14 );
        a.format = s.text( 15 );
        a.crs = s.text( 16 );
        a.bandCount = s.i64( 17 );
        a.bandRoles = s.text( 18 );
        a.sensor = s.text( 19 );
        a.modality = s.text( 20 );
        a.availability = s.text( 21 );
        {
            Stmt al( db, "SELECT path FROM aliases WHERE asset_id=?" );
            al.bind( 1, a.assetId );
            while ( al.stepRow() )
                a.aliases.append( al.text( 0 ) );
            Stmt tg( db, "SELECT tag FROM tags WHERE entity_kind='asset' AND entity_id=?" );
            tg.bind( 1, a.assetId );
            while ( tg.stepRow() )
                a.tags.append( tg.text( 0 ) );
        }
        a.createdAtMs = s.i64( 22 );
        a.updatedAtMs = s.i64( 23 );
        return a;
    }

    DatasetRecord readDataset( const Stmt &s ) const
    {
        DatasetRecord d;
        d.id = DatasetId::fromString( s.text( 0 ) ).value_or( DatasetId() );
        d.kind = datasetKindFromString( s.text( 1 ) ).value_or( DatasetKind::Group );
        d.header.name = s.text( 2 );
        d.header.revision = static_cast<quint64>( s.i64( 3 ) );
        d.header.metadata = textToJson( s.text( 5 ) );
        d.header.createdAtMs = s.i64( 6 );
        d.header.updatedAtMs = s.i64( 7 );
        {
            Stmt m( db, "SELECT asset_id FROM dataset_members WHERE dataset_id=? ORDER BY position" );
            m.bind( 1, d.id.toString() );
            while ( m.stepRow() )
                d.memberAssetIds.append( m.text( 0 ) );
            Stmt tg( db, "SELECT tag FROM tags WHERE entity_kind='dataset' AND entity_id=?" );
            tg.bind( 1, d.id.toString() );
            while ( tg.stepRow() )
                d.header.tags.append( tg.text( 0 ) );
        }
        return d;
    }

    ResultRecord readResult( const Stmt &s ) const
    {
        ResultRecord r;
        r.id = ResultId::fromString( s.text( 0 ) ).value_or( ResultId() );
        r.semanticType = resultSemanticTypeFromString( s.text( 1 ) ).value_or( ResultSemanticType::Other );
        r.header.name = s.text( 2 );
        r.status = resultStatusFromString( s.text( 3 ) ).value_or( ResultStatus::Draft );
        r.header.revision = static_cast<quint64>( s.i64( 4 ) );
        r.producer = textToJson( s.text( 5 ) );
        r.metrics = textToJson( s.text( 7 ) );
        r.quality = textToJson( s.text( 8 ) );
        r.header.metadata = textToJson( s.text( 9 ) );
        r.header.tags = splitRoles( s.text( 10 ) );
        r.supersededBy = s.text( 11 );
        r.validationNotes = s.text( 12 );
        r.header.createdAtMs = s.i64( 13 );
        r.header.updatedAtMs = s.i64( 14 );
        {
            Stmt i( db, "SELECT asset_id, revision, role FROM result_inputs WHERE result_id=?" );
            i.bind( 1, r.id.toString() );
            while ( i.stepRow() )
            {
                ResultInput in;
                in.assetId = i.text( 0 );
                in.revision = static_cast<quint64>( i.i64( 1 ) );
                in.role = i.text( 2 );
                r.inputs.append( in );
            }
            Stmt a( db, "SELECT path, role, digest, size_bytes FROM result_artifacts WHERE result_id=?" );
            a.bind( 1, r.id.toString() );
            while ( a.stepRow() )
            {
                ResultArtifact art;
                art.path = a.text( 0 );
                art.role = a.text( 1 );
                art.contentDigest = a.text( 2 );
                art.sizeBytes = a.i64( 3 );
                r.artifacts.append( art );
            }
        }
        return r;
    }

    RunRecord readRun( const Stmt &s ) const
    {
        RunRecord r;
        r.id = s.text( 0 );
        r.workflowId = s.text( 1 );
        r.state = s.text( 2 );
        r.header.name = s.text( 3 );
        r.startedMs = s.i64( 4 );
        r.finishedMs = s.i64( 5 );
        r.definition = textToJson( s.text( 6 ) );
        r.summary = textToJson( s.text( 7 ) );
        r.header.metadata = textToJson( s.text( 8 ) );
        r.header.tags = splitRoles( s.text( 9 ) );
        r.header.updatedAtMs = s.i64( 10 );
        {
            Stmt o( db, "SELECT asset_id FROM run_outputs WHERE run_id=?" );
            o.bind( 1, r.id );
            while ( o.stepRow() )
                r.outputAssetIds.append( o.text( 0 ) );
        }
        return r;
    }

    ExperimentRecord readExperiment( const Stmt &s ) const
    {
        ExperimentRecord e;
        e.id = ExperimentId::fromString( s.text( 0 ) ).value_or( ExperimentId() );
        e.header.name = s.text( 1 );
        e.objective = s.text( 2 );
        e.header.metadata = textToJson( s.text( 3 ) );
        e.header.tags = splitRoles( s.text( 4 ) );
        e.header.createdAtMs = s.i64( 5 );
        e.header.updatedAtMs = s.i64( 6 );
        {
            Stmt v( db, "SELECT variant_key, variant_json FROM experiment_variants WHERE experiment_id=?" );
            v.bind( 1, e.id.toString() );
            while ( v.stepRow() )
            {
                ExperimentVariant variant;
                variant.key = v.text( 0 );
                variant.value = textToJson( v.text( 1 ) );
                e.variants.append( variant );
            }
            Stmt r( db, "SELECT run_id FROM experiment_runs WHERE experiment_id=?" );
            r.bind( 1, e.id.toString() );
            while ( r.stepRow() )
                e.runIds.append( r.text( 0 ) );
        }
        return e;
    }

    SmartCollectionRecord readSmart( const Stmt &s ) const
    {
        SmartCollectionRecord c;
        c.id = SmartCollectionId::fromString( s.text( 0 ) ).value_or( SmartCollectionId() );
        c.header.name = s.text( 1 );
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson( s.text( 2 ).toUtf8(), &error );
        const QJsonArray predicates = doc.object().value( QLatin1String( "predicates" ) ).toArray();
        for ( const QJsonValue &v : predicates )
            c.predicates.append( SmartPredicate{ v.toObject().value( QLatin1String( "field" ) ).toString(),
                                                 v.toObject().value( QLatin1String( "op" ) ).toString(),
                                                 v.toObject().value( QLatin1String( "value" ) ).toString() } );
        c.header.metadata = textToJson( s.text( 3 ) );
        return c;
    }

    ExportRecord readExport( const Stmt &s ) const
    {
        ExportRecord e;
        e.id = ExportId::fromString( s.text( 0 ) ).value_or( ExportId() );
        e.kind = s.text( 1 );
        e.target = s.text( 2 );
        e.resultId = s.text( 3 );
        e.header.name = s.text( 4 );
        e.header.metadata = textToJson( s.text( 5 ) );
        e.header.createdAtMs = s.i64( 6 );
        return e;
    }
};

GovernanceStore::~GovernanceStore()
{
    close();
}

bool GovernanceStore::open( const QString &dbPath, QString *errorOut )
{
    close();
    m_impl = new Impl;
    if ( sqlite3_open_v2( dbPath.toUtf8().constData(), &m_impl->db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr ) != SQLITE_OK )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot open governance store %1" ).arg( dbPath );
        close();
        return false;
    }
    // Forward tolerance: a newer schema opens read-only, never auto-deleted.
    {
        Stmt v( m_impl->db, "SELECT value FROM gov_meta WHERE key='schema_version'" );
        if ( v && v.stepRow() )
        {
            bool ok = false;
            const int version = v.text( 0 ).toInt( &ok );
            if ( ok && version > 1 )
            {
                m_impl->readOnly = true;
                m_impl->schemaVersion = v.text( 0 );
                if ( errorOut )
                    *errorOut = QStringLiteral( "governance schema %1 is newer than supported; opened read-only" )
                                    .arg( v.text( 0 ) );
                return true;
            }
        }
    }
    if ( !m_impl->createSchema() )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "cannot create governance schema" );
        close();
        return false;
    }
    m_impl->schemaVersion = QStringLiteral( SICNU_GOVERNANCE_SCHEMA_VERSION );
    return true;
}

void GovernanceStore::close()
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

bool GovernanceStore::isReadOnly() const
{
    return m_impl ? m_impl->readOnly : false;
}

QString GovernanceStore::schemaVersion() const
{
    return m_impl ? m_impl->schemaVersion : QString();
}

Result<void> GovernanceStore::upsertAsset( const GovernedAsset &asset )
{
    return upsertAssets( { asset } );
}

Result<void> GovernanceStore::upsertAssets( const QVector<GovernedAsset> &assets )
{
    if ( !m_impl )
        return Result<void>::failure( govDiag( QStringLiteral( "store.closed" ), QStringLiteral( "governance store not open" ) ) );
    if ( m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.read_only" ), QStringLiteral( "store opened read-only (newer schema)" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt up( m_impl->db,
            "INSERT INTO assets(" GOV_ASSET_COLS ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(asset_id) DO UPDATE SET"
            "  source_key=excluded.source_key, canonical_source=excluded.canonical_source,"
            "  kind=excluded.kind, state=excluded.state, persistence=excluded.persistence,"
            "  display_name=excluded.display_name, parent_collection_id=excluded.parent_collection_id,"
            "  acquisition_ms=excluded.acquisition_ms, revision=excluded.revision,"
            "  metadata_json=excluded.metadata_json, content_fingerprint=excluded.content_fingerprint,"
            "  size_bytes=excluded.size_bytes, mtime_ms=excluded.mtime_ms,"
            "  verified_ms=excluded.verified_ms, format=excluded.format, crs=excluded.crs,"
            "  band_count=excluded.band_count, band_roles=excluded.band_roles,"
            "  sensor=excluded.sensor, modality=excluded.modality,"
            "  availability=excluded.availability, updated_ms=excluded.updated_ms" );
        Stmt delAlias( m_impl->db, "DELETE FROM aliases WHERE asset_id=?" );
        Stmt insAlias( m_impl->db, "INSERT OR REPLACE INTO aliases(path, asset_id) VALUES(?,?)" );
        if ( !up || !delAlias || !insAlias )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "asset upsert prepare failed" ) ) );
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for ( const GovernedAsset &asset : assets )
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
            up.bind( 11, jsonToText( asset.metadata ) );
            up.bind( 12, asset.contentFingerprint );
            up.bind( 13, asset.sizeBytes );
            up.bind( 14, asset.mtimeMs );
            up.bind( 15, asset.verifiedMs );
            up.bind( 16, asset.format );
            up.bind( 17, asset.crs );
            up.bind( 18, asset.bandCount );
            up.bind( 19, asset.bandRoles );
            up.bind( 20, asset.sensor );
            up.bind( 21, asset.modality );
            up.bind( 22, asset.availability.isEmpty() ? QStringLiteral( "unverified" ) : asset.availability );
            // ON CONFLICT deliberately leaves created_ms untouched, so the
            // original creation stamp survives every update.
            up.bind( 23, now );
            up.bind( 24, now );
            if ( !up.step() )
            {
                m_impl->exec( "ROLLBACK" );
                return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_asset" ),
                                                       QStringLiteral( "asset %1 failed" ).arg( asset.assetId ) ) );
            }
            delAlias.reset();
            delAlias.bind( 1, asset.assetId );
            delAlias.step();
            insAlias.reset();
            insAlias.bind( 1, asset.canonicalSource );
            insAlias.bind( 2, asset.assetId );
            insAlias.step();
            for ( const QString &alias : asset.aliases )
            {
                if ( alias == asset.canonicalSource || alias.isEmpty() )
                    continue;
                insAlias.reset();
                insAlias.bind( 1, alias );
                insAlias.bind( 2, asset.assetId );
                insAlias.step();
            }
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

Result<void> GovernanceStore::removeAsset( const QString &assetId )
{
    if ( !m_impl )
        return Result<void>::failure( govDiag( QStringLiteral( "store.closed" ), QStringLiteral( "governance store not open" ) ) );
    if ( m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.read_only" ), QStringLiteral( "store opened read-only" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt a( m_impl->db, "DELETE FROM assets WHERE asset_id=?" );
        Stmt al( m_impl->db, "DELETE FROM aliases WHERE asset_id=?" );
        Stmt t( m_impl->db, "DELETE FROM tags WHERE entity_kind='asset' AND entity_id=?" );
        Stmt li( m_impl->db, "DELETE FROM lineage_edges WHERE input_asset_id=?" );
        Stmt lo( m_impl->db, "DELETE FROM lineage_edges WHERE output_asset_id=?" );
        if ( !a || !al || !t || !li || !lo )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "remove prepare failed" ) ) );
        }
        a.bind( 1, assetId );
        a.step();
        const bool removed = sqlite3_changes( m_impl->db ) > 0;
        al.bind( 1, assetId );
        al.step();
        t.bind( 1, assetId );
        t.step();
        li.bind( 1, assetId );
        li.step();
        lo.bind( 1, assetId );
        lo.step();
        m_impl->exec( "COMMIT" );
        if ( !removed )
            return Result<void>::failure( govDiag( QStringLiteral( "store.unknown_asset" ),
                                                   QStringLiteral( "no asset %1" ).arg( assetId ) ) );
    }
    return Result<void>::success();
}

std::optional<GovernedAsset> GovernanceStore::assetById( const QString &assetId ) const
{
    if ( !m_impl || assetId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT " GOV_ASSET_COLS " FROM assets WHERE asset_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, assetId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readAsset( s );
}

std::optional<GovernedAsset> GovernanceStore::assetByPath( const QString &path ) const
{
    if ( !m_impl || path.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
            "SELECT a.asset_id, a.source_key, a.canonical_source, a.kind, a.state, a.persistence,"
            " a.display_name, a.parent_collection_id, a.acquisition_ms, a.revision, a.metadata_json,"
            " a.content_fingerprint, a.size_bytes, a.mtime_ms, a.verified_ms, a.format, a.crs,"
            " a.band_count, a.band_roles, a.sensor, a.modality, a.availability, a.created_ms, a.updated_ms"
            " FROM assets a JOIN aliases al ON al.asset_id=a.asset_id WHERE al.path=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, path );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readAsset( s );
}

QVector<GovernedAsset> GovernanceStore::assetsByFingerprint( const QString &digest, qint64 limit ) const
{
    QVector<GovernedAsset> out;
    if ( !m_impl || digest.isEmpty() )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
            "SELECT " GOV_ASSET_COLS " FROM assets WHERE content_fingerprint=? LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, digest );
    s.bind( 2, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readAsset( s ) );
    return out;
}

QVector<GovernedAsset> GovernanceStore::allAssets( qint64 limit ) const
{
    QVector<GovernedAsset> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    if ( limit <= 0 )
    {
        Stmt s( m_impl->db, "SELECT " GOV_ASSET_COLS " FROM assets ORDER BY updated_ms DESC, asset_id" );
        if ( s )
            while ( s.stepRow() )
                out.append( m_impl->readAsset( s ) );
        return out;
    }
    Stmt s( m_impl->db, "SELECT " GOV_ASSET_COLS " FROM assets ORDER BY updated_ms DESC, asset_id LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, limit );
    while ( s.stepRow() )
        out.append( m_impl->readAsset( s ) );
    return out;
}

qint64 GovernanceStore::assetCount() const
{
    if ( !m_impl )
        return 0;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT COUNT(*) FROM assets" );
    return s.stepRow() ? s.i64( 0 ) : 0;
}

// --- tags -------------------------------------------------------------------

Result<void> GovernanceStore::setTags( const QString &entityKind, const QString &entityId, const QStringList &tags )
{
    if ( !m_impl )
        return Result<void>::failure( govDiag( QStringLiteral( "store.closed" ), QStringLiteral( "governance store not open" ) ) );
    if ( m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.read_only" ), QStringLiteral( "store opened read-only" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt del( m_impl->db, "DELETE FROM tags WHERE entity_kind=? AND entity_id=?" );
        Stmt ins( m_impl->db, "INSERT OR IGNORE INTO tags(entity_kind, entity_id, tag) VALUES(?,?,?)" );
        if ( !del || !ins )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "tag prepare failed" ) ) );
        }
        del.bind( 1, entityKind );
        del.bind( 2, entityId );
        del.step();
        for ( const QString &tag : tags )
        {
            if ( tag.isEmpty() )
                continue;
            ins.reset();
            ins.bind( 1, entityKind );
            ins.bind( 2, entityId );
            ins.bind( 3, tag );
            ins.step();
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

Result<void> GovernanceStore::addTag( const QString &entityKind, const QString &entityId, const QString &tag )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt ins( m_impl->db, "INSERT OR IGNORE INTO tags(entity_kind, entity_id, tag) VALUES(?,?,?)" );
    if ( !ins )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "tag prepare failed" ) ) );
    ins.bind( 1, entityKind );
    ins.bind( 2, entityId );
    ins.bind( 3, tag );
    m_impl->exec( "BEGIN IMMEDIATE" );
    ins.step();
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

QStringList GovernanceStore::tagsOf( const QString &entityKind, const QString &entityId ) const
{
    QStringList out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT tag FROM tags WHERE entity_kind=? AND entity_id=?" );
    if ( !s )
        return out;
    s.bind( 1, entityKind );
    s.bind( 2, entityId );
    while ( s.stepRow() )
        out.append( s.text( 0 ) );
    return out;
}

QVector<GovernanceStore::TagRow> GovernanceStore::allTags() const
{
    QVector<TagRow> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT entity_kind, entity_id, tag FROM tags ORDER BY entity_kind, entity_id, tag" );
    if ( !s )
        return out;
    while ( s.stepRow() )
        out.append( TagRow{ s.text( 0 ), s.text( 1 ), s.text( 2 ) } );
    return out;
}

Result<qint64> GovernanceStore::bulkTag( const QVector<QString> &entityIds, const QString &entityKind, const QString &tag )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<qint64>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    if ( tag.isEmpty() )
        return Result<qint64>::failure( govDiag( QStringLiteral( "store.empty_tag" ), QStringLiteral( "tag must not be empty" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    qint64 touched = 0;
    {
        Stmt ins( m_impl->db, "INSERT OR IGNORE INTO tags(entity_kind, entity_id, tag) VALUES(?,?,?)" );
        if ( !ins )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<qint64>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "bulk tag prepare failed" ) ) );
        }
        for ( const QString &id : entityIds )
        {
            ins.reset();
            ins.bind( 1, entityKind );
            ins.bind( 2, id );
            ins.bind( 3, tag );
            ins.step();
            touched += sqlite3_changes( m_impl->db );
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<qint64>::success( touched );
}

// --- datasets ---------------------------------------------------------------

Result<void> GovernanceStore::upsertDataset( const DatasetRecord &dataset )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString idText = dataset.id.toString();
        Stmt up( m_impl->db,
            "INSERT INTO datasets(dataset_id, kind, name, revision, status, metadata_json, created_ms, updated_ms)"
            " VALUES(?,?,?,?,?,?,?,?)"
            " ON CONFLICT(dataset_id) DO UPDATE SET kind=excluded.kind, name=excluded.name,"
            " revision=excluded.revision, status=excluded.status, metadata_json=excluded.metadata_json,"
            " updated_ms=excluded.updated_ms" );
        Stmt clear( m_impl->db, "DELETE FROM dataset_members WHERE dataset_id=?" );
        Stmt ins( m_impl->db, "INSERT OR REPLACE INTO dataset_members(dataset_id, asset_id, position) VALUES(?,?,?)" );
        Stmt fix( m_impl->db, "UPDATE datasets SET created_ms=? WHERE dataset_id=? AND created_ms=0" );
        if ( !up || !clear || !ins || !fix )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "dataset prepare failed" ) ) );
        }
        up.bind( 1, idText );
        up.bind( 2, datasetKindToString( dataset.kind ) );
        up.bind( 3, dataset.header.name );
        up.bind( 4, static_cast<qint64>( dataset.header.revision ) );
        up.bind( 5, QString() );
        up.bind( 6, jsonToText( dataset.header.metadata ) );
        up.bind( 7, now );
        up.bind( 8, now );
        if ( !up.step() )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_dataset" ), QStringLiteral( "dataset upsert failed" ) ) );
        }
        fix.bind( 1, now );
        fix.bind( 2, idText );
        fix.step();
        clear.bind( 1, idText );
        clear.step();
        int position = 0;
        for ( const QString &member : dataset.memberAssetIds )
        {
            ins.reset();
            ins.bind( 1, idText );
            ins.bind( 2, member );
            ins.bind( 3, position++ );
            ins.step();
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

Result<void> GovernanceStore::removeDataset( const QString &datasetId )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt d( m_impl->db, "DELETE FROM datasets WHERE dataset_id=?" );
        Stmt m( m_impl->db, "DELETE FROM dataset_members WHERE dataset_id=?" );
        d.bind( 1, datasetId );
        d.step();
        const bool removed = sqlite3_changes( m_impl->db ) > 0;
        m.bind( 1, datasetId );
        m.step();
        m_impl->exec( "COMMIT" );
        if ( !removed )
            return Result<void>::failure( govDiag( QStringLiteral( "store.unknown_dataset" ),
                                                   QStringLiteral( "no dataset %1" ).arg( datasetId ) ) );
    }
    return Result<void>::success();
}

std::optional<DatasetRecord> GovernanceStore::datasetById( const QString &datasetId ) const
{
    if ( !m_impl || datasetId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT dataset_id, kind, name, revision, status, metadata_json, created_ms, updated_ms"
                        " FROM datasets WHERE dataset_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, datasetId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readDataset( s );
}

QVector<DatasetRecord> GovernanceStore::datasets( qint64 limit ) const
{
    QVector<DatasetRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT dataset_id, kind, name, revision, status, metadata_json, created_ms, updated_ms"
                        " FROM datasets ORDER BY updated_ms DESC, dataset_id LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readDataset( s ) );
    return out;
}

// --- results ----------------------------------------------------------------

Result<void> GovernanceStore::upsertResult( const ResultRecord &result )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString idText = result.id.toString();
        Stmt up( m_impl->db,
            "INSERT INTO results(result_id, semantic_type, name, status, revision, producer_json, run_id,"
            " metrics_json, quality_json, metadata_json, tags_json, superseded_by, validation_notes,"
            " created_ms, updated_ms) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(result_id) DO UPDATE SET semantic_type=excluded.semantic_type,"
            " name=excluded.name, status=excluded.status, revision=excluded.revision,"
            " producer_json=excluded.producer_json, run_id=excluded.run_id,"
            " metrics_json=excluded.metrics_json, quality_json=excluded.quality_json,"
            " metadata_json=excluded.metadata_json, tags_json=excluded.tags_json,"
            " superseded_by=excluded.superseded_by, validation_notes=excluded.validation_notes,"
            " updated_ms=excluded.updated_ms" );
        Stmt clearIn( m_impl->db, "DELETE FROM result_inputs WHERE result_id=?" );
        Stmt insIn( m_impl->db, "INSERT OR REPLACE INTO result_inputs(result_id, asset_id, revision, role) VALUES(?,?,?,?)" );
        Stmt clearArt( m_impl->db, "DELETE FROM result_artifacts WHERE result_id=?" );
        Stmt insArt( m_impl->db, "INSERT OR REPLACE INTO result_artifacts(result_id, path, role, digest, size_bytes) VALUES(?,?,?,?,?)" );
        Stmt fix( m_impl->db, "UPDATE results SET created_ms=? WHERE result_id=? AND created_ms=0" );
        if ( !up || !clearIn || !insIn || !clearArt || !insArt || !fix )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "result prepare failed" ) ) );
        }
        up.bind( 1, idText );
        up.bind( 2, resultSemanticTypeToString( result.semanticType ) );
        up.bind( 3, result.header.name );
        up.bind( 4, resultStatusToString( result.status ) );
        up.bind( 5, static_cast<qint64>( result.header.revision ) );
        up.bind( 6, jsonToText( result.producer ) );
        up.bind( 7, result.producer.value( QLatin1String( "runId" ) ).toString() );
        up.bind( 8, jsonToText( result.metrics ) );
        up.bind( 9, jsonToText( result.quality ) );
        up.bind( 10, jsonToText( result.header.metadata ) );
        up.bind( 11, result.header.tags.join( QLatin1Char( ',' ) ) );
        up.bind( 12, result.supersededBy );
        up.bind( 13, result.validationNotes );
        up.bind( 14, now );
        up.bind( 15, now );
        if ( !up.step() )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_result" ), QStringLiteral( "result upsert failed" ) ) );
        }
        fix.bind( 1, now );
        fix.bind( 2, idText );
        fix.step();
        clearIn.bind( 1, idText );
        clearIn.step();
        for ( const ResultInput &in : result.inputs )
        {
            insIn.reset();
            insIn.bind( 1, idText );
            insIn.bind( 2, in.assetId );
            insIn.bind( 3, static_cast<qint64>( in.revision ) );
            insIn.bind( 4, in.role );
            insIn.step();
        }
        clearArt.bind( 1, idText );
        clearArt.step();
        for ( const ResultArtifact &art : result.artifacts )
        {
            insArt.reset();
            insArt.bind( 1, idText );
            insArt.bind( 2, art.path );
            insArt.bind( 3, art.role );
            insArt.bind( 4, art.contentDigest );
            insArt.bind( 5, art.sizeBytes );
            insArt.step();
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

Result<void> GovernanceStore::removeResult( const QString &resultId )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt r( m_impl->db, "DELETE FROM results WHERE result_id=?" );
        Stmt i( m_impl->db, "DELETE FROM result_inputs WHERE result_id=?" );
        Stmt a( m_impl->db, "DELETE FROM result_artifacts WHERE result_id=?" );
        r.bind( 1, resultId );
        r.step();
        const bool removed = sqlite3_changes( m_impl->db ) > 0;
        i.bind( 1, resultId );
        i.step();
        a.bind( 1, resultId );
        a.step();
        m_impl->exec( "COMMIT" );
        if ( !removed )
            return Result<void>::failure( govDiag( QStringLiteral( "store.unknown_result" ),
                                                   QStringLiteral( "no result %1" ).arg( resultId ) ) );
    }
    return Result<void>::success();
}

std::optional<ResultRecord> GovernanceStore::resultById( const QString &resultId ) const
{
    if ( !m_impl || resultId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT result_id, semantic_type, name, status, revision, producer_json, run_id,"
                        " metrics_json, quality_json, metadata_json, tags_json, superseded_by,"
                        " validation_notes, created_ms, updated_ms FROM results WHERE result_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, resultId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readResult( s );
}

QVector<ResultRecord> GovernanceStore::results( qint64 limit ) const
{
    QVector<ResultRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT result_id, semantic_type, name, status, revision, producer_json, run_id,"
                        " metrics_json, quality_json, metadata_json, tags_json, superseded_by,"
                        " validation_notes, created_ms, updated_ms FROM results"
                        " ORDER BY updated_ms DESC, result_id LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readResult( s ) );
    return out;
}

QVector<ResultRecord> GovernanceStore::resultsDependingOnAsset( const QString &assetId ) const
{
    QVector<ResultRecord> out;
    if ( !m_impl || assetId.isEmpty() )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
            "SELECT DISTINCT r.result_id, r.semantic_type, r.name, r.status, r.revision, r.producer_json,"
            " r.run_id, r.metrics_json, r.quality_json, r.metadata_json, r.tags_json, r.superseded_by,"
            " r.validation_notes, r.created_ms, r.updated_ms FROM results r"
            " JOIN result_inputs i ON i.result_id=r.result_id WHERE i.asset_id=?" );
    if ( !s )
        return out;
    s.bind( 1, assetId );
    while ( s.stepRow() )
        out.append( m_impl->readResult( s ) );
    return out;
}

QVector<ResultRecord> GovernanceStore::orphanResults() const
{
    // An orphan result has neither a producing run row nor a registered
    // producing operator anchor in its producer JSON.
    QVector<ResultRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
            "SELECT r.result_id, r.semantic_type, r.name, r.status, r.revision, r.producer_json,"
            " r.run_id, r.metrics_json, r.quality_json, r.metadata_json, r.tags_json, r.superseded_by,"
            " r.validation_notes, r.created_ms, r.updated_ms FROM results r"
            " LEFT JOIN runs ru ON ru.run_id=r.run_id"
            " WHERE r.run_id<>'' AND ru.run_id IS NULL"
            " ORDER BY r.updated_ms DESC" );
    if ( !s )
        return out;
    while ( s.stepRow() )
        out.append( m_impl->readResult( s ) );
    return out;
}

// --- runs -------------------------------------------------------------------

Result<void> GovernanceStore::upsertRun( const RunRecord &run )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        Stmt up( m_impl->db,
            "INSERT INTO runs(run_id, workflow_id, state, name, started_ms, finished_ms, definition_json,"
            " summary_json, metadata_json, tags_json, updated_ms) VALUES(?,?,?,?,?,?,?,?,?,?,?)"
            " ON CONFLICT(run_id) DO UPDATE SET workflow_id=excluded.workflow_id, state=excluded.state,"
            " name=excluded.name, started_ms=excluded.started_ms, finished_ms=excluded.finished_ms,"
            " definition_json=excluded.definition_json, summary_json=excluded.summary_json,"
            " metadata_json=excluded.metadata_json, tags_json=excluded.tags_json,"
            " updated_ms=excluded.updated_ms" );
        if ( !up )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "run prepare failed" ) ) );
        }
        up.bind( 1, run.id );
        up.bind( 2, run.workflowId );
        up.bind( 3, run.state );
        up.bind( 4, run.header.name );
        up.bind( 5, run.startedMs );
        up.bind( 6, run.finishedMs );
        up.bind( 7, jsonToText( run.definition ) );
        up.bind( 8, jsonToText( run.summary ) );
        up.bind( 9, jsonToText( run.header.metadata ) );
        up.bind( 10, run.header.tags.join( QLatin1Char( ',' ) ) );
        up.bind( 11, now );
        if ( !up.step() )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_run" ), QStringLiteral( "run upsert failed" ) ) );
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

std::optional<RunRecord> GovernanceStore::runById( const QString &runId ) const
{
    if ( !m_impl || runId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT run_id, workflow_id, state, name, started_ms, finished_ms, definition_json,"
                        " summary_json, metadata_json, tags_json, updated_ms FROM runs WHERE run_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, runId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readRun( s );
}

QVector<RunRecord> GovernanceStore::runs( qint64 limit ) const
{
    QVector<RunRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT run_id, workflow_id, state, name, started_ms, finished_ms, definition_json,"
                        " summary_json, metadata_json, tags_json, updated_ms FROM runs"
                        " ORDER BY updated_ms DESC, run_id LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readRun( s ) );
    return out;
}

void GovernanceStore::linkRunOutput( const QString &runId, const QString &assetId )
{
    if ( !m_impl || m_impl->readOnly || runId.isEmpty() || assetId.isEmpty() )
        return;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "INSERT OR IGNORE INTO run_outputs(run_id, asset_id) VALUES(?,?)" );
    if ( !s )
        return;
    s.bind( 1, runId );
    s.bind( 2, assetId );
    m_impl->exec( "BEGIN IMMEDIATE" );
    s.step();
    m_impl->exec( "COMMIT" );
}

// --- experiments ------------------------------------------------------------

Result<void> GovernanceStore::upsertExperiment( const ExperimentRecord &experiment )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString idText = experiment.id.toString();
        Stmt up( m_impl->db,
            "INSERT INTO experiments(experiment_id, name, objective, metadata_json, tags_json, created_ms, updated_ms)"
            " VALUES(?,?,?,?,?,?,?)"
            " ON CONFLICT(experiment_id) DO UPDATE SET name=excluded.name, objective=excluded.objective,"
            " metadata_json=excluded.metadata_json, tags_json=excluded.tags_json, updated_ms=excluded.updated_ms" );
        Stmt clearV( m_impl->db, "DELETE FROM experiment_variants WHERE experiment_id=?" );
        Stmt insV( m_impl->db, "INSERT OR REPLACE INTO experiment_variants(experiment_id, variant_key, variant_json) VALUES(?,?,?)" );
        Stmt clearR( m_impl->db, "DELETE FROM experiment_runs WHERE experiment_id=?" );
        Stmt insR( m_impl->db, "INSERT OR IGNORE INTO experiment_runs(experiment_id, run_id) VALUES(?,?)" );
        Stmt fix( m_impl->db, "UPDATE experiments SET created_ms=? WHERE experiment_id=? AND created_ms=0" );
        if ( !up || !clearV || !insV || !clearR || !insR || !fix )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "experiment prepare failed" ) ) );
        }
        up.bind( 1, idText );
        up.bind( 2, experiment.header.name );
        up.bind( 3, experiment.objective );
        up.bind( 4, jsonToText( experiment.header.metadata ) );
        up.bind( 5, experiment.header.tags.join( QLatin1Char( ',' ) ) );
        up.bind( 6, now );
        up.bind( 7, now );
        if ( !up.step() )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_experiment" ), QStringLiteral( "experiment upsert failed" ) ) );
        }
        fix.bind( 1, now );
        fix.bind( 2, idText );
        fix.step();
        clearV.bind( 1, idText );
        clearV.step();
        for ( const ExperimentVariant &variant : experiment.variants )
        {
            insV.reset();
            insV.bind( 1, idText );
            insV.bind( 2, variant.key );
            insV.bind( 3, jsonToText( variant.value ) );
            insV.step();
        }
        clearR.bind( 1, idText );
        clearR.step();
        for ( const QString &runId : experiment.runIds )
        {
            insR.reset();
            insR.bind( 1, idText );
            insR.bind( 2, runId );
            insR.step();
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

Result<void> GovernanceStore::removeExperiment( const QString &experimentId )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt e( m_impl->db, "DELETE FROM experiments WHERE experiment_id=?" );
        Stmt v( m_impl->db, "DELETE FROM experiment_variants WHERE experiment_id=?" );
        Stmt r( m_impl->db, "DELETE FROM experiment_runs WHERE experiment_id=?" );
        e.bind( 1, experimentId );
        e.step();
        const bool removed = sqlite3_changes( m_impl->db ) > 0;
        v.bind( 1, experimentId );
        v.step();
        r.bind( 1, experimentId );
        r.step();
        m_impl->exec( "COMMIT" );
        if ( !removed )
            return Result<void>::failure( govDiag( QStringLiteral( "store.unknown_experiment" ),
                                                   QStringLiteral( "no experiment %1" ).arg( experimentId ) ) );
    }
    return Result<void>::success();
}

std::optional<ExperimentRecord> GovernanceStore::experimentById( const QString &experimentId ) const
{
    if ( !m_impl || experimentId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT experiment_id, name, objective, metadata_json, tags_json, created_ms, updated_ms"
                        " FROM experiments WHERE experiment_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, experimentId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readExperiment( s );
}

QVector<ExperimentRecord> GovernanceStore::experiments( qint64 limit ) const
{
    QVector<ExperimentRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT experiment_id, name, objective, metadata_json, tags_json, created_ms, updated_ms"
                        " FROM experiments ORDER BY updated_ms DESC, experiment_id LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readExperiment( s ) );
    return out;
}

// --- lineage ----------------------------------------------------------------

Result<void> GovernanceStore::addLineageEdges( const QVector<LineageEdge> &edges )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    m_impl->exec( "BEGIN IMMEDIATE" );
    {
        Stmt ins( m_impl->db,
            "INSERT OR IGNORE INTO lineage_edges(output_asset_id, input_asset_id, input_revision,"
            " operator_id, run_id, step_id, fingerprint) VALUES(?,?,?,?,?,?,?)" );
        if ( !ins )
        {
            m_impl->exec( "ROLLBACK" );
            return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "lineage prepare failed" ) ) );
        }
        for ( const LineageEdge &edge : edges )
        {
            ins.reset();
            ins.bind( 1, edge.outputAssetId );
            ins.bind( 2, edge.inputAssetId );
            ins.bind( 3, static_cast<qint64>( edge.inputRevision ) );
            ins.bind( 4, edge.operatorId );
            ins.bind( 5, edge.runId );
            ins.bind( 6, edge.stepId );
            ins.bind( 7, edge.fingerprint );
            ins.step();
        }
    }
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

QVector<QVariantMap> GovernanceStore::lineageUpstream( const QString &assetId, int maxDepth ) const
{
    QVector<QVariantMap> out;
    if ( !m_impl || assetId.isEmpty() )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    // UNION dedups (node, depth); GROUP BY node collapses depth duplicates a
    // cycle would otherwise produce; the root never re-enters its own result.
    Stmt s( m_impl->db,
            "WITH RECURSIVE up(node, depth) AS ("
            "  SELECT ?, 0"
            "  UNION"
            "  SELECT e.input_asset_id, up.depth+1 FROM lineage_edges e"
            "   JOIN up ON e.output_asset_id=up.node WHERE up.depth < ?"
            ") SELECT node, MIN(depth) FROM up WHERE depth>0 AND node<>? GROUP BY node ORDER BY MIN(depth), node" );
    if ( !s )
        return out;
    s.bind( 1, assetId );
    s.bind( 2, std::clamp( maxDepth, 1, 64 ) );
    s.bind( 3, assetId );
    while ( s.stepRow() )
    {
        QVariantMap row;
        row.insert( QStringLiteral( "assetId" ), s.text( 0 ) );
        row.insert( QStringLiteral( "depth" ), s.i64( 1 ) );
        out.append( row );
    }
    return out;
}

QVector<QVariantMap> GovernanceStore::lineageDownstream( const QString &assetId, int maxDepth ) const
{
    QVector<QVariantMap> out;
    if ( !m_impl || assetId.isEmpty() )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    // UNION dedups (node, depth); GROUP BY node collapses depth duplicates;
    // the root never re-enters its own result.
    Stmt s( m_impl->db,
            "WITH RECURSIVE down(node, depth) AS ("
            "  SELECT ?, 0"
            "  UNION"
            "  SELECT e.output_asset_id, down.depth+1 FROM lineage_edges e"
            "   JOIN down ON e.input_asset_id=down.node WHERE down.depth < ?"
            ") SELECT node, MIN(depth) FROM down WHERE depth>0 AND node<>? GROUP BY node ORDER BY MIN(depth), node" );
    if ( !s )
        return out;
    s.bind( 1, assetId );
    s.bind( 2, std::clamp( maxDepth, 1, 64 ) );
    s.bind( 3, assetId );
    while ( s.stepRow() )
    {
        QVariantMap row;
        row.insert( QStringLiteral( "assetId" ), s.text( 0 ) );
        row.insert( QStringLiteral( "depth" ), s.i64( 1 ) );
        out.append( row );
    }
    return out;
}

QVector<GovernanceStore::LineageEdge> GovernanceStore::directEdges( const QString &assetId, bool outgoing ) const
{
    QVector<LineageEdge> out;
    if ( !m_impl || assetId.isEmpty() )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, outgoing
            ? "SELECT output_asset_id, input_asset_id, input_revision, operator_id, run_id, step_id, fingerprint"
              " FROM lineage_edges WHERE output_asset_id=?"
            : "SELECT output_asset_id, input_asset_id, input_revision, operator_id, run_id, step_id, fingerprint"
              " FROM lineage_edges WHERE input_asset_id=?" );
    if ( !s )
        return out;
    s.bind( 1, assetId );
    while ( s.stepRow() )
    {
        LineageEdge e;
        e.outputAssetId = s.text( 0 );
        e.inputAssetId = s.text( 1 );
        e.inputRevision = static_cast<quint64>( s.i64( 2 ) );
        e.operatorId = s.text( 3 );
        e.runId = s.text( 4 );
        e.stepId = s.text( 5 );
        e.fingerprint = s.text( 6 );
        out.append( e );
    }
    return out;
}

// --- smart collections -------------------------------------------------------

Result<void> GovernanceStore::upsertSmartCollection( const SmartCollectionRecord &collection )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    QJsonArray array;
    for ( const SmartPredicate &p : collection.predicates )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "field" ), p.field );
        o.insert( QStringLiteral( "op" ), p.op );
        o.insert( QStringLiteral( "value" ), p.value );
        array.append( o );
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Stmt s( m_impl->db,
        "INSERT INTO smart_collections(collection_id, name, predicate_json, metadata_json, created_ms, updated_ms)"
        " VALUES(?,?,?,?,?,?)"
        " ON CONFLICT(collection_id) DO UPDATE SET name=excluded.name,"
        " predicate_json=excluded.predicate_json, metadata_json=excluded.metadata_json,"
        " updated_ms=excluded.updated_ms" );
    if ( !s )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "smart collection prepare failed" ) ) );
    const QString idText = collection.id.toString();
    s.bind( 1, idText );
    s.bind( 2, collection.header.name );
    s.bind( 3, jsonToText( QJsonObject{ { QLatin1String( "predicates" ), array } } ) );
    s.bind( 4, jsonToText( collection.header.metadata ) );
    s.bind( 5, now );
    s.bind( 6, now );
    m_impl->exec( "BEGIN IMMEDIATE" );
    const bool ok = s.step();
    m_impl->exec( "COMMIT" );
    if ( !ok )
        return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_smart" ), QStringLiteral( "smart collection upsert failed" ) ) );
    return Result<void>::success();
}

Result<void> GovernanceStore::removeSmartCollection( const QString &collectionId )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "DELETE FROM smart_collections WHERE collection_id=?" );
    if ( !s )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "smart collection prepare failed" ) ) );
    s.bind( 1, collectionId );
    m_impl->exec( "BEGIN IMMEDIATE" );
    s.step();
    const bool removed = sqlite3_changes( m_impl->db ) > 0;
    m_impl->exec( "COMMIT" );
    if ( !removed )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unknown_smart" ),
                                               QStringLiteral( "no smart collection %1" ).arg( collectionId ) ) );
    return Result<void>::success();
}

std::optional<SmartCollectionRecord> GovernanceStore::smartCollectionById( const QString &collectionId ) const
{
    if ( !m_impl || collectionId.isEmpty() )
        return std::nullopt;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT collection_id, name, predicate_json, metadata_json, created_ms, updated_ms"
                        " FROM smart_collections WHERE collection_id=?" );
    if ( !s )
        return std::nullopt;
    s.bind( 1, collectionId );
    if ( !s.stepRow() )
        return std::nullopt;
    return m_impl->readSmart( s );
}

QVector<SmartCollectionRecord> GovernanceStore::smartCollections() const
{
    QVector<SmartCollectionRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT collection_id, name, predicate_json, metadata_json, created_ms, updated_ms"
                        " FROM smart_collections ORDER BY name" );
    if ( !s )
        return out;
    while ( s.stepRow() )
        out.append( m_impl->readSmart( s ) );
    return out;
}

// --- exports / mappings ------------------------------------------------------

Result<void> GovernanceStore::upsertExport( const ExportRecord &record )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Stmt s( m_impl->db,
        "INSERT INTO exports(export_id, kind, target, result_id, name, metadata_json, created_ms, updated_ms)"
        " VALUES(?,?,?,?,?,?,?,?)"
        " ON CONFLICT(export_id) DO UPDATE SET kind=excluded.kind, target=excluded.target,"
        " result_id=excluded.result_id, name=excluded.name, metadata_json=excluded.metadata_json,"
        " updated_ms=excluded.updated_ms" );
    if ( !s )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "export prepare failed" ) ) );
    const QString idText = record.id.toString();
    s.bind( 1, idText );
    s.bind( 2, record.kind );
    s.bind( 3, record.target );
    s.bind( 4, record.resultId );
    s.bind( 5, record.header.name );
    s.bind( 6, jsonToText( record.header.metadata ) );
    s.bind( 7, now );
    s.bind( 8, now );
    m_impl->exec( "BEGIN IMMEDIATE" );
    const bool ok = s.step();
    m_impl->exec( "COMMIT" );
    if ( !ok )
        return Result<void>::failure( govDiag( QStringLiteral( "store.upsert_export" ), QStringLiteral( "export upsert failed" ) ) );
    return Result<void>::success();
}

QVector<ExportRecord> GovernanceStore::exports( qint64 limit ) const
{
    QVector<ExportRecord> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT export_id, kind, target, result_id, name, metadata_json, created_ms, updated_ms"
                        " FROM exports ORDER BY created_ms DESC LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, kMaxPageSize ) );
    while ( s.stepRow() )
        out.append( m_impl->readExport( s ) );
    return out;
}

Result<void> GovernanceStore::upsertPathMapping( const PathMapping &mapping )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db,
        "INSERT INTO path_mappings(kind, from_path, to_path) VALUES(?,?,?)"
        " ON CONFLICT(kind, from_path) DO UPDATE SET to_path=excluded.to_path" );
    if ( !s )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "mapping prepare failed" ) ) );
    s.bind( 1, mapping.kind );
    s.bind( 2, mapping.fromPath );
    s.bind( 3, mapping.toPath );
    m_impl->exec( "BEGIN IMMEDIATE" );
    s.step();
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

QVector<PathMapping> GovernanceStore::pathMappings() const
{
    QVector<PathMapping> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT kind, from_path, to_path FROM path_mappings" );
    if ( !s )
        return out;
    while ( s.stepRow() )
        out.append( PathMapping{ s.text( 0 ), s.text( 1 ), s.text( 2 ) } );
    return out;
}

// --- unified query -----------------------------------------------------------

WorkspacePage GovernanceStore::query( const WorkspaceQuery &query, const QString &facetField ) const
{
    WorkspacePage out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );

    const qint64 clampedLimit = std::clamp<qint64>( query.limit, 1, kMaxPageSize );
    const qint64 clampedOffset = std::max<qint64>( query.offset, 0 );
    QStringList where;
    QVector<QString> textBinds;
    QVector<qint64> intBinds;

    if ( query.set == EntitySet::Assets )
    {
        if ( !query.text.isEmpty() )
        {
            where << QStringLiteral( "(a.display_name LIKE ? ESCAPE '\\' OR a.canonical_source LIKE ? ESCAPE '\\')" );
            const QString pat = QLatin1Char( '%' ) + likeContains( query.text ) + QLatin1Char( '%' );
            textBinds << pat << pat;
        }
        if ( !query.kind.isEmpty() ) { where << QStringLiteral( "a.kind=?" ); textBinds << query.kind; }
        if ( !query.state.isEmpty() ) { where << QStringLiteral( "a.state=?" ); textBinds << query.state; }
        if ( !query.sensor.isEmpty() ) { where << QStringLiteral( "a.sensor=?" ); textBinds << query.sensor; }
        if ( !query.modality.isEmpty() ) { where << QStringLiteral( "a.modality=?" ); textBinds << query.modality; }
        if ( !query.crs.isEmpty() ) { where << QStringLiteral( "a.crs=?" ); textBinds << query.crs; }
        if ( !query.collectionId.isEmpty() ) { where << QStringLiteral( "a.parent_collection_id=?" ); textBinds << query.collectionId; }
        if ( !query.datasetId.isEmpty() )
        {
            where << QStringLiteral( "a.asset_id IN (SELECT asset_id FROM dataset_members WHERE dataset_id=?)" );
            textBinds << query.datasetId;
        }
        if ( query.acquiredFromMs > 0 ) { where << QStringLiteral( "a.acquisition_ms>=?" ); intBinds << query.acquiredFromMs; }
        if ( query.acquiredToMs > 0 ) { where << QStringLiteral( "a.acquisition_ms<=?" ); intBinds << query.acquiredToMs; }
        if ( !query.tag.isEmpty() )
        {
            where << QStringLiteral( "a.asset_id IN (SELECT entity_id FROM tags WHERE entity_kind='asset' AND tag=?)" );
            textBinds << query.tag;
        }
    }
    else if ( query.set == EntitySet::Results )
    {
        if ( !query.text.isEmpty() )
        {
            where << QStringLiteral( "r.name LIKE ? ESCAPE '\\'" );
            textBinds << ( QLatin1Char( '%' ) + likeContains( query.text ) + QLatin1Char( '%' ) );
        }
        if ( !query.kind.isEmpty() ) { where << QStringLiteral( "r.semantic_type=?" ); textBinds << query.kind; }
        if ( !query.state.isEmpty() ) { where << QStringLiteral( "r.status=?" ); textBinds << query.state; }
        if ( !query.runId.isEmpty() ) { where << QStringLiteral( "r.run_id=?" ); textBinds << query.runId; }
        if ( !query.tag.isEmpty() )
        {
            where << QStringLiteral( "r.result_id IN (SELECT entity_id FROM tags WHERE entity_kind='result' AND tag=?)" );
            textBinds << query.tag;
        }
    }
    else if ( query.set == EntitySet::Runs )
    {
        if ( !query.text.isEmpty() )
        {
            where << QStringLiteral( "(ru.name LIKE ? ESCAPE '\\' OR ru.workflow_id LIKE ? ESCAPE '\\')" );
            const QString pat = QLatin1Char( '%' ) + likeContains( query.text ) + QLatin1Char( '%' );
            textBinds << pat << pat;
        }
        if ( !query.state.isEmpty() ) { where << QStringLiteral( "ru.state=?" ); textBinds << query.state; }
        if ( !query.runId.isEmpty() ) { where << QStringLiteral( "ru.run_id=?" ); textBinds << query.runId; }
    }
    else // Datasets
    {
        if ( !query.text.isEmpty() )
        {
            where << QStringLiteral( "d.name LIKE ? ESCAPE '\\'" );
            textBinds << ( QLatin1Char( '%' ) + likeContains( query.text ) + QLatin1Char( '%' ) );
        }
        if ( !query.kind.isEmpty() ) { where << QStringLiteral( "d.kind=?" ); textBinds << query.kind; }
        if ( !query.datasetId.isEmpty() ) { where << QStringLiteral( "d.dataset_id=?" ); textBinds << query.datasetId; }
    }

    const QString whereSql = where.isEmpty() ? QString() : QStringLiteral( " WHERE " ) + where.join( QStringLiteral( " AND " ) );
    auto bindAll = [ & ]( Stmt &s ) -> int {
        int idx = 1;
        for ( const QString &t : textBinds )
            s.bind( idx++, t );
        for ( const qint64 v : intBinds )
            s.bind( idx++, v );
        return idx;
    };

    // Facet branch: GROUP BY over the same WHERE, no paging.
    if ( !facetField.isEmpty() && query.set == EntitySet::Assets )
    {
        static const QHash<QString, QString> kFacetColumns = {
            { QStringLiteral( "kind" ), QStringLiteral( "a.kind" ) },
            { QStringLiteral( "state" ), QStringLiteral( "a.state" ) },
            { QStringLiteral( "sensor" ), QStringLiteral( "a.sensor" ) },
            { QStringLiteral( "modality" ), QStringLiteral( "a.modality" ) },
            { QStringLiteral( "crs" ), QStringLiteral( "a.crs" ) },
            { QStringLiteral( "format" ), QStringLiteral( "a.format" ) },
        };
        const QString column = kFacetColumns.value( facetField );
        if ( !column.isEmpty() )
        {
            out.facetField = facetField;
            Stmt s( m_impl->db, QStringLiteral( "SELECT %1 AS facet, COUNT(*) FROM assets a%2 GROUP BY facet ORDER BY COUNT(*) DESC LIMIT 64" )
                                     .arg( column, whereSql ) );
            if ( s )
            {
                bindAll( s );
                while ( s.stepRow() )
                {
                    const QString value = s.text( 0 );
                    if ( value.isEmpty() )
                        continue;
                    out.facets.append( FacetCount{ value, s.i64( 1 ) } );
                }
            }
            return out;
        }
        if ( facetField == QLatin1String( "tag" ) )
        {
            out.facetField = facetField;
            Stmt s( m_impl->db, QStringLiteral( "SELECT t.tag, COUNT(*) FROM tags t WHERE t.entity_kind='asset'"
                                                " AND t.entity_id IN (SELECT a.asset_id FROM assets a%1)"
                                                " GROUP BY t.tag ORDER BY COUNT(*) DESC LIMIT 64" )
                                     .arg( whereSql ) );
            if ( s )
            {
                bindAll( s );
                while ( s.stepRow() )
                    out.facets.append( FacetCount{ s.text( 0 ), s.i64( 1 ) } );
            }
            return out;
        }
    }

    QString orderBy = QStringLiteral( "updated_ms DESC" );
    if ( query.sortBy == QLatin1String( "name" ) )
        orderBy = QStringLiteral( "name COLLATE NOCASE ASC" );
    else if ( query.sortBy == QLatin1String( "acquisition" ) )
        orderBy = QStringLiteral( "acquisition_ms DESC" );

    QString from;
    QString cols;
    QString idCol;
    switch ( query.set )
    {
        case EntitySet::Assets:
            from = QStringLiteral( "assets a" );
            cols = QStringLiteral( "a.asset_id, a.kind, a.state, a.display_name, a.canonical_source, a.sensor,"
                                   " a.modality, a.crs, a.acquisition_ms, a.revision, a.size_bytes, a.format,"
                                   " a.content_fingerprint, a.availability, a.updated_ms" );
            idCol = QStringLiteral( "a.asset_id" );
            orderBy = query.sortBy == QLatin1String( "name" ) ? QStringLiteral( "a.display_name COLLATE NOCASE ASC" )
                      : query.sortBy == QLatin1String( "acquisition" ) ? QStringLiteral( "a.acquisition_ms DESC" )
                      : QStringLiteral( "a.updated_ms DESC" );
            break;
        case EntitySet::Results:
            from = QStringLiteral( "results r" );
            cols = QStringLiteral( "r.result_id, r.semantic_type, r.status, r.name, r.run_id, r.revision,"
                                   " r.updated_ms, r.metrics_json" );
            idCol = QStringLiteral( "r.result_id" );
            break;
        case EntitySet::Runs:
            from = QStringLiteral( "runs ru" );
            cols = QStringLiteral( "ru.run_id, ru.workflow_id, ru.state, ru.name, ru.started_ms, ru.finished_ms, ru.updated_ms" );
            idCol = QStringLiteral( "ru.run_id" );
            break;
        case EntitySet::Datasets:
            from = QStringLiteral( "datasets d" );
            cols = QStringLiteral( "d.dataset_id, d.kind, d.name, d.revision, d.updated_ms" );
            idCol = QStringLiteral( "d.dataset_id" );
            break;
    }

    {
        Stmt s( m_impl->db, QStringLiteral( "SELECT COUNT(*) FROM %1%2" ).arg( from, whereSql ) );
        if ( !s )
            return out;
        bindAll( s );
        if ( s.stepRow() )
            out.total = s.i64( 0 );
    }
    {
        const QString sql = QStringLiteral( "SELECT %1 FROM %2%3 ORDER BY %4 LIMIT ? OFFSET ?" )
                                .arg( cols, from, whereSql, orderBy );
        Stmt s( m_impl->db, sql );
        if ( !s )
            return out;
        int idx = bindAll( s );
        s.bind( idx++, clampedLimit );
        s.bind( idx++, clampedOffset );
        while ( s.stepRow() )
        {
            QVariantMap row;
            const int columns = sqlite3_column_count( s.get() );
            for ( int c = 0; c < columns; ++c )
            {
                switch ( sqlite3_column_type( s.get(), c ) )
                {
                    case SQLITE_INTEGER:
                        row.insert( QString::fromUtf8( sqlite3_column_name( s.get(), c ) ), s.i64( c ) );
                        break;
                    default:
                        row.insert( QString::fromUtf8( sqlite3_column_name( s.get(), c ) ), s.text( c ) );
                        break;
                }
            }
            out.items.append( row );
        }
        Q_UNUSED( idCol );
    }
    return out;
}

// --- audit log ---------------------------------------------------------------

Result<void> GovernanceStore::appendAudit( const QString &actor, const QString &action,
                                           const QString &entityKind, const QString &entityId,
                                           const QJsonObject &detail )
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "INSERT INTO audit_log(ts_ms, actor, action, entity_kind, entity_id, detail_json)"
                        " VALUES(?,?,?,?,?,?)" );
    if ( !s )
        return Result<void>::failure( govDiag( QStringLiteral( "store.prepare" ), QStringLiteral( "audit prepare failed" ) ) );
    s.bind( 1, QDateTime::currentMSecsSinceEpoch() );
    s.bind( 2, actor );
    s.bind( 3, action );
    s.bind( 4, entityKind );
    s.bind( 5, entityId );
    s.bind( 6, jsonToText( detail ) );
    m_impl->exec( "BEGIN IMMEDIATE" );
    const bool ok = s.step();
    m_impl->exec( "COMMIT" );
    if ( !ok )
        return Result<void>::failure( govDiag( QStringLiteral( "store.audit" ), QStringLiteral( "audit append failed" ) ) );
    return Result<void>::success();
}

QVector<GovernanceStore::AuditEntry> GovernanceStore::auditTail( qint64 limit ) const
{
    QVector<AuditEntry> out;
    if ( !m_impl )
        return out;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT seq, ts_ms, actor, action, entity_kind, entity_id, detail_json"
                        " FROM audit_log ORDER BY seq DESC LIMIT ?" );
    if ( !s )
        return out;
    s.bind( 1, std::clamp<qint64>( limit, 1, 1000 ) );
    while ( s.stepRow() )
    {
        AuditEntry e;
        e.seq = s.i64( 0 );
        e.tsMs = s.i64( 1 );
        e.actor = s.text( 2 );
        e.action = s.text( 3 );
        e.entityKind = s.text( 4 );
        e.entityId = s.text( 5 );
        e.detail = textToJson( s.text( 6 ) );
        out.append( e );
    }
    return out;
}

// --- meta / integrity ---------------------------------------------------------

void GovernanceStore::setMeta( const QString &key, const QString &value )
{
    if ( !m_impl || m_impl->readOnly )
        return;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "INSERT OR REPLACE INTO gov_meta(key, value) VALUES(?,?)" );
    if ( !s )
        return;
    s.bind( 1, key );
    s.bind( 2, value );
    m_impl->exec( "BEGIN IMMEDIATE" );
    s.step();
    m_impl->exec( "COMMIT" );
}

QString GovernanceStore::meta( const QString &key ) const
{
    if ( !m_impl )
        return QString();
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "SELECT value FROM gov_meta WHERE key=?" );
    if ( !s )
        return QString();
    s.bind( 1, key );
    return s.stepRow() ? s.text( 0 ) : QString();
}

GovernanceDiagnostic GovernanceStore::integrityCheck() const
{
    GovernanceDiagnostic d;
    d.entityKind = QStringLiteral( "workspace" );
    d.kind = DiagnosticKind::StoreCorruption;
    d.code = QStringLiteral( "store.integrity_ok" );
    d.severity = DiagnosticSeverity::Info;
    d.message = QStringLiteral( "governance store integrity ok" );
    if ( !m_impl )
    {
        d.code = QStringLiteral( "store.closed" );
        d.severity = DiagnosticSeverity::Error;
        d.message = QStringLiteral( "governance store not open" );
        return d;
    }
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    Stmt s( m_impl->db, "PRAGMA quick_check" );
    if ( !s )
    {
        d.code = QStringLiteral( "store.integrity_failed" );
        d.severity = DiagnosticSeverity::Error;
        d.message = QStringLiteral( "quick_check could not run" );
        return d;
    }
    if ( !s.stepRow() || s.text( 0 ) != QLatin1String( "ok" ) )
    {
        d.code = QStringLiteral( "store.integrity_failed" );
        d.severity = DiagnosticSeverity::Error;
        d.message = s.text( 0 ).isEmpty() ? QStringLiteral( "quick_check reported corruption" ) : s.text( 0 );
        d.repairSuggestion = QStringLiteral( "Rebuild the governance index from the project document (project:rebuild-index)." );
        return d;
    }
    return d;
}

Result<void> GovernanceStore::clearAll()
{
    if ( !m_impl || m_impl->readOnly )
        return Result<void>::failure( govDiag( QStringLiteral( "store.unavailable" ), QStringLiteral( "store not writable" ) ) );
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    const char *tables[] = {
        "assets", "aliases", "tags", "datasets", "dataset_members", "results",
        "result_inputs", "result_artifacts", "runs", "run_outputs", "experiments",
        "experiment_variants", "experiment_runs", "lineage_edges", "smart_collections",
        "exports", "path_mappings", "audit_log",
    };
    m_impl->exec( "BEGIN IMMEDIATE" );
    for ( const char *table : tables )
        m_impl->exec( ( QStringLiteral( "DELETE FROM " ) + table ).toUtf8().constData() );
    m_impl->exec( "COMMIT" );
    return Result<void>::success();
}

} // namespace sicnu::workspace
