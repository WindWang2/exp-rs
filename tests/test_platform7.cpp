// tests/test_platform7.cpp
//
// Cartography Platform 7.0 — explainable constraint solving (package A):
//   hardness (hard|soft), priority + weighted objective, rank-aware soft
//   rejection, canonical multi-fixpoint policy, bounded unsat cores,
//   decisions ledger, v4 validation and v3 back-compatibility.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/composition.h"
#include "agent/mapspec/mapspec.h"

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value rectItem( const std::string &id, double x, double y, double w, double h )
{
  Json::Value item( Json::objectValue );
  item["id"] = id;
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  item["rect_mm"] = rect;
  return item;
}

Json::Value constraint( const std::string &id, const std::string &kind, const std::string &a,
                        const std::string &b, double gapMm = -1.0 )
{
  Json::Value c( Json::objectValue );
  c["id"] = id;
  c["kind"] = kind;
  Json::Value items( Json::arrayValue );
  items.append( a );
  if ( !b.empty() )
    items.append( b );
  c["items"] = items;
  if ( gapMm >= 0.0 )
    c["gap_mm"] = gapMm;
  return c;
}

/// Marks a constraint entry with the v4 surface fields.
void markSoft( Json::Value &c, int priority, double weight )
{
  c["hardness"] = "soft";
  c["priority"] = priority;
  c["weight"] = weight;
}

const Json::Value &itemById( const Json::Value &spec, const char *collection,
                             const std::string &id )
{
  for ( const auto &item : spec[collection] )
    if ( item["id"].asString() == id )
      return item;
  FAIL( "item not found: " << id );
  static Json::Value null;
  return null;
}

bool decisionsContain( const Json::Value &composition, const std::string &cid,
                       const std::string &outcome )
{
  for ( const auto &decision : composition["decisions"] )
    if ( decision["cid"].asString() == cid && decision["outcome"].asString() == outcome )
      return true;
  return false;
}

std::string rejectionReason( const Json::Value &composition, const std::string &cid )
{
  for ( const auto &decision : composition["decisions"] )
    if ( decision["cid"].asString() == cid && decision["outcome"].asString() == "rejected" )
      return decision["reason"].asString();
  return std::string();
}

} // namespace

TEST_CASE( "P7 solver: soft failure downgrades the objective, never convergence",
           "[platform7][solver]" )
{
  // A soft constraint that can never be evaluated (invalid direction →
  // permanent failure) counts into the violated objective — but softs never
  // flip `converged` (that is reserved for hard failures). A healthy soft
  // alongside it contributes its weight on the satisfied side.
  Json::Value spec = makeMapSpec( "p7-soft", Json::Value() );
  spec["titles"].append( rectItem( "t1", 10.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 10.0, 30.0, 40.0, 30.0 ) );
  Json::Value ok = constraint( "s-ok", "below", "t1", "lg", 2.0 );
  markSoft( ok, 50, 2.0 );
  spec["constraints"].append( ok );
  Json::Value broken = constraint( "s-broken", "stack", "t1", "lg" );
  broken["direction"] = "diagonal";
  markSoft( broken, 50, 3.0 );
  spec["constraints"].append( broken );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  Json::Value report = result.toJson();

  REQUIRE( result.converged );
  REQUIRE( result.constraintsSolved == 0 ); // no hard constraints declared
  REQUIRE( result.softTotal == 2 );
  REQUIRE( result.softSatisfied == 1 );
  REQUIRE( result.satisfiedWeight == Catch::Approx( 2.0 ) );
  REQUIRE( result.violatedWeight == Catch::Approx( 3.0 ) );
  REQUIRE( result.violated.size() == 1 );
  REQUIRE( result.violated[0].cid == "s-broken" );
  REQUIRE( report["soft_total"].asInt() == 2 );
  REQUIRE( report["violated_weight"].asDouble() == Catch::Approx( 3.0 ) );
  REQUIRE( report["fixpoint_policy"].asString().find( "canonical" ) != std::string::npos );
}

TEST_CASE( "P7 solver: soft satisfied contributes its weight",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-soft-ok", Json::Value() );
  spec["titles"].append( rectItem( "t1", 10.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 10.0, 22.0, 40.0, 30.0 ) );
  Json::Value soft = constraint( "s1", "below", "t1", "lg", 2.0 );
  markSoft( soft, 50, 3.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  // lg already sits exactly 2 mm below t1 → satisfied without movement.
  REQUIRE( result.converged );
  REQUIRE( result.softTotal == 1 );
  REQUIRE( result.softSatisfied == 1 );
  REQUIRE( result.satisfiedWeight == Catch::Approx( 3.0 ) );
  REQUIRE( result.violatedWeight == Catch::Approx( 0.0 ) );
}

TEST_CASE( "P7 solver: priority decides scarce space; loser is rejected with the winner named",
           "[platform7][solver]" )
{
  // Two soft constraints pull the same follower to contradictory targets;
  // the higher priority keeps the geometry, the lower is REVERTED and
  // reported (never silently kept, never order-dependent).
  for ( int permutation = 0; permutation < 2; ++permutation )
  {
    Json::Value spec = makeMapSpec( "p7-priority", Json::Value() );
    spec["titles"].append( rectItem( "top-ref", 20.0, 10.0, 60.0, 10.0 ) );
    spec["source_notes"].append( rectItem( "bottom-ref", 20.0, 180.0, 60.0, 10.0 ) );
    spec["legends"].append( rectItem( "box", 20.0, 100.0, 40.0, 20.0 ) );
    Json::Value high = constraint( "s-high", "below", "top-ref", "box", 2.0 );
    markSoft( high, 90, 1.0 );
    Json::Value low = constraint( "s-low", "above", "bottom-ref", "box", 2.0 );
    markSoft( low, 10, 5.0 );
    spec["constraints"].append( permutation == 0 ? high : low );
    spec["constraints"].append( permutation == 0 ? low : high );

    const CompositionResult result = resolveComposition( spec, 10.0 );
    const Json::Value &box = itemById( spec, "legends", "box" );
    // s-high (priority 90) wins despite s-low's larger weight.
    REQUIRE( box["rect_mm"][1].asDouble() == Catch::Approx( 22.0 ) );
    REQUIRE( result.softTotal == 2 );
    REQUIRE( result.softSatisfied == 1 );
    REQUIRE( result.satisfiedWeight == Catch::Approx( 1.0 ) );
    REQUIRE( result.violatedWeight == Catch::Approx( 5.0 ) );
    REQUIRE( decisionsContain( result.toJson(), "s-high", "applied" ) );
    REQUIRE( decisionsContain( result.toJson(), "s-low", "rejected" ) );
    const std::string reason = rejectionReason( result.toJson(), "s-low" );
    REQUIRE( reason.find( "s-high" ) != std::string::npos );
    // Declaration order must not decide the outcome (multi-fixpoint policy).
    REQUIRE( result.fixpointPolicy.find( "priority desc" ) != std::string::npos );
  }
}

TEST_CASE( "P7 solver: soft application cannot break a hard constraint",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-soft-hard", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
  spec["scale_bars"].append( rectItem( "sb", 20.0, 80.0, 30.0, 8.0 ) );
  // Hard: legend must match the scale bar's height.
  Json::Value hard = constraint( "h1", "match_height", "sb", "lg" );
  spec["constraints"].append( hard );
  // Soft: pull the legend up (would resize it, breaking h1's match).
  Json::Value soft = constraint( "s1", "above", "t1", "lg", 2.0 );
  markSoft( soft, 80, 4.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  // Hard solved and untouched by the soft phase.
  REQUIRE( result.constraintsSolved == 1 );
  REQUIRE( decisionsContain( result.toJson(), "h1", "applied" ) );
  // The soft was applied only if it kept h1 satisfied — moving above t1
  // (y = 22) does not change the height, so it is applied; a size-breaking
  // variant would be rejected. Either way h1 must hold afterwards.
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][3].asDouble() == Catch::Approx( 8.0 ) );
}

TEST_CASE( "P7 solver: unsat core names the minimal conflicting subset",
           "[platform7][solver]" )
{
  // Two hard constraints fight over the same follower with contradictory
  // targets — the relaxation cannot converge, and the core search must
  // identify the pair.
  Json::Value spec = makeMapSpec( "p7-core", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["source_notes"].append( rectItem( "note", 20.0, 180.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "box", 20.0, 100.0, 40.0, 20.0 ) );
  spec["constraints"].append( constraint( "c1", "below", "t1", "box", 2.0 ) );
  spec["constraints"].append( constraint( "c2", "above", "note", "box", 2.0 ) );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE_FALSE( result.converged ); // the pair cannot both hold
  REQUIRE_FALSE( result.unsatCores.isNull() );

  bool sawCore = false;
  for ( const auto &entry : result.unsatCores )
  {
    if ( entry["constraint"].asString() != "c1" )
      continue;
    sawCore = true;
    REQUIRE( entry["core"].isArray() );
    std::vector<std::string> members;
    for ( const auto &member : entry["core"] )
      members.push_back( member.asString() );
    REQUIRE( members.size() == 2 );
    REQUIRE( members[0] == "c1" );
    REQUIRE( members[1] == "c2" );
    REQUIRE( entry["explanation"].asString().find( "satisfiable" ) != std::string::npos );
  }
  REQUIRE( sawCore );
}

TEST_CASE( "P7 solver: anchor authority is reported as a decision",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-anchor", Json::Value() );
  Json::Value anchored = rectItem( "lg", 200.0, 100.0, 40.0, 30.0 );
  Json::Value anchor( Json::objectValue );
  anchor["edge"] = "bottom-right";
  anchored["anchor"] = anchor;
  spec["legends"].append( anchored );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  Json::Value hard = constraint( "h1", "below", "t1", "lg", 2.0 );
  spec["constraints"].append( hard );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  // The anchor wins: the constraint is disabled with a targeted report and
  // the anchored geometry is preserved.
  REQUIRE( decisionsContain( result.toJson(), "h1", "anchor_wins" ) );
  bool sawNote = false;
  for ( const auto &note : result.unsatisfied )
    sawNote = sawNote || note.find( "anchor wins" ) != std::string::npos;
  REQUIRE( sawNote );
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][0].asDouble() == Catch::Approx( 200.0 ) );
}

TEST_CASE( "P7 solver: declaration permutations produce identical geometry",
           "[platform7][solver]" )
{
  // Over-determined (multi-fixpoint) system: 6.0 was deterministic per input
  // but order-sensitive; 7.0's canonical policy must produce identical
  // geometry under declaration permutations.
  auto build = []( int permutation ) {
    Json::Value spec = makeMapSpec( "p7-perm", Json::Value() );
    spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    spec["scale_bars"].append( rectItem( "sb", 20.0, 80.0, 30.0, 8.0 ) );
    spec["north_arrows"].append( rectItem( "na", 250.0, 20.0, 15.0, 15.0 ) );
    Json::Value c1 = constraint( "c1", "align", "t1", "lg" );
    c1["edge"] = "left";
    Json::Value c2 = constraint( "c2", "match_width", "t1", "lg" );
    Json::Value c3 = constraint( "c3", "match_height", "sb", "lg" );
    Json::Value c4 = constraint( "c4", "right_of", "sb", "na", 6.0 );
    Json::Value *order[4] = { &c1, &c2, &c3, &c4 };
    const int permuted[2][4] = { { 0, 1, 2, 3 }, { 3, 2, 1, 0 } };
    for ( int i = 0; i < 4; ++i )
      spec["constraints"].append( *order[permuted[permutation][i]] );
    resolveComposition( spec, 10.0 );
    return spec;
  };
  const Json::Value a = build( 0 );
  const Json::Value b = build( 1 );
  for ( const char *collection : { "titles", "legends", "scale_bars", "north_arrows" } )
  {
    const Json::Value &ia = a[collection][0];
    const Json::Value &ib = b[collection][0];
    for ( int k = 0; k < 4; ++k )
      REQUIRE( ia["rect_mm"][k].asDouble() == Catch::Approx( ib["rect_mm"][k].asDouble() ) );
  }
}

TEST_CASE( "P7 solver: decisions ledger is bounded and ordered",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-ledger", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
  Json::Value soft = constraint( "s1", "below", "t1", "lg", 2.0 );
  markSoft( soft, 50, 1.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( static_cast<int>( result.decisions.size() ) <= kMaxDecisions );
  int expectedOrder = 0;
  for ( const auto &decision : result.decisions )
    REQUIRE( decision.order == expectedOrder++ );
}

TEST_CASE( "P7 validation: v4 constraint surface is closed and bounded",
           "[platform7][solver][validation]" )
{
  Json::Value spec = makeMapSpec( "p7-validate", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );

  {
    Json::Value bad = makeMapSpec( "p7-validate-1", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["hardness"] = "rigid";
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "hardness must be" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value bad = makeMapSpec( "p7-validate-2", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["priority"] = 101;
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "priority must be an integer in [0, 100]" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value bad = makeMapSpec( "p7-validate-3", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["weight"] = 5.0; // weight on a (default-hard) constraint is a contradiction
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "only meaningful on hardness" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value good = spec;
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["hardness"] = "soft";
    c["priority"] = 100;
    c["weight"] = 0.5;
    good["constraints"].append( c );
    REQUIRE( validateMapSpec( good ).empty() );
  }
}

TEST_CASE( "P7 solver: documents without v4 fields keep the 6.0 contract",
           "[platform7][solver][compat]" )
{
  Json::Value spec = makeMapSpec( "p7-compat", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 25.0, 40.0, 40.0, 20.0 ) );
  Json::Value hard = constraint( "h1", "align", "t1", "lg" );
  hard["edge"] = "left";
  spec["constraints"].append( hard );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  REQUIRE( result.constraintsTotal == 1 );
  REQUIRE( result.constraintsSolved == 1 );
  REQUIRE( result.softTotal == 0 );
  REQUIRE( result.softSatisfied == 0 );
  REQUIRE( result.decisions.size() == 1 ); // the h1 application is recorded
  REQUIRE( result.decisions[0].outcome == "applied" );
  // The alignment moved lg to t1's left edge.
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][0].asDouble() == Catch::Approx( 20.0 ) );
}

TEST_CASE( "P7 solver: upgradeMapSpec re-stamps v3 documents to v4",
           "[platform7][solver][compat]" )
{
  Json::Value spec = makeMapSpec( "p7-upgrade", Json::Value() );
  spec["spec_version"] = 3;
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  REQUIRE( validateMapSpec( spec ).empty() ); // v3 is still a supported version
  const Json::Value upgraded = upgradeMapSpec( spec );
  REQUIRE( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  REQUIRE( kMapSpecCurrentVersion == 4 );
}
