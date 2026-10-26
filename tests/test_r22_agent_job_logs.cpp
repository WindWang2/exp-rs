// tests/test_r22_agent_job_logs.cpp — R22-04 (#1705) regression.
//
// RED first: JobEngine::appendLog was an unbounded push_back, so a chatty
// running job grew logLines without limit (live repro in the issue: 50 000
// logInfo lines produced a 50 002-line record) and every snapshot/terminal
// notify copied the whole vector under the engine mutex. Retention is now
// bounded (block eviction of the oldest lines past the cap, per-line byte
// truncation, droppedLogLines accounting, delta-cursor compensation), and
// the retained window stays a contiguous RECENT slice whose contents and
// ordering are exactly what the job printed.
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"

#include <json/json.h>

#include <mutex>
#include <set>
#include <string>

using namespace sicnu::jobs;

namespace
{
struct EngineGuard
{
  EngineGuard()
  {
    auto &eng = JobEngine::instance();
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setMaxWorkers( 2 );
  }
  ~EngineGuard()
  {
    auto &eng = JobEngine::instance();
    eng.waitUntilIdleForTests( 15000 );
    eng.setListener( nullptr );
    eng.clearExecutors();
    eng.setFallbackExecutor( {} );
  }
  JobEngine &engine() { return JobEngine::instance(); }
};
} // namespace

TEST_CASE( "chatty jobs keep bounded logLines with drop accounting (#1705)",
           "[job][r22]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  constexpr int kTotalLines = 25'000; // > 2x the 10 000-line cap
  eng.registerExecutor( "chatty:", []( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
    for ( int i = 0; i < kTotalLines; ++i )
      ctx.logInfo( "line " + std::to_string( i ) );
    Json::Value r( Json::objectValue );
    r["ok"] = true;
    return r;
  } );

  JobRequest req;
  req.algorithmId = "chatty:loop";
  req.title = "chatty";
  req.source = "test";
  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests( 15000 );

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );

  // Appended total = Started + kTotalLines + Succeeded.
  const std::size_t appended = 2u + static_cast<std::size_t>( kTotalLines );
  // RED behavior: logLines.size() == appended (unbounded). GREEN: bounded
  // to the retention window with exact drop accounting.
  REQUIRE( snap->logLines.size() <= 10'000 );
  REQUIRE( snap->logLines.size() + snap->droppedLogLines == appended );
  REQUIRE( snap->droppedLogLines == appended - snap->logLines.size() );

  // The retained window is the RECENT slice of what the job actually
  // printed: first retained line is exactly the (dropped)-th appended
  // line, the terminal "Succeeded" line is retained.
  REQUIRE( snap->logLines.front().text ==
           "line " + std::to_string( snap->droppedLogLines - 1 ) );
  REQUIRE( snap->logLines.back().text == "Succeeded" );
}

TEST_CASE( "log deltas stay contiguous and duplicate-free under eviction (#1705)",
           "[job][r22]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  constexpr int kTotalLines = 12'000;
  eng.registerExecutor( "chatty2:", []( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
    for ( int i = 0; i < kTotalLines; ++i )
      ctx.logInfo( "line " + std::to_string( i ) );
    return Json::Value( Json::objectValue );
  } );

  struct Delta
  {
    std::size_t offset = 0; // logLinesOffset (1-based start)
    std::vector<std::string> lines;
  };
  std::mutex mu;
  std::vector<Delta> deltas;
  eng.setListener( [&]( const JobRecord &rec ) {
    if ( rec.logLinesOffset == 0 )
      return; // cumulative records (Started/terminal) — not deltas
    std::lock_guard<std::mutex> lock( mu );
    deltas.push_back( Delta{ rec.logLinesOffset, {} } );
    for ( const auto &l : rec.logLines )
      deltas.back().lines.push_back( l.text );
  } );

  JobRequest req;
  req.algorithmId = "chatty2:loop";
  req.title = "chatty2";
  req.source = "test";
  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests( 15000 );
  eng.setListener( nullptr );

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );

  std::lock_guard<std::mutex> lock( mu );
  REQUIRE( deltas.size() >= 2 );

  // Delta offsets reference the ABSOLUTE appended-line space (#1705), so
  // ranges are strictly consecutive (no gap, no overlap ⇒ no lost or
  // duplicated forwarded line) even across bounded-window evictions, and no
  // content is delivered twice.
  std::set<std::string> seen;
  std::size_t expectedNext = deltas.front().offset;
  for ( const Delta &d : deltas )
  {
    REQUIRE( d.offset == expectedNext );
    for ( const std::string &text : d.lines )
    {
      REQUIRE_FALSE( seen.count( text ) );
      seen.insert( text );
    }
    expectedNext = d.offset + d.lines.size();
  }
  // The stream ends exactly at the ABSOLUTE terminal index: Started +
  // kTotalLines + terminal "Succeeded" — the terminal line ships as the
  // last delta (never dropped by the bounded window, and deliverable even
  // though the retained window is smaller than the consumer's index).
  // (offset is 1-based: front.offset - 1 + delivered == appended, so the
  // consecutive-stream cursor lands on appended + 1.)
  const std::size_t appended = 2u + static_cast<std::size_t>( kTotalLines );
  REQUIRE( expectedNext == appended + 1 );
  REQUIRE( deltas.back().lines.back() == "Succeeded" );
  // Eviction happened (bounded) exactly once at the 10001st append: 5001
  // lines dropped, then the remaining 2001 appends refill the window to
  // 7001 — and the retained window is exactly the recent tail.
  REQUIRE( snap->droppedLogLines == 5'001 );
  REQUIRE( snap->logLines.size() == 7'001 );
  REQUIRE( snap->logLines.size() + snap->droppedLogLines == 12'002 );
}

TEST_CASE( "oversized single log lines are truncated, not stored verbatim (#1705)",
           "[job][r22]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.registerExecutor(
      "fatline:", []( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
        ctx.logInfo( std::string( 1024 * 1024, 'X' ) );
        ctx.logInfo( "small" );
        return Json::Value( Json::objectValue );
      } );

  JobRequest req;
  req.algorithmId = "fatline:one";
  req.title = "fat";
  req.source = "test";
  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests( 15000 );

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->state == JobState::Succeeded );
  REQUIRE( snap->droppedLogLines == 0 );
  // Started, truncated fat line, "small", Succeeded — in order.
  REQUIRE( snap->logLines.size() == 4 );
  const std::string &fat = snap->logLines[1].text;
  INFO( "fat line retained size: " << fat.size() );
  REQUIRE( fat.size() < 8 * 1024 );
  REQUIRE( fat.find( "[truncated" ) != std::string::npos );
  REQUIRE( fat.find( "small" ) == std::string::npos );
  REQUIRE( snap->logLines[2].text == "small" );
  REQUIRE( snap->logLines[3].text == "Succeeded" );
}

TEST_CASE( "ordinary short jobs keep their full log history (#1705 control)",
           "[job][r22]" )
{
  EngineGuard guard;
  auto &eng = guard.engine();

  eng.registerExecutor( "quiet:", []( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
    for ( int i = 0; i < 40; ++i )
      ctx.logInfo( "q" + std::to_string( i ) );
    return Json::Value( Json::objectValue );
  } );

  JobRequest req;
  req.algorithmId = "quiet:loop";
  req.title = "quiet";
  req.source = "test";
  const auto id = eng.submit( req );
  eng.waitUntilIdleForTests( 15000 );

  auto snap = eng.snapshot( id );
  REQUIRE( snap.has_value() );
  REQUIRE( snap->droppedLogLines == 0 );
  // Started + 40 lines + Succeeded
  REQUIRE( snap->logLines.size() == 42 );
  REQUIRE( snap->logLines[1].text == "q0" );
  REQUIRE( snap->logLines[40].text == "q39" );
}
