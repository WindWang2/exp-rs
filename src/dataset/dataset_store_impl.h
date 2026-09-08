// dataset_store_impl.h — PRIVATE header shared by the dataset_store_*.cpp
// translation units. Not part of the public interface; do not include
// outside src/dataset store implementations.
#pragma once

#include "dataset_store.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <QUuid>
#include <sqlite3.h>

namespace sicnu::dataset
{

/// RAII prepared statement. Prepared per call (the store's query volume is
/// page-bounded; prepare-once caches are a later optimization if profiling
/// asks for one).
class StoreStmt
{
  public:
    StoreStmt( sqlite3 *db, const QString &sql )
    {
        if ( sqlite3_prepare_v2( db, sql.toUtf8().constData(), -1, &m_stmt, nullptr ) != SQLITE_OK )
            m_stmt = nullptr;
    }
    ~StoreStmt() { if ( m_stmt ) sqlite3_finalize( m_stmt ); }
    StoreStmt( const StoreStmt & ) = delete;
    StoreStmt &operator=( const StoreStmt & ) = delete;

    explicit operator bool() const { return m_stmt != nullptr; }
    sqlite3_stmt *get() const { return m_stmt; }
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
    void bind( int idx, int v ) const { sqlite3_bind_int64( m_stmt, idx, v ); }
    void bind( int idx, double v ) const { sqlite3_bind_double( m_stmt, idx, v ); }
    QString text( int col ) const
    {
        const unsigned char *p = sqlite3_column_text( m_stmt, col );
        return p ? QString::fromUtf8( reinterpret_cast<const char *>( p ) ) : QString();
    }
    qint64 i64( int col ) const { return sqlite3_column_int64( m_stmt, col ); }
    double real( int col ) const { return sqlite3_column_double( m_stmt, col ); }

  private:
    sqlite3_stmt *m_stmt = nullptr;
};

inline Diagnostic storeDiag( QString code, QString message )
{
    return Diagnostic{ std::move( code ), std::move( message ), DiagnosticSeverity::Error };
}

inline QString jsonToText( const QJsonObject &object )
{
    return QJsonDocument( object ).toJson( QJsonDocument::Compact );
}

inline QJsonObject textToJson( const QString &text )
{
    if ( text.isEmpty() )
        return {};
    return QJsonDocument::fromJson( text.toUtf8() ).object();
}

/// Validates + canonicalizes a UUID-ish id; false when absent/malformed.
inline bool canonicalId( const QString &text, QString *out )
{
    const QUuid parsed = QUuid::fromString( text );
    if ( text.isEmpty() || parsed.isNull() )
        return false;
    *out = parsed.toString( QUuid::WithoutBraces );
    return true;
}

struct DatasetStore::Impl
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

    bool begin( QString *errorOut )
    {
        return exec( "BEGIN IMMEDIATE", errorOut );
    }

    // Checked commit with rollback-on-failure (the ADR 0130 rule): the
    // connection is never left inside a dangling transaction.
    bool commit( QString *errorOut )
    {
        if ( !exec( "COMMIT", errorOut ) )
        {
            exec( "ROLLBACK", nullptr );
            return false;
        }
        return true;
    }

    void rollback()
    {
        exec( "ROLLBACK", nullptr );
    }

    QString schemaVersion()
    {
        QMutexLocker lock( &mutex );
        StoreStmt stmt( db, QStringLiteral( "SELECT value FROM ds_meta WHERE key='schema_version'" ) );
        if ( !stmt || !stmt.stepRow() )
            return QString();
        return stmt.text( 0 );
    }
};

} // namespace sicnu::dataset
