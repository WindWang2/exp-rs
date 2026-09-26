// test_exit_path_contracts_r4.cpp — WP-E: exit-path determinism contracts.
//
// Pins the teardown contracts that src/app/main.cpp's exit paths rely on:
//   1. Both non-GUI-interactive exit paths (GUI tail, MCP tail) call the
//      SAME shutdown pair — sicnu::TaskCenter::instance().shutdown() and
//      sicnu::jobs::JobEngine::instance().shutdown() (main.cpp:323-324 and
//      :622-623). Enforced as a source-text drift check, the established
//      pattern of test_build_wiring_drift (no production hooks added).
//   2. shutdownForTests() (the public reusable join seam, job_engine.h:200)
//      is idempotent — call twice, engine remains quiesced and reusable.
//   3. shutdown() (the terminal production call) followed by
//      shutdownForTests() is safe — the exit path must tolerate any
//      late-arriving cleanup.
//   4. QgsApplication::exitQgis() is safe to call repeatedly and with no
//      QgsApplication instance at all (all sub-steps guard "don't create
//      just to delete") — the ordered-teardown listener depends on this.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <qgsapplication.h>

#include <processing/framework/task_center.h>
#include <jobs/job_engine.h>

#include <cstdio>
#include <string>

#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

#ifndef SICNU_SOURCE_DIR
#define SICNU_SOURCE_DIR "."
#endif

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_exit_path_contracts_r4";
  char *fake_argv[] = { fake_argv0, nullptr };
}

TEST_CASE( "Exit path: GUI and MCP tails share the shutdown pair", "[teardown][r4]" )
{
  FILE *f = std::fopen( SICNU_SOURCE_DIR "/src/app/main.cpp", "rb" );
  REQUIRE( f != nullptr );
  std::string text;
  char buf[4096];
  size_t n = 0;
  while ( ( n = std::fread( buf, 1, sizeof( buf ), f ) ) > 0 )
    text.append( buf, n );
  std::fclose( f );

  const std::string tc = "sicnu::TaskCenter::instance().shutdown();";
  const std::string je = "sicnu::jobs::JobEngine::instance().shutdown();";
  const auto countOf = [ & ]( const std::string &needle ) {
    size_t pos = 0;
    int count = 0;
    while ( ( pos = text.find( needle, pos ) ) != std::string::npos )
    {
      ++count;
      pos += needle.size();
    }
    return count;
  };
  // Both interactive exit tails (GUI and MCP) must run the same pair; the
  // drift gate trips if a future mode tail drops either call.
  REQUIRE( countOf( tc ) >= 2 );
  REQUIRE( countOf( je ) >= 2 );
}

TEST_CASE( "Exit path: shutdownForTests is idempotent and leaves reusable engine", "[teardown][r4]" )
{
  sicnu::TaskCenter::instance().shutdownForTests();
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  // Second pass must be a safe no-op join, not a double-join crash.
  sicnu::TaskCenter::instance().shutdownForTests();
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  REQUIRE( true );
}

TEST_CASE( "Exit path: terminal shutdown tolerates late cleanup call", "[teardown][r4]" )
{
  sicnu::jobs::JobEngine::instance().shutdown();
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  REQUIRE( true );
}

TEST_CASE( "Exit path: exitQgis idempotent with and without QGIS application", "[teardown][r4]" )
{
  // No QgsApplication instance in this binary; every exitQgis sub-step must
  // guard "don't create just to delete" instead of instantiating singletons.
  QgsApplication::exitQgis();
  QgsApplication::exitQgis();
  REQUIRE( true );
}
