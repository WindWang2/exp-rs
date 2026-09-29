// test_dataset_fingerprint_determinism.cpp — Track 13 R4 WP-A: dataset
// fingerprint determinism under checkout-level byte perturbation.
//
// #1336 lesson (applied to the dataset domain): the same semantic manifest
// reaches the fingerprint entry with different BYTES depending on checkout
// configuration (core.autocrlf → CRLF vs LF), editor BOM insertion, trailing
// newline conventions and JSON spelling. A fingerprint is a content identity,
// so it must be a function of the parsed semantic payload only — never of
// those byte-level differences.
//
// Independent authorities used here (never the code-under-test's own logic):
//   - known-answer vectors: SHA-256 over a hand-canonicalized payload,
//     computed off-line with an independent hashlib implementation;
//   - double-run consistency: two computations over identical input;
//   - the perturbation matrix: byte-variant pairs of one semantic payload.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_fingerprint.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store_impl.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QJsonValue>

#include <functional>

using namespace sicnu::dataset;

namespace
{

/// The shared semantic payload of the whole matrix. Contains every JSON
/// value family the canonicalizer must agree on across spellings: strings
/// (ASCII + non-ASCII), integers inside and beyond 32 bits, a shortest
/// round-trip decimal, booleans, null, a nested object and an array.
QJsonObject basePayload()
{
    QJsonObject inner;
    inner.insert( QStringLiteral( "text" ), QStringLiteral( "\u4e2d\u6587 b\u00e9ta" ) );
    inner.insert( QStringLiteral( "ratio" ), 0.1 );
    inner.insert( QStringLiteral( "big" ), static_cast<qint64>( 250000 ) );
    QJsonObject payload;
    payload.insert( QStringLiteral( "dataset_id" ),
                    QStringLiteral( "0a0a0a0a-1111-4222-8333-444444444444" ) );
    payload.insert( QStringLiteral( "schema_version" ), 1 );
    payload.insert( QStringLiteral( "name" ), QStringLiteral( "matrix" ) );
    payload.insert( QStringLiteral( "flag" ), true );
    payload.insert( QStringLiteral( "nothing" ), QJsonValue::Null );
    payload.insert( QStringLiteral( "count" ), 2.5 );
    payload.insert( QStringLiteral( "inner" ), inner );
    payload.insert( QStringLiteral( "tags" ),
                    QJsonArray{ QStringLiteral( "a" ), QStringLiteral( "b" ) } );
    return payload;
}

/// Compact JSON text of @p object — the reference spelling.
QByteArray compactText( const QJsonObject &object )
{
    return QJsonDocument( object ).toJson( QJsonDocument::Compact );
}

DatasetFingerprint fingerprintOfText( const QByteArray &text )
{
    const QJsonObject parsed = textToJson( QString::fromUtf8( text ) );
    if ( parsed.isEmpty() )
        return DatasetFingerprint();
    return makeDatasetFingerprint( parsed );
}

/// One perturbation group: two byte spellings of the SAME semantic payload.
struct PerturbationGroup
{
    const char *name;
    std::function<QByteArray( const QJsonObject & )> spellingA;
    std::function<QByteArray( const QJsonObject & )> spellingB;
    /// False for rows that vary VALUE spellings (both spellings produce a
    /// payload distinct from the base one): the pair must agree with each
    /// other, not with the base reference.
    bool sameAsBase = true;
};

/// Pretty-print with @p indent and @p newline conventions, mimicking what a
/// Windows/macOS/Linux editor or git produces for the same logical document.
/// @p bom and @p trailing prepend/append raw bytes the way checkout filters,
/// editors and concatenation do.
QByteArray reindented( const QJsonObject &object, const QByteArray &indent,
                       const QByteArray &newline, const QByteArray &bom = QByteArray(),
                       const QByteArray &trailing = QByteArray() )
{
    QByteArray text = bom;
    text += QJsonDocument( object ).toJson( QJsonDocument::Indented );
    text.replace( "\n", newline );
    text.replace( "    ", indent );
    text += trailing;
    return text;
}

} // namespace

TEST_CASE( "dataset fingerprint known-answer vector (independent authority)",
           "[dataset][fingerprint][determinism]" )
{
    // Hand-canonicalized form of basePayload(): keys sorted (UTF-16 code
    // unit order), no whitespace, integers as plain integers, shortest
    // round-trip decimals, non-ASCII as raw UTF-8 bytes (Qt's JSON writer
    // does not emit \u escapes). SHA-256 computed off-line (hashlib over
    // these exact bytes), never by the code under test.
    const QByteArray canonical =
        "{\"count\":2.5,\"dataset_id\":\"0a0a0a0a-1111-4222-8333-444444444444\","
        "\"flag\":true,\"inner\":{\"big\":250000,\"ratio\":0.1,"
        "\"text\":\"\xe4\xb8\xad\xe6\x96\x87 b\xc3\xa9ta\"},\"name\":\"matrix\","
        "\"nothing\":null,\"schema_version\":1,\"tags\":[\"a\",\"b\"]}";
    const QByteArray expectedHex =
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex();
    // Off-line reference: ff5f7f566493f670…a2cb (hashlib, UTF-8 bytes above).
    CHECK( QString::fromUtf8( expectedHex ) ==
           QStringLiteral( "ff5f7f566493f670c7615839ce9cf14c1fe85443834bf2f9f309a80f1236a2cb" ) );

    // The implementation must reproduce the off-line digest byte-for-byte.
    INFO( "canonical bytes: " << canonical.toHex().toStdString() );
    CHECK( makeDatasetFingerprint( basePayload() ).toHex() ==
           QString::fromUtf8( expectedHex ) );

    // The recorded fingerprint field is exempt (self-reference guard) and
    // foreign values in it do not change the content identity.
    QJsonObject stamped = basePayload();
    stamped.insert( QStringLiteral( "fingerprint" ), QStringLiteral( "deadbeef" ) );
    CHECK( makeDatasetFingerprint( stamped ).toHex() ==
           makeDatasetFingerprint( basePayload() ).toHex() );
}

TEST_CASE( "dataset fingerprint double-run and store round-trip consistency",
           "[dataset][fingerprint][determinism]" )
{
    const QJsonObject payload = basePayload();
    const QString hexA = makeDatasetFingerprint( payload ).toHex();
    const QString hexB = makeDatasetFingerprint( payload ).toHex();
    CHECK( hexA == hexB );
    CHECK( hexA.size() == 64 );

    // The store's text funnel is the ingest boundary for every persisted
    // dataset payload; a round-trip through it must not move the identity.
    const QJsonObject reparsed = textToJson( jsonToText( payload ) );
    REQUIRE( !reparsed.isEmpty() );
    CHECK( makeDatasetFingerprint( reparsed ).toHex() == hexA );
}

TEST_CASE( "dataset fingerprint is invariant under checkout byte perturbation",
           "[dataset][fingerprint][determinism][matrix]" )
{
    const QJsonObject payload = basePayload();
    const QString reference = makeDatasetFingerprint( payload ).toHex();

    const QVector<PerturbationGroup> groups = {
        { "LF vs CRLF line endings",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n" ); },
          []( const QJsonObject &o ) { return reindented( o, "  ", "\r\n" ); } },
        { "CRLF vs CR line endings",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\r\n" ); },
          []( const QJsonObject &o ) { return reindented( o, "  ", "\r" ); } },
        { "no trailing newline vs trailing LF",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n" ); },
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n", {}, "\n" ); } },
        { "trailing LF vs trailing CRLF",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n", {}, "\n" ); },
          []( const QJsonObject &o ) { return reindented( o, "  ", "\r\n", {}, "\r\n" ); } },
        { "UTF-8 BOM vs no BOM",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n" ); },
          []( const QJsonObject &o ) {
              return reindented( o, "  ", "\n", "\xEF\xBB\xBF" );
          } },
        { "BOM + CRLF combined",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n" ); },
          []( const QJsonObject &o ) {
              return reindented( o, "  ", "\r\n", "\xEF\xBB\xBF", "\r\n" );
          } },
        { "4-space indent vs tab indent",
          []( const QJsonObject &o ) { return reindented( o, "    ", "\n" ); },
          []( const QJsonObject &o ) { return reindented( o, "\t", "\n" ); } },
        { "pretty vs compact spelling",
          []( const QJsonObject &o ) { return reindented( o, "  ", "\n" ); },
          []( const QJsonObject &o ) { return compactText( o ); } },
        { "key order reversed at the byte level (flat document)",
          []( const QJsonObject & ) {
              // Hand-serialized spellings: QJsonObject re-sorts after parse,
              // so the variant must exist in the BYTES to be meaningful —
              // the same pairs, reverse order, as an editor might write.
              return QByteArray(
                  "{\"zz_last\":1,\"middle\":2.5,\"aa_first\":true,\"s\":\"x\"}" );
          },
          []( const QJsonObject & ) {
              return QByteArray(
                  "{\"aa_first\":true,\"middle\":2.5,\"s\":\"x\",\"zz_last\":1}" );
          },
          false },
        { "integer spellings 1 / 1.0 / 1e0 agree",
          []( const QJsonObject &o ) {
              return compactText( o ).replace( "\"count\":2.5", "\"count\":1e0" );
          },
          []( const QJsonObject &o ) {
              return compactText( o ).replace( "\"count\":2.5", "\"count\":1.0" );
          },
          false },
        { "zero spellings 0 / -0.0 agree",
          []( const QJsonObject &o ) {
              return compactText( o ).replace( "\"count\":2.5", "\"count\":0" );
          },
          []( const QJsonObject &o ) {
              return compactText( o ).replace( "\"count\":2.5", "\"count\":-0.0" );
          },
          false },
        { "decimal spellings 0.1 / 1e-1 agree",
          []( const QJsonObject &o ) { return compactText( o ); },
          []( const QJsonObject &o ) {
              return compactText( o ).replace( "\"ratio\":0.1", "\"ratio\":1e-1" );
          },
          false },
    };

    for ( const PerturbationGroup &group : groups )
    {
        INFO( "group: " << group.name );
        const DatasetFingerprint a = fingerprintOfText( group.spellingA( payload ) );
        const DatasetFingerprint b = fingerprintOfText( group.spellingB( payload ) );
        REQUIRE( a.isValid() );
        REQUIRE( b.isValid() );
        if ( group.sameAsBase )
        {
            CHECK( a.toHex() == reference );
            CHECK( b.toHex() == reference );
        }
        CHECK( a.toHex() == b.toHex() );
    }
}

TEST_CASE( "dataset fingerprint: escape spelling and canonical idempotence",
           "[dataset][fingerprint][determinism][matrix]" )
{
    // The \uXXXX-escaped spelling and the literal UTF-8 spelling are the
    // same string after parse — one payload, two byte spellings.
    const QJsonObject payload = basePayload();
    const QString reference = makeDatasetFingerprint( payload ).toHex();

    QJsonObject literalVariant = payload;
    literalVariant.insert( QStringLiteral( "name" ), QStringLiteral( "\u4e2d\u6587" ) );
    QByteArray escapedBytes = compactText( literalVariant );
    // Rewrite the raw UTF-8 bytes of the name value into their \uXXXX
    // spellings — the escape form exists at the JSON TEXT level only.
    escapedBytes.replace( QString::fromUtf8( "\u4e2d\u6587" ).toUtf8(),
                          QByteArray( "\\u4e2d\\u6587" ) );
    CHECK( escapedBytes.contains( "\\u4e2d" ) );
    CHECK( fingerprintOfText( escapedBytes ).toHex() ==
           fingerprintOfText( compactText( literalVariant ) ).toHex() );

    // Canonical form re-ingests to the same identity (idempotence of the
    // canonical bytes through the funnel).
    const QJsonObject canonicalReparse =
        textToJson( QString::fromUtf8( compactText( payload ) ) );
    REQUIRE( !canonicalReparse.isEmpty() );
    CHECK( makeDatasetFingerprint( canonicalReparse ).toHex() == reference );
}

TEST_CASE( "dataset fingerprint treats content-level differences as content",
           "[dataset][fingerprint][determinism][matrix]" )
{
    // Separator spelling INSIDE a string value is a semantic difference
    // (git never rewrites it on checkout): the fingerprint must differ.
    // This row pins the boundary of the invariance matrix — equal
    // fingerprints are promised for byte-level variance only.
    QJsonObject slash = basePayload();
    slash.insert( QStringLiteral( "name" ), QStringLiteral( "a/b" ) );
    QJsonObject backslash = basePayload();
    backslash.insert( QStringLiteral( "name" ), QStringLiteral( "a\\b" ) );
    CHECK( makeDatasetFingerprint( slash ).toHex() !=
           makeDatasetFingerprint( backslash ).toHex() );
}

TEST_CASE( "binary or NUL-bearing ingest never yields the intact fingerprint",
           "[dataset][fingerprint][determinism][binary]" )
{
    // #1336-equivalent binary guard for the dataset domain: a corrupted
    // (NUL-bearing / truncated) document can parse to a typed failure OR be
    // accepted with insignificant NULs treated as whitespace — the only
    // forbidden outcome is a fingerprint EQUAL to the intact payload's
    // while the content differs.
    const QString reference = makeDatasetFingerprint( basePayload() ).toHex();

    // NUL INSIDE a string value is a content change: the funnel must never
    // hand back the intact payload.
    QByteArray nulInString = compactText( basePayload() );
    nulInString.replace( "matrix", QByteArray( "mat\0rix" ) );
    CHECK( nulInString != compactText( basePayload() ) );
    const DatasetFingerprint nulFingerprint =
        fingerprintOfText( nulInString );
    CHECK( ( !nulFingerprint.isValid() || nulFingerprint.toHex() != reference ) );

    // A truncated document never yields the intact fingerprint either.
    QByteArray truncated = compactText( basePayload() );
    truncated.chop( 8 );
    const DatasetFingerprint truncatedFingerprint =
        fingerprintOfText( truncated );
    CHECK( ( !truncatedFingerprint.isValid() || truncatedFingerprint.toHex() != reference ) );

    // Downstream: an empty funnel result is rejected by the strict manifest
    // reader (typed failure), it does not become fake data.
    CHECK( !DatasetManifest::fromJson( QJsonObject{} ).has_value() );
}
