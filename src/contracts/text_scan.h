/***************************************************************************
 * text_scan.h — literal/comment-aware source scanning primitives
 *
 * Shared by the contract scanners (Contract Platform 9.0). All scanners
 * must skip string literals, char literals and comments before applying
 * brace matching, so that a brace or quote inside a comment/string can
 * never desynchronize an extraction.
 ***************************************************************************/
#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sicnu::contracts {

/// Half-open span [begin, end) into a source buffer.
struct Span
{
    size_t begin = 0;
    size_t end = 0;
    bool valid() const { return end > begin; }
};

/// Returns the position of the first character of `src` at or after `from`
/// that is not whitespace.
size_t skipSpace( std::string_view src, size_t from );

/// Finds the opening brace that starts the body of a C++ function/method.
/// `nameToken` is e.g. "Foo::schema" — the first occurrence of the token
/// outside literals/comments is used. Returns an invalid Span when the
/// token or its body brace cannot be found.
Span findFunctionBody( std::string_view src, const std::string &nameToken );

/// Given the position of an opening '{', returns the span of its matching
/// closing brace (exclusive of both braces). Skips string/char literals and
/// comments. Returns invalid Span on unbalanced input.
Span matchBrace( std::string_view src, size_t openBrace );

/// Same for an opening '(' — returns span between the parens.
Span matchParen( std::string_view src, size_t openParen );

/// Signature span of the function whose body opens at `openBrace`:
/// scans backwards to the matching '(' and further to the start of the
/// declaration. Returns [begin, openBrace).
Span functionSignature( std::string_view src, size_t openBrace );

/// Splits `src` on commas at paren/brace/bracket depth 0 (literal/comment
/// aware). Used for call argument extraction.
std::vector<Span> splitArgs( std::string_view src, Span range );

/// Returns the first identifier token in `src` (trimmed), or "".
std::string firstIdentifier( std::string_view src );

/// True when position `pos` in `src` is inside a comment or a string/char
/// literal.
bool insideLiteralOrComment( std::string_view src, size_t pos );

/// Returns only the code bytes of `range` — comments and string/char
/// literal contents (including their delimiters) are dropped. Used for
/// identifier-level parsing over comment-bearing spans.
std::string codeOnly( std::string_view src, Span range );

/// All occurrences of `regex` inside `span` with capture group 1 (or the
/// whole match when the pattern has no groups), skipping matches inside
/// literals/comments.
std::vector<std::string> findMatches( std::string_view src, Span span,
                                      const std::string &regex, int group = 1 );

/// Same, but returns match positions paired with group 1 text.
struct MatchPos
{
    size_t pos = 0;
    std::string text;
    std::string whole;
};
std::vector<MatchPos> findMatchesWithPos( std::string_view src, Span span,
                                          const std::string &regex );

} // namespace sicnu::contracts
