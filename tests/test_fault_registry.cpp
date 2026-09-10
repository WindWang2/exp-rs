// test_fault_registry.cpp — fault-injection registry semantics (task B).
//
// Asserts arm/fire/disarm determinism, NextN/Always/EveryNth modes, payload
// plumbing, RAII scoping, hot-path coherence under concurrency, and the
// production contract "a fault point routes through the caller's failure
// path — never a fabricated success".
#include <catch2/catch_test_macros.hpp>

#include "runtime/observability/fault_point.h"
#include "runtime/observability/fault_registry.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::runtime::observability::fault;

namespace
{
/// Simulates a production seam: returns "failure" exactly when the point
/// fires — the caller's own failure branch, never a fabricated result.
std::string writeThroughSeam( const std::string &bytes )
{
    if ( SICNU_FAULT_POINT( "seam.write" ) )
        return "failure";
    return "ok:" + bytes;
}

struct AllDisarmed
{
    ~AllDisarmed() { disarmAllFaults(); }
};
} // namespace

TEST_CASE( "fault registry: NextN fires once then disarms itself", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::NextN, 1, {} } );
    REQUIRE( armedFaultCount() == 1 );
    REQUIRE( writeThroughSeam( "a" ) == "failure" ); // fires
    REQUIRE( armedFaultCount() == 0 );               // self-disarmed
    REQUIRE( writeThroughSeam( "b" ) == "ok:b" );
    REQUIRE( writeThroughSeam( "c" ) == "ok:c" );
}

TEST_CASE( "fault registry: NextN count semantics are exact", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::NextN, 2, {} } );
    REQUIRE( writeThroughSeam( "1" ) == "failure" );
    REQUIRE( writeThroughSeam( "2" ) == "failure" );
    REQUIRE( writeThroughSeam( "3" ) == "ok:3" );
}

TEST_CASE( "fault registry: Always persists until disarm", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::Always, 1, {} } );
    for ( int i = 0; i < 5; ++i )
        REQUIRE( writeThroughSeam( "x" ) == "failure" );
    disarmFault( "seam.write" );
    REQUIRE( writeThroughSeam( "y" ) == "ok:y" );
}

TEST_CASE( "fault registry: EveryNth fires on the n-th call", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::EveryNth, 3, {} } );
    std::vector<std::string> results;
    for ( int i = 0; i < 7; ++i )
        results.push_back( writeThroughSeam( std::to_string( i ) ) );
    // calls 1..7 against period 3: fires at 3 and 6.
    REQUIRE( results == std::vector<std::string>{ "ok:0", "ok:1", "failure", "ok:3", "ok:4",
                                                  "failure", "ok:6" } );
}

TEST_CASE( "fault registry: payload is readable while armed", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::NextN, 1, "truncated-bytes" } );
    REQUIRE( faultPayload( "seam.write" ) == "truncated-bytes" );
    writeThroughSeam( "z" );
    REQUIRE( faultPayload( "seam.write" ).empty() ); // disarmed itself
}

TEST_CASE( "fault registry: RAII arming survives throwing assertions", "[fault][registry]" )
{
    disarmAllFaults();
    try
    {
        ArmedFault armed( { "seam.write", Mode::Always, 1, {} } );
        REQUIRE( writeThroughSeam( "t" ) == "failure" );
        throw std::runtime_error( "simulated assertion failure" );
    }
    catch ( const std::runtime_error & )
    {
    }
    // ArmedFault's destructor disarmed everything despite the throw.
    REQUIRE( writeThroughSeam( "u" ) == "ok:u" );
    REQUIRE( armedFaultCount() == 0 );
}

TEST_CASE( "fault registry: distinct names do not interfere", "[fault][registry]" )
{
    AllDisarmed guard;
    armFault( { "a.one", Mode::NextN, 1, {} } );
    armFault( { "a.two", Mode::Always, 1, {} } );
    REQUIRE( armedFaultCount() == 2 );
    REQUIRE( shouldFail( "a.one" ) );
    REQUIRE_FALSE( shouldFail( "a.one" ) );
    REQUIRE( shouldFail( "a.two" ) );
    REQUIRE( shouldFail( "a.two" ) );
}

TEST_CASE( "fault registry: concurrent probes never crash and NextN is bounded",
           "[fault][registry][stress]" )
{
    AllDisarmed guard;
    armFault( { "seam.write", Mode::NextN, 100, {} } );
    std::atomic<int> fired{ 0 };
    std::vector<std::thread> threads;
    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back( [&] {
            for ( int i = 0; i < 200; ++i )
                if ( shouldFail( "seam.write" ) )
                    ++fired;
        } );
    }
    for ( auto &thread : threads )
        thread.join();
    REQUIRE( fired.load() == 100 ); // exactly the armed budget, no more
    REQUIRE( armedFaultCount() == 0 );
}

TEST_CASE( "fault registry: rollback after a fired fault runs fault-free", "[fault][registry]" )
{
    AllDisarmed guard;
    // Real seams: the fault-induced failure triggers a rollback that re-enters
    // the seam. With NextN(1), the entry is consumed by the first firing, so
    // the re-entrant cleanup call must observe a healthy path.
    armFault( { "seam.write", Mode::NextN, 1, {} } );
    REQUIRE( writeThroughSeam( "outer" ) == "failure" );
    REQUIRE( writeThroughSeam( "rollback" ) == "ok:rollback" );
}
