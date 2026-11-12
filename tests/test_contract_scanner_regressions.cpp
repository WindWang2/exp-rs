#include <catch2/catch_test_macros.hpp>

#include "contracts/determinism_census.h"
#include "contracts/error_code_scanner.h"
#include "contracts/graph_assembly.h"
#include "support/process_scratch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace sicnu::contracts;

namespace
{
void writeSource( const std::filesystem::path &path, const std::string &text )
{
    std::filesystem::create_directories( path.parent_path() );
    std::ofstream out( path );
    REQUIRE( out.is_open() );
    out << text;
    REQUIRE( out.good() );
}
} // namespace

TEST_CASE( "Error vocabulary ignores quoted declarations and table entries",
           "[contracts][scanner]" )
{
    ErrorCodeScanner scanner;
    ErrorCodeReport report;
    scanner.scanEnum( R"src(
// enum class ErrorCode { Commented };
const char *example = R"text(enum class ErrorCode { Quoted };)text";
enum class ErrorCode { Ok, /* Fake, */ Failed = 7 };
)src", "ErrorCode", report );
    CHECK( report.enumValues == std::set<std::string>{ "Ok", "Failed" } );

    scanner.scanToStringSwitch( R"src(
// case ErrorCode::Commented: return "COMMENTED";
const char *example = R"text(case ErrorCode::Quoted: return "QUOTED";)text";
case ErrorCode::Ok: return "OK";
case ErrorCode::Failed: return "FAILED";
)src", "ErrorCode", report );
    REQUIRE( report.caseMap.size() == 2 );
    CHECK( report.caseMap.at( "Ok" ) == "OK" );
    CHECK( report.caseMap.at( "Failed" ) == "FAILED" );

    scanner.scanHarnessCodes( R"src(
/* inline constexpr const char *kCommented = "COMMENTED"; */
const char *example = R"text(inline constexpr const char *kQuoted = "QUOTED";)text";
inline constexpr const char *kKnown = "KNOWN";
)src", report );
    REQUIRE( report.harnessCodes.size() == 1 );
    CHECK( report.harnessCodes.at( "Known" ) == "KNOWN" );

    scanner.scanHarnessErrorTable( R"src(
// { "COMMENTED", { "runtime", RetryClass::Never } },
/* { "BLOCK_COMMENTED", { "runtime", RetryClass::Never } }, */
const char *example = R"text({ "QUOTED", { "runtime", RetryClass::Never } })text";
{ "KNOWN", { "runtime", RetryClass::Never } },
{ "TABLE_ONLY", { "runtime", RetryClass::Never } },
)src", report );
    REQUIRE( report.harnessCodes.size() == 2 );
    CHECK( report.harnessCodes.at( "Known" ) == "KNOWN" );
    CHECK( report.harnessCodes.at( "TABLE_ONLY" ) == "TABLE_ONLY" );
}

TEST_CASE( "Census class bodies survive quoted closing braces and nested classes",
           "[contracts][scanner]" )
{
    const std::filesystem::path root = sicnu_test::processScratchDir( "contract-scanners", "classes" );
    writeSource( root / "src/operators.cpp", R"src(
// REGISTER_RS_OPERATOR( Ghost, "rs:commented" )
const char *example = R"text(REGISTER_RS_OPERATOR( Ghost, "rs:quoted" ))text";
/* class Real : public RSOperator {
  std::string determinismGrade() const override { return "wrong"; }
}; */
class Real : public RSOperator {
  /* A documentation example may include a class terminator:
};
  */
  struct Nested {
    int value;
};
  // std::string determinismGrade() const override { return "commented"; }
  std::string determinismGrade() const override { return "bit-exact"; }
  RSOperatorDeterminism determinism() const override { return RSOperatorDeterminism::BitExact; }
};
REGISTER_RS_OPERATOR( Real, "rs:real" )
class Empty : public RSOperator {};
REGISTER_RS_OPERATOR( Empty, "rs:empty" )
)src" );
    const auto facts = scanDeterminismOverrides( root.string() );
    REQUIRE( facts.size() == 2 );
    CHECK( facts.at( "rs:empty" ).classFound );
    CHECK_FALSE( facts.at( "rs:empty" ).gradeOverride );
    const auto &real = facts.at( "rs:real" );
    CHECK( real.classFound );
    CHECK( real.gradeOverride );
    CHECK( real.gradeLiteral == "bit-exact" );
    CHECK( real.runtimeOverride );
    CHECK( real.runtimeLiteral == "BitExact" );
}

TEST_CASE( "Census scans real macro definitions after quoted examples",
           "[contracts][scanner]" )
{
    const std::filesystem::path root = sicnu_test::processScratchDir( "contract-scanners", "macros" );
    writeSource( root / "src/operators.cpp", R"src(
/*
#define SICNU_DECLARE_SPATIAL_OP(Name, Id) \
  class Name { std::string determinismGrade() const override { return "wrong"; } };
*/
#define SICNU_DECLARE_SPATIAL_OP(Name, Id) \
  class Name { \
  std::string determinismGrade() const override { return "bit-exact"; } \
  };
#define SICNU_DECLARE_WINDOW_OP(Name, Id) \
  class Name { std::string determinismGrade() const override { return "tolerance"; } };
SICNU_DECLARE_SPATIAL_OP( Spatial, "rs:spatial" )
SICNU_DECLARE_WINDOW_OP( Window, "rs:window" )
REGISTER_RS_OPERATOR( Spatial, "rs:spatial" )
REGISTER_RS_OPERATOR( Window, "rs:window" )
)src" );
    const auto facts = scanDeterminismOverrides( root.string() );
    REQUIRE( facts.size() == 2 );
    CHECK( facts.at( "rs:spatial" ).classFound );
    CHECK( facts.at( "rs:spatial" ).gradeLiteral == "bit-exact" );
    CHECK( facts.at( "rs:window" ).classFound );
    CHECK( facts.at( "rs:window" ).gradeLiteral == "tolerance" );
}

TEST_CASE( "Graph assembly reports missing and empty command sources",
           "[contracts][scanner]" )
{
    const std::filesystem::path root = sicnu_test::processScratchDir( "contract-scanners", "sources" );
    const auto countSourceNotes = []( const AssemblyResult &assembled ) {
        return std::count_if( assembled.notes.begin(), assembled.notes.end(), []( const std::string &note ) {
            return note.find( "command source:" ) != std::string::npos;
        } );
    };
    CHECK( countSourceNotes( buildLiveGraph( root.string() ) ) == 3 );
    for ( const char *path : { "src/app/workbench/command_defs.cpp", "src/app/main_window_workbench.cpp",
                              "src/agent/harness/scientific_preflight.cpp" } )
        writeSource( root / path, "" );
    CHECK( countSourceNotes( buildLiveGraph( root.string() ) ) == 3 );
    for ( const char *path : { "src/app/workbench/command_defs.cpp", "src/app/main_window_workbench.cpp",
                              "src/agent/harness/scientific_preflight.cpp" } )
        writeSource( root / path, "// This source is readable.\n" );
    CHECK( countSourceNotes( buildLiveGraph( root.string() ) ) == 0 );
}
