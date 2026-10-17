/***************************************************************************
 * help_search_index.cpp — deterministic token/bigram search implementation
 ***************************************************************************/
#include "help/help_search_index.h"

#include <algorithm>
#include <cmath>

namespace sicnu::help
{
namespace
{

// Fixed field weights: keyword matches matter most, then title, then the id
// itself (users paste ids), then the summary.
constexpr double kWeightKeyword = 3.0;
constexpr double kWeightTitle = 2.0;
constexpr double kWeightId = 1.5;
constexpr double kWeightSummary = 1.0;

bool isCjk( char32_t cp )
{
    return ( cp >= 0x4E00 && cp <= 0x9FFF )    // CJK unified
           || ( cp >= 0x3400 && cp <= 0x4DBF )  // extension A
           || ( cp >= 0xF900 && cp <= 0xFAFF )  // compatibility
           || ( cp >= 0x20000 && cp <= 0x2A6DF ) // extension B
           || ( cp >= 0x2A700 && cp <= 0x2EBEF ); // extensions C–F
}

/// Appends tokens for one field's text. Latin/alphanumeric runs become
/// lowercase words; CJK runs become character bigrams (single char if run of 1).
void tokenizeField( const QString &text, QStringList &out )
{
    QString latinRun;
    QString cjkRun;
    auto flush = [&]() {
        if ( !latinRun.isEmpty() ) {
            out << latinRun;
            latinRun.clear();
        }
        if ( !cjkRun.isEmpty() ) {
            // bigrams over code POINTS (surrogate pairs stay intact);
            // QString::toUcs4 yields QList<uint> on this Qt version
            const auto cps = cjkRun.toUcs4();
            QString previous;
            for ( uint upoint : cps ) {
                const auto cp = static_cast<char32_t>( upoint );
                const QString current = QString::fromUcs4( &cp, 1 );
                if ( !previous.isEmpty() )
                    out << ( previous + current );
                previous = current;
            }
            if ( cps.size() == 1 )
                out << cjkRun;
            cjkRun.clear();
        }
    };

    const auto codePoints = text.toUcs4();
    for ( uint rawCp : codePoints ) {
        const char32_t cp = static_cast<char32_t>( rawCp );
        if ( isCjk( cp ) ) {
            if ( !latinRun.isEmpty() ) {
                out << latinRun;
                latinRun.clear();
            }
            cjkRun += QString::fromUcs4( &cp, 1 );
        } else {
            const QChar ch( cp <= 0xFFFF ? static_cast<char16_t>( cp ) : u'?' );
            if ( cp <= 0xFFFF && ch.isLetterOrNumber() ) {
                if ( !cjkRun.isEmpty() )
                    flush();
                latinRun += ch.toLower();
            } else {
                flush();
            }
        }
    }
    flush();
}

} // namespace

QStringList HelpSearchIndex::tokenize( const QString &text )
{
    QStringList tokens;
    tokenizeField( text, tokens );
    tokens.removeAll( QString() );
    tokens.removeDuplicates();
    return tokens;
}

void HelpSearchIndex::build( const QVector<const HelpDescriptor *> &descriptors )
{
    m_index.clear();
    m_documents.clear();
    m_topics = 0;

    // id → dense index; a repeated id reuses its slot so scores merge and the
    // last descriptor's display fields win — exactly the semantics the old
    // id-keyed posting map had.
    QHash<QString, int> indexOfId;
    for ( const HelpDescriptor *d : descriptors ) {
        if ( !d || d->id.isEmpty() )
            continue;

        int docIndex = indexOfId.value( d->id, -1 );
        if ( docIndex < 0 ) {
            docIndex = m_documents.size();
            m_documents.push_back( d );
            indexOfId.insert( d->id, docIndex );
        } else {
            m_documents[ docIndex ] = d;
        }

        auto addField = [this, docIndex]( const QStringList &tokens, double weight ) {
            for ( const QString &token : tokens ) {
                // repeated tokens accumulate — bounded by doc length
                m_index[token][docIndex] += weight;
            }
        };

        addField( tokenize( d->title ), kWeightTitle );
        addField( tokenize( d->summary ), kWeightSummary );
        addField( tokenize( d->id ), kWeightId );
        for ( const QString &keyword : d->keywords )
            addField( tokenize( keyword ), kWeightKeyword );

        ++m_topics;
    }
    m_built = true;
}

QVector<SearchHit> HelpSearchIndex::search( const QString &query, int maxResults ) const
{
    QVector<SearchHit> hits;
    if ( !m_built )
        return hits;
    maxResults = qBound( 1, maxResults, 100 );

    const QStringList tokens = tokenize( query );
    if ( tokens.isEmpty() )
        return hits;

    // Dense per-document score vector: the token join touches only integer
    // keys (no QString hashing per posting entry), and per-query allocation
    // is one zeroed double per indexed document.
    QVector<double> scores( m_documents.size(), 0.0 );
    for ( const QString &token : tokens ) {
        const auto it = m_index.constFind( token );
        if ( it == m_index.constEnd() )
            continue;
        for ( auto doc = it->constBegin(); doc != it->constEnd(); ++doc )
            scores[ doc.key() ] += doc.value();
    }

    // Rank on trivially-copyable (score, descriptor) pairs and materialize
    // SearchHit (two shared-string copies + fields) only for the surviving
    // top-K: a query token hitting most of the corpus otherwise pays a full
    // hit construction per matching document, per query.
    struct Scored
    {
        double score;
        const HelpDescriptor *d;
    };
    QVector<Scored> scored;
    scored.reserve( scores.size() );
    const double norm = std::sqrt( double( tokens.size() ) );
    for ( int i = 0; i < scores.size(); ++i ) {
        if ( scores[ i ] <= 0.0 )
            continue;
        const HelpDescriptor *d = m_documents[ i ];
        if ( !d )
            continue;
        // normalize by query length so multi-term queries don't dominate purely
        // by token count; keep monotone in matches.
        scored.push_back( { scores[ i ] / norm, d } );
    }

    static const auto byRank = []( const Scored &a, const Scored &b ) {
        if ( a.score != b.score )
            return a.score > b.score;
        return a.d->id < b.d->id; // deterministic tie-break
    };
    // Top-K selection: the rank order (score desc, id asc) is a TOTAL order
    // (ids are unique), so the first-maxResults SET is uniquely determined and
    // nth_element + sorting only the head is byte-identical to the old
    // full-sort-then-truncate — at O(N + K log K) comparisons instead of
    // O(N log N). A query token that hits most of the corpus (e.g. a common
    // keyword) used to pay a full sort of every matching document per query.
    if ( scored.size() > maxResults ) {
        std::nth_element( scored.begin(), scored.begin() + maxResults, scored.end(), byRank );
        scored.resize( maxResults );
    }
    std::sort( scored.begin(), scored.end(), byRank );

    hits.reserve( scored.size() );
    for ( const Scored &s : scored ) {
        SearchHit hit;
        hit.id = s.d->id;
        hit.title = s.d->title;
        hit.summary = s.d->summary;
        hit.score = s.score;
        hits.push_back( std::move( hit ) );
    }
    return hits;
}

} // namespace sicnu::help
