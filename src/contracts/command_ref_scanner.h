/***************************************************************************
 * command_ref_scanner.h — command/action reference extraction (M3)
 *
 * Extracts the Workbench command-id vocabulary and every consumer family
 * that must agree with it (issue classes #869/#881/#882):
 *
 *   registeredIds      — ids registered on the CommandRegistry
 *                        (RS_CMD/base literals in command_defs.cpp, plus
 *                        `.id = QStringLiteral("…")` registration sites);
 *   lookupIds          — `->action("…")` surface references;
 *   ctaCommandIds      — empty-state/context CTA `commandId = "…"`;
 *   preflightActionIds — suggested-repair action literals passed to
 *                        addBlocker/addWarning (argument position 4);
 *   evidence           — id → "file:line" of first reference.
 *
 * The idiom list is explicit and test-pinned; an unrecognized construct
 * fails coverage elsewhere (the tests assert non-empty registered set and
 * spot-check known ids), never silently passes.
 ***************************************************************************/
#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sicnu::contracts {

struct CommandRefReport
{
    std::set<std::string> registeredIds;
    std::set<std::string> lookupIds;
    std::set<std::string> ctaCommandIds;
    std::set<std::string> preflightActionIds;
    std::map<std::string, std::string> evidence; // id → first "file:line"
};

class CommandRefScanner
{
  public:
    /// Scans one source buffer for one idiom family.
    void scanRegistered( std::string_view src, const std::string &file,
                         CommandRefReport &out ) const;
    void scanLookups( std::string_view src, const std::string &file,
                      CommandRefReport &out ) const;
    void scanCtas( std::string_view src, const std::string &file,
                   CommandRefReport &out ) const;
    void scanPreflightActions( std::string_view src, const std::string &file,
                               CommandRefReport &out ) const;
};

} // namespace sicnu::contracts
