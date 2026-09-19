#include "query_cursor.h"

#include <QByteArrayView>
#include <QChar>

namespace sicnu::data
{

namespace
{

constexpr QChar kPartSeparator = QLatin1Char( '\x1F' );

Diagnostic cursorDiag( const QString &message )
{
    return Diagnostic{ QStringLiteral( "data.cursor_invalid" ), message,
                       DiagnosticSeverity::Error };
}

} // namespace

QString QueryCursor::encode( const QStringList &parts )
{
    // Empty FIELDS are legal (an absent filter must round-trip); only the
    // structure requirement (filter echo + at least one key column) holds.
    if ( parts.size() < 2 )
        return QString();
    const QString payload = QStringLiteral( "v1" ) + kPartSeparator +
                            parts.join( kPartSeparator );
    return QString::fromLatin1( payload.toUtf8().toBase64( QByteArray::Base64UrlEncoding ) );
}

Result<QStringList> QueryCursor::decode( const QString &cursor )
{
    if ( cursor.isEmpty() )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor is empty" ) ) );
    const QByteArray raw =
        QByteArray::fromBase64( cursor.toLatin1(), QByteArray::Base64UrlEncoding );
    if ( raw.isEmpty() )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor is not decodable" ) ) );
    const QStringList parts =
        QString::fromUtf8( raw ).split( kPartSeparator, Qt::KeepEmptyParts );
    if ( parts.size() < 3 || parts.first() != QStringLiteral( "v1" ) )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor version or shape is not recognized" ) ) );
    QStringList payload = parts;
    payload.removeFirst();
    return Result<QStringList>::success( payload );
}

} // namespace sicnu::data
