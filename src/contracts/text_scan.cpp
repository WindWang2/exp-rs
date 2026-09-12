/***************************************************************************
 * text_scan.cpp — literal/comment-aware source scanning primitives
 ***************************************************************************/
#include "text_scan.h"

#include <cctype>
#include <regex>

namespace sicnu::contracts {

namespace
{

/// Classification of every byte of interest for literal/comment tracking.
enum class ByteKind : unsigned char
{
    Code = 0,
    InLineComment,
    InBlockComment,
    InString,
    InCharLiteral,
};

std::vector<ByteKind> classify( std::string_view src )
{
    std::vector<ByteKind> kinds( src.size(), ByteKind::Code );
    size_t i = 0;
    while ( i < src.size() )
    {
        const char c = src[i];
        const char n = ( i + 1 < src.size() ) ? src[i + 1] : '\0';
        if ( c == '/' && n == '/' )
        {
            while ( i < src.size() && src[i] != '\n' )
                kinds[i++] = ByteKind::InLineComment;
        }
        else if ( c == '/' && n == '*' )
        {
            kinds[i++] = ByteKind::InBlockComment;
            kinds[i++] = ByteKind::InBlockComment;
            while ( i < src.size() )
            {
                kinds[i] = ByteKind::InBlockComment;
                if ( src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/' )
                {
                    kinds[i + 1] = ByteKind::InBlockComment;
                    i += 2;
                    break;
                }
                ++i;
            }
        }
        else if ( c == 'R' && n == '"' )
        {
            // C++ raw string literal: R"delim( ... )delim" — the content may
            // contain arbitrary quotes; only the closing )delim" ends it.
            const size_t rawStart = i;
            size_t d = i + 2;
            while ( d < src.size() && src[d] != '(' )
            {
                if ( src[d] == '\n' ) // not a raw string after all
                    break;
                ++d;
            }
            if ( d < src.size() && src[d] == '(' )
            {
                const std::string delim(
                    src.substr( i + 2, d - ( i + 2 ) ) );
                const std::string closer = ")" + delim + "\"";
                const size_t close = src.find( closer, d );
                const size_t end =
                    ( close == std::string_view::npos )
                        ? src.size()
                        : close + closer.size();
                while ( i < end )
                    kinds[i++] = ByteKind::InString;
                (void)rawStart;
            }
            else
            {
                // R" followed by something else: plain identifier path —
                // fall through by marking just this byte as code.
                ++i;
            }
        }
        else if ( c == '"' )
        {
            kinds[i++] = ByteKind::InString;
            while ( i < src.size() )
            {
                kinds[i] = ByteKind::InString;
                if ( src[i] == '\\' && i + 1 < src.size() )
                {
                    kinds[i + 1] = ByteKind::InString;
                    i += 2;
                    continue;
                }
                if ( src[i] == '"' )
                {
                    ++i;
                    break;
                }
                if ( src[i] == '\n' ) // unterminated — treat as code again
                    break;
                ++i;
            }
        }
        else if ( c == '\'' )
        {
            kinds[i++] = ByteKind::InCharLiteral;
            while ( i < src.size() )
            {
                kinds[i] = ByteKind::InCharLiteral;
                if ( src[i] == '\\' && i + 1 < src.size() )
                {
                    kinds[i + 1] = ByteKind::InCharLiteral;
                    i += 2;
                    continue;
                }
                if ( src[i] == '\'' )
                {
                    ++i;
                    break;
                }
                if ( src[i] == '\n' )
                    break;
                ++i;
            }
        }
        else
        {
            ++i;
        }
    }
    return kinds;
}

} // namespace

bool insideLiteralOrComment( std::string_view src, size_t pos )
{
    const auto kinds = classify( src );
    return pos < kinds.size() && kinds[pos] != ByteKind::Code;
}

size_t skipSpace( std::string_view src, size_t from )
{
    while ( from < src.size() &&
            ( src[from] == ' ' || src[from] == '\t' || src[from] == '\r' ||
              src[from] == '\n' ) )
        ++from;
    return from;
}

Span matchBrace( std::string_view src, size_t openBrace )
{
    if ( openBrace >= src.size() || src[openBrace] != '{' )
        return {};
    const auto kinds = classify( src );
    int depth = 0;
    for ( size_t i = openBrace; i < src.size(); ++i )
    {
        if ( kinds[i] != ByteKind::Code )
            continue;
        const char c = src[i];
        if ( c == '{' )
            ++depth;
        else if ( c == '}' )
        {
            --depth;
            if ( depth == 0 )
                return { openBrace + 1, i };
        }
    }
    return {};
}

Span matchParen( std::string_view src, size_t openParen )
{
    if ( openParen >= src.size() || src[openParen] != '(' )
        return {};
    const auto kinds = classify( src );
    int depth = 0;
    for ( size_t i = openParen; i < src.size(); ++i )
    {
        if ( kinds[i] != ByteKind::Code )
            continue;
        const char c = src[i];
        if ( c == '(' )
            ++depth;
        else if ( c == ')' )
        {
            --depth;
            if ( depth == 0 )
                return { openParen + 1, i };
        }
    }
    return {};
}

Span findFunctionBody( std::string_view src, const std::string &nameToken )
{
    const auto kinds = classify( src );
    size_t searchFrom = 0;
    while ( searchFrom < src.size() )
    {
        const size_t pos = src.find( nameToken, searchFrom );
        if ( pos == std::string_view::npos )
            return {};
        searchFrom = pos + 1;
        // Token must be preceded by a non-identifier char and be code.
        if ( pos > 0 &&
             ( std::isalnum( static_cast<unsigned char>( src[pos - 1] ) ) ||
               src[pos - 1] == '_' ||
               kinds[pos] != ByteKind::Code ) )
            continue;
        size_t p = skipSpace( src, pos + nameToken.size() );
        if ( p >= src.size() || src[p] != '(' )
            continue; // e.g. forward declaration without args / comment
        // Skip to the matching ')' of the parameter list.
        int parenDepth = 0;
        for ( ; p < src.size(); ++p )
        {
            if ( kinds[p] != ByteKind::Code )
                continue;
            if ( src[p] == '(' )
                ++parenDepth;
            else if ( src[p] == ')' )
            {
                --parenDepth;
                if ( parenDepth == 0 )
                {
                    ++p;
                    break;
                }
            }
        }
        // Between ')' and the body brace only a const/noexcept qualifier may
        // appear; skip whitespace and those tokens.
        p = skipSpace( src, p );
        static const std::regex qualifier( R"(^(const|noexcept|override)\b)" );
        std::cmatch m;
        while ( p < src.size() &&
                std::regex_search( src.data() + p,
                                   src.data() + std::min( src.size(), p + 32 ),
                                   m, qualifier ) )
            p = skipSpace( src, p + m.length() );
        if ( p < src.size() && src[p] == '{' )
            return matchBrace( src, p );
        // `);` style declaration or `= default` — keep searching.
    }
    return {};
}

Span functionSignature( std::string_view src, size_t openBrace )
{
    // Walk backwards from openBrace to the matching '('.
    const auto kinds = classify( src );
    int parenDepth = 0;
    size_t i = openBrace;
    while ( i > 0 )
    {
        --i;
        if ( kinds[i] != ByteKind::Code )
            continue;
        const char c = src[i];
        if ( c == ')' )
            ++parenDepth;
        else if ( c == '(' )
        {
            --parenDepth;
            if ( parenDepth == 0 )
            {
                // Now walk further back to the beginning of the declarator.
                size_t j = i;
                int angle = 0;
                while ( j > 0 )
                {
                    const char d = src[j - 1];
                    if ( kinds[j - 1] != ByteKind::Code )
                    {
                        --j;
                        continue;
                    }
                    if ( d == '>' )
                        ++angle;
                    else if ( d == '<' )
                    {
                        if ( angle == 0 )
                            break;
                        --angle;
                    }
                    else if ( angle == 0 &&
                              ( d == ';' || d == '{' || d == '}' ) )
                        break;
                    --j;
                }
                return { j, openBrace };
            }
        }
    }
    return {};
}

std::string codeOnly( std::string_view src, Span range )
{
    if ( !range.valid() )
        return {};
    const auto kinds = classify( src );
    std::string out;
    out.reserve( range.end - range.begin );
    for ( size_t i = range.begin; i < range.end && i < src.size(); ++i )
        if ( kinds[i] == ByteKind::Code )
            out.push_back( src[i] );
    return out;
}

std::vector<Span> splitArgs( std::string_view src, Span range )
{
    std::vector<Span> out;
    if ( !range.valid() )
        return out;
    const auto kinds = classify( src );
    int depth = 0;
    size_t start = range.begin;
    for ( size_t i = range.begin; i < range.end && i < src.size(); ++i )
    {
        if ( kinds[i] != ByteKind::Code )
            continue;
        const char c = src[i];
        if ( c == '(' || c == '[' || c == '{' )
            ++depth;
        else if ( c == ')' || c == ']' || c == '}' )
            --depth;
        else if ( c == ',' && depth == 0 )
        {
            out.push_back( { start, i } );
            start = i + 1;
        }
    }
    out.push_back( { start, range.end } );
    return out;
}

std::string firstIdentifier( std::string_view src )
{
    size_t b = 0;
    while ( b < src.size() &&
            !( std::isalpha( static_cast<unsigned char>( src[b] ) ) ||
               src[b] == '_' ) )
        ++b;
    size_t e = b;
    while ( e < src.size() &&
            ( std::isalnum( static_cast<unsigned char>( src[e] ) ) ||
              src[e] == '_' ) )
        ++e;
    return std::string( src.substr( b, e - b ) );
}

std::vector<std::string> findMatches( std::string_view src, Span span,
                                      const std::string &regexStr, int group )
{
    // group must be 1: findMatchesWithPos already extracts capture 1 (all
    // scanner patterns are single-capture). Compilation and re-search per
    // match would be O(matches × pattern) for no benefit.
    (void)regexStr;
    (void)group;
    std::vector<std::string> out;
    for ( const auto &m : findMatchesWithPos( src, span, regexStr ) )
        out.push_back( m.text );
    return out;
}

std::vector<MatchPos> findMatchesWithPos( std::string_view src, Span span,
                                          const std::string &regexStr )
{
    std::vector<MatchPos> out;
    if ( !span.valid() )
        return out;
    const auto kinds = classify( src );
    const std::string text( src.substr( span.begin, span.end - span.begin ) );
    std::regex re( regexStr, std::regex::optimize );
    auto begin = std::sregex_iterator( text.begin(), text.end(), re );
    const auto endIt = std::sregex_iterator();
    for ( auto it = begin; it != endIt; ++it )
    {
        const size_t absPos = span.begin + static_cast<size_t>( it->position() );
        // Skip only matches whose START sits inside a literal or comment:
        // commented-out code and quoted text never start a real construct.
        // (Checking the whole span would reject every match that touches a
        // quote — e.g. props["key"] — since the quote bytes themselves are
        // classified as string bytes.)
        if ( kinds[absPos] != ByteKind::Code )
            continue;
        MatchPos mp;
        mp.pos = absPos;
        mp.whole = it->str();
        mp.text = ( it->size() > 1 ) ? ( *it )[1].str() : it->str();
        out.push_back( std::move( mp ) );
    }
    return out;
}

} // namespace sicnu::contracts
