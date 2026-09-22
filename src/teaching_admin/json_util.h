// json_util.h — small JSON helpers (canonical serialization, path safety).
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
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
