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

namespace detail {

/// Stream @p path through the git canonical-bytes rule. Returns the canonical
/// byte count (-1 when unreadable); when @p hash is non-null it is fed the
/// same bytes.
inline qint64 canonicalFileDigest( const QString &path, QCryptographicHash *hash,
                                   qint64 chunkBytes )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return -1;
    QCryptographicHash local( QCryptographicHash::Sha256 );
    if ( !hash )
        hash = &local;
    const QByteArray head = f.read( 8000 );
    if ( head.contains( '\0' ) )
    {
        // Binary fixture: bytes hash as-is.
        hash->addData( head );
        qint64 size = head.size();
        while ( !f.atEnd() )
        {
            const QByteArray chunk = f.read( chunkBytes );
            hash->addData( chunk );
            size += chunk.size();
        }
        return size;
    }
    // Text: CRLF→LF; a lone CR — mid-file, across a chunk boundary, or at
    // EOF — is kept, which is what git stores.
    QByteArray pending;
    qint64 size = 0;
    const auto consume = [&]( const QByteArray &in, bool eof ) {
        QByteArray data = pending + in;
        pending.clear();
        if ( !eof && data.endsWith( '\r' ) )
        {
            pending = QByteArray( 1, '\r' );
            data.chop( 1 );
        }
        QByteArray out;
        out.reserve( data.size() );
        for ( int i = 0; i < data.size(); ++i )
        {
            if ( data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n' )
            {
                out.append( '\n' );
                ++i;
            }
            else
            {
                out.append( data[i] );
            }
        }
        hash->addData( out );
        size += out.size();
    };
    consume( head, false );
    while ( !f.atEnd() )
        consume( f.read( chunkBytes ), false );
    consume( QByteArray(), true );
    return size;
}

} // namespace detail

/// sha256 over the canonical bytes of @p path — the byte stream git would
/// store for it (any NUL in the first 8000 bytes ⇒ binary, hashed as-is;
/// otherwise CRLF→LF with lone CRs kept). ONE normalization entry for the
/// admin side, implementing the canonical-bytes spec the foundry's pins are
/// computed over (gen_lab_packs.py canonical_bytes(), arriving with #1336).
/// MERGE-ORDER NOTE: until #1336 lands, the agent-side authority
/// (lab_pack.cpp fileSha256) still hashes raw bytes, so on a Windows CRLF
/// checkout the admin verdict (matches pins) and the agent verdict
/// (spurious mismatch) diverge — the agent-side gap is exactly what #1336
/// fixes; this entry must delegate to it once that lands.
/// Empty string when unreadable.
inline QString canonicalFileSha256( const QString &path, qint64 chunkBytes = 64 * 1024 )
{
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    if ( detail::canonicalFileDigest( path, &hash, chunkBytes ) < 0 )
        return QString();
    return QString::fromLatin1( hash.result().toHex() );
}

/// Byte size of the canonical stream (git blob size: raw for binary,
/// CRLF-deleted for text). -1 when unreadable.
inline qint64 canonicalFileSize( const QString &path, qint64 chunkBytes = 64 * 1024 )
{
    return detail::canonicalFileDigest( path, nullptr, chunkBytes );
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
