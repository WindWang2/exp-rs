/***************************************************************************
  tests/test_teaching_cockpit_authority_r4.cpp — WP-E (Track 12, teaching-lab
  R4): cockpit AUTHORITY consistency under registry drift + projection/view
  family failure paths.

  Authority truth = the runtime operator registry (registered parameter
  descriptors) and the authoritative documents (curriculum manifest,
  autonomy-status). Drift injection: present the seams with references the
  authority does not know and require REFUSAL (typed), never a hardcoded
  fallback. "" vs missing are asserted as two states where the contract
  distinguishes them. Public seams only.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <QString>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <sstream>

#include "teaching/autonomy_effective_display.h"
#include "teaching/course_home_view_model.h"
#include "teaching/lab_readiness.h"
#include "teaching/lab_status.h"
#include "teaching_admin/student_projection.h"
#include "app/teaching/lab_operator_launch.h"

using namespace sicnu::teaching;
using namespace sicnu::teaching_admin;
using JsonDoc = Json::Value;

namespace
{

JsonDoc parse( const std::string &text )
{
    JsonDoc doc;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream stream( text );
    if ( !Json::parseFromStream( b, stream, &doc, &errs ) )
        FAIL( errs );
    return doc;
}

QJsonObject qjson( const char *text )
{
    return QJsonDocument::fromJson( QByteArray( text ) ).object();
}

} // namespace

TEST_CASE( "cockpit r4: registry drift (unknown operator) refuses prefill fail-closed",
           "[teaching_r4][cockpit][authority]" )
{
    // An operator the runtime registry does not know (drifted / removed /
    // renamed) with NON-EMPTY params: nothing downstream would validate
    // them, so the seam must refuse — a hardcoded allow-list or silent drop
    // would both lie to the student.
    const sicnu::app::teaching::LabOperatorLaunchPlan plan =
      sicnu::app::teaching::prepareLabOperatorLaunch(
        QStringLiteral( "rs:drifted_away_operator" ),
        QStringLiteral( R"({"before":"data/a.tif","after":"data/b.tif"})" ) );
    CHECK_FALSE( plan.ok );
    CHECK_FALSE( plan.issuesZh.empty() );
    CHECK( plan.params.empty() );
}

TEST_CASE( "cockpit r4: empty vs missing params are two honest states for an unknown operator",
           "[teaching_r4][cockpit][authority]" )
{
    // EMPTY params + unknown operator: the operator surface opens with its
    // own defaults and the shell must NOT claim a prefill happened — ok,
    // with an empty params object.
    const sicnu::app::teaching::LabOperatorLaunchPlan empty =
      sicnu::app::teaching::prepareLabOperatorLaunch(
        QStringLiteral( "rs:drifted_away_operator" ), QStringLiteral( "" ) );
    CHECK( empty.ok );
    CHECK( empty.params.empty() );

    // Non-object params (unparseable): refused with a typed reason — never
    // prefilled from garbage.
    const sicnu::app::teaching::LabOperatorLaunchPlan garbage =
      sicnu::app::teaching::prepareLabOperatorLaunch(
        QStringLiteral( "rs:drifted_away_operator" ), QStringLiteral( "[1,2,3]" ) );
    CHECK_FALSE( garbage.ok );
    CHECK_FALSE( garbage.issuesZh.empty() );
}

TEST_CASE( "cockpit r4: tampered student view is killed by the leak oracle",
           "[teaching_r4][cockpit][projection]" )
{
    const QJsonObject teacher = qjson( R"JSON({
      "id": "lab90",
      "title_zh": "变化检测",
      "expected_results": [ { "claim": "新增暗色斑块即为变化区域" } ],
      "grading_ref": { "intent_ref": "data/labs/lab90/grade_intent.json" },
      "steps": [
        {
          "id": "s1",
          "title": "Detect",
          "operator_id": "rs:change_detection",
          "params": { "method": "normalized_difference", "threshold": 0.25 }
        }
      ]
    })JSON" );

    const QJsonObject student = projectStudentLabView( teacher );
    CHECK( assertNoAnswerLeak( student, teacher ).ok );

    // Drift the VIEW (a tampered cockpit cache): the true threshold value
    // replaces the mask — the oracle must flag the exact param position.
    QJsonObject tampered = student;
    QJsonArray steps = tampered.value( "steps" ).toArray();
    QJsonObject step = steps.at( 0 ).toObject();
    QJsonObject params = step.value( "params" ).toObject();
    params.insert( QStringLiteral( "threshold" ), 0.25 );
    step.insert( QStringLiteral( "params" ), params );
    steps.replace( 0, step );
    tampered.insert( QStringLiteral( "steps" ), steps );

    const ValidationResult leak = assertNoAnswerLeak( tampered, teacher );
    CHECK_FALSE( leak.ok );
    bool paramLeak = false;
    for ( const AdminIssue &issue : leak.issues )
        if ( issue.code == QLatin1String( "answer_leak" ) &&
             issue.path == QLatin1String( "steps[0].params.threshold" ) )
            paramLeak = true;
    CHECK( paramLeak );

    // Teacher-only field smuggled into the view: typed, by key.
    QJsonObject smuggled = student;
    smuggled.insert( QStringLiteral( "grading_ref" ), teacher.value( "grading_ref" ) );
    const ValidationResult forbidden = assertNoAnswerLeak( smuggled, teacher );
    CHECK_FALSE( forbidden.ok );
    bool teacherOnly = false;
    for ( const AdminIssue &issue : forbidden.issues )
        if ( issue.code == QLatin1String( "teacher_only_field" ) &&
             issue.path == QLatin1String( "grading_ref" ) )
            teacherOnly = true;
    CHECK( teacherOnly );
}

TEST_CASE( "cockpit r4: course home refuses missing, foreign-schema and shape-broken manifests",
           "[teaching_r4][cockpit][course]" )
{
    const JsonDoc null_manifest = JsonDoc( Json::nullValue );
    const CourseHomeViewModel missing =
      CourseHomeViewModel::fromDocuments( null_manifest, JsonDoc(), JsonDoc() );
    CHECK_FALSE( missing.ok );
    REQUIRE_FALSE( missing.issuesZh.empty() );

    const CourseHomeViewModel foreign = CourseHomeViewModel::fromDocuments(
      parse( R"JSON({ "schema": "other.curriculum/9", "modules": [] })JSON" ),
      JsonDoc(), JsonDoc() );
    CHECK_FALSE( foreign.ok );
    CHECK( foreign.issuesZh.front().find( "未知课程 schema" ) != std::string::npos );

    const CourseHomeViewModel shapeBroken = CourseHomeViewModel::fromDocuments(
      parse( R"JSON({ "schema": "sicnu.curriculum/1", "title_zh": "x" })JSON" ),
      JsonDoc(), JsonDoc() );
    CHECK_FALSE( shapeBroken.ok );
    CHECK( shapeBroken.issuesZh.front().find( "modules" ) != std::string::npos );
}

TEST_CASE( "cockpit r4: autonomy display refuses absent and foreign status documents",
           "[teaching_r4][cockpit][autonomy]" )
{
    const AutonomyEffectiveDisplay absent =
      AutonomyEffectiveDisplay::fromStatusDoc( JsonDoc( Json::nullValue ) );
    CHECK_FALSE( absent.ok );
    CHECK_FALSE( absent.issuesZh.empty() );

    const AutonomyEffectiveDisplay foreign = AutonomyEffectiveDisplay::fromStatusDoc(
      parse( R"JSON({ "schema": "not.autonomy/1", "effective_level": "L5" })JSON" ) );
    CHECK_FALSE( foreign.ok );
    // A foreign document must not be mined for an inflated level either.
    CHECK( foreign.effectiveLevel.empty() );
}

TEST_CASE( "cockpit r4: status wire vocabulary refuses unknown spellings and keeps labels",
           "[teaching_r4][cockpit][status]" )
{
    // Refusal leaves the caller's variable untouched (no silent rewrite to a
    // fake value — the caller's default survives).
    LabUiStatus status = LabUiStatus::Completed;
    CHECK_FALSE( labUiStatusFromWire( "about_to_pass", status ) );
    CHECK( status == LabUiStatus::Completed );

    ReadinessLevel level = ReadinessLevel::Ready;
    CHECK_FALSE( readinessLevelFromWire( "sure_thing", level ) );
    CHECK( level == ReadinessLevel::Ready );

    ExperienceMode mode = ExperienceMode::Expert;
    CHECK_FALSE( experienceModeFromWire( "wizard", mode ) );
    CHECK( mode == ExperienceMode::Expert );

    // Legal spellings round-trip into the enum.
    CHECK( labUiStatusFromWire( "in_progress", status ) );
    CHECK( status == LabUiStatus::InProgress );

    // Text+icon contract: every legal status has both a label and an icon
    // token (never color-only).
    for ( int s = 0; s <= static_cast<int>( LabUiStatus::Unknown ); ++s )
    {
        const LabUiStatus value = static_cast<LabUiStatus>( s );
        REQUIRE( std::string( labUiStatusLabelZh( value ) ).size() > 0 );
        REQUIRE( std::string( labUiStatusIconToken( value ) ).size() > 0 );
    }
}

TEST_CASE( "cockpit r4: readiness degrades to UNKNOWN on missing inputs, never to Ready",
           "[teaching_r4][cockpit][readiness]" )
{
    const LabReadiness noId =
      LabReadiness::aggregate( "", JsonDoc(), JsonDoc(), JsonDoc(), JsonDoc(), false );
    CHECK_FALSE( noId.issuesZh.empty() );
    CHECK( noId.level == ReadinessLevel::Unknown );

    // Every authoritative slice missing: no item may claim ok=true (nothing
    // was verified), and the level must stay fail-closed UNKNOWN.
    const LabReadiness blind =
      LabReadiness::aggregate( "lab90", JsonDoc(), JsonDoc(), JsonDoc(), JsonDoc(), false );
    for ( const ReadinessItem &item : blind.items )
        CHECK_FALSE( item.ok );
    CHECK( blind.level != ReadinessLevel::Ready );
}
