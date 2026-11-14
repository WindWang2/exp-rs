#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <string>

#ifndef SICNU_TEST_PYTHON_EXECUTABLE
#error "SICNU_TEST_PYTHON_EXECUTABLE must name the test Python interpreter"
#endif

namespace {

exprs::ExternalProcessRequest pythonRequest( const std::string &code )
{
    exprs::ExternalProcessRequest request;
    request.argv = { SICNU_TEST_PYTHON_EXECUTABLE, "-I", "-c", code };
    request.timeoutSeconds = 20;
    return request;
}

constexpr std::uint64_t kMemoryBudget = 128ULL * 1024 * 1024;

} // namespace

TEST_CASE( "Python allocation is bounded only when a process memory budget is requested",
           "[python][external][memory]" )
{
    const std::string code =
        "import sys\n"
        "try:\n"
        "    block = bytearray(256 * 1024 * 1024)\n"
        "except MemoryError:\n"
        "    print('memory-limit-enforced', flush=True)\n"
        "    sys.exit(17)\n"
        "print('allocated', flush=True)\n";
    auto request = pythonRequest( code );
    const auto baseline = exprs::ExternalProcess::run( request );
    INFO( baseline.error << "\n" << baseline.stdErr );
    REQUIRE( baseline.exitedCleanly() );
    REQUIRE( baseline.stdOut.find( "allocated" ) != std::string::npos );

    request.memoryLimitBytes = kMemoryBudget;
    const auto limited = exprs::ExternalProcess::run( request );
    INFO( limited.error << "\n" << limited.stdErr );
#ifdef __APPLE__
    // Darwin rejects lowering RLIMIT_AS below the forked host's existing
    // address space; that refusal must never become an unlimited execution.
    if ( !limited.started )
    {
        REQUIRE( limited.error.find( "memory limit" ) != std::string::npos );
        REQUIRE( limited.stdOut.empty() );
        return;
    }
#endif
    REQUIRE( limited.started );
    REQUIRE_FALSE( limited.exitedCleanly() );
    REQUIRE_FALSE( limited.timedOut );
    REQUIRE_FALSE( limited.cancelled );
    REQUIRE( limited.exitCode == 17 );
    REQUIRE( limited.stdOut.find( "memory-limit-enforced" ) != std::string::npos );
}

TEST_CASE( "A memory-limited Python process still honors cancellation",
           "[python][external][cancel]" )
{
    auto request = pythonRequest(
        "import time\nprint('ready', flush=True)\ntime.sleep(30)\n" );
    request.memoryLimitBytes = kMemoryBudget;
    bool ready = false;
    request.onOutput = [&ready]( long stdoutBytes, long ) {
        ready = stdoutBytes > 0;
    };
    request.isCancelled = [&ready]() { return ready; };
    const auto result = exprs::ExternalProcess::run( request );
    INFO( result.error << "\n" << result.stdErr );
#ifdef __APPLE__
    if ( !result.started )
    {
        REQUIRE( result.error.find( "memory limit" ) != std::string::npos );
        REQUIRE( result.stdOut.empty() );
        return;
    }
#endif
    REQUIRE( result.started );
    REQUIRE( ready );
    REQUIRE( result.cancelled );
    REQUIRE_FALSE( result.timedOut );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE( result.durationMs < 10000 );
}
