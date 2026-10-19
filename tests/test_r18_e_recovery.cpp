/***************************************************************************
  tests/test_r18_e_recovery.cpp — R18-E E02 restart & hostile-input lane.

  RED-first pins:

    T201 (D8): the lab-session seq ledger is dense. Every recorder draws
      nextSeq() = lastSeq + 1, so a genuine ledger's events are exactly the
      seqs 1..last_seq, each used once. The existing envelope only bounds
      seqs by last_seq — which is itself file-controlled, so a hand-edited
      last_seq=999999 with a planted pass at seq 999999 satisfies the
      envelope and then wins every latest-wins gate forever while genuine
      events draw 4, 5, ... and can never supersede it. The loader must
      refuse inflated and gapped ledgers instead of adopting them.

    T202 (H3): StudioSessionState::fromJson is schema-blind — a foreign or
      future session document imports as if it were current. The reader
      must refuse non-matching schema markers (same fail-closed rule its
      sibling export reader already enforces).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "lab/session_state.h"

#include <json/json.h>

#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::lab;

namespace
{

Json::Value parse( const std::string &text )
{
  Json::Value root;
  Json::CharReaderBuilder b;
  std::string errs;
  std::istringstream ss( text );
  REQUIRE( Json::parseFromStream( b, ss, &root, &errs ) );
  return root;
}

/// A shape-valid, dense session ledger: events at seq 1 (gate pass) and
/// seq 2 (an answer), last_seq = 2.
Json::Value validSessionDoc()
{
  return parse( R"({
    "schema": "sicnu.lab-session/1",
    "session_id": "labX/stu1/1",
    "lab_id": "labX",
    "lab_spec_fingerprint": "sha256:0f",
    "lab_spec_version": 3,
    "plan_source": "runtime",
    "student_id": "stu1",
    "state": "active",
    "last_seq": 2,
    "stages": [ { "stage_id": "s1", "status": "active" } ],
    "checkpoint_results": [
      { "checkpoint_id": "cp1", "verdict": "pass", "attempt": 1,
        "evidence": [], "seq": 1 }
    ],
    "question_answers": [
      { "question_id": "q1", "answer": "42", "seq": 2 }
    ],
    "hint_events": [],
    "tool_choices": [],
    "execution_refs": []
  })" );
}

} // namespace

// ---------------------------------------------------------------------------
// T201 — the seq ledger must be dense (D8)
// ---------------------------------------------------------------------------

TEST_CASE( "r18e T201: a pristine dense ledger loads", "[lab][session][seq][r18_e]" )
{
  const auto loaded = sessionFromJson( validSessionDoc() );
  REQUIRE( loaded.ok );
}

TEST_CASE( "r18e T201b: an inflated last_seq with a planted high-seq pass is refused",
           "[lab][session][seq][r18_e]" )
{
  Json::Value doc = validSessionDoc();
  // The forgery: bump the envelope and plant a pass that no recorder drew.
  doc["last_seq"] = 999999;
  Json::Value planted = doc["checkpoint_results"][0];
  planted["seq"] = 999999;
  doc["checkpoint_results"].append( planted );

  const auto loaded = sessionFromJson( doc );
  CHECK_FALSE( loaded.ok );
}

TEST_CASE( "r18e T201c: a gapped ledger (last_seq with no event) is refused",
           "[lab][session][seq][r18_e]" )
{
  Json::Value doc = validSessionDoc();
  // last_seq claims 3 events were drawn; only seqs 1..2 exist. No genuine
  // save produces this shape — nextSeq() draws lastSeq+1 every time.
  doc["last_seq"] = 3;

  const auto loaded = sessionFromJson( doc );
  CHECK_FALSE( loaded.ok );
}

TEST_CASE( "r18e T201d: a duplicate seq is refused",
           "[lab][session][seq][r18_e]" )
{
  Json::Value doc = validSessionDoc();
  // Two events claiming the same draw — the strict `>` in the latest-wins
  // scan would then resolve gates by file order.
  Json::Value twin = doc["question_answers"][0];
  twin["question_id"] = "q2";
  twin["seq"] = 2;
  doc["question_answers"].append( twin );

  const auto loaded = sessionFromJson( doc );
  CHECK_FALSE( loaded.ok );
}
