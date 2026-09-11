/***************************************************************************
 * command_ref_scanner.cpp — command/action reference extraction (M3)
 ***************************************************************************/
#include "command_ref_scanner.h"

#include "text_scan.h"

#include <algorithm>
#include <regex>

namespace sicnu::contracts {

namespace {

int lineAt( std::string_view src, size_t pos )
{
    return 1 + static_cast<int>(
                   std::count( src.begin(), src.begin() + pos, '\n' ) );
}

/// Appends to `set` recording evidence.
void addWithEvidence( std::set<std::string> &set,
                      std::map<std::string, std::string> &evidence,
                      const std::string &id, const std::string &file,
                      size_t pos, std::string_view src )
{
    set.insert( id );
    if ( !evidence.count( id ) )
        evidence[id] = file + ":" + std::to_string( lineAt( src, pos ) );
}

} // namespace

void CommandRefScanner::scanRegistered( std::string_view src,
                                        const std::string &file,
                                        CommandRefReport &out ) const
{
    // RS_CMD( d, "project.new", ... ) — shell command macro
    const std::string patterns[] = {
        R"re(RS_CMD\s*\(\s*\w+\s*,\s*"([^"]+)")re",
        R"re(\bbase\s*\(\s*"([^"]+)")re",
        // rasterTool( "rs.bandMath", ... ) — local lambda helper in
        // command_defs.cpp (id-first, like base()).
        R"re(\brasterTool\s*\(\s*"([^"]+)")re",
        R"re(\.id\s*=\s*(?:QStringLiteral|QString::fromUtf8|QString::fromLatin1)\s*\(\s*"([^"]+)")re",
    };
    for ( const auto &pattern : patterns )
    {
        const std::regex re( pattern );
        for ( const auto &m :
              findMatchesWithPos( src, { 0, src.size() }, pattern ) )
        {
            std::smatch sm;
            const std::string text( m.whole );
            std::regex_search( text, sm, re );
            addWithEvidence( out.registeredIds, out.evidence, sm[1].str(),
                             file, m.pos, src );
        }
    }
}

void CommandRefScanner::scanLookups( std::string_view src,
                                     const std::string &file,
                                     CommandRefReport &out ) const
{
    // registry->action( QStringLiteral( "workbench.temporal" ), true )
    static const std::string pattern =
        R"re(->\s*action\s*\(\s*(?:QStringLiteral\s*\(\s*)?"([^"]+)")re";
    const std::regex re( pattern );
    for ( const auto &m : findMatchesWithPos( src, { 0, src.size() }, pattern ) )
    {
        std::smatch sm;
        const std::string text( m.whole );
        std::regex_search( text, sm, re );
        addWithEvidence( out.lookupIds, out.evidence, sm[1].str(), file,
                         m.pos, src );
    }
}

void CommandRefScanner::scanCtas( std::string_view src,
                                  const std::string &file,
                                  CommandRefReport &out ) const
{
    // action.commandId = QStringLiteral( "workbench.temporal" );
    static const std::string pattern =
        R"re(commandId\s*=\s*(?:QStringLiteral|QString::fromUtf8)\s*\(\s*"([^"]+)")re";
    const std::regex re( pattern );
    for ( const auto &m : findMatchesWithPos( src, { 0, src.size() }, pattern ) )
    {
        std::smatch sm;
        const std::string text( m.whole );
        std::regex_search( text, sm, re );
        addWithEvidence( out.ctaCommandIds, out.evidence, sm[1].str(), file,
                         m.pos, src );
    }
}

void CommandRefScanner::scanPreflightActions( std::string_view src,
                                              const std::string &file,
                                              CommandRefReport &out ) const
{
    // addBlocker(outcome, code, message, action[, details]) — action is the
    // 4th argument. Same position for addWarning.
    static const std::string callPattern = R"re(\b(addBlocker|addWarning)\s*\()re";
    for ( const auto &m :
          findMatchesWithPos( src, { 0, src.size() }, callPattern ) )
    {
        const size_t openParen = m.pos + m.whole.size() - 1;
        const Span argsSpan = matchParen( src, openParen );
        if ( !argsSpan.valid() )
            continue;
        const auto args = splitArgs( src, argsSpan );
        if ( args.size() < 4 )
            continue;
        const std::string_view arg = src.substr(
            args[3].begin, args[3].end - args[3].begin );
        // The action must be a plain string literal.
        static const std::regex reLit( R"re(^\s*"([^"]+)"\s*$)re" );
        std::cmatch cm;
        if ( std::regex_match( arg.begin(), arg.end(), cm, reLit ) )
            addWithEvidence( out.preflightActionIds, out.evidence, cm[1].str(),
                             file, args[3].begin, src );
    }
}

} // namespace sicnu::contracts
