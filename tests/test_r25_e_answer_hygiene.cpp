/***************************************************************************
  tests/test_r25_e_answer_hygiene.cpp
  R25-E (#1695-17d): free-text answer hygiene at BOTH session boundaries.

  recordAnswer used to reject only the EMPTY string, so a whitespace-only
  answer ("   ") was accepted as a valid free-text answer and later counted
  as answered by completion gating — the recorded evidence says nothing the
  student wrote. The same discipline #1678 established for non-finite
  numerics applies: the value must be refused at the record boundary AND at
  the reload boundary, so a hand-edited or corrupted session document
  cannot smuggle a blank answer back in.

  Qt-free on purpose (same link graph as test_lab_runtime: Catch2 +
  sicnu_lab_runtime + jsoncpp transitively).
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "lab/spec_runtime.h"
#include "lab/session_state.h"

#include <sstream>
#include <string>

namespace
{

Json::Value parseJson( const std::string &text )
{
  Json::Value doc;
  Json::CharReaderBuilder builder;
  builder[ "stackLimit" ] = 64;
  std::string errors;
  std::istringstream stream( text );
  [[maybe_unused]] const bool ok = Json::parseFromStream( builder, stream, &doc, &errors );
  REQUIRE( ok );
  return doc;
}

/// A minimal, fully valid v3 lab document with one free-text question, one
/// numeric question and one choice question (mirrors the runtime probe
/// shape used by test_lab_runtime so plan semantics stay identical).
std::string validV3Doc()
{
  return R"JSON({
    "spec_version": 3,
    "id": "lab25e_hygiene_probe",
    "title": "Hygiene Probe",
    "title_zh": "作答卫生探针",
    "objective": "probe",
    "steps": [
      { "title": "s0", "title_zh": "第0步", "description_zh": "d" },
      { "title": "s1", "title_zh": "第1步", "description_zh": "d", "operator_id": "rs:clip" }
    ],
    "runtime": {
      "data_packs": [ "lab25e_hygiene_probe" ],
      "stages": [
        {
          "id": "s1_preprocess",
          "title": "Preprocess",
          "title_zh": "预处理",
          "objective": "prepare data",
          "objective_zh": "准备数据",
          "step_indices": [ 0, 1 ],
          "allowed_tools": [ "rs:clip" ],
          "checkpoints": [
            {
              "id": "ckpt_clip",
              "title": "Clip done",
              "title_zh": "裁剪完成",
              "checks": [ { "kind": "operator_invoked", "operator_id": "rs:clip" } ],
              "attempts_allowed": 3,
              "advance": "gate"
            }
          ]
        }
      ],
      "questions": [
        { "id": "q_extent", "prompt": "What extent?", "prompt_zh": "范围是什么？", "kind": "free_text" },
        { "id": "q_ndvi", "prompt": "NDVI range?", "prompt_zh": "NDVI 范围？", "kind": "numeric",
          "expected_numeric": { "min": -1.0, "max": 1.0 } }
      ],
      "hints": {
        "policy": { "max_reveals_per_target": 3, "escalation": [ "nudge", "hint" ] },
        "entries": []
      },
      "reproducibility": { "require_seed": true, "deterministic_operators_only": true, "notes": "seeded" }
    }
  })JSON";
}

sicnu::lab::LabRuntimePlan probePlan()
{
  const auto result = sicnu::lab::parseRuntimeBlock( parseJson( validV3Doc() ) );
  REQUIRE( result.ok );
  return result.value;
}

sicnu::lab::SessionMeta probeMeta()
{
  sicnu::lab::SessionMeta meta;
  meta.labId = "lab25e_hygiene_probe";
  meta.studentId = "student1";
  meta.labSpecVersion = 3;
  meta.planSource = "authored_v3";
  meta.hasSeed = true;
  meta.seed = 42;
  return meta;
}

sicnu::lab::LabSession startedSession()
{
  auto result = sicnu::lab::startSession( probePlan(), probeMeta(), 1 );
  REQUIRE( result.ok );
  return result.value;
}

bool isBlank( const std::string &s )
{
  for ( const char c : s )
    if ( !std::isspace( static_cast<unsigned char>( c ) ) )
      return false;
  return true;
}

} // namespace

TEST_CASE( "recordAnswer refuses whitespace-only free-text answers", "[r25_e][lab_runtime][answers]" )
{
  const sicnu::lab::LabRuntimePlan plan = probePlan();
  sicnu::lab::LabSession session = startedSession();

  // #1695-17d: blank answers (spaces / tabs / newlines / mixes) are not
  // answers — the recorded evidence would claim the student responded
  // while carrying no content. Refused with the same typed code as the
  // empty string, so callers see one vocabulary.
  for ( const std::string *blank : { std::string( "   " ), std::string( " \t " ),
                                     std::string( "\n" ), std::string( " \r\n\t " ) } )
  {
    const auto refused = sicnu::lab::recordAnswer( session, plan, "q_extent", *blank, std::nullopt );
    INFO( "answer='" << *blank << "'" );
    REQUIRE( !refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.field" );
  }

  // Unicode space family (Chinese cohorts): ideographic spaces (U+3000),
  // no-break spaces (U+00A0) and narrow NBSP (U+202F) carry no content
  // either — std::isspace cannot see them, so the boundary decodes them
  // itself. Mixed ASCII+Unicode blanks are still blank.
  for ( const std::string *blank :
        { std::string( "\xE3\x80\x80\xE3\x80\x80" ),           // 　　 (U+3000 ×2)
          std::string( " \xC2\xA0 " ),                          // NBSP between spaces
          std::string( "\xE2\x80\xAF\xE2\x80\xAF" ),            // narrow NBSP ×2
          std::string( "\xE3\x80\x80 \t\xC2\xA0" ) } )          // the whole family at once
  {
    const auto refused = sicnu::lab::recordAnswer( session, plan, "q_extent", *blank, std::nullopt );
    INFO( "answer-bytes=" << *blank );
    REQUIRE( !refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.field" );
  }

  // The session carries no answer afterwards: completion still lists the
  // question as missing, and answering for real still works.
  REQUIRE( session.questionAnswers.empty() );
  REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent", "the clipped extent", std::nullopt ).ok );
  REQUIRE( session.questionAnswers.size() == 1 );
}

TEST_CASE( "recordAnswer keeps accepting free text that merely contains whitespace",
           "[r25_e][lab_runtime][answers]" )
{
  const sicnu::lab::LabRuntimePlan plan = probePlan();
  sicnu::lab::LabSession session = startedSession();

  // Leading/trailing/inner whitespace in an answer WITH content is the
  // student's wording, not a blank answer — the rejection must not eat it.
  REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent", " about 30 m ", std::nullopt ).ok );
  REQUIRE( session.questionAnswers.front().answer == " about 30 m " );

  // A Chinese answer using an ideographic space as its inner separator is
  // content with a space in it, not a blank answer.
  session.questionAnswers.clear();
  REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent",
                                     "\xE7\xBA\xA6\xE3\x80\x80""30\xE3\x80\x80\xE7\xB1\xB3", // 约　30　米
                                     std::nullopt ).ok );
  REQUIRE( session.questionAnswers.front().answer ==
           std::string( "\xE7\xBA\xA6\xE3\x80\x80""30\xE3\x80\x80\xE7\xB1\xB3" ) );

  // Numeric questions still accept an empty text payload (the value is the
  // answer) and choice questions are untouched by this boundary.
  REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_ndvi", "", 0.35 ).ok );
}

TEST_CASE( "session reload refuses blank non-numeric answers (#1678 discipline)",
           "[r25_e][lab_runtime][answers]" )
{
  const sicnu::lab::LabRuntimePlan plan = probePlan();

  // A session with a REAL free-text answer round-trips (control).
  {
    sicnu::lab::LabSession session = startedSession();
    REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent", "the clipped extent", std::nullopt ).ok );
    const Json::Value doc = sicnu::lab::sessionToJson( session );
    const auto reloaded = sicnu::lab::sessionFromJson( doc );
    REQUIRE( reloaded.ok );
    REQUIRE( reloaded.value.questionAnswers.size() == 1 );
    REQUIRE( reloaded.value.questionAnswers.front().answer == "the clipped extent" );
  }

  // A numeric answer with an empty text member round-trips (control): the
  // value is the payload, the empty string is its documented carrier.
  {
    sicnu::lab::LabSession session = startedSession();
    REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_ndvi", "", 0.35 ).ok );
    const Json::Value doc = sicnu::lab::sessionToJson( session );
    const auto reloaded = sicnu::lab::sessionFromJson( doc );
    REQUIRE( reloaded.ok );
    REQUIRE( reloaded.value.questionAnswers.front().hasNumeric );
  }

  // #1695-17d reload boundary: a hand-edited or corrupted document whose
  // non-numeric answer is whitespace-only is refused — the same value the
  // record boundary refuses must not be adoptable by editing the file.
  {
    sicnu::lab::LabSession session = startedSession();
    REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent", "the clipped extent", std::nullopt ).ok );
    Json::Value doc = sicnu::lab::sessionToJson( session );
    REQUIRE( doc[ "question_answers" ].isArray() );
    doc[ "question_answers" ][0][ "answer" ] = "   ";
    const auto refused = sicnu::lab::sessionFromJson( doc );
    REQUIRE( !refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.schema" );
  }

  // …including the Unicode blanks the C locale cannot see (U+3000).
  {
    sicnu::lab::LabSession session = startedSession();
    REQUIRE( sicnu::lab::recordAnswer( session, plan, "q_extent", "the clipped extent", std::nullopt ).ok );
    Json::Value doc = sicnu::lab::sessionToJson( session );
    doc[ "question_answers" ][0][ "answer" ] = "\xE3\x80\x80\xE3\x80\x80";
    const auto refused = sicnu::lab::sessionFromJson( doc );
    REQUIRE( !refused.ok );
    REQUIRE( refused.diagnostics.front().code == "lab.session.schema" );
  }
}
