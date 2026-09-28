// json_util.h — small JSON helpers (canonical serialization, path safety).
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "lab_pack/lab_pack.h"

#include <string>

namespace sicnu::teaching_admin {

inline QByteArray canonicalJsonBytes( const QJsonObject &obj )
{
    return QJsonDocument( obj ).toJson( QJsonDocument::Compact );
}

inline QString sha256Hex( const QByteArray &bytes )
{
    return QString::fromLatin1(
        QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

/// Streamed sha256 of a file in bounded chunks (mirrors the pack
/// verifier's no-whole-file-load policy). Empty string when unreadable.
inline QString sha256OfFile( const QString &path, qint64 chunkBytes = 1024 * 1024 )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return QString();
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    while ( !f.atEnd() )
        hash.addData( f.read( chunkBytes ) );
    return QString::fromLatin1( hash.result().toHex() );
}

/// sha256 over the canonical bytes of @p path — the byte stream git would
/// store for it. DELEGATES to the lab-pack authority
/// (sicnu::labpack::canonicalFileSha256): one implementation serves both
/// verdicts, so an admin verdict and an agent verdict cannot diverge on a
/// checkout whose EOL policy differs from the committed pins'. The admin-side
/// mirror (detail::canonicalFileDigest) lived only until #1336 landed the
/// authority — see .planning/teaching-lab-r4/BASELINE-R2.md 遗留5.
/// Empty string when unreadable.
inline QString canonicalFileSha256( const QString &path )
{
    return QString::fromStdString( sicnu::labpack::canonicalFileSha256(
      sicnu::labpack::pathFromUtf8( path.toStdString() ) ) );
}

/// Byte size of the canonical stream (git blob size: raw for binary,
/// CRLF-deleted for text). -1 when unreadable.
inline qint64 canonicalFileSize( const QString &path )
{
    std::int64_t bytes = 0;
    if ( sicnu::labpack::canonicalFileSha256(
           sicnu::labpack::pathFromUtf8( path.toStdString() ), &bytes )
           .empty() )
        return -1;
    return bytes;
}

/// True when @p rel could escape a root (absolute, drive-qualified, or '..').
inline bool isUnsafeRelativePath( const QString &rel )
{
    if ( rel.isEmpty() )
        return true;
    QString n = rel;
    n.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
    if ( n.startsWith( QLatin1Char( '/' ) ) )
        return true;
    if ( n.size() >= 2 && n.at( 0 ).isLetter() && n.at( 1 ) == QLatin1Char( ':' ) )
        return true;
    const QStringList parts = n.split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    for ( const auto &p : parts )
    {
        if ( p == QLatin1String( ".." ) )
            return true;
    }
    return false;
}

inline QJsonValue sortedValue( const QJsonValue &v );

inline QJsonObject sortKeys( const QJsonObject &in )
{
    QStringList keys = in.keys();
    keys.sort();
    QJsonObject out;
    for ( const auto &k : keys )
        out.insert( k, sortedValue( in.value( k ) ) );
    return out;
}

inline QJsonValue sortedValue( const QJsonValue &v )
{
    if ( v.isObject() )
        return sortKeys( v.toObject() );
    if ( v.isArray() )
    {
        QJsonArray arr;
        for ( const auto &item : v.toArray() )
            arr.append( sortedValue( item ) );
        return arr;
    }
    return v;
}

} // namespace sicnu::teaching_admin
