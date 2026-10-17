/***************************************************************************
 * tests/test_r16_E_help_search_perf.cpp — R16-E T09 WP4
 *
 * Performance + determinism oracle for HelpSearchIndex over a fixed
 * synthetic dataset (5000 descriptors, CJK+EN mixed titles/summaries/keywords,
 * seeded reproducible content) and a fixed mixed query set.
 *
 *   - determinism: two independently built indexes over the same descriptor
 *     vector must return BYTE-IDENTICAL result sequences (id + score with
 *     17-significant-digit formatting) for every query, and repeated searches
 *     on one index must be stable too. The final sort is (score, id) with a
 *     deterministic tie-break, and per-document score accumulation follows the
 *     token order — so any nondeterminism here is a real defect (e.g. hash
 *     iteration order leaking into floating-point accumulation order).
 *   - performance: generous wall-clock ceilings that catch quadratic
 *     blowups without flaking on slow CI (build 5k < 30 s, 3k queries
 *     < 15 s). Exact measurements are printed to stderr and recorded in
 *     .planning/r16-e-t09/PERFORMANCE.md; RSS is captured externally via
 *     /usr/bin/time -v when running this binary.
 ***************************************************************************/
#include "help/help_descriptor.h"
#include "help/help_search_index.h"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdio>
#include <random>

using namespace sicnu::help;

namespace
{

/// Fixed-seed synthetic corpus: 5000 descriptors mixing CJK and Latin text
/// across title/summary/keywords so both token paths (bigram + word) are hot.
QVector<HelpDescriptor> syntheticCorpus( int count )
{
    std::mt19937 rng( 20261017 ); // fixed seed: dataset is reproducible
    QVector<HelpDescriptor> descriptors;
    descriptors.reserve( count );

    static const char *kCjkTitles[] = {
        "辐射定标与大气校正是光学遥感预处理的基础",
        "合成孔径雷达斑点噪声抑制采用多视与自适应滤波",
        "地形分析与水文建模需要数字高程模型支撑",
        "非监督分类通过 K 均值聚类发现光谱结构",
        "变化检测要求两期影像严格配准与辐射归一化",
    };
    static const char *kLatinTitles[] = {
        "Radiometric calibration pipeline",
        "SAR speckle filtering with Lee kernel",
        "Terrain derivative computation",
        "Unsupervised spectral clustering",
        "Post-classification change detection",
    };
    static const char *kKeywords[] = { "定标", "calibration", "斑点", "speckle",
                                       "DEM", "聚类", "clustering", "变化", "change" };

    for ( int i = 0; i < count; ++i )
    {
        HelpDescriptor d;
        d.id = QStringLiteral( "operator.rs.synthetic_%1" ).arg( i );
        d.kind = HelpKind::Operator;
        const bool cjk = i % 2 == 0;
        d.title = QStringLiteral( "%1 #%2" )
                      .arg( QString::fromUtf8( cjk ? kCjkTitles[i % 5] : kLatinTitles[i % 5] ), QString::number( i ) );
        d.summary = QStringLiteral( "%1 summary for descriptor %2 with mixed 辐射/斑点/terrain vocabulary" )
                        .arg( QString::fromUtf8( cjk ? kLatinTitles[i % 5] : kCjkTitles[i % 5] ),
                              QString::number( i ) );
        d.category = QStringLiteral( "合成" );
        d.keywords << QString::fromUtf8( kKeywords[i % 9] )
                   << QString::fromUtf8( kKeywords[( i + 3 ) % 9] )
                   << QStringLiteral( "synthetic_%1" ).arg( i % 97 );
        descriptors.push_back( d );
    }
    return descriptors;
}

QString renderHits( const QVector<SearchHit> &hits )
{
    QString out;
    for ( const SearchHit &hit : hits )
        out += hit.id + u'|' + QString::number( hit.score, 'g', 17 ) + u'\n';
    return out;
}

HelpSearchIndex builtIndex( const QVector<HelpDescriptor> &descriptors, qint64 *ms = nullptr )
{
    HelpSearchIndex index;
    QVector<const HelpDescriptor *> pointers;
    pointers.reserve( descriptors.size() );
    for ( const HelpDescriptor &d : descriptors )
        pointers.push_back( &d );
    QElapsedTimer timer;
    timer.start();
    index.build( pointers );
    if ( ms )
        *ms = timer.elapsed();
    return index;
}

} // namespace

TEST_CASE( "search index scoring is deterministic across rebuilds and repeats",
           "[r16e][help][wp4][determinism]" )
{
    const QVector<HelpDescriptor> corpus = syntheticCorpus( 5000 );

    qint64 buildMs1 = 0, buildMs2 = 0;
    const HelpSearchIndex index1 = builtIndex( corpus, &buildMs1 );
    const HelpSearchIndex index2 = builtIndex( corpus, &buildMs2 );
    std::fprintf( stderr, "[wp4] build 5000 descriptors: %lld ms / %lld ms\n",
                  static_cast<long long>( buildMs1 ), static_cast<long long>( buildMs2 ) );

    QStringList queries = { QStringLiteral( "辐射定标" ),
                            QStringLiteral( "speckle filtering" ),
                            QStringLiteral( "斑点 Lee 滤波" ),
                            QStringLiteral( "change 变化 detection" ),
                            QStringLiteral( "synthetic_42" ),
                            QStringLiteral( "DEM terrain 地形" ),
                            QStringLiteral( "聚类 clustering K 均值" ),
                            QStringLiteral( "radiometric calibration pipeline" ) };

    qint64 totalQueryMs = 0;
    for ( const QString &query : queries )
    {
        QElapsedTimer timer;
        timer.start();
        const QString run1 = renderHits( index1.search( query, 25 ) );
        const QString run2 = renderHits( index1.search( query, 25 ) ); // same index, repeat
        const QString other = renderHits( index2.search( query, 25 ) ); // rebuilt index
        totalQueryMs += timer.elapsed();

        INFO( "query: " << query.toStdString() );
        CHECK( run1 == run2 ); // repeat stability
        CHECK( run1 == other ); // rebuild stability
        CHECK( !run1.isEmpty() ); // non-vacuous: CJK and Latin both hit
    }
    // wider sweep: every 10th synthetic keyword id
    for ( int i = 0; i < 5000; i += 10 )
    {
        const QString query = QStringLiteral( "synthetic_%1" ).arg( i % 97 );
        CHECK( renderHits( index1.search( query, 10 ) ) == renderHits( index2.search( query, 10 ) ) );
    }
    std::fprintf( stderr, "[wp4] %zu queries x3 runs: %lld ms\n",
                  static_cast<size_t>( queries.size() ) + 500, static_cast<long long>( totalQueryMs ) );

    // ranked sanity: an exact-id query ranks its own descriptor first
    const QVector<SearchHit> top = index1.search( QStringLiteral( "operator.rs.synthetic_123" ), 5 );
    REQUIRE( !top.isEmpty() );
    CHECK( top.first().id == QStringLiteral( "operator.rs.synthetic_123" ) );
}

TEST_CASE( "search index build and query stay within linear-ish ceilings",
           "[r16e][help][wp4][performance]" )
{
    const QVector<HelpDescriptor> corpus = syntheticCorpus( 5000 );

    qint64 buildMs = 0;
    const HelpSearchIndex index = builtIndex( corpus, &buildMs );
    std::fprintf( stderr, "[wp4] perf build: %lld ms\n", static_cast<long long>( buildMs ) );
    CHECK( buildMs < 30000 ); // generous: catches quadratic tokenization blowups

    // 3000 mixed queries against the built index
    QElapsedTimer timer;
    timer.start();
    int nonEmpty = 0;
    for ( int i = 0; i < 3000; ++i )
    {
        const QString query = ( i % 3 == 0 )   ? QStringLiteral( "辐射定标 synthetic_%1" ).arg( i % 97 )
                              : ( i % 3 == 1 ) ? QStringLiteral( "speckle 斑点 %1" ).arg( i % 89 )
                                               : QStringLiteral( "terrain terrain_ignored_%1" ).arg( i % 83 );
        if ( !index.search( query, 20 ).isEmpty() )
            ++nonEmpty;
    }
    const qint64 queryMs = timer.elapsed();
    std::fprintf( stderr, "[wp4] perf 3000 queries: %lld ms (%d non-empty)\n",
                  static_cast<long long>( queryMs ), nonEmpty );
    CHECK( queryMs < 15000 );
    CHECK( nonEmpty > 2500 ); // nearly all queries should hit something

    // token cost on a 1 MiB VARIED CJK field stays bounded (single call).
    // Varied alphabet: tokenize() deduplicates, so a repeating pattern would
    // collapse to a few tokens however big the input; the 15k-wide cycle
    // keeps the dedup hash hot with real cardinality.
    QString mega;
    mega.reserve( 1 << 20 );
    for ( int i = 0; i < ( 1 << 20 ); ++i )
        mega += QChar( ushort( 0x4E00 + ( i % 15000 ) ) );
    QElapsedTimer tokenizeTimer;
    tokenizeTimer.start();
    const QStringList tokens = HelpSearchIndex::tokenize( mega );
    const qint64 tokenizeMs = tokenizeTimer.elapsed();
    std::fprintf( stderr, "[wp4] tokenize 1MiB CJK: %lld ms -> %d tokens\n",
                  static_cast<long long>( tokenizeMs ), tokens.size() );
    CHECK( tokenizeMs < 10000 );
    CHECK( tokens.size() >= 10000 );
}
