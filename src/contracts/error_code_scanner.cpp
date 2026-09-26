/***************************************************************************
 * error_code_scanner.cpp — error taxonomy extraction (M3)
 ***************************************************************************/
#include "error_code_scanner.h"

#include "text_scan.h"

#include <regex>

namespace sicnu::contracts {

void ErrorCodeScanner::scanEnum( std::string_view headerSrc,
                                 const std::string &enumName,
                                 ErrorCodeReport &out ) const
{
    // Enums have no parameter list: locate `enum class Name` then the body
    // brace directly.
    const std::string token = "enum class " + enumName;
    const size_t pos = headerSrc.find( token );
    if ( pos == std::string_view::npos )
        return;
    const size_t brace = headerSrc.find( '{', pos );
    if ( brace == std::string_view::npos )
        return;
    const Span body = matchBrace( headerSrc, brace );
    if ( !body.valid() )
        return;
    const std::string_view bodyView = headerSrc.substr(
        body.begin, body.end - body.begin );

    // Enumerator items: `Name,` or `Name = 1234,` — split on top-level
    // commas so comments and explicit values are handled uniformly.
    static const std::regex reEnum(
        R"re(^\s*([A-Za-z_]\w*)\s*(?:=|,|$))re" );
    // Split on commas at depth 0 (enum bodies are flat).
    const auto items = splitArgs( headerSrc, body );
    for ( const auto &item : items )
    {
        const std::string s = codeOnly( headerSrc, item );
        if ( s.empty() )
            continue;
        std::smatch sm;
        if ( std::regex_search( s, sm, reEnum ) )
            out.enumValues.insert( sm[1].str() );
    }
}

void ErrorCodeScanner::scanToStringSwitch( std::string_view src,
                                           const std::string &enumName,
                                           ErrorCodeReport &out ) const
{
    const std::string reStr =
        "case\\s+" + enumName +
        "::(\\w+)\\s*:\\s*(?:\\n\\s*)?return\\s+\"([^\"]+)\";";
    const std::string text( src );
    std::regex re( reStr );
    for ( auto it = std::sregex_iterator( text.begin(), text.end(), re );
          it != std::sregex_iterator(); ++it )
        out.caseMap[( *it )[1].str()] = ( *it )[2].str();
}

void ErrorCodeScanner::scanHarnessCodes( std::string_view headerSrc,
                                         ErrorCodeReport &out ) const
{
    static const std::regex re(
        R"re(inline\s+constexpr\s+const\s+char\s*\*\s*k(\w+)\s*=\s*"([^"]+)")re" );
    const std::string text( headerSrc );
    for ( auto it = std::sregex_iterator( text.begin(), text.end(), re );
          it != std::sregex_iterator(); ++it )
        out.harnessCodes[( *it )[1].str()] = ( *it )[2].str();
}

void ErrorCodeScanner::scanHarnessErrorTable( std::string_view src,
                                              ErrorCodeReport &out ) const
{
    static const std::regex re(
        R"re(\{\s*"([A-Z][A-Z_0-9]+)"\s*,\s*\{\s*"[a-z_]+")re" );
    const std::string text( src );
    for ( auto it = std::sregex_iterator( text.begin(), text.end(), re );
          it != std::sregex_iterator(); ++it )
    {
        // Dedupe by the WIRE CODE value: a code that already exists through a
        // header constant must not produce a second identical node under the
        // table's (different) variable name.
        const std::string code = ( *it )[1].str();
        bool present = false;
        for ( const auto &existing : out.harnessCodes )
            if ( existing.second == code )
            {
                present = true;
                break;
            }
        if ( !present )
            out.harnessCodes[code] = code;
    }
}

} // namespace sicnu::contracts
