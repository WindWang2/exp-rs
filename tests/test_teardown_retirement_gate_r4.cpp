// test_teardown_retirement_gate_r4.cpp — WP-G: retirement drift gate.
//
// Contract: every test file whose `_Exit` workaround RETIREMENT.md declares
// as 退役 (retired) must be free of std::_Exit / _exit( calls. If a retired
// workaround is ever reintroduced (or the doc claims a retirement the code
// does not have), this gate goes red. Reads the planning doc from the
// source tree — the same text-drift pattern as test_build_wiring_drift and
// the WP-E shutdown-pair check; no production hooks.
//
// Doc contract (RETIREMENT.md table): columns
//   # | file:line | family | root cause (A-n) | verdict | condition | commit
// A row counts as "retired" iff the verdict cell contains 退役 but NOT
// 语义保留 and NOT 保留 (so 语义保留/保留 never gate; they are documented
// verdicts, not retirements).
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdio>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

#ifndef SICNU_SOURCE_DIR
#define SICNU_SOURCE_DIR "."
#endif

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_teardown_retirement_gate_r4";
  char *fake_argv[] = { fake_argv0, nullptr };

  std::string slurp( const std::string &path, bool *ok )
  {
    FILE *f = std::fopen( path.c_str(), "rb" );
    if ( !f )
    {
      *ok = false;
      return {};
    }
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ( ( n = std::fread( buf, 1, sizeof( buf ), f ) ) > 0 )
      text.append( buf, n );
    std::fclose( f );
    *ok = true;
    return text;
  }

  bool rowIsRetiredVerdict( const std::string &verdictCell )
  {
    return verdictCell.find( "退役" ) != std::string::npos &&
           verdictCell.find( "语义保留" ) == std::string::npos &&
           verdictCell.find( "保留" ) == std::string::npos;
  }
} // namespace

TEST_CASE( "Retirement gate: RETIREMENT.md 退役 rows are _Exit-free", "[teardown][r4]" )
{
  bool ok = false;
  const std::string doc = slurp(
    SICNU_SOURCE_DIR "/.planning/qt-teardown-lifecycle-r4/RETIREMENT.md", &ok );
  REQUIRE( ok ); // planning artifact ships with the branch

  // Parse table rows: | N | tests/file.cpp:LINE | ... | verdict | ... |
  static const std::regex rowRe(
    "^\\|\\s*[0-9]+\\s*\\|\\s*([^|]+?)\\s*\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|([^|]*)\\|");
  std::map<std::string, int> retiredSites;   // file -> count of retired rows
  std::map<std::string, int> fileExitHits;   // file -> _Exit call sites present

  std::istringstream stream( doc );
  std::string line;
  while ( std::getline( stream, line ) )
  {
    std::smatch m;
    if ( !std::regex_search( line, m, rowRe ) )
      continue;
    const std::string site = m[1].str();
    const std::string verdict = m[4].str();
    const auto colon = site.find( ':' );
    if ( colon == std::string::npos )
      continue;
    const std::string file = site.substr( 0, colon );
    if ( rowIsRetiredVerdict( verdict ) )
      retiredSites[file]++;
  }
  REQUIRE( retiredSites.size() >= 8 ); // brief floor: ≥8 retirements

  bool drift = false;
  for ( const auto &entry : retiredSites )
  {
    const std::string path = SICNU_SOURCE_DIR "/tests/" + entry.first;
    const std::string src = slurp( path, &ok );
    if ( !ok )
    {
      FAIL( "retired file missing: " << entry.first );
      continue;
    }
    // Count real call sites (std::_Exit / bare _exit( ) — comments may name
    // them, so require the call form with an open parenthesis.
    int hits = 0;
    size_t pos = 0;
    while ( ( pos = src.find( "::_Exit(", pos ) ) != std::string::npos )
    {
      ++hits;
      pos += 8;
    }
    pos = 0;
    while ( ( pos = src.find( "_exit(", pos ) ) != std::string::npos )
    {
      ++hits;
      pos += 6;
    }
    if ( hits > 0 )
    {
      drift = true;
      FAIL( "retirement drift in " << entry.first << ": " << hits << " _Exit/_exit call(s) reintroduced" );
    }
  }
  REQUIRE_FALSE( drift );
}
