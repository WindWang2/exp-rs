/***************************************************************************
 * tests/test_r16_E_help_terminology_regression.cpp — R16-E T09 WP5
 *
 * Regression lane for issue #1465 (help/science_context cluster):
 *   "[P2] src/help/terminology_provider.cpp:179 — the `_2` dedup suffix
 *    computed at :166-173 only feeds the related-resolution table while
 *    registration still uses the unsuffixed id: duplicate en entries
 *    overwrite each other and relatedIds dangle."
 *
 * The fix (assignedIds, e710cec86) is already on master — these cases pin it
 * so the exact symptom cannot recur, and extend beyond the existing single
 * pin in test_help_core.cpp:
 *   - N-way duplicate en terms each get a distinct suffixed id (no overwrite,
 *     count preserved);
 *   - slug collisions between DIFFERENT en spellings are suffixed too;
 *   - related[] entries that point at a duplicated en resolve to the FIRST
 *     duplicate's assigned id (no dangling relatedIds);
 *   - the end-to-end path (loadFromJson -> appendDescriptors) reports the
 *     data-level duplicate as a load error while still registering every
 *     entry — content guard fires, composition never drops knowledge.
 ***************************************************************************/
#include "help/help_registry.h"
#include "help/terminology_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>
#include <QVector>

using namespace sicnu::help;

namespace
{

TermEntry makeTerm( const QString &en, const QString &zh, const char *category,
                    const QStringList &related = {} )
{
    TermEntry t;
    t.en = en;
    t.zh = zh;
    t.definitionZh = QStringLiteral( "关于 %1 的课堂定义。" ).arg( en );
    t.category = QString::fromLatin1( category );
    t.related = related;
    return t;
}

} // namespace

TEST_CASE( "terminology dedup: n-way duplicate en terms keep distinct ids",
           "[r16e][help][wp5][terminology]" )
{
    QVector<TermEntry> terms;
    terms << makeTerm( QStringLiteral( "resolution" ), QStringLiteral( "分辨率 (光谱)" ), "spectral" )
          << makeTerm( QStringLiteral( "resolution" ), QStringLiteral( "分辨率 (空间)" ), "geometric" )
          << makeTerm( QStringLiteral( "resolution" ), QStringLiteral( "分辨率 (辐射)" ), "radiometric" )
          << makeTerm( QStringLiteral( "pixel size" ), QStringLiteral( "像元大小" ), "geometric",
                       { QStringLiteral( "resolution" ) } );

    HelpRegistry registry;
    QStringList errors;
    TerminologyProvider::appendDescriptors( terms, registry, &errors );

    INFO( "append errors: " << errors.join( QStringLiteral( "; " ) ).toStdString() );
    CHECK( errors.isEmpty() );

    // #1465 exact symptom: duplicates overwrote each other (count < terms)
    REQUIRE( registry.count() == 4 );
    REQUIRE( registry.find( QStringLiteral( "concept.term.resolution" ) ) != nullptr );
    REQUIRE( registry.find( QStringLiteral( "concept.term.resolution_2" ) ) != nullptr );
    REQUIRE( registry.find( QStringLiteral( "concept.term.resolution_3" ) ) != nullptr );
    REQUIRE( registry.find( QStringLiteral( "concept.term.pixel_size" ) ) != nullptr );

    // the three resolution descriptors kept their distinct zh titles
    CHECK( registry.find( QStringLiteral( "concept.term.resolution" ) )->title.contains( QStringLiteral( "光谱" ) ) );
    CHECK( registry.find( QStringLiteral( "concept.term.resolution_2" ) )->title.contains( QStringLiteral( "空间" ) ) );
    CHECK( registry.find( QStringLiteral( "concept.term.resolution_3" ) )->title.contains( QStringLiteral( "辐射" ) ) );

    // related['resolution'] resolves to the FIRST duplicate's id — no dangle
    const HelpDescriptor *pixel = registry.find( QStringLiteral( "concept.term.pixel_size" ) );
    REQUIRE( pixel );
    REQUIRE( pixel->relatedIds.size() == 1 );
    CHECK( pixel->relatedIds.first() == QStringLiteral( "concept.term.resolution" ) );

    // registry-level dangling check stays clean
    CHECK( registry.validateReferences().isEmpty() );
}

TEST_CASE( "terminology dedup: slug collisions between different en spellings",
           "[r16e][help][wp5][terminology]" )
{
    // "At-Best" and "at best" slugify to the same "at_best" — distinct en
    // keys, colliding ids. The dedup suffix must apply here too.
    QVector<TermEntry> terms;
    terms << makeTerm( QStringLiteral( "At-Best" ), QStringLiteral( "最佳 (合成)" ), "cartography" )
          << makeTerm( QStringLiteral( "at best" ), QStringLiteral( "至多 (统计)" ), "cartography" );

    HelpRegistry registry;
    QStringList errors;
    TerminologyProvider::appendDescriptors( terms, registry, &errors );
    CHECK( errors.isEmpty() );
    REQUIRE( registry.count() == 2 );
    CHECK( registry.find( QStringLiteral( "concept.term.at_best" ) ) != nullptr );
    CHECK( registry.find( QStringLiteral( "concept.term.at_best_2" ) ) != nullptr );
    CHECK( registry.validateReferences().isEmpty() );
}

TEST_CASE( "terminology end-to-end: duplicate en reported as data error, knowledge kept",
           "[r16e][help][wp5][terminology]" )
{
    const QByteArray json = R"([
        {"en":"base term","zh":"基础术语","definition_zh":"d1","category":"terrain"},
        {"en":"base term","zh":"重复术语","definition_zh":"d2","category":"terrain"},
        {"en":"third term","zh":"第三术语","definition_zh":"d3","category":"terrain",
         "related":["base term","third term"]}
    ])";
    const TerminologyProvider::LoadResult loaded =
        TerminologyProvider::loadFromJson( json, QStringLiteral( "dup.json" ) );

    // data-level guard: the duplicate is reported
    REQUIRE( loaded.terms.size() == 3 );
    REQUIRE( loaded.errors.size() == 1 );
    CHECK( loaded.errors.first().contains( QStringLiteral( "duplicate en term" ) ) );

    // composition-level robustness: appendDescriptors still registers all 3
    HelpRegistry registry;
    QStringList appendErrors;
    TerminologyProvider::appendDescriptors( loaded.terms, registry, &appendErrors );
    CHECK( appendErrors.isEmpty() );
    REQUIRE( registry.count() == 3 );

    // related resolution across the duplicated key works (first id wins)
    const HelpDescriptor *third = registry.find( QStringLiteral( "concept.term.third_term" ) );
    REQUIRE( third );
    CHECK( third->relatedIds.contains( QStringLiteral( "concept.term.base_term" ) ) );
    // self-related is filtered by design
    CHECK_FALSE( third->relatedIds.contains( QStringLiteral( "concept.term.third_term" ) ) );
    CHECK( registry.validateReferences().isEmpty() );
}

TEST_CASE( "terminology provider lookup across en zh and alias",
           "[r16e][help][wp5][terminology]" )
{
    QVector<TermEntry> terms;
    TermEntry t = makeTerm( QStringLiteral( "radiometric calibration" ), QStringLiteral( "辐射定标" ),
                            "radiometric", { QStringLiteral( "digital number" ) } );
    t.aliases << QStringLiteral( "绝对辐射定标" );
    terms << t << makeTerm( QStringLiteral( "digital number" ), QStringLiteral( "数字量化值" ),
                            "radiometric" );

    const TerminologyProvider provider( terms );
    REQUIRE( provider.lookup( QStringLiteral( "Radiometric Calibration" ) ) != nullptr );
    REQUIRE( provider.lookup( QStringLiteral( "辐射定标" ) ) != nullptr );
    REQUIRE( provider.lookup( QStringLiteral( "绝对辐射定标" ) ) != nullptr );
    CHECK( provider.lookup( QStringLiteral( "不存在的术语" ) ) == nullptr );

    // the #1465 dangling symptom via the lookup path: related target exists
    HelpRegistry registry;
    QStringList errors;
    TerminologyProvider::appendDescriptors( terms, registry, &errors );
    CHECK( errors.isEmpty() );
    const HelpDescriptor *cal = registry.find( QStringLiteral( "concept.term.radiometric_calibration" ) );
    REQUIRE( cal );
    CHECK( cal->relatedIds == QStringList{ QStringLiteral( "concept.term.digital_number" ) } );
    CHECK( registry.validateReferences().isEmpty() );
}
