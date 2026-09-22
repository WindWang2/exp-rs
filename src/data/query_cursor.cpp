#include "query_cursor.h"

#include <QCryptographicHash>

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
    // #1186: the utility boundary owns field escaping — a raw id containing
    // 0x1F would split the walk mid-iteration. Refuse rather than silently
    // truncate; callers that need free-form text must escape first.
    for ( const QString &part : parts )
    {
        if ( part.contains( kPartSeparator ) )
            return QString();
    }
    const QString payload = QStringLiteral( "v1" ) + kPartSeparator +
                            parts.join( kPartSeparator );
    // A 16-hex integrity prefix lets decode() reject truncation/forgery:
    // without it, a cursor cut at a base64 quantum boundary could decode to
    // a shorter VALID-looking payload and silently resume elsewhere.
    const QByteArray digest =
        QCryptographicHash::hash( payload.toUtf8(), QCryptographicHash::Sha256 ).toHex();
    const QString framed = QString::fromUtf8( digest.left( 16 ) ) + kPartSeparator + payload;
    return QString::fromLatin1( framed.toUtf8().toBase64( QByteArray::Base64UrlEncoding ) );
}

Result<QStringList> QueryCursor::decode( const QString &cursor )
{
    if ( cursor.isEmpty() )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor is empty" ) ) );
    const QByteArray raw = QByteArray::fromBase64(
        cursor.toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors );
    if ( raw.isEmpty() )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor is not decodable" ) ) );
    const QStringList parts =
        QString::fromUtf8( raw ).split( kPartSeparator, Qt::KeepEmptyParts );
    if ( parts.size() < 4 )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor shape is not recognized" ) ) );
    // Integrity check FIRST: any truncation, re-padding or edit of the
    // payload fails here instead of decoding to a plausible half-cursor.
    const QStringList payloadParts = parts.mid( 2 );
    const QString framedPayload = parts.at( 1 ) + kPartSeparator +
                                  payloadParts.join( kPartSeparator );
    const QByteArray digest =
        QCryptographicHash::hash( framedPayload.toUtf8(), QCryptographicHash::Sha256 ).toHex();
    if ( QString::fromUtf8( digest.left( 16 ) ) != parts.at( 0 ) )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor integrity check failed" ) ) );
    if ( parts.at( 1 ) != QStringLiteral( "v1" ) )
        return Result<QStringList>::failure(
            cursorDiag( QStringLiteral( "cursor version is not recognized" ) ) );
    // payloadParts already exclude the integrity frame and the version
    // field: exactly the fields encode() received.
    return Result<QStringList>::success( payloadParts );
}

} // namespace sicnu::data
