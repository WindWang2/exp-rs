/***************************************************************************
 * error_code_scanner.h — error taxonomy extraction (M3)
 *
 * Extracts the machine-side error vocabularies so the diagnostics catalog
 * census can prove completeness (issue class #870):
 *
 *   - RSOperatorError: `enum class ErrorCode` values from the header and
 *     the `case ErrorCode::X: return "…";` pairs of errorCodeToString —
 *     the switch must cover every enum value (enum drift guard);
 *   - HarnessError: `inline constexpr const char *kXxx = "XXX";` pairs from
 *     the error_codes namespace.
 *
 * With these, the census test can require: every code resolves to a curated
 * diagnostics page or an explicit allow-list entry — fallback() can no
 * longer silently absorb a new code.
 ***************************************************************************/
#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace sicnu::contracts {

struct ErrorCodeReport
{
    std::set<std::string> enumValues;            // ErrorCode enumerators
    std::map<std::string, std::string> caseMap;  // enumerator → string form
    std::map<std::string, std::string> harnessCodes; // kVar → wire code
};

class ErrorCodeScanner
{
  public:
    /// Extracts enumerators of `enum class Name` in the given header source.
    void scanEnum( std::string_view headerSrc, const std::string &enumName,
                   ErrorCodeReport &out ) const;

    /// Extracts `case Enum::X: return "…";` pairs from an implementation.
    void scanToStringSwitch( std::string_view src, const std::string &enumName,
                             ErrorCodeReport &out ) const;

    /// Extracts `inline constexpr const char *kVar = "CODE";` pairs.
    void scanHarnessCodes( std::string_view headerSrc,
                           ErrorCodeReport &out ) const;
};

} // namespace sicnu::contracts
