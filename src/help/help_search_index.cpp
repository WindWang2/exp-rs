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

    for ( const HelpDescriptor *d : descriptors ) {
        if ( !d || d->id.isEmpty() )
            continue;

        auto addField = [this, &id = d->id]( const QStringList &tokens, double weight ) {
            for ( const QString &token : tokens ) {
                QHash<QString, double> &doc = m_index[token];
                doc[id] += weight; // repeated tokens accumulate — bounded by doc length
            }
        };

        addField( tokenize( d->title ), kWeightTitle );
        addField( tokenize( d->summary ), kWeightSummary );
        addField( tokenize( d->id ), kWeightId );
        for ( const QString &keyword : d->keywords )
            addField( tokenize( keyword ), kWeightKeyword );

        m_documents.insert( d->id, d );
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

    QHash<QString, double> scores;
    for ( const QString &token : tokens ) {
        const auto it = m_index.constFind( token );
        if ( it == m_index.constEnd() )
            continue;
        for ( auto doc = it->constBegin(); doc != it->constEnd(); ++doc )
            scores[doc.key()] += doc.value();
    }
    if ( scores.isEmpty() )
        return hits;

    hits.reserve( scores.size() );
    for ( auto it = scores.constBegin(); it != scores.constEnd(); ++it ) {
        const HelpDescriptor *d = m_documents.value( it.key(), nullptr );
        if ( !d )
            continue;
        SearchHit hit;
        hit.id = d->id;
        hit.title = d->title;
        hit.summary = d->summary;
        // normalize by query length so multi-term queries don't dominate purely
        // by token count; keep monotone in matches.
        hit.score = it.value() / std::sqrt( double( tokens.size() ) );
        hits.push_back( hit );
    }

    std::sort( hits.begin(), hits.end(), []( const SearchHit &a, const SearchHit &b ) {
        if ( a.score != b.score )
            return a.score > b.score;
        return a.id < b.id; // deterministic tie-break
    } );
    if ( hits.size() > maxResults )
        hits.resize( maxResults );
    return hits;
}

} // namespace sicnu::help
