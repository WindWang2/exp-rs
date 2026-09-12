// tests/test_platform9.cpp
//
// Cartography Platform 9.0 — M0 regression corpus first:
//   * the four closed solver/condition issues (#864/#865/#866/#877) pinned
//     as old-code-fails/new-code-passes regressions on the CURRENT master;
//   * ordering totality across value kinds (±Inf, mixed kinds, bools);
//   * has() semantics incl. present-null;
//   * deterministic evaluation and solve (repeat runs, identical digests);
//   * the ledger-dedupe contract for permanently-failed constraints across
//     the unsat-core restore sweep (P2-1).
//
// Later 9.0 milestones append their sections below.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/cartography/quality.h"
#include "agent/cartography/registry.h"
#include "agent/cartography/style_compiler.h"
#include "agent/cartography/style_spec.h"
#include "agent/cartography/typography.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"
#include "agent/mapspec/mapspec_compiler.h"

#include "agent/cartography/export.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <qgslayoutitemlabel.h>
#include <qgsprintlayout.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "agent/layout_tools/layout_service.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value item( const std::string &id, double x, double y, double w, double h )
{
  Json::Value entry( Json::objectValue );
  entry["id"] = id;
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  entry["rect_mm"] = rect;
  return entry;
}

Json::Value rectOf( const Json::Value &spec, const char *collection, const std::string &id )
{
  for ( const auto &candidate : spec[collection] )
    if ( candidate.isObject() && candidate.isMember( "id" ) &&
         candidate["id"].asString() == id )
      return candidate["rect_mm"];
  return Json::Value();
}

Json::Value constraint( const std::string &kind, const std::vector<std::string> &ids )
{
  Json::Value c( Json::objectValue );
  c["kind"] = kind;
  Json::Value items( Json::arrayValue );
  for ( const auto &id : ids )
    items.append( id );
  c["items"] = items;
  return c;
}

int countDecisions( const CompositionResult &result, const std::string &cid,
                    const std::string &outcome )
{
  int hits = 0;
  for ( const auto &decision : result.decisions )
    hits += ( decision.cid == cid && decision.outcome == outcome ) ? 1 : 0;
  return hits;
}

} // namespace

TEST_CASE( "M0: #864 non-convergence rolls the layout back to pre-solve geometry",
           "[platform9][solver][regression]" )
{
  // Two `inside` constraints pull X toward two different container centers:
  // a genuine contradiction. Pre-8.0-fix master parked the layout at
  // whichever midpoint the pass budget exhausted on; the fix rolls the
  // geometry back and reports non-convergence.
  Json::Value spec = makeMapSpec( "m0-rollback", Json::Value() );
  spec["map_frames"].append( item( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( item( "c2", 150, 10, 80, 60 ) );
  spec["titles"].append( item( "x", 70, 35, 20, 20 ) );
  Json::Value c1 = constraint( "inside", { "c1", "x" } );
  c1["id"] = "pull-left";
  Json::Value c2 = constraint( "inside", { "c2", "x" } );
  c2["id"] = "pull-right";
  spec["constraints"].append( c1 );
  spec["constraints"].append( c2 );

  const Json::Value preSolve = rectOf( spec, "titles", "x" );
  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged == false );
  REQUIRE( result.constraintsSolved == 0 );
  REQUIRE( rectOf( spec, "titles", "x" ) == preSolve );
  // Both fighting constraints are reported as unsatisfied, not absorbed.
  REQUIRE( result.violated.size() == 2 );
  bool nonConvergenceReported = false;
  for ( const auto &note : result.unsatisfied )
    nonConvergenceReported =
      nonConvergenceReported || note.find( "did not converge" ) != std::string::npos;
  REQUIRE( nonConvergenceReported );
}

TEST_CASE( "M0: #865 fit_content materializes a zero-size initial rect",
           "[platform9][solver][regression]" )
{
  Json::Value spec = makeMapSpec( "m0-fit-zero", Json::Value() );
  Json::Value legend = item( "legend-1", 220, 30, 0, 0 );
  legend["min_size_mm"] = [] {
    Json::Value s( Json::arrayValue );
    s.append( 40.0 );
    s.append( 10.0 );
    return s;
  }();
  spec["legends"].append( legend );
  Json::Value fit = constraint( "fit_content", { "legend-1" } );
  fit["content_mm"] = [] {
    Json::Value s( Json::arrayValue );
    s.append( 50.0 );
    s.append( 12.0 );
    return s;
  }();
  spec["constraints"].append( fit );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged );
  const Json::Value rect = rectOf( spec, "legends", "legend-1" );
  REQUIRE( rect[0].asDouble() == Catch::Approx( 220.0 ) );
  REQUIRE( rect[1].asDouble() == Catch::Approx( 30.0 ) );
  REQUIRE( rect[2].asDouble() == Catch::Approx( 50.0 ).margin( 1e-9 ) );
  REQUIRE( rect[3].asDouble() == Catch::Approx( 12.0 ).margin( 1e-9 ) );
}

TEST_CASE( "M0: #866 has(path) works as operand and as whole condition",
           "[platform9][conditions][regression]" )
{
  Json::Value context( Json::objectValue );
  context["atlas"] = Json::Value( Json::objectValue );
  context["atlas"]["feature"] = Json::Value( Json::objectValue );
  context["atlas"]["feature"]["name"] = "B";
  context["flag"] = true;

  bool value = false;
  std::string error;

  // has(path) as a comparison operand (the #866 crash).
  REQUIRE( evaluateCondition( "has(atlas.feature.name) == true", context, &value, &error ) );
  REQUIRE( error.empty() );
  REQUIRE( value );
  REQUIRE( evaluateCondition( "has(atlas.feature) == false", context, &value, &error ) );
  REQUIRE( error.empty() );
  REQUIRE( !value );
  REQUIRE( evaluateCondition( "has(missing.path) == false", context, &value, &error ) );
  REQUIRE( error.empty() );
  REQUIRE( value );
  // Bare has(path) as the whole condition.
  REQUIRE( evaluateCondition( "has(flag)", context, &value, &error ) );
  REQUIRE( value );
  // Presence guard idiom keeps working.
  REQUIRE( evaluateCondition( "has(atlas.feature.name) or flag", context, &value, &error ) );
  REQUIRE( value );
  // A present null still counts as present (resolvePath finds the member).
  context["nothing"] = Json::Value();
  REQUIRE( evaluateCondition( "has(nothing) == true", context, &value, &error ) );
  REQUIRE( value );
}

TEST_CASE( "M0: #877 NaN never satisfies an ordering (IEEE honesty)",
           "[platform9][conditions][regression]" )
{
  Json::Value context( Json::objectValue );
  context["metrics"] = Json::Value( Json::objectValue );
  context["metrics"]["score"] = std::numeric_limits<double>::quiet_NaN();
  context["metrics"]["limit"] = 5.0;

  const char *ops[] = { "==", "!=", "<", "<=", ">", ">=" };
  // NaN on either side: only `!=` is true.
  for ( const char *op : ops )
  {
    const std::string exprL = std::string( "metrics.score " ) + op + " metrics.limit";
    const std::string exprR = std::string( "metrics.limit " ) + op + " metrics.score";
    const std::string exprN = std::string( "metrics.score " ) + op + " metrics.score";
    bool value = false;
    std::string error;
    REQUIRE( evaluateCondition( exprL, context, &value, &error ) );
    REQUIRE( error.empty() );
    const bool expected = std::string( op ) == "!=";
    CHECK( value == expected );
    REQUIRE( evaluateCondition( exprR, context, &value, &error ) );
    CHECK( value == expected );
    REQUIRE( evaluateCondition( exprN, context, &value, &error ) );
    CHECK( value == expected ); // NaN != NaN is true; every other op false
  }
}

TEST_CASE( "M0: ±Inf orders totally and compares to finite values",
           "[platform9][conditions]" )
{
  Json::Value context( Json::objectValue );
  context["range"] = Json::Value( Json::objectValue );
  context["range"]["max"] = std::numeric_limits<double>::infinity();
  context["range"]["min"] = -std::numeric_limits<double>::infinity();
  context["range"]["zero"] = 0.0;

  bool value = false;
  std::string error;
  REQUIRE( evaluateCondition( "range.zero < range.max", context, &value, &error ) );
  REQUIRE( value );
  REQUIRE( evaluateCondition( "range.min < range.zero", context, &value, &error ) );
  REQUIRE( value );
  REQUIRE( evaluateCondition( "range.max > range.min", context, &value, &error ) );
  REQUIRE( value );
  REQUIRE( evaluateCondition( "range.max == range.max", context, &value, &error ) );
  REQUIRE( value );
  // +/-inf is a NUMBER, not a missing value: has() is true and it orders.
  REQUIRE( evaluateCondition( "has(range.max) == true", context, &value, &error ) );
  REQUIRE( value );
}

TEST_CASE( "M0: mixed-kind and bool ordering semantics are pinned",
           "[platform9][conditions]" )
{
  Json::Value context( Json::objectValue );
  context["v"] = Json::Value( Json::objectValue );
  context["v"]["text"] = "abc";
  context["v"]["num"] = 2.0;
  context["v"]["yes"] = true;
  context["v"]["no"] = false;

  bool value = false;
  std::string error;
  // Mixed-kind equality is well-defined false / != true.
  REQUIRE( evaluateCondition( "v.text == 5", context, &value, &error ) );
  REQUIRE( !value );
  REQUIRE( evaluateCondition( "v.text != 5", context, &value, &error ) );
  REQUIRE( value );
  // Mixed-kind ordering is an ERROR, never a silent false.
  error.clear();
  CHECK( !evaluateCondition( "v.text < 5", context, &value, &error ) );
  CHECK( !error.empty() );
  // Bool ordering: false < true.
  REQUIRE( evaluateCondition( "v.no < v.yes", context, &value, &error ) );
  REQUIRE( value );
  REQUIRE( evaluateCondition( "v.yes == true", context, &value, &error ) );
  REQUIRE( value );
  // String ordering is byte-wise.
  REQUIRE( evaluateCondition( "v.text < \"abd\"", context, &value, &error ) );
  REQUIRE( value );
}

TEST_CASE( "M0: condition evaluation and composition solving are deterministic",
           "[platform9][determinism]" )
{
  Json::Value context( Json::objectValue );
  context["flag"] = true;
  context["n"] = 3.0;
  bool a = false;
  bool b = false;
  std::string error;
  REQUIRE( evaluateCondition( "flag and n >= 3", context, &a, &error ) );
  REQUIRE( evaluateCondition( "flag and n >= 3", context, &b, &error ) );
  REQUIRE( a == b );

  Json::Value spec = makeMapSpec( "m0-determinism", Json::Value() );
  spec["titles"].append( item( "t", 12, 6, 120, 14 ) );
  spec["labels"].append( item( "l", 12, 24, 60, 8 ) );
  Json::Value c = constraint( "stack", { "t", "l" } );
  c["direction"] = "below";
  spec["constraints"].append( c );

  Json::Value first = spec;
  Json::Value second = spec;
  const CompositionResult ra = resolveComposition( first, 12.0 );
  const CompositionResult rb = resolveComposition( second, 12.0 );
  REQUIRE( structuralDigest( first ) == structuralDigest( second ) );
  REQUIRE( ra.converged == rb.converged );
  REQUIRE( ra.constraintsSolved == rb.constraintsSolved );
  REQUIRE( ra.decisions.size() == rb.decisions.size() );
}

TEST_CASE( "M0 P2-1: permanently-failed constraints are recorded in the ledger "
           "exactly once across the unsat-core restore sweep",
           "[platform9][solver][ledger]" )
{
  // Scenario: an inside/inside contradiction makes searchCores run (two
  // unsatisfied targets), while keep_with[T,F] is permanently refused for
  // page overflow. The core-search restore sweep re-runs the final real
  // pass from the pre-solve snapshot; the refusal must not be duplicated
  // per restore (pre-fix: 1 + 2 entries for the two targets).
  Json::Value spec = makeMapSpec( "m0-ledger-dedupe", Json::Value() );
  spec["map_frames"].append( item( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( item( "c2", 150, 10, 80, 60 ) );
  spec["titles"].append( item( "x", 70, 35, 20, 20 ) );
  spec["titles"].append( item( "t", 10, 160, 60, 20 ) );
  spec["labels"].append( item( "f", 10, 185, 60, 20 ) );
  Json::Value c1 = constraint( "inside", { "c1", "x" } );
  c1["id"] = "pull-left";
  Json::Value c2 = constraint( "inside", { "c2", "x" } );
  c2["id"] = "pull-right";
  Json::Value c3 = constraint( "keep_with", { "t", "f" } );
  c3["id"] = "pin-f";
  c3["gap_mm"] = 5.0; // 160 + 20 + 5 = 185 … stays on page: NOT refused
  spec["constraints"].append( c1 );
  spec["constraints"].append( c2 );
  spec["constraints"].append( c3 );

  // Force the refusal: move f near the page bottom so the pin would end
  // past 210 mm.
  spec["titles"][1]["rect_mm"][1] = 195.0; // t at y=195 …
  spec["titles"][1]["rect_mm"][3] = 10.0;
  spec["labels"][0]["rect_mm"][1] = 205.0;
  spec["labels"][0]["rect_mm"][3] = 4.0;
  // pin target y = 195 + 10 + 5 = 210 → 210 + 4 = 214 > 210 → page_overflow.

  const CompositionResult result = resolveComposition( spec, 12.0 );
  // The pin IS recorded exactly once.
  REQUIRE( countDecisions( result, "pin-f", "failed" ) == 1 );
  // And it is reported (human-readable) exactly once.
  int failedNotes = 0;
  for ( const auto &note : result.unsatisfied )
    failedNotes += note.find( "pin-f" ) != std::string::npos &&
                          note.find( "page_overflow" ) != std::string::npos
                     ? 1
                     : 0;
  REQUIRE( failedNotes == 1 );
}

TEST_CASE( "M0: page-aware keep_with refusal is preserved verbatim in violations",
           "[platform9][solver][regression][platform8-pin]" )
{
  // Pins the 8.0 page-aware contract while M1 changes the sweep internals:
  // the refusal reason survives into violated[].reason untouched.
  Json::Value spec = makeMapSpec( "m0-page-pin", Json::Value() );
  spec["titles"].append( item( "t", 10, 195, 60, 10 ) );
  spec["labels"].append( item( "f", 10, 205, 60, 4 ) );
  Json::Value c = constraint( "keep_with", { "t", "f" } );
  c["id"] = "pin-f";
  c["gap_mm"] = 5.0;
  spec["constraints"].append( c );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged ); // a refused pin is a settled outcome, not a hang
  bool refusalFound = false;
  for ( const auto &violation : result.violated )
    refusalFound = refusalFound || violation.cid == "pin-f" &&
                                        violation.reason.find( "page_overflow" ) !=
                                          std::string::npos;
  REQUIRE( refusalFound );
  // The follower rect was never written off-page.
  REQUIRE( rectOf( spec, "labels", "f" )[1].asDouble() == Catch::Approx( 205.0 ) );
}

// ---------------------------------------------------------------------------
// M1 — Constraint Solver 9.0: oscillation attribution, convergence trace,
// text-driven sizing, scoped re-solve.
// ---------------------------------------------------------------------------

TEST_CASE( "M1: oscillation attribution names the constraints still writing "
           "at budget exhaustion",
           "[platform9][solver][oscillation]" )
{
  // The inside/inside contradiction from the M0 rollback case: both pulls
  // fight forever. 9.0 names them as oscillations in the result JSON and in
  // the human-readable report.
  Json::Value spec = makeMapSpec( "m1-oscillation", Json::Value() );
  spec["map_frames"].append( item( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( item( "c2", 150, 10, 80, 60 ) );
  spec["titles"].append( item( "x", 70, 35, 20, 20 ) );
  Json::Value a = constraint( "inside", { "c1", "x" } );
  a["id"] = "pull-left";
  Json::Value b = constraint( "inside", { "c2", "x" } );
  b["id"] = "pull-right";
  spec["constraints"].append( a );
  spec["constraints"].append( b );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged == false );
  REQUIRE( result.oscillations.size() == 2 );
  std::set<std::string> fighters;
  for ( const auto &oscillation : result.oscillations )
  {
    fighters.insert( oscillation.cid );
    REQUIRE( oscillation.kind == "inside" );
  }
  REQUIRE( fighters.count( "pull-left" ) == 1 );
  REQUIRE( fighters.count( "pull-right" ) == 1 );
  bool oscillationReported = false;
  for ( const auto &note : result.unsatisfied )
    oscillationReported =
      oscillationReported || note.find( "oscillation detected" ) != std::string::npos;
  REQUIRE( oscillationReported );

  const Json::Value json = result.toJson();
  REQUIRE( json["oscillations"].isArray() );
  REQUIRE( json["oscillations"].size() == 2 );
}

TEST_CASE( "M1: converged layouts carry an empty oscillation list and a "
           "bounded monotone trace",
           "[platform9][solver][trace]" )
{
  Json::Value spec = makeMapSpec( "m1-trace", Json::Value() );
  spec["titles"].append( item( "t", 12, 6, 120, 14 ) );
  spec["labels"].append( item( "l1", 12, 30, 60, 8 ) );
  spec["labels"].append( item( "l2", 12, 60, 60, 8 ) );
  Json::Value s = constraint( "stack", { "t", "l1", "l2" } );
  s["direction"] = "below";
  s["gap_mm"] = 4.0;
  spec["constraints"].append( s );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged );
  REQUIRE( result.oscillations.empty() );
  // A stack settles in one sweep: exactly one recorded pass with moves.
  REQUIRE( result.trace.size() == 1 );
  REQUIRE( result.trace[0].pass == 1 );
  REQUIRE( result.trace[0].moves == 1 ); // one stack constraint wrote (two items)
  REQUIRE( result.trace[0].applied.size() == 1 );
  // The stack declared no id: the solver synthesizes "stack#<declared index>".
  REQUIRE( result.trace[0].applied[0].rfind( "stack#", 0 ) == 0 );
  REQUIRE( result.trace[0].truncated == false );
  const Json::Value json = result.toJson();
  REQUIRE( json["trace"].isArray() );
  REQUIRE( json["trace"][0]["moves"].asInt() == 1 );
}

TEST_CASE( "M1: fit_content.text_ref sizes furniture from the deterministic "
           "typography model",
           "[platform9][solver][text-sizing]" )
{
  // Known-answer: a CJK+Latin title at 18 pt measures
  //   6 CJK glyphs × 1.0 em + 6 latin/digits/space × (0.55|0.35) em.
  // Text: "港口A监测2026" → 港口监测 = 4 fullwidth, A = 0.55, 2026 = 4×0.55
  // width_em = 4 + 5×0.55 = 6.75 em → × 18 pt × 0.3528 mm/pt.
  Json::Value spec = makeMapSpec( "m1-text-ref", Json::Value() );
  Json::Value label = item( "label-1", 12, 6, 200, 10 );
  label["text"] = "港口A监测2026";
  label["font"] = Json::Value( Json::objectValue );
  label["font"]["size_pt"] = 18.0;
  spec["labels"].append( label );
  Json::Value title = item( "title-1", 12, 24, 60, 8 );
  title["min_size_mm"] = [] {
    Json::Value s( Json::arrayValue );
    s.append( 1.0 );
    s.append( 1.0 );
    return s;
  }();
  spec["titles"].append( title );
  Json::Value fit = constraint( "fit_content", { "title-1" } );
  fit["id"] = "fit-title";
  fit["text_ref"] = "label-1";
  spec["constraints"].append( fit );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged );
  const Json::Value rect = rectOf( spec, "titles", "title-1" );
  const double expectedW = ( 4.0 + 5.0 * 0.55 ) * 18.0 * 0.3528;
  const double expectedH = 1 * 18.0 * 0.3528 * 1.25; // one line, default leading
  REQUIRE( rect[2].asDouble() == Catch::Approx( expectedW ).margin( 1e-6 ) );
  REQUIRE( rect[3].asDouble() == Catch::Approx( expectedH ).margin( 1e-6 ) );
  // Position preserved.
  REQUIRE( rect[0].asDouble() == Catch::Approx( 12.0 ) );
  REQUIRE( rect[1].asDouble() == Catch::Approx( 24.0 ) );
}

TEST_CASE( "M1: fit_content.text_ref with a missing source is a permanent "
           "failure, never a zero-size box",
           "[platform9][solver][text-sizing][negative]" )
{
  Json::Value spec = makeMapSpec( "m1-text-ref-missing", Json::Value() );
  spec["titles"].append( item( "title-1", 12, 24, 60, 8 ) );
  Json::Value fit = constraint( "fit_content", { "title-1" } );
  fit["id"] = "fit-title";
  fit["text_ref"] = "ghost-item";
  spec["constraints"].append( fit );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  // The buildRuntimes arity check cannot see text_ref; the failure happens
  // at solve time as a permanent failure reported in the ledger.
  bool reported = false;
  for ( const auto &note : result.unsatisfied )
    reported = reported || note.find( "text_ref" ) != std::string::npos;
  REQUIRE( reported );
  REQUIRE( rectOf( spec, "titles", "title-1" )[2].asDouble() == Catch::Approx( 60.0 ) );
}

TEST_CASE( "M1: validateMapSpec enforces the content_mm xor text_ref contract",
           "[platform9][validation][text-sizing]" )
{
  Json::Value spec = makeMapSpec( "m1-fit-validation", Json::Value() );
  for ( const char *id : { "title-1", "title-2", "title-3" } )
  {
    Json::Value title = item( id, 12, 24, 60, 8 );
    title["text"] = "t";
    spec["titles"].append( title );
  }

  // (a) neither → problem.
  Json::Value doc = spec;
  Json::Value ghostA = constraint( "fit_content", { "title-1" } );
  ghostA["id"] = "fit-a";
  doc["constraints"].append( ghostA );
  bool flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "fit_content needs" ) != std::string::npos;
  REQUIRE( flagged );

  // (b) both → problem (explicit beats derived; the document must be honest).
  doc = spec;
  Json::Value both = constraint( "fit_content", { "title-1" } );
  both["id"] = "fit-b";
  Json::Value content( Json::arrayValue );
  content.append( 40.0 );
  content.append( 10.0 );
  both["content_mm"] = content;
  both["text_ref"] = "title-2";
  doc["constraints"].append( both );
  flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "both content_mm and" ) != std::string::npos;
  REQUIRE( flagged );

  // (c) text_ref that does not resolve → problem.
  doc = spec;
  Json::Value ghost = constraint( "fit_content", { "title-1" } );
  ghost["id"] = "fit-c";
  ghost["text_ref"] = "ghost";
  doc["constraints"].append( ghost );
  flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "text_ref 'ghost' does not resolve" ) !=
                              std::string::npos;
  REQUIRE( flagged );

  // (d) text_ref alone resolves → valid.
  doc = spec;
  Json::Value ok = constraint( "fit_content", { "title-1" } );
  ok["id"] = "fit-d";
  ok["text_ref"] = "title-2";
  doc["constraints"].append( ok );
  REQUIRE( validateMapSpec( doc ).empty() );
}

TEST_CASE( "M1: scoped re-solve equals the full solve on closed focus sets "
           "and leaves foreign geometry untouched",
           "[platform9][solver][scoped]" )
{
  // Two independent stacks (no shared items): solving only stack A's items
  // must produce A's full-solve geometry and leave B exactly as declared.
  const auto buildSpec = [] {
    Json::Value spec = makeMapSpec( "m1-scoped", Json::Value() );
    spec["titles"].append( item( "a-head", 12, 6, 80, 14 ) );
    spec["labels"].append( item( "a1", 12, 40, 60, 8 ) );
    spec["titles"].append( item( "b-head", 150, 6, 80, 14 ) );
    spec["labels"].append( item( "b1", 150, 40, 60, 8 ) );
    Json::Value sa = constraint( "stack", { "a-head", "a1" } );
    sa["id"] = "stack-a";
    sa["direction"] = "below";
    sa["gap_mm"] = 6.0;
    Json::Value sb = constraint( "stack", { "b-head", "b1" } );
    sb["id"] = "stack-b";
    sb["direction"] = "below";
    sb["gap_mm"] = 6.0;
    spec["constraints"].append( sa );
    spec["constraints"].append( sb );
    return spec;
  };

  Json::Value full = buildSpec();
  const CompositionResult fullResult = resolveComposition( full, 12.0 );
  REQUIRE( fullResult.converged );
  // stack: a1.y = 6 + 14 + 6 = 26; b1.y = 26.
  REQUIRE( rectOf( full, "labels", "a1" )[1].asDouble() == Catch::Approx( 26.0 ) );
  REQUIRE( rectOf( full, "labels", "b1" )[1].asDouble() == Catch::Approx( 26.0 ) );

  Json::Value scoped = buildSpec();
  // Move b-head out of the way first: the scoped solve of stack A must not
  // care, and b1 must stay where it was declared.
  scoped["titles"][1]["rect_mm"][1] = 100.0;
  const CompositionResult scopedResult =
    resolveCompositionScoped( scoped, 12.0, { "a-head", "a1" } );
  REQUIRE( scopedResult.converged );
  REQUIRE( rectOf( scoped, "labels", "a1" )[1].asDouble() == Catch::Approx( 26.0 ) );
  // Foreign item untouched at its pre-solve declaration.
  REQUIRE( rectOf( scoped, "labels", "b1" )[1].asDouble() == Catch::Approx( 40.0 ) );
  REQUIRE( rectOf( scoped, "titles", "b-head" )[1].asDouble() == Catch::Approx( 100.0 ) );
}

// ---------------------------------------------------------------------------
// M2 — Multi-page / atlas: page_break, master furniture, atlas expressions,
// continuation references.
// ---------------------------------------------------------------------------

TEST_CASE( "M2: page_break validation requires exactly one resolvable item on "
           "a declared target page",
           "[platform9][pagebreak][validation]" )
{
  auto buildSpec = [] {
    Json::Value spec = makeMapSpec( "m2-pb-validation", Json::Value() );
    spec["map_frames"].append( item( "map-1", 10, 10, 100, 80 ) );
    Json::Value title = item( "t", 12, 100, 60, 10 );
    title["text"] = "t";
    spec["titles"].append( title );
    Json::Value extraPage( Json::objectValue );
    extraPage["width_mm"] = 297.0;
    extraPage["height_mm"] = 210.0;
    spec["pages"].append( extraPage );
    return spec;
  };

  // Valid: t moves from page 0 to declared page 1.
  Json::Value doc = buildSpec();
  Json::Value pb = constraint( "page_break", { "t" } );
  pb["id"] = "pb-t";
  doc["constraints"].append( pb );
  REQUIRE( validateMapSpec( doc ).empty() );

  // Missing target page: no pages[] declared.
  doc = buildSpec();
  doc.removeMember( "pages" );
  Json::Value pb2 = constraint( "page_break", { "t" } );
  pb2["id"] = "pb-t";
  doc["constraints"].append( pb2 );
  bool flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "target page 1" ) != std::string::npos;
  REQUIRE( flagged );

  // Item two pages ahead of the declaration.
  doc = buildSpec();
  doc["titles"][0]["page"] = 1;
  Json::Value pb3 = constraint( "page_break", { "t" } );
  pb3["id"] = "pb-t";
  doc["constraints"].append( pb3 );
  flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "target page 2" ) != std::string::npos;
  REQUIRE( flagged );

  // Arity and resolution.
  doc = buildSpec();
  Json::Value pb4 = constraint( "page_break", { "t", "map-1" } );
  pb4["id"] = "pb-t";
  doc["constraints"].append( pb4 );
  flagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    flagged = flagged || problem.find( "exactly 1 id" ) != std::string::npos;
  REQUIRE( flagged );
}

TEST_CASE( "M2: the solver applies page_break once and keeps it in the "
           "explainability surface",
           "[platform9][pagebreak][solver]" )
{
  Json::Value spec = makeMapSpec( "m2-pb-solve", Json::Value() );
  spec["titles"].append( item( "t", 12, 6, 120, 14 ) );
  Json::Value extraPage( Json::objectValue );
  extraPage["width_mm"] = 297.0;
  extraPage["height_mm"] = 210.0;
  spec["pages"].append( extraPage );
  Json::Value pb = constraint( "page_break", { "t" } );
  pb["id"] = "pb-t";
  spec["constraints"].append( pb );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged );
  REQUIRE( spec["titles"][0]["page"].asInt() == 1 );
  bool applied = false;
  for ( const auto &decision : result.decisions )
    applied = applied || ( decision.cid == "pb-t" && decision.outcome == "applied" );
  REQUIRE( applied );

  // Re-solving the resolved document is a satisfied no-op (idempotent).
  const CompositionResult second = resolveComposition( spec, 12.0 );
  REQUIRE( second.converged );
  REQUIRE( second.constraintsSolved == second.constraintsTotal );
}

TEST_CASE( "M2: master furniture compiles a provenance clone onto every "
           "declared page",
           "[platform9][furniture][compile]" )
{
  Json::Value spec = makeMapSpec( "m2-furniture", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  spec["map_frames"][0]["extent"] = extent;
  spec["titles"].append( item( "title-1", 12, 6, 120, 14 ) );
  spec["titles"][0]["text"] = "master title";

  Json::Value footer = item( "source-1", 200, 196, 80, 8 );
  footer["text"] = "source: test";
  spec["source_notes"].append( footer );

  Json::Value page2( Json::objectValue );
  page2["width_mm"] = 297.0;
  page2["height_mm"] = 210.0;
  page2["role"] = "map";
  Json::Value furniture( Json::arrayValue );
  furniture.append( "title-1" );
  furniture.append( "source-1" );
  page2["furniture"] = furniture;
  spec["pages"].append( page2 );

  REQUIRE( validateMapSpec( spec ).empty() );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto &service = sicnu::agent::layout_tools::LayoutService::instance();
  // Clones exist with the deterministic ids and live on page 1.
  QgsLayoutItem *titleClone = service.findItem( layout, "title-1-p1" );
  QgsLayoutItem *sourceClone = service.findItem( layout, "source-1-p1" );
  REQUIRE( titleClone != nullptr );
  REQUIRE( sourceClone != nullptr );
  CHECK( titleClone->page() == 1 );
  CHECK( sourceClone->page() == 1 );
  // Same relative position as the masters.
  CHECK( titleClone->pagePositionWithUnits().x() ==
         Catch::Approx( 12.0 ).margin( 0.01 ) );
  CHECK( titleClone->pagePositionWithUnits().y() == Catch::Approx( 6.0 ).margin( 0.01 ) );
  // Masters stay on page 0.
  CHECK( service.findItem( layout, "title-1" )->page() == 0 );
}

TEST_CASE( "M2: master furniture validation catches dangling and duplicate "
           "references",
           "[platform9][furniture][validation]" )
{
  Json::Value spec = makeMapSpec( "m2-furniture-validation", Json::Value() );
  spec["titles"].append( item( "title-1", 12, 6, 120, 14 ) );
  Json::Value page2( Json::objectValue );
  page2["width_mm"] = 297.0;
  page2["height_mm"] = 210.0;
  Json::Value furniture( Json::arrayValue );
  furniture.append( "ghost" );
  page2["furniture"] = furniture;
  spec["pages"].append( page2 );
  bool flagged = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    flagged = flagged || problem.find( "'ghost' does not resolve" ) != std::string::npos;
  REQUIRE( flagged );

  // Duplicates within one page are rejected (the clone would collide).
  spec["pages"][0]["furniture"][0] = "title-1";
  spec["pages"][0]["furniture"].append( "title-1" );
  flagged = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    flagged = flagged || problem.find( "repeats item 'title-1'" ) != std::string::npos;
  REQUIRE( flagged );
}

TEST_CASE( "M2: atlas-driven expressions compile to QGIS-native label markup "
           "and reject unparseable expressions",
           "[platform9][atlas][compile]" )
{
  Json::Value spec = makeMapSpec( "m2-expression", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  spec["titles"].append( item( "title-1", 12, 6, 120, 14 ) );
  spec["titles"][0]["text"] = "fallback";
  spec["titles"][0]["expression"] = "'page ' || 1";
  spec["labels"].append( item( "label-1", 12, 190, 80, 8 ) );
  spec["labels"][0]["text"] = "fallback";
  spec["labels"][0]["expression"] = "1 +";

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout == nullptr ); // label-1's expression cannot parse
  CHECK( error.toStdString().find( "invalid expression" ) != std::string::npos );

  // Fixing the expression compiles, and the raw label text carries the
  // native `[% … %]` markup (evaluation happens at render time).
  spec["labels"][0]["expression"] = "'page ' || 1";
  layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto *label = qobject_cast<QgsLayoutItemLabel *>(
    sicnu::agent::layout_tools::LayoutService::instance().findItem( layout, "label-1" ) );
  REQUIRE( label != nullptr );
  CHECK( label->text().toStdString() == "[% 'page ' || 1 %]" );
}

TEST_CASE( "M2: continuation labels resolve display page numbers",
           "[platform9][continuation][compile]" )
{
  Json::Value spec = makeMapSpec( "m2-continuation", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value table = item( "table-1", 12, 24, 120, 90 );
  table["page"] = 1;
  table["text"] = "results continued";
  table["continuation"] = Json::Value( Json::objectValue );
  table["continuation"]["label"] = "fortgesetzt auf";
  spec["labels"].append( table );

  Json::Value page2( Json::objectValue );
  page2["width_mm"] = 297.0;
  page2["height_mm"] = 210.0;
  spec["pages"].append( page2 );

  REQUIRE( validateMapSpec( spec ).empty() );
  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  auto *caption = qobject_cast<QgsLayoutItemLabel *>(
    sicnu::agent::layout_tools::LayoutService::instance().findItem(
      layout, "table-1-continuation" ) );
  REQUIRE( caption != nullptr );
  CHECK( caption->page() == 1 );
  CHECK( caption->text().toStdString().find( "fortgesetzt auf page 2" ) !=
         std::string::npos );
}

// ---------------------------------------------------------------------------
// M4 — template composition: multi-parent extends pin, semantic diff.
// ---------------------------------------------------------------------------

TEST_CASE( "M4: multi-parent extends folds left-to-right on the disk catalog",
           "[platform9][templates][inheritance]" )
{
  auto &registry = TemplateRegistry::instance();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString dir = tempDir.path();
  const auto writeTemplate = [ & ]( const char *file, const Json::Value &descriptor ) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::ofstream out( ( dir + "/" + file ).toStdString(), std::ios::binary );
    out << Json::writeString( builder, descriptor );
  };
  const auto slotsBlock = [] {
    Json::Value slot( Json::objectValue );
    slot["role"] = "title.main";
    slot["accepts"] = "title";
    Json::Value slotList( Json::arrayValue ); // not "slots": Qt moc macro
    slotList.append( slot );
    return slotList;
  };
  const auto pageBlock = []( double w, double h ) {
    Json::Value p( Json::objectValue );
    p["width_mm"] = w;
    p["height_mm"] = h;
    return p;
  };

  Json::Value p1( Json::objectValue );
  p1["id"] = "m4-p1";
  p1["description"] = "base description";
  p1["slots"] = slotsBlock();
  p1["page"] = pageBlock( 297.0, 210.0 );
  writeTemplate( "m4-p1.json", p1 );

  Json::Value p2( Json::objectValue );
  p2["id"] = "m4-p2";
  p2["description"] = "parent two description";
  p2["slots"] = slotsBlock();
  p2["page"] = pageBlock( 297.0, 210.0 );
  p2["medium"] = "a4";
  writeTemplate( "m4-p2.json", p2 );

  Json::Value child( Json::objectValue );
  child["id"] = "m4-child";
  Json::Value parents( Json::arrayValue );
  parents.append( "m4-p1" );
  parents.append( "m4-p2" );
  child["extends"] = parents;
  child["description"] = "child description";
  child["slots"] = slotsBlock();
  child["page"] = pageBlock( 420.0, 297.0 );
  writeTemplate( "m4-child.json", child );

  registry.setDirectory( dir );
  registry.reload();
  CHECK( registry.loadProblems().isEmpty() );

  const Json::Value resolved = registry.find( "m4-child" );
  REQUIRE( !resolved.isNull() );
  // Linearized parents recorded in provenance order.
  REQUIRE( resolved["inheritance"]["parents"].isArray() );
  REQUIRE( resolved["inheritance"]["parents"].size() == 2 );
  CHECK( resolved["inheritance"]["parents"][0].asString() == "m4-p1" );
  CHECK( resolved["inheritance"]["parents"][1].asString() == "m4-p2" );
  // The child wins; the later parent wins where the child stayed silent
  // (medium arrives from m4-p2).
  CHECK( resolved["description"].asString() == "child description" );
  CHECK( resolved["page"]["width_mm"].asDouble() == 420.0 );
  CHECK( resolved["medium"].asString() == "a4" );

  // Restore the shipped catalog.
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  CHECK( registry.loadProblems().isEmpty() );
}

TEST_CASE( "M4: diffTemplates reports slot and member deltas with truncation",
           "[platform9][templates][diff]" )
{
  Json::Value before;
  before["id"] = "t";
  before["description"] = "old";
  Json::Value slotA( Json::objectValue );
  slotA["role"] = "title.main";
  slotA["accepts"] = "title";
  Json::Value slotB( Json::objectValue );
  slotB["role"] = "legend.primary";
  slotB["accepts"] = "legend";
  before["slots"] = Json::Value( Json::arrayValue );
  before["slots"].append( slotA );
  before["slots"].append( slotB );

  Json::Value after;
  after["id"] = "t";
  after["description"] = "new";
  after["medium"] = "a4";
  Json::Value slotA2( Json::objectValue );
  slotA2["role"] = "title.main";
  slotA2["accepts"] = "title";
  slotA2["overrides"] = Json::Value( Json::objectValue );
  slotA2["overrides"]["font_pt"] = 24.0;
  Json::Value slotC( Json::objectValue );
  slotC["role"] = "footer.primary";
  slotC["accepts"] = "footer";
  after["slots"] = Json::Value( Json::arrayValue );
  after["slots"].append( slotA2 );
  after["slots"].append( slotC );

  const Json::Value diff = diffTemplates( before, after );
  CHECK( diff["added_slots"].size() == 1 );
  CHECK( diff["added_slots"][0].asString() == "footer.primary" );
  CHECK( diff["removed_slots"].size() == 1 );
  CHECK( diff["removed_slots"][0].asString() == "legend.primary" );
  CHECK( diff["changed_slots"].size() == 1 );
  CHECK( diff["changed_slots"][0]["role"].asString() == "title.main" );
  bool descriptionChanged = false;
  for ( const auto &delta : diff["changed_keys"] )
    descriptionChanged = descriptionChanged || delta["path"].asString() == "description";
  REQUIRE( descriptionChanged );
  bool mediumAdded = false;
  for ( const auto &delta : diff["added_keys"] )
    mediumAdded = mediumAdded || delta["path"].asString() == "medium";
  REQUIRE( mediumAdded );
  CHECK( diff["truncated"].asBool() == false );
}

TEST_CASE( "M4: instantiateTemplate stamps structured provenance",
           "[platform9][templates][provenance]" )
{
  auto &registry = TemplateRegistry::instance();
  const Json::Value draft = registry.instantiateTemplate(
    QStringLiteral( "classification-a4l" ), Json::Value( Json::objectValue ) );
  REQUIRE( !draft.isNull() );
  CHECK( draft["template"].isString() );
  REQUIRE( draft.isMember( "template_provenance" ) );
  CHECK( draft["template_provenance"]["id"].asString() == "classification-a4l" );
}

// ---------------------------------------------------------------------------
// M5 — thematic cartography: raster scale ranges + bivariate contract gate.
// ---------------------------------------------------------------------------

TEST_CASE( "M5: raster scale_ranges validate and reject nonsense",
           "[platform9][thematic][scale]" )
{
  auto style = [] {
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = "1.0";
    doc["kind"] = "style_spec";
    doc["id"] = "m5-scale";
    doc["version"] = 1;
    doc["applies_to"] = "raster";
    doc["raster"] = Json::Value( Json::objectValue );
    doc["raster"]["renderertype"] = "singleband_gray";
    return doc;
  };

  Json::Value doc = style();
  doc["raster"]["scale_ranges"] = [] {
    Json::Value s( Json::objectValue );
    s["min"] = 500000.0;
    s["max"] = 1000.0;
    return s;
  }();
  CHECK( validateStyleSpec( doc ).empty() );

  doc = style();
  doc["raster"]["scale_ranges"] = [] {
    Json::Value s( Json::objectValue );
    s["min"] = 1000.0;
    s["max"] = 500000.0;
    return s;
  }(); // min < max is a denominator inversion
  bool flagged = false;
  for ( const auto &problem : validateStyleSpec( doc ) )
    flagged = flagged || problem.find( "min must exceed max" ) != std::string::npos;
  REQUIRE( flagged );

  doc = style();
  doc["raster"]["scale_ranges"] = "300000";
  flagged = false;
  for ( const auto &problem : validateStyleSpec( doc ) )
    flagged = flagged || problem.find( "must be an object" ) != std::string::npos;
  REQUIRE( flagged );
}

TEST_CASE( "M5: bivariate requires the explicit semantic contract",
           "[platform9][thematic][bivariate]" )
{
  auto style = [] {
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = "1.0";
    doc["kind"] = "style_spec";
    doc["id"] = "m5-bivariate";
    doc["version"] = 1;
    doc["applies_to"] = "vector";
    doc["vector"] = Json::Value( Json::objectValue );
    doc["vector"]["renderertype"] = "categorized";
    return doc;
  };

  // Without contract: invalid (never silently ignored).
  Json::Value doc = style();
  doc["bivariate"] = [] {
    Json::Value b( Json::objectValue );
    b["x"] = [] {
      Json::Value a( Json::objectValue );
      a["field"] = "ndvi";
      return a;
    }();
    b["y"] = [] {
      Json::Value a( Json::objectValue );
      a["field"] = "soil_moisture";
      return a;
    }();
    return b;
  }();
  bool flagged = false;
  for ( const auto &problem : validateStyleSpec( doc ) )
    flagged = flagged || problem.find( "bivariate.contract" ) != std::string::npos;
  REQUIRE( flagged );

  // Same field on both axes: not bivariate.
  doc["bivariate"]["contract"] = "NDVI vs soil moisture jointly classify drought stress";
  doc["bivariate"]["y"]["field"] = "ndvi";
  flagged = false;
  for ( const auto &problem : validateStyleSpec( doc ) )
    flagged = flagged || problem.find( "different fields" ) != std::string::npos;
  REQUIRE( flagged );

  // Complete contract: valid.
  doc["bivariate"]["y"]["field"] = "soil_moisture";
  CHECK( validateStyleSpec( doc ).empty() );
}

// ---------------------------------------------------------------------------
// M6 — charts/tables: furniture-over-map gate, dual-axis honesty.
// ---------------------------------------------------------------------------

TEST_CASE( "M6: charts over map frames are flagged and repaired out",
           "[platform9][charts][over-map]" )
{
  Json::Value spec = makeMapSpec( "m6-chart-over-map", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-1";
  chart["rect_mm"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( 40.0 );
    r.append( 60.0 );
    r.append( 80.0 );
    r.append( 60.0 );
    return r;
  }(); // squarely over the frame
  Json::Value chartSpec( Json::objectValue );
  chartSpec["kind"] = "bar";
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  chartSpec["binding"] = binding;
  chart["chart"] = chartSpec;
  spec["charts"].append( chart );

  const Json::Value report = preflightMapSpec( spec );
  bool flagged = false;
  for ( const auto &issueEntry : report["issues"] )
    flagged = flagged || issueEntry["code"].asString() == "MAP_CHART_OVER_MAP";
  REQUIRE( flagged );

  // Repair moves the chart to a free slot outside every frame.
  Json::Value ledger;
  const int applied = repairMapSpecWithLedger( spec, report, &ledger );
  REQUIRE( applied >= 1 );
  const Json::Value rect = rectOf( spec, "charts", "chart-1" );
  const bool clearOfFrame = rect[0].asDouble() >= 202.0 || // right of the frame
                            rect[1].asDouble() >= 164.0 || // below the frame
                            rect[0].asDouble() + rect[2].asDouble() <= 12.0 ||
                            rect[1].asDouble() + rect[3].asDouble() <= 24.0;
  REQUIRE( clearOfFrame );
  bool ledgerApplied = false;
  for ( const auto &entry : ledger )
    ledgerApplied = ledgerApplied || ( entry["code"].asString() == "MAP_CHART_OVER_MAP" &&
                                       entry["outcome"].asString() == "applied" );
  REQUIRE( ledgerApplied );
}

TEST_CASE( "M6: dual_axis declarations are honestly reported as unsupported",
           "[platform9][charts][dual-axis]" )
{
  Json::Value spec = makeMapSpec( "m6-dual-axis", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-2";
  chart["rect_mm"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( 220.0 );
    r.append( 30.0 );
    r.append( 60.0 );
    r.append( 40.0 );
    return r;
  }();
  Json::Value chartSpec( Json::objectValue );
  chartSpec["kind"] = "line";
  chartSpec["dual_axis"] = true;
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  chartSpec["binding"] = binding;
  chart["chart"] = chartSpec;
  spec["charts"].append( chart );
  REQUIRE( validateMapSpec( spec ).empty() ); // shape is legal

  const Json::Value report = preflightMapSpec( spec );
  bool flagged = false;
  for ( const auto &issueEntry : report["issues"] )
    flagged = flagged || issueEntry["code"].asString() == "MAP_DUAL_AXIS_UNSUPPORTED";
  REQUIRE( flagged );

  // Non-boolean dual_axis is structurally rejected.
  spec["charts"][0]["chart"]["dual_axis"] = "yes";
  bool invalid = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    invalid = invalid || problem.find( "dual_axis must be a boolean" ) != std::string::npos;
  REQUIRE( invalid );
}

TEST_CASE( "M6: numeric rendering stays locale-independent (C-locale dot)",
           "[platform9][charts][formatting]" )
{
  // Scope note (accepted review P3): this pins the C-locale contract of the
  // exact formatting call the chart/table renderers use
  // (QString::number(v,'g',n) at chart_registry.cpp) — asserting the drawn
  // pixels would require OCR. If a renderer switches to locale-aware
  // APIs, this test still passes; the render-determinism hashes are the
  // backstop.
{
  // QString::number(v, 'g', n) is C-locale by contract: the chart
  // renderers rely on it, so pin the exact known-answer the table cells
  // will draw regardless of the host locale.
  CHECK( QString::number( 0.5, 'g', 3 ).toStdString() == "0.5" );
  CHECK( QString::number( 1234567.0, 'g', 3 ).toStdString() == "1.23e+06" );
  CHECK( QString::number( -2.5, 'g', 4 ).toStdString() == "-2.5" );
}

// ---------------------------------------------------------------------------
// M7 — typography: kinsoku line-end opening guard (push-out) pinned.
// ---------------------------------------------------------------------------

TEST_CASE( "M7: wrap never ends a line on an opening punctuation mark",
           "[platform9][typography][kinsoku]" )
{
  // "监测（NDVI）分析" — if a break lands after （ the line ends on an
  // opening mark. The gap after （ must be forbidden, forcing the （ to
  // travel with NDVI or pull the break earlier.
  const std::string text = "监测（NDVI）分析";
  const std::vector<std::string> lines = wrapTextMm( text, 16.0, 9.0 );
  REQUIRE( lines.size() >= 2 );
  for ( const std::string &line : lines )
  {
    REQUIRE( !line.empty() );
    size_t pos = 0;
    char32_t lastCp = 0;
    while ( pos < line.size() )
      lastCp = decodeUtf8( line, pos );
    CHECK( !isOpeningPunctuation( lastCp ) );
  }
  // Round-trip: the wrap never loses glyphs.
  std::string rejoined;
  for ( const std::string &line : lines )
    rejoined += line;
  CHECK( rejoined == text );
}

TEST_CASE( "M7: fitTextIntoBox ellipsis policy truncates with evidence",
           "[platform9][typography][ellipsis]" )
{
  TextFitRequest request;
  request.text = "黄河流域地表覆盖变化监测成果图集";
  request.boxWidthMm = 30.0;
  request.boxHeightMm = 11.0; // fits two 12 pt lines; the text needs three
  request.fontPt = 12.0;
  request.policy = "ellipsis";
  const TextFitReport report = fitTextIntoBox( request );
  CHECK( report.policyApplied == "ellipsis" );
  CHECK( report.truncated );
  REQUIRE( !report.lines.empty() );
  // The last visible line carries the ellipsis, and the cut layout fits.
  CHECK( report.lines.back().find( "…" ) != std::string::npos );
  CHECK( report.fits );
  CHECK( report.lines.size() == 2 );
}

// ---------------------------------------------------------------------------
// M8 — QA: repair ledger + frame CRS obligations.
// ---------------------------------------------------------------------------

TEST_CASE( "M8: repair ledger attributes outcomes per finding",
           "[platform9][qa][ledger]" )
{
  Json::Value spec = makeMapSpec( "m8-ledger", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  spec["titles"].append( item( "t", 12, 6, 120, 14 ) );
  spec["titles"][0]["text"] = "tiny title";
  spec["titles"][0]["font"] = Json::Value( Json::objectValue );
  spec["titles"][0]["font"]["size_pt"] = 4.0; // MAP_TINY_FONT (repairable)

  Json::Value quality = preflightMapSpec( spec );
  Json::Value ledger;
  const int applied = repairMapSpecWithLedger( spec, quality, &ledger );
  REQUIRE( applied >= 1 );
  REQUIRE( ledger.isArray() );
  REQUIRE( ledger.size() >= 1 );
  bool tinyFontRecorded = false;
  for ( const auto &entry : ledger )
    if ( entry["code"].asString() == "MAP_TINY_FONT" )
    {
      tinyFontRecorded = true;
      CHECK( entry["outcome"].asString() == "applied" );
    }
  REQUIRE( tinyFontRecorded );
  // The repaired document clears the finding.
  quality = preflightMapSpec( spec );
  for ( const auto &issueEntry : quality["issues"] )
    REQUIRE( issueEntry["code"].asString() != "MAP_TINY_FONT" );
}

TEST_CASE( "M8: a declared map frame CRS satisfies the report CRS obligation",
           "[platform9][qa][crs]" )
{
  Json::Value spec = makeMapSpec( "m8-crs", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  spec["map_frames"][0]["crs"] = "EPSG:4326";
  Json::Value page2( Json::objectValue );
  page2["width_mm"] = 297.0;
  page2["height_mm"] = 210.0;
  spec["pages"].append( page2 ); // report-ish document
  Json::Value note( Json::objectValue );
  note["id"] = "note-1";
  note["text"] = "数据来源: test";
  Json::Value rect( Json::arrayValue );
  rect.append( 200.0 );
  rect.append( 190.0 );
  rect.append( 80.0 );
  rect.append( 8.0 );
  note["rect_mm"] = rect;
  spec["source_notes"].append( note );

  REQUIRE( validateMapSpec( spec ).empty() );
  const Json::Value report = preflightMapSpec( spec );
  for ( const auto &issueEntry : report["issues"] )
    REQUIRE( issueEntry["code"].asString() != "MAP_MISSING_CRS_NOTE" );

  // Bad crs shape is structural.
  spec["map_frames"][0]["crs"] = 4326;
  bool flagged = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    flagged = flagged || problem.find( "crs must be a non-empty string" ) != std::string::npos;
  REQUIRE( flagged );
}

// ---------------------------------------------------------------------------
// M9 — governed export: capability honesty, atomicity, digest stability.
// ---------------------------------------------------------------------------

TEST_CASE( "M9: export request validation refuses undeclared capabilities",
           "[platform9][export][validation]" )
{
  Json::Value spec = makeMapSpec( "m9-export-validate", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );

  MapExportRequest request;
  request.format = "gif";
  request.directory = "/tmp";
  auto problems = validateMapExportRequest( layout, request );
  REQUIRE( !problems.empty() );
  CHECK( problems.front().find( "png|pdf|svg" ) != std::string::npos );

  request.format = "pdf";
  request.pages = { 1 };
  problems = validateMapExportRequest( layout, request );
  REQUIRE( !problems.empty() );
  CHECK( problems.front().find( "only supported for png" ) != std::string::npos );

  request.format = "png";
  request.pages = { 9 };
  problems = validateMapExportRequest( layout, request );
  REQUIRE( !problems.empty() );
  CHECK( problems.front().find( "out of range" ) != std::string::npos );

  request.pages = { 0 };
  problems = validateMapExportRequest( layout, request );
  CHECK( problems.empty() );
}

TEST_CASE( "M9: png export is atomic, hashed, and page-selectable",
           "[platform9][export][atomic]" )
{
  Json::Value spec = makeMapSpec( "m9-export-atomic", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  spec["map_frames"][0]["extent"] = extent;

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  MapExportRequest request;
  request.format = "png";
  request.dpi = 96.0;
  request.directory = dir.path().toStdString();
  request.pages = { 0 };
  const MapExportResult result = exportMapLayout( layout, request );
  REQUIRE( result.ok );
  CHECK( QFile::exists( QString::fromStdString( result.path ) ) );
  CHECK( result.bytes > 0 );
  CHECK( result.sha256.size() == 64 );
  // The digest matches the delivered bytes.
  QFile delivered( QString::fromStdString( result.path ) );
  REQUIRE( delivered.open( QIODevice::ReadOnly ) );
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  hash.addData( &delivered );
  delivered.close();
  CHECK( hash.result().toHex().toStdString() == result.sha256 );
  // No temp litter survives.
  const QStringList leftovers =
    QDir( dir.path() ).entryList( QStringList() << "*.XXXXXX*" );
  CHECK( leftovers.isEmpty() );

  // Second identical export reproduces the same digest (render determinism
  // at export scale; the PNG hash determinism contract from 7.0).
  const MapExportResult second = exportMapLayout( layout, request );
  REQUIRE( second.ok );
  CHECK( second.sha256 == result.sha256 );
}

// ---------------------------------------------------------------------------
// M10 — harness integration: typed tools (diff/explain/export) through the
// registry, the same surface Pi invokes.
// ---------------------------------------------------------------------------

TEST_CASE( "M10: cartography tools expose the 9.0 typed surface",
           "[platform9][tools]" )
{
  auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
  // Must not depend on ambient registration order: other TUs in this binary
  // may or may not have registered builtins before this case runs.
  registry.registerBuiltinTools();
  for ( const char *tool : { "cartography:compose", "cartography:preflight",
                             "cartography:repair", "cartography:export",
                             "cartography:explain", "cartography:diff_templates" } )
  {
    INFO( "tool: " << tool );
    CHECK( registry.find( tool ).has_value() );
  }
}

TEST_CASE( "M10: explain returns bounded per-item evidence",
           "[platform9][tools][explain]" )
{
  auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
  auto explain = registry.find( "cartography:explain" );
  if ( !explain )
  {
    sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerBuiltinTools();
    explain = registry.find( "cartography:explain" );
  }
  REQUIRE( explain.has_value() );

  Json::Value spec = makeMapSpec( "m10-explain", Json::Value() );
  spec["map_frames"].append( item( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( item( "c2", 150, 10, 80, 60 ) );
  spec["titles"].append( item( "x", 70, 35, 20, 20 ) );
  Json::Value a = constraint( "inside", { "c1", "x" } );
  a["id"] = "pull-left";
  Json::Value b = constraint( "inside", { "c2", "x" } );
  b["id"] = "pull-right";
  spec["constraints"].append( a );
  spec["constraints"].append( b );

  Json::Value input( Json::objectValue );
  input["mapspec"] = spec;
  input["item_id"] = "x";
  using sicnu::agent::spatial_tools::SpatialToolResult;
  const SpatialToolResult result = ( *explain )->execute( input );
  // The result envelope is a typed failure/success pair; explain succeeds
  // and reports both fighting constraints plus the non-convergence state.
  REQUIRE( result.success );
  CHECK( result.output["item_id"].asString() == "x" );
  CHECK( result.output["solver"]["converged"].asBool() == false );
  CHECK( result.output["solver"]["decisions"].size() >= 1 );
  CHECK( result.output["solver"]["violated"].size() >= 1 );

  // A ghost item is a typed miss, not an empty report.
  input["item_id"] = "ghost";
  const SpatialToolResult missing = ( *explain )->execute( input );
  CHECK( !missing.success );
  CHECK( missing.errorCode == "NOT_FOUND" );
}

// ---------------------------------------------------------------------------
// Review-hardening additions: apply-path and edge coverage flagged by the
// adversarial review.
// ---------------------------------------------------------------------------

namespace {

Json::Value makeStyleSpec( const std::string &id )
{
  Json::Value style( Json::objectValue );
  style["schema_version"] = "1.0";
  style["kind"] = "style_spec";
  style["id"] = id;
  style["version"] = 1;
  style["applies_to"] = "raster";
  return style;
}

/// Writes a 1-band 4x4 GeoTIFF fixture and returns its path (empty on
/// failure). Local copy of the platform8 pattern — the helper there is
/// TU-private.
QString writeRasterFixture( const QString &dir )
{
  const QString path = QDir( dir ).filePath( "p9-scale-fixture.tif" );
  constexpr int kCols = 4;
  constexpr int kRows = 4;
  std::vector<float> cells( kCols * kRows, 1.0f );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return QString();
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), kCols, kRows, 1,
                                     GDT_Float32, nullptr );
  if ( !dataset )
    return QString();
  double geotransform[6] = { 0, 1, 0, 0, 0, -1 };
  GDALSetGeoTransform( dataset, geotransform );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  CPLErr writeErr = GDALRasterIO( band, GF_Write, 0, 0, kCols, kRows, cells.data(), kCols, kRows,
                                  GDT_Float32, 0, 0 );
  GDALClose( dataset );
  return writeErr == CE_None ? path : QString();
}

} // namespace

TEST_CASE( "M5: raster scale_ranges reach the layer's scale visibility",
           "[platform9][thematic][scale][apply]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = writeRasterFixture( dir.path() );
  REQUIRE_FALSE( path.isEmpty() );
  QgsRasterLayer raster( path, QStringLiteral( "p9-scale" ) );
  REQUIRE( raster.isValid() );

  Json::Value style = makeStyleSpec( "m5-scale-apply" );
  style["raster"]["renderertype"] = "singleband_gray";
  style["raster"]["scale_ranges"] = [] {
    Json::Value s( Json::objectValue );
    s["min"] = 500000.0;
    s["max"] = 1000.0;
    return s;
  }();
  REQUIRE( validateStyleSpec( style ).empty() );

  QStringList problems;
  QString error;
  REQUIRE( applyStyleSpecToLayer( &raster, style, &error, &problems ) );
  CHECK( problems.isEmpty() ); // no bivariate block → no advisory
  REQUIRE( raster.hasScaleBasedVisibility() );
  CHECK( raster.minimumScale() == Catch::Approx( 500000.0 ).margin( 1e-6 ) );
  CHECK( raster.maximumScale() == Catch::Approx( 1000.0 ).margin( 1e-6 ) );
}

TEST_CASE( "M5: applying a bivariate style reports the single-axis honesty advisory",
           "[platform9][thematic][bivariate][apply]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = writeRasterFixture( dir.path() );
  REQUIRE_FALSE( path.isEmpty() );
  QgsRasterLayer raster( path, QStringLiteral( "p9-bivariate" ) );
  REQUIRE( raster.isValid() );

  Json::Value style = makeStyleSpec( "m5-bivariate-apply" );
  style["raster"]["renderertype"] = "singleband_gray";
  style["bivariate"] = [] {
    Json::Value b( Json::objectValue );
    b["contract"] = "NDVI vs soil moisture jointly classify drought stress";
    b["x"] = [] {
      Json::Value a( Json::objectValue );
      a["field"] = "ndvi";
      return a;
    }();
    b["y"] = [] {
      Json::Value a( Json::objectValue );
      a["field"] = "soil_moisture";
      return a;
    }();
    return b;
  }();
  REQUIRE( validateStyleSpec( style ).empty() );

  QStringList problems;
  QString error;
  REQUIRE( applyStyleSpecToLayer( &raster, style, &error, &problems ) );
  bool advisory = false;
  for ( const QString &problem : problems )
    advisory = advisory || problem.contains( QLatin1String( "not renderable" ) );
  REQUIRE( advisory );
}

TEST_CASE( "M6: overlay_on declares intent — dangling refs flagged, declared "
           "coverage stays silent",
           "[platform9][charts][overlay]" )
{
  // (a) dangling overlay_on reference is structural.
  Json::Value spec = makeMapSpec( "m6-overlay", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-1";
  chart["rect_mm"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( 40.0 );
    r.append( 60.0 );
    r.append( 80.0 );
    r.append( 60.0 );
    return r;
  }();
  Json::Value chartSpec( Json::objectValue );
  chartSpec["kind"] = "bar";
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  chartSpec["binding"] = binding;
  chart["chart"] = chartSpec;
  chart["overlay_on"] = "ghost-frame";
  spec["charts"].append( chart );
  bool flagged = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    flagged = flagged || problem.find( "'ghost-frame' does not resolve" ) != std::string::npos;
  REQUIRE( flagged );

  // (b) declared coverage: the rule stays silent for that frame.
  spec["charts"][0]["overlay_on"] = "map-1";
  REQUIRE( validateMapSpec( spec ).empty() );
  const Json::Value report = preflightMapSpec( spec );
  for ( const auto &issueEntry : report["issues"] )
    REQUIRE( issueEntry["code"].asString() != "MAP_CHART_OVER_MAP" );
}

TEST_CASE( "M8: page-scoped charts keep the finding with a still_reported ledger",
           "[platform9][qa][ledger][still-reported]" )
{
  // The chart repair deliberately skips page-scoped items (page-0 page
  // geometry only): the ledger must say still_reported, never claim applied.
  Json::Value spec = makeMapSpec( "m8-ledger-still", Json::Value() );
  spec["map_frames"].append( item( "map-1", 12, 24, 190, 140 ) );
  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-1";
  chart["page"] = 0; // explicitly declared: the repair skips it on purpose
  chart["rect_mm"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( 40.0 );
    r.append( 60.0 );
    r.append( 80.0 );
    r.append( 60.0 );
    return r;
  }();
  Json::Value chartSpec( Json::objectValue );
  chartSpec["kind"] = "bar";
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  chartSpec["binding"] = binding;
  chart["chart"] = chartSpec;
  spec["charts"].append( chart );

  const Json::Value report = preflightMapSpec( spec );
  Json::Value ledger;
  repairMapSpecWithLedger( spec, report, &ledger );
  bool stillReported = false;
  for ( const auto &entry : ledger )
    if ( entry["code"].asString() == "MAP_CHART_OVER_MAP" )
    {
      CHECK( entry["outcome"].asString() == "still_reported" );
      stillReported = true;
    }
  REQUIRE( stillReported );
}

TEST_CASE( "M9: validation edges — non-string expression, oversized furniture, "
           "page-0 continuation",
           "[platform9][validation][edges]" )
{
  Json::Value spec = makeMapSpec( "m9-edges", Json::Value() );
  spec["titles"].append( item( "t", 12, 6, 60, 10 ) );
  spec["titles"][0]["text"] = "t";
  spec["titles"][0]["expression"] = 42; // non-string

  Json::Value page2( Json::objectValue );
  page2["width_mm"] = 297.0;
  page2["height_mm"] = 210.0;
  Json::Value furniture( Json::arrayValue );
  for ( int i = 0; i < 40; ++i )
    furniture.append( std::string( "x" ) + std::to_string( i ) ); // > 32 budget
  page2["furniture"] = furniture;
  spec["pages"].append( page2 );

  const auto problems = validateMapSpec( spec );
  bool expressionFlagged = false;
  bool furnitureFlagged = false;
  for ( const auto &problem : problems )
  {
    expressionFlagged =
      expressionFlagged || problem.find( "expression must be a non-empty" ) != std::string::npos;
    furnitureFlagged =
      furnitureFlagged || problem.find( "32 entry budget" ) != std::string::npos;
  }
  REQUIRE( expressionFlagged );
  REQUIRE( furnitureFlagged );

  // continuation on a page-0 item is flagged (meaningless there).
  Json::Value doc = makeMapSpec( "m9-edges-continuation", Json::Value() );
  Json::Value label = item( "l", 12, 24, 60, 8 );
  label["text"] = "l";
  label["continuation"] = Json::Value( Json::objectValue );
  label["continuation"]["label"] = "continued on";
  doc["labels"].append( label );
  bool continuationFlagged = false;
  for ( const auto &problem : validateMapSpec( doc ) )
    continuationFlagged =
      continuationFlagged || problem.find( "only meaningful on items moved" ) != std::string::npos;
  REQUIRE( continuationFlagged );
}

// ---------------------------------------------------------------------------
// Adversarial-review regressions (P1/P2 findings).
// ---------------------------------------------------------------------------

TEST_CASE( "Review: #864 rollback also restores the page field and the "
           "page_break stamp",
           "[platform9][pagebreak][rollback][regression]" )
{
  // page_break applies in pass 1 (t → page 1, stamped); the inside/inside
  // contradiction then exhausts the pass budget. The rollback must undo the
  // page write too — otherwise the rolled-back rect lands on the wrong page
  // and a re-solve would absorb the break as already-applied.
  Json::Value spec = makeMapSpec( "review-pb-rollback", Json::Value() );
  spec["map_frames"].append( item( "c1", 10, 10, 80, 60 ) );
  spec["map_frames"].append( item( "c2", 150, 10, 80, 60 ) );
  spec["titles"].append( item( "x", 70, 35, 20, 20 ) );
  Json::Value title = item( "t", 12, 100, 60, 10 );
  title["text"] = "t";
  spec["titles"].append( title );
  Json::Value extraPage( Json::objectValue );
  extraPage["width_mm"] = 297.0;
  extraPage["height_mm"] = 210.0;
  spec["pages"].append( extraPage );

  Json::Value a = constraint( "inside", { "c1", "x" } );
  a["id"] = "pull-left";
  Json::Value b = constraint( "inside", { "c2", "x" } );
  b["id"] = "pull-right";
  Json::Value pb = constraint( "page_break", { "t" } );
  pb["id"] = "pb-t";
  spec["constraints"].append( a );
  spec["constraints"].append( b );
  spec["constraints"].append( pb );

  const CompositionResult result = resolveComposition( spec, 12.0 );
  REQUIRE( result.converged == false );
  // Rolled back: no page move, no applied stamp on t.
  REQUIRE( spec["titles"][1]["page"].isNull() );
  REQUIRE( spec["titles"][1]["page_break_applied_by"].isNull() );
  // The rollback is not a one-way trap: once the contradiction is removed,
  // the break applies again (the rollback restored its pre-state) and the
  // stamp lands exactly once. (jsoncpp arrays cannot remove by name —
  // rebuild the array without the two fighting constraints.)
  Json::Value kept( Json::arrayValue );
  for ( const auto &constraint : spec["constraints"] )
    if ( constraint["id"].asString() != "pull-left" &&
         constraint["id"].asString() != "pull-right" )
      kept.append( constraint );
  spec["constraints"] = kept;
  const CompositionResult second = resolveComposition( spec, 12.0 );
  REQUIRE( second.converged );
  REQUIRE( spec["titles"][1]["page"].asInt() == 1 );
  REQUIRE( spec["titles"][1]["page_break_applied_by"].asString() == "pb-t" );
}

TEST_CASE( "Review: the overlay deadlock fallback merges per-element and the "
           "declared document re-validates",
           "[platform9][charts][overlay][fallback]" )
{
  // Two full-page frames: no free slot can exist, so the repair must fall
  // back to declaring. map-a is pre-declared (string); map-b overlaps
  // undeclared. The merged overlay_on must be ["map-a", "map-b"] — flat,
  // and the repaired document must pass validateMapSpec (a nested array
  // would be rejected).
  Json::Value spec = makeMapSpec( "review-overlay-merge", Json::Value() );
  Json::Value full( Json::arrayValue );
  full.append( 0.0 );
  full.append( 0.0 );
  full.append( 420.0 );
  full.append( 297.0 );
  Json::Value mapA = item( "map-a", 0, 0, 420, 297 );
  mapA["extent"] = [] {
    Json::Value e( Json::arrayValue );
    e.append( 116.0 );
    e.append( 39.0 );
    e.append( 117.0 );
    e.append( 40.0 );
    return e;
  }();
  Json::Value mapB = item( "map-b", 0, 0, 420, 297 );
  spec["map_frames"].append( mapA );
  spec["map_frames"].append( mapB );

  Json::Value chart( Json::objectValue );
  chart["id"] = "chart-1";
  chart["rect_mm"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( 100.0 );
    r.append( 100.0 );
    r.append( 70.0 );
    r.append( 44.0 );
    return r;
  }();
  chart["overlay_on"] = "map-a";
  Json::Value chartSpec( Json::objectValue );
  chartSpec["kind"] = "bar";
  Json::Value binding( Json::objectValue );
  binding["mode"] = "inline";
  chartSpec["binding"] = binding;
  chart["chart"] = chartSpec;
  spec["charts"].append( chart );

  REQUIRE( validateMapSpec( spec ).empty() );
  const Json::Value report = preflightMapSpec( spec );
  // map-b coverage is undeclared → flagged.
  bool flagged = false;
  for ( const auto &issueEntry : report["issues"] )
    flagged = flagged || issueEntry["code"].asString() == "MAP_CHART_OVER_MAP";
  REQUIRE( flagged );

  Json::Value ledger;
  repairMapSpecWithLedger( spec, report, &ledger );
  const Json::Value &overlays = spec["charts"][0]["overlay_on"];
  REQUIRE( overlays.isArray() );
  REQUIRE( overlays.size() == 2 );
  CHECK( overlays[0].asString() == "map-a" );
  CHECK( overlays[1].asString() == "map-b" );
  // The declared document re-validates cleanly (no nested-array rejection).
  REQUIRE( validateMapSpec( spec ).empty() );
}

TEST_CASE( "Review: a second page_break on the same item is rejected",
           "[platform9][pagebreak][validation]" )
{
  Json::Value spec = makeMapSpec( "review-pb-dup", Json::Value() );
  Json::Value title = item( "t", 12, 6, 60, 10 );
  title["text"] = "t";
  spec["titles"].append( title );
  Json::Value p1( Json::objectValue );
  p1["width_mm"] = 297.0;
  p1["height_mm"] = 210.0;
  Json::Value p2 = p1;
  spec["pages"].append( p1 );
  spec["pages"].append( p2 );
  Json::Value pb1 = constraint( "page_break", { "t" } );
  pb1["id"] = "pb-1";
  Json::Value pb2 = constraint( "page_break", { "t" } );
  pb2["id"] = "pb-2";
  spec["constraints"].append( pb1 );
  spec["constraints"].append( pb2 );

  bool flagged = false;
  for ( const auto &problem : validateMapSpec( spec ) )
    flagged = flagged || problem.find( "more than one page_break" ) != std::string::npos;
  REQUIRE( flagged );
}
