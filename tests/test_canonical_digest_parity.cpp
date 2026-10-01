// tests/test_canonical_digest_parity.cpp
//
// #1387 digest-authority convergence — the cross-path parity lock.
//
// One fixed document, digested through EVERY canonical path that existed as
// a fork, must yield one hex digest (spirit of the three-surface parity
// lock, commit fe1c66631):
//   A — the authority: sicnu::data::canonicalizeJsonRfc8785 (QJsonObject);
//   B — the teaching-admin fork: sicnu::teaching_admin::canonicalJsonBytes,
//       which now DELEGATES to A (curriculum/labSpec/rules/pack/release
//       digests ride on it);
//   C — the jsoncpp copy-paste family (harness projection digest, cartography
//       export manifest digest, workflow-explain byte accounting), now the
//       authority's jsoncpp adapter overload.
//   D — the plan-optimizer lineage digest (workflow cache keys, CSE, resume
//       stamps): node parameters canonicalized through a Qt-Compact local
//       ("compact document == canonical"), now a direct call into A. The
//       local was byte-equivalent to A on the current Qt, so no signature
//       value moves — what converges is the SECOND implementation.
//
// Oracle discipline: the canonical bytes AND the digest of the fixed
// document are pinned as golden constants here (determinism by re-hash, not
// by re-running a second implementation). When the authority's number or
// ordering format intentionally changes, re-pin the constants in the same
// commit — a drift without a re-pin fails this test, which is the point.
//
// The document deliberately carries the values that made the forks diverge:
// integer-valued doubles (dpi 300.0 → "300"), -0 (→ "0"), exponent-form
// doubles, escaped/unicode/non-BMP strings, and two KEYS whose UTF-16 order
// (U+1D7D8 before U+FF01) is the reverse of jsoncpp's native UTF-8-byte
// order — proof the jsoncpp path now sorts like the authority, not like
// jsoncpp.
//
// Charset discipline: every non-ASCII byte is written as \xNN — narrow-literal
// \uXXXX escapes are compiler-codepage-converted and would not be byte-stable
// across toolchains (MSVC).

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <json/json.h>

#include <string>

#include "data/execution_fingerprint.h"
#include "teaching_admin/json_util.h"
#include "workflow/plan_optimizer.h"

using sicnu::data::canonicalizeJsonRfc8785;

namespace
{

// U+1D7D8 MATHEMATICAL DOUBLE-STRUCK DIGIT ZERO (UTF-8 f0 9d 9f 98).
const QString kNonBmpKey = QString::fromUtf8( "\xf0\x9d\x9f\x98" );
// U+FF01 FULLWIDTH EXCLAMATION MARK (UTF-8 ef bc 81): UTF-8-before the
// non-BMP key, UTF-16-after it — the ordering fork made visible.
const QString kFullwidthKey = QString::fromUtf8( "\xef\xbc\x81" );

/// The fixed document. Insertion order is deliberately scrambled: every path
/// must sort, so no path may depend on it.
QJsonObject fixtureDocument()
{
    QJsonObject doc;
    doc.insert( QStringLiteral( "zeta" ), QStringLiteral( "plain" ) );
    doc.insert( QStringLiteral( "alpha" ), 42 );
    doc.insert( QStringLiteral( "pi" ), 0.3 );
    doc.insert( QStringLiteral( "neg_zero" ), -0.0 );
    doc.insert( QStringLiteral( "big" ), 1e21 );
    doc.insert( QStringLiteral( "tiny" ), 1e-7 );
    doc.insert( QStringLiteral( "dpi" ), 300.0 );
    doc.insert( QStringLiteral( "esc" ),
                QStringLiteral( "quote\" back\\slash\nnewline\ttab" ) );
    doc.insert( QStringLiteral( "cjk" ), QString::fromUtf8( "\xe4\xb8\xad\xe6\x96\x87" ) );
    doc.insert( QStringLiteral( "emoji" ), kNonBmpKey );
    doc.insert( QStringLiteral( "flag" ), true );
    doc.insert( QStringLiteral( "nada" ), QJsonValue::Null );
    QJsonArray arr;
    arr.append( 1 );
    arr.append( QStringLiteral( "two" ) );
    arr.append( 3.5 );
    arr.append( false );
    arr.append( QJsonValue::Null );
    QJsonObject nested;
    nested.insert( QStringLiteral( "y" ), 2 );
    nested.insert( QStringLiteral( "x" ), 1 );
    doc.insert( QStringLiteral( "nested" ), nested );
    doc.insert( QStringLiteral( "arr" ), arr );
    QJsonObject inner;
    inner.insert( QStringLiteral( "b" ), QStringLiteral( "B" ) );
    QJsonArray pair;
    pair.append( QStringLiteral( "z" ) );
    pair.append( QStringLiteral( "y" ) );
    inner.insert( QStringLiteral( "a" ), pair );
    QJsonArray deep;
    deep.append( inner );
    deep.append( QStringLiteral( "tail" ) );
    doc.insert( QStringLiteral( "deep" ), deep );
    // Inserted in the UTF-8-sorted order on purpose: the canonical output
    // must nonetheless list the non-BMP key first (UTF-16 order).
    doc.insert( kFullwidthKey, 1 );
    doc.insert( kNonBmpKey, 2 );
    return doc;
}

/// The same content as a jsoncpp tree — with a DIFFERENT insertion order, so
/// the adapter must sort, not inherit jsoncpp's member order.
Json::Value fixtureJsoncpp()
{
    Json::Value doc( Json::objectValue );
    doc["zeta"] = "plain";
    doc["alpha"] = 42;
    doc["pi"] = 0.3;
    doc["neg_zero"] = -0.0;
    doc["big"] = 1e21;
    doc["tiny"] = 1e-7;
    doc["dpi"] = 300.0;
    doc["esc"] = "quote\" back\\slash\nnewline\ttab";
    doc["cjk"] = std::string( "\xe4\xb8\xad\xe6\x96\x87" );
    doc["emoji"] = std::string( "\xf0\x9d\x9f\x98" );
    doc["flag"] = true;
    doc["nada"] = Json::Value::null;
    Json::Value arr( Json::arrayValue );
    arr.append( 1 );
    arr.append( "two" );
    arr.append( 3.5 );
    arr.append( false );
    arr.append( Json::Value::null );
    Json::Value nested( Json::objectValue );
    nested["y"] = 2;
    nested["x"] = 1;
    doc["nested"] = nested;
    doc["arr"] = arr;
    Json::Value inner( Json::objectValue );
    inner["b"] = "B";
    Json::Value pair( Json::arrayValue );
    pair.append( "z" );
    pair.append( "y" );
    inner["a"] = pair;
    Json::Value deep( Json::arrayValue );
    deep.append( inner );
    deep.append( "tail" );
    doc["deep"] = deep;
    doc[std::string( "\xef\xbc\x81" )] = 1;
    doc[std::string( "\xf0\x9d\x9f\x98" )] = 2;
    return doc;
}

std::string sha256Hex( const QByteArray &bytes )
{
    return QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex().toStdString();
}

} // namespace

TEST_CASE( "canonical digest parity: every path hashes the same bytes",
           "[data][teaching_admin][canonical][1387]" )
{
    const QJsonObject doc = fixtureDocument();

    // A — the authority.
    const QByteArray canonical = canonicalizeJsonRfc8785( doc );
    const std::string digestA = sha256Hex( canonical );

    // B — the teaching-admin fork delegates to the authority.
    const std::string digestB =
        sicnu::teaching_admin::sha256Hex( sicnu::teaching_admin::canonicalJsonBytes( doc ) )
            .toStdString();
    CHECK( digestB == digestA );

    // C — the jsoncpp-family adapter.
    const std::string digestC = sha256Hex( canonicalizeJsonRfc8785( fixtureJsoncpp() ) );
    CHECK( digestC == digestA );

    // D — the plan-optimizer lineage path: the node signature of a parentless
    // node is SHA-256( operatorId \x1f canonical(params) ); its parameters
    // must canonicalize to the same bytes as every other path.
    sicnu::workflow::NodeFact node;
    node.nodeId = QStringLiteral( "n1" );
    node.operatorId = QStringLiteral( "rs:test_op" );
    node.parameters = doc;
    const QString lineage = node.operatorId + QLatin1Char( '\x1f' )
        + QString::fromUtf8( canonical );
    const std::string digestD =
        sicnu::workflow::WorkflowPlanOptimizer::computeNodeSignature( node, {}, {} )
            .toStdString();
    CHECK( digestD == sha256Hex( lineage.toUtf8() ) );

    // The pinned golden: one fixed document, one canonical byte string.
    const QByteArray golden =
        "{\"alpha\":42,\"arr\":[1,\"two\",3.5,false,null],\"big\":1e+21,"
        "\"cjk\":\"\xe4\xb8\xad\xe6\x96\x87\",\"deep\":[{\"a\":[\"z\",\"y\"],\"b\":\"B\"},"
        "\"tail\"],\"dpi\":300,\"emoji\":\"\xf0\x9d\x9f\x98\","
        "\"esc\":\"quote\\\" back\\\\slash\\nnewline\\ttab\",\"flag\":true,"
        "\"nada\":null,\"neg_zero\":0,\"nested\":{\"x\":1,\"y\":2},\"pi\":0.3,"
        "\"tiny\":1e-07,\"zeta\":\"plain\",\"\xf0\x9d\x9f\x98\":2,\"\xef\xbc\x81\":1}";
    CHECK( canonical == golden );
    CHECK( digestA == "f844b76255becf5444b5caea088e7cbbc119b9f286155148a5232ff6466e4843" );
}

TEST_CASE( "canonical serialization is insertion-order independent",
           "[data][canonical][1387]" )
{
    QJsonObject forward;
    forward.insert( QStringLiteral( "a" ), 1 );
    forward.insert( QStringLiteral( "b" ), 2 );

    QJsonObject reversed;
    reversed.insert( QStringLiteral( "b" ), 2 );
    reversed.insert( QStringLiteral( "a" ), 1 );

    CHECK( canonicalizeJsonRfc8785( forward ) == canonicalizeJsonRfc8785( reversed ) );
    CHECK( canonicalizeJsonRfc8785( forward ).toStdString() == "{\"a\":1,\"b\":2}" );
}
