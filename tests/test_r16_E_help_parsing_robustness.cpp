/***************************************************************************
 * tests/test_r16_E_help_parsing_robustness.cpp — R16-E T09 WP3
 *
 * Boundary tests for the help layer's JSON parsing paths, driven through
 * the PUBLIC loader surface (HelpContentStore::loadFromDirectory /
 * TerminologyProvider::loadFromJson / loadFromFile), HelpSearchIndex::
 * tokenize and ErrorDiagnosticsBridge's code scan.
 *
 * The oracle for every case: NO crash, NO hang, and NO exception escaping
 * the typed boundary (LoadResult::errors / a normally returned LoadResult).
 *
 * RED on master: a glossary with JSON nesting deeper than jsoncpp's default
 * stackLimit (256) makes Json::parseFromStream THROW Json::LogicError, and
 * TerminologyProvider::loadFromJson has no guard — the exception escapes and
 * terminates the process (composeHelpSystem calls loadFromResources at
 * startup). Fixed in this track by bounding the reader's stackLimit and
 * catching the violation into the typed error list, matching what
 * HelpContentStore's loader already does.
 *
 * Empirical jsoncpp facts these cases are pinned against (verified against
 * the system jsoncpp on this host before authoring):
 *   - default stackLimit = 256; exceeding it throws.
 *   - default skipBom = true for the UTF-8 BOM; a UTF-16 LE BOM (FF FE) is a
 *     plain syntax error (typed).
 *   - raw invalid UTF-8 bytes inside strings are ACCEPTED by the parser; the
 *     conversion to QString yields replacement characters (typed, no crash).
 *   - huge exponents / oversized integers parse as doubles without throwing.
 ***************************************************************************/
#include "help/error_diagnostics_bridge.h"
#include "help/help_content_store.h"
#include "help/help_registry.h"
#include "help/help_search_index.h"
#include "help/terminology_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <chrono>

using namespace sicnu::help;

namespace
{

/// Writes one content file into a fresh temp dir and loads that dir through
/// the public directory loader (end-to-end: file IO + parse + register).
sicnu::help::HelpContentStore::LoadResult
loadContent( const QByteArray &bytes, const char *name = "content.json" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QFile f( dir.filePath( QString::fromLatin1( name ) ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
    f.close();
    return HelpContentStore::loadFromDirectory( dir.path() );
}

/// Bounded-wall-clock guard: the lambda must finish within the budget or the
/// case fails (a pathological re-parse or regex backtrack shows up as a hang
/// here rather than a stuck CI job).
template <typename F>
auto withinMs( int budgetMs, F &&fn ) -> decltype( fn() )
{
    const auto started = std::chrono::steady_clock::now();
    auto result = fn();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started );
    INFO( "elapsed ms: " << elapsed.count() << " (budget " << budgetMs << ")" );
    CHECK( elapsed.count() <= budgetMs );
    return result;
}

} // namespace

// THE robustness gap this track closes: deep nesting must be a typed load
// error, never an escaping Json::LogicError. RED on master (the process
// aborts with an uncaught exception before any CHECK runs).
TEST_CASE( "terminology glossary rejects deep nesting as a typed error",
           "[r16e][help][wp3][robustness]" )
{
    // 300 levels > jsoncpp default stackLimit (256): parseFromStream throws
    // unless the reader is configured + guarded.
    const QByteArray deep = QByteArray( 300, '[' ) + QByteArray( 300, ']' );

    const TerminologyProvider::LoadResult result = withinMs( 5000, [&] {
        return TerminologyProvider::loadFromJson( deep, QStringLiteral( "hostile.json" ) );
    } );

    CHECK( result.terms.isEmpty() );
    REQUIRE( result.errors.size() >= 1 );
    CHECK( result.errors.first().contains( QStringLiteral( "hostile.json" ) ) );
}

// Same input class through the content store's public loader: its
// stackLimit=64 guard plus try/catch must already yield a typed error
// (regression pin — proves the two loaders behave identically after the fix).
TEST_CASE( "content store rejects deep nesting as a typed error",
           "[r16e][help][wp3][robustness]" )
{
    const auto result = withinMs( 5000, [&] {
        return loadContent( QByteArray( 300, '[' ) + QByteArray( 300, ']' ) );
    } );
    REQUIRE( result.errors.size() >= 1 );
    CHECK( result.errors.first().contains( QStringLiteral( "content.json" ) ) );
    CHECK( result.registry.count() == 0 );
}

TEST_CASE( "loaders survive byte-order marks", "[r16e][help][wp3][robustness]" )
{
    // UTF-16 LE BOM in front of a valid array: jsoncpp cannot read it — the
    // requirement is a typed parse error, not a crash.
    const QByteArray u16bom = QByteArray::fromHex( "fffe" ) + R"([{"en":"x"}])";

    const TerminologyProvider::LoadResult glossary =
        TerminologyProvider::loadFromJson( u16bom, QStringLiteral( "bom.json" ) );
    CHECK( glossary.terms.isEmpty() );
    CHECK( glossary.errors.size() >= 1 );

    const auto store = loadContent( u16bom );
    CHECK( store.registry.count() == 0 );
    CHECK( store.errors.size() >= 1 );

    // UTF-8 BOM is skipped by jsoncpp (skipBom default true): valid content
    // behind it must LOAD (not be rejected), for both loaders.
    const auto store8 = loadContent(
        QByteArray::fromHex( "efbbbf" )
            + R"([{"id":"concept.test_bom","title":"t","summary":"s"}])",
        "bom8.json" );
    CHECK( store8.errors.isEmpty() );
    CHECK( store8.registry.count() == 1 );

    const TerminologyProvider::LoadResult glossary8 = TerminologyProvider::loadFromJson(
        QByteArray::fromHex( "efbbbf" )
            + R"([{"en":"ndvi","zh":"NDVI","definition_zh":"归一化植被指数。","category":"classification"}])",
        QStringLiteral( "bom8.json" ) );
    CHECK( glossary8.errors.isEmpty() );
    REQUIRE( glossary8.terms.size() == 1 );
}

TEST_CASE( "loaders tolerate invalid UTF-8 bytes without crashing",
           "[r16e][help][wp3][robustness]" )
{
    // jsoncpp accepts raw invalid UTF-8 inside strings (verified against the
    // system build); the loader must convert to QString without crashing and
    // the entry stays loadable with replacement characters.
    const auto result = withinMs( 5000, [&] {
        return loadContent(
            "[{\"id\":\"concept.bad_utf8\",\"title\":\"\xFF\xFE\x80ab\",\"summary\":\"s\"}]" );
    } );
    CHECK( result.errors.isEmpty() );
    REQUIRE( result.registry.count() == 1 );
    const HelpDescriptor *d = result.registry.find( QStringLiteral( "concept.bad_utf8" ) );
    REQUIRE( d );
    // non-empty, whatever the replacement shaping is — no assert on exact glyphs
    CHECK_FALSE( d->title.isEmpty() );

    // Same for the glossary path (en/zh fields).
    const TerminologyProvider::LoadResult glossary = withinMs( 5000, [&] {
        return TerminologyProvider::loadFromJson(
            "[{\"en\":\"bad\xFF\",\"zh\":\"坏\xFE\",\"definition_zh\":\"d\",\"category\":\"terrain\"}]",
            QStringLiteral( "bad.json" ) );
    } );
    CHECK( glossary.errors.isEmpty() );
    REQUIRE( glossary.terms.size() == 1 );
}

TEST_CASE( "huge fields and huge field names parse within bounds",
           "[r16e][help][wp3][robustness]" )
{
    // 2 MiB title + 1 MiB unknown field name in one entry.
    const QByteArray bigTitle( 2 * 1024 * 1024, 'x' );
    const QByteArray bigName( 1024 * 1024, 'n' );
    const auto result = withinMs( 30000, [&] {
        return loadContent( "[{\"id\":\"concept.big\",\"title\":\"" + bigTitle + "\",\""
                            + bigName + "\":1,\"summary\":\"s\"}]" );
    } );
    CHECK( result.errors.isEmpty() );
    REQUIRE( result.registry.count() == 1 );
    const HelpDescriptor *d = result.registry.find( QStringLiteral( "concept.big" ) );
    REQUIRE( d );
    CHECK( d->title.size() == bigTitle.size() );

    // ~2 MiB CJK glossary definition: load must not exhibit quadratic
    // behavior. "植" is E6 A4 8D in UTF-8; 700000 reps ≈ 2 MB.
    QByteArray bigDef;
    bigDef.reserve( 3 * 700000 );
    for ( int i = 0; i < 700000; ++i )
        bigDef += "\xe6\xa4\x8d";
    const TerminologyProvider::LoadResult glossary = withinMs( 30000, [&] {
        return TerminologyProvider::loadFromJson(
            "[{\"en\":\"bigterm\",\"zh\":\"大条目\",\"definition_zh\":\"" + bigDef
                + "\",\"category\":\"terrain\"}]",
            QStringLiteral( "big.json" ) );
    } );
    CHECK( glossary.errors.isEmpty() );
    REQUIRE( glossary.terms.size() == 1 );
    CHECK( glossary.terms.first().definitionZh.size() >= 600000 );
}

TEST_CASE( "numeric values where strings are expected are typed errors",
           "[r16e][help][wp3][robustness]" )
{
    // jsoncpp stores 1e999 / huge integers as doubles without throwing
    // (verified); the store's readString() then sees a non-string member and
    // the entry is rejected or the field read as empty — never a crash.
    const auto result = withinMs( 5000, [&] {
        return loadContent(
            R"([{"id":"concept.numeric","title":12345,"summary":1e999,"deprecated":"yes"},)"
            R"({"id":999,"title":"numeric id"}])" );
    } );
    // numeric id -> "invalid id" typed error; the concept with numeric title
    // registers but with an empty title (typed skip, no crash)
    CHECK( result.errors.size() >= 1 );
    CHECK( result.registry.count() <= 1 );
}

TEST_CASE( "cjk and combining characters survive tokenize",
           "[r16e][help][wp3][robustness]" )
{
    // combining marks (U+0300..) are neither CJK nor letter/number for token
    // purposes — they must be dropped without crashing or hanging; astral
    // CJK extension-B chars (surrogate pairs) must stay intact inside bigrams.
    const QString text = QStringLiteral( "植被指\u0301数𠀀測試 radiation 植被" );
    const QStringList tokens = withinMs( 2000, [&] { return HelpSearchIndex::tokenize( text ); } );
    CHECK( tokens.contains( QStringLiteral( "radiation" ) ) );
    CHECK( tokens.join( u';' ).contains( QChar( 0xD840 ) ) ); // high surrogate of 𠀀 kept intact

    // 512 KiB of VARIED CJK through tokenize: linear cost, bounded wall
    // clock. (A varied alphabet matters: tokenize() deduplicates, so a
    // repeating 4-char pattern would collapse to a handful of tokens however
    // long the input — the cycle below keeps ~15k distinct bigrams hot.)
    QString big;
    big.reserve( 512 * 1024 );
    for ( int i = 0; i < 512 * 1024; ++i )
        big += QChar( ushort( 0x4E00 + ( i % 15000 ) ) ); // CJK unified block
    const QStringList bigTokens = withinMs( 20000, [&] { return HelpSearchIndex::tokenize( big ); } );
    CHECK( bigTokens.size() >= 10000 );

    // glossary entries with combining marks load and look up.
    const TerminologyProvider::LoadResult result = TerminologyProvider::loadFromJson(
        "[{\"en\":\"ve\uCC8Agetation\",\"zh\":\"植被\\u0301\",\"definition_zh\":\"d\",\"category\":\"terrain\"}]",
        QStringLiteral( "combining.json" ) );
    CHECK( result.errors.isEmpty() );
    REQUIRE( result.terms.size() == 1 );
}

TEST_CASE( "diagnostics bridge handles hostile failure text",
           "[r16e][help][wp3][robustness]" )
{
    // one curated diagnostic page so the bridge table is non-empty
    const auto content = loadContent(
        R"([{"id":"diagnostic.harness.hostile_probe","family":"harness","code":"HOSTILE_PROBE",)"
        R"("title":"t","whatHappened":"w","whyItMatters":"m","severity":"error","retry":"derived"}])" );
    REQUIRE( content.errors.isEmpty() );
    REQUIRE( content.registry.count() == 1 );

    const ErrorDiagnosticsBridge bridge( content.registry );

    // regex-hostile 512 KiB CamelCase hump chain: must complete in bounds
    QString humps;
    humps.reserve( 512 * 1024 );
    for ( int i = 0; i < 65536; ++i )
        humps += QStringLiteral( "AaBa" );
    const QString id1 =
        withinMs( 10000, [&] { return bridge.helpIdFromMessage( humps ); } );
    CHECK( id1.isEmpty() ); // no curated code matches the noise

    // 256 KiB of UPPER_SNAKE filler, with the curated code buried at the end
    QString snake;
    snake.reserve( 256 * 1024 );
    for ( int i = 0; i < 8192; ++i )
        snake += QStringLiteral( "XX_YY_ZZ " );
    snake += QStringLiteral( " HOSTILE_PROBE" );
    const QString id2 =
        withinMs( 10000, [&] { return bridge.helpIdFromMessage( snake ); } );
    CHECK( id2 == QStringLiteral( "diagnostic.harness.hostile_probe" ) );
}

TEST_CASE( "absurd paths are typed errors", "[r16e][help][wp3][robustness]" )
{
    // >4096-char path: the directory loader reports the missing directory,
    // the file loader the unopenable file — both typed, neither crashing.
    const QString longDir =
        QStringLiteral( "/tmp/t09-does-not-exist/" ) + QString( 5000, QChar( u'a' ) );

    const auto store = HelpContentStore::loadFromDirectory( longDir );
    REQUIRE( store.errors.size() >= 1 );
    CHECK( store.errors.first().contains( QStringLiteral( "content directory missing" ) ) );

    const TerminologyProvider::LoadResult glossary =
        TerminologyProvider::loadFromFile( longDir + QStringLiteral( "/content.json" ) );
    CHECK( glossary.terms.isEmpty() );
    REQUIRE( glossary.errors.size() >= 1 );
    CHECK( glossary.errors.first().contains( QStringLiteral( "cannot open" ) ) );
}
