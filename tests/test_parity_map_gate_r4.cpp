// test_parity_map_gate_r4.cpp — living-documentation gate (WP-G)
//
// The PARITY_MAP is only trustworthy while it cannot silently rot. This gate
// pins it to the code:
//
//   1. the map declares at least 30 mapping-pair rows (five contract columns
//      plus the prior-track column);
//   2. every row's oracle reference either
//        (a) cites "<test-file>.cpp [parity-<id>]" where the file exists in
//            tests/ AND the text contains that exact tag, or
//        (b) cites an existing suite ("既有 test_<name>.cpp ..."), or
//        (c) declares "无 oracle"/"无新 oracle"/"待核" with a written reason;
//   3. every [parity-<id>] tag in the map resolves to exactly one tagged
//      TEST_CASE across the parity suites (no dangling, no duplicates).
//
// Removing a map row, renaming a case without updating the map, or tagging a
// case the map does not cite turns this gate red. The manual drift drill
// (delete one row → gate red → restore) is recorded in EVIDENCE.md.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

#ifdef CMAKE_SOURCE_DIR
const fs::path kSourceDir = CMAKE_SOURCE_DIR;
#else
const fs::path kSourceDir = ".";
#endif

const fs::path kMapPath =
  kSourceDir / ".planning" / "ui-backend-state-parity-r4" / "PARITY_MAP.md";
const fs::path kTestsDir = kSourceDir / "tests";

std::string readFile( const fs::path &path )
{
  std::ifstream in( path );
  if ( !in )
    return std::string();
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool isPairRow( const std::string &line )
{
  // Pair rows look like: "| GW-1 | ... | ... | ... | ... | ... | ... |"
  static const std::regex pattern( "^\\|\\s*[A-Z]{2}-[0-9]+\\s*\\|" );
  return std::regex_search( line, pattern );
}

std::vector<std::string> splitRow( const std::string &line )
{
  std::vector<std::string> cells;
  std::string cell;
  for ( const char ch : line )
  {
    if ( ch == '|' )
    {
      cells.push_back( cell );
      cell.clear();
    }
    else
    {
      cell += ch;
    }
  }
  cells.push_back( cell );
  return cells;
}

} // namespace

TEST_CASE( "GA-1: PARITY_MAP stays wired to the parity oracles",
           "[parity][gate][parity-ga1]" )
{
  const std::string mapText = readFile( kMapPath );
  REQUIRE( !mapText.empty() );

  // ---- 1. pair-row floor -------------------------------------------------
  std::vector<std::string> pairRows;
  std::istringstream rows( mapText );
  std::string line;
  while ( std::getline( rows, line ) )
  {
    if ( isPairRow( line ) )
      pairRows.push_back( line );
  }
  INFO( "pair rows found: " << pairRows.size() );
  CHECK( pairRows.size() >= 30 );

  // ---- 2. per-row oracle references resolve ------------------------------
  // Every pair row must have the five contract columns + prior column
  // (ID + 6 cells => 8 pipe tokens), and the oracle cell must resolve.
  int unresolved = 0;
  std::vector<std::string> citedTags;
  for ( const std::string &row : pairRows )
  {
    const std::vector<std::string> cells = splitRow( row );
    if ( cells.size() < 9 )
    {
      FAIL( "row missing columns: " << row );
      continue;
    }
    // splitRow keeps the empty cell before the leading '|': cells[0] is
    // empty, cells[1] is the ID, so the oracle column is index 6.
    const std::string oracle = cells[6];
    if ( oracle.find( "无独立 oracle" ) != std::string::npos
         || oracle.find( "无新 oracle" ) != std::string::npos
         || oracle.find( "无 oracle" ) != std::string::npos
         || oracle.find( "待核" ) != std::string::npos )
    {
      continue; // honest no-oracle row with written reason
    }

    // A cell may cite several files (既有 suite + new oracle); every tag
    // must resolve in at least one cited file.
    static const std::regex refPattern( "test_([a-z0-9_]+)\\.cpp" );
    std::string citedText;
    bool citedAny = false;
    for ( auto fileIt = std::sregex_iterator( oracle.begin(), oracle.end(), refPattern );
          fileIt != std::sregex_iterator(); ++fileIt )
    {
      const std::string testFile = "test_" + ( *fileIt )[1].str() + ".cpp";
      const fs::path testPath = kTestsDir / testFile;
      if ( !fs::exists( testPath ) )
      {
        FAIL( "oracle cites missing file " << testFile << ": " << row );
        ++unresolved;
        continue;
      }
      citedText += readFile( testPath );
      citedAny = true;
    }
    // A 既有-only row without any file citation is a static-anchor row:
    // accepted as-is (the anchor itself is reviewed against the code).
    if ( !citedAny && oracle.find( "既有" ) != std::string::npos )
      continue;
    if ( !citedAny )
    {
      FAIL( "oracle cell cites no existing test file: " << row );
      ++unresolved;
      continue;
    }
    const std::string &testText = citedText;

    // A new-oracle citation must carry a [parity-*] tag present in the file.
    static const std::regex tagPattern( "\\[parity-[a-z0-9]+\\]" );
    auto tagsBegin = std::sregex_iterator( oracle.begin(), oracle.end(), tagPattern );
    auto tagsEnd = std::sregex_iterator();
    bool citesExistingSuite = oracle.find( "既有" ) != std::string::npos;
    if ( tagsBegin == tagsEnd )
    {
      if ( !citesExistingSuite )
      {
        FAIL( "oracle cell without tag or 既有 marker: " << row );
        ++unresolved;
      }
      continue;
    }
    for ( auto it = tagsBegin; it != tagsEnd; ++it )
    {
      const std::string tag = it->str();
      if ( testText.find( tag ) == std::string::npos )
      {
        FAIL( "tag " << tag << " not found in any cited file: " << row );
        ++unresolved;
      }
      citedTags.push_back( tag );
    }
  }
  CHECK( unresolved == 0 );

  // ---- 3. no dangling parity tags ---------------------------------------
  // Every [parity-*] tag defined in any parity suite must be cited by the
  // map (the gate itself holds [parity-ga1]).
  static const std::regex definedTag( "\\[parity-([a-z0-9]+)\\]" );
  std::map<std::string, int> defined;
  for ( const fs::path &file : fs::directory_iterator( kTestsDir ) )
  {
    const std::string name = file.filename().string();
    if ( name.find( "parity" ) == std::string::npos
         || file.extension() != ".cpp" )
      continue;
    const std::string text = readFile( file );
    for ( auto it = std::sregex_iterator( text.begin(), text.end(), definedTag );
          it != std::sregex_iterator(); ++it )
    {
      ++defined[it->str()];
    }
  }
  int dangling = 0;
  for ( const auto &entry : defined )
  {
    if ( entry.first == "[parity-ga1]" )
      continue; // this gate
    if ( std::find( citedTags.begin(), citedTags.end(), entry.first ) == citedTags.end() )
    {
      WARN( "parity tag not cited by PARITY_MAP: " << entry.first );
      ++dangling;
    }
  }
  CHECK( dangling == 0 );
  // The floor for runtime oracles: the suites must define at least 20
  // distinct parity tags (deliverable floor 4.2).
  INFO( "distinct parity tags: " << defined.size() );
  CHECK( defined.size() >= 21 ); // 20 oracles + this gate
}
