// tests/test_autonomy_gate.cpp
//
// RS14-12 teaching autonomy ladder — Slice G: the gates, wired into the real
// seams, plus the attempted-bypass matrix.
//
// The policy is only real if a forbidden action cannot be reached from the
// backend. This suite drives the two live seams — the lab copilot entry
// point (assistance) and the SpatialToolRegistry execute path for
// harness:execute_plan (execution) — under restrictive policies, and proves:
//   * assistive requests are denied or downgraded with typed codes before
//     any answer is produced;
//   * a student routed at the executor gets the teaching refusal (a forged
//     session claim cannot escalate);
//   * the execution gate refuses BEFORE the workflow engine is touched —
//     including via a direct registry execute (the MCP/CLI path);
//   * the research default keeps pre-autonomy flows behavior-compatible;
//   * the risk-class mirror and the role rule cannot drift from the
//     harness authorities;
//   * the autonomy codes are part of the closed error taxonomy.
//
// Fake action providers (no LLM, no network) drive the policy checks.

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QTemporaryDir>

#include "agent/autonomy/autonomy_audit.h"
#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_classification.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"
#include "agent/autonomy/fake_action_provider.h"
#include "agent/harness/harness_actions.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/lab_copilot.h"
#include "agent/harness/lab_spec.h"
#include "agent/harness/tool_manifest.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <json/json.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace sicnu::agent::autonomy;
using namespace sicnu::agent::harness;

namespace {

struct ScopedEnv
{
    ScopedEnv( const char *key, const char *value ) : key( key )
    {
        const char *previous = std::getenv( key );
        if ( previous )
            oldValue = previous;
#ifdef _WIN32
        if ( value )
            ::_putenv_s( key, value );
        else
            ::_putenv( ( std::string( key ) + "=" ).c_str() );
#else
        if ( value )
            ::setenv( key, value, 1 );
        else
            ::unsetenv( key );
#endif
    }
    ~ScopedEnv()
    {
#ifdef _WIN32
        if ( oldValue )
            ::_putenv_s( key, oldValue->c_str() );
        else
            ::_putenv( ( std::string( key ) + "=" ).c_str() );
#else
        if ( oldValue )
            ::setenv( key, oldValue->c_str(), 1 );
        else
            ::unsetenv( key );
#endif
    }
    const char *key;
    std::optional<std::string> oldValue;
};

void installPolicy( const std::string &json )
{
    REQUIRE( AutonomyPolicyHolder::instance().installCoursePolicyJson( json ) );
}

std::string errorCodeOf( const Json::Value &envelope )
{
    return envelope.get( "error", Json::Value() ).get( "code", "" ).asString();
}

Json::Value labAskJson( const std::string &role, const std::string &message )
{
    Json::Value input( Json::objectValue );
    input["role"] = role;
    input["message"] = message;
    return labAsk( input );
}

} // namespace

TEST_CASE( "exam mode caps next-step assistance at error localization", "[autonomy][gate]" )
{
  AutonomyAuditLog::instance().clear();
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"exam"})" );

  const Json::Value envelope = labAskJson( "student", "我卡在第3步了，下一步做什么？" );
  REQUIRE( envelope.get( "success", false ).asBool() );
  const Json::Value &result = envelope[ "result" ];
  REQUIRE( result[ "autonomy" ][ "decision" ].asString() == "downgrade" );
  REQUIRE( result[ "autonomy" ][ "downgrade_to" ].asString() ==
           assistance_capabilities::kErrorLocalization );
  REQUIRE( result[ "autonomy" ][ "effective_level" ].asString() == "L2" );
  REQUIRE( result[ "autonomy" ][ "reason_code" ].asString() ==
           autonomy_reason_codes::kDowngraded );
  // The answer is served at the downgraded capability.
  REQUIRE( result[ "intent" ].asString() == "lab_troubleshoot" );

  // The downgrade is audited with its typed reason.
  const std::vector<AutonomyAuditRecord> records = AutonomyAuditLog::instance().records();
  REQUIRE( records.size() == 1 );
  REQUIRE( records[ 0 ].decision == "downgrade" );
  REQUIRE( records[ 0 ].capability == assistance_capabilities::kNextStepRecommendation );
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "an L0 policy denies all assistance (no downgrade target exists)", "[autonomy][gate]" )
{
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L0","mode":"practice"})" );
  const Json::Value envelope = labAskJson( "student", "我卡在第3步了，下一步做什么？" );
  REQUIRE_FALSE( envelope.get( "success", true ).asBool() );
  REQUIRE( errorCodeOf( envelope ) == autonomy_reason_codes::kLevelTooLow );
  REQUIRE( envelope[ "refusal" ][ "reason_code" ].asString() ==
           autonomy_reason_codes::kLevelTooLow );

  const Json::Value conceptAsk = labAskJson( "student", "什么是对比度拉伸？" );
  REQUIRE_FALSE( conceptAsk.get( "success", true ).asBool() );
  REQUIRE( errorCodeOf( conceptAsk ) == autonomy_reason_codes::kLevelTooLow );
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "practice mode allows next-step help up to its ceiling", "[autonomy][gate]" )
{
  AutonomyAuditLog::instance().clear();
  // practice ceiling is L4: next-step (L3) is allowed outright — the
  // difference from the exam ceiling (L2), which caps it.
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L4","mode":"practice"})" );

  const Json::Value envelope = labAskJson( "student", "我卡在第3步了，下一步做什么？" );
  REQUIRE( envelope.get( "success", false ).asBool() );
  const Json::Value &result = envelope[ "result" ];
  REQUIRE( result[ "autonomy" ][ "decision" ].asString() == "allow" );
  REQUIRE( result[ "autonomy" ][ "effective_level" ].asString() == "L4" );
  REQUIRE( result[ "intent" ].asString() == "lab_hint" );

  const std::vector<AutonomyAuditRecord> records = AutonomyAuditLog::instance().records();
  REQUIRE( records.size() == 1 );
  REQUIRE( records[ 0 ].decision == "allow" );
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "concept assistance is allowed within the exam ceiling", "[autonomy][gate]" )
{
    installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"exam"})" );
    const Json::Value envelope = labAskJson( "student", "什么是对比度拉伸？" );
    REQUIRE( envelope.get( "success", false ).asBool() );
    REQUIRE( envelope[ "result" ][ "autonomy" ][ "decision" ].asString() == "allow" );
    REQUIRE( envelope[ "result" ][ "intent" ].asString() == "lab_concept" );
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "a student routed at the executor gets the teaching refusal", "[autonomy][gate]" )
{
    installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"instructor"})" );
    Json::Value input( Json::objectValue );
    input["role"] = "student";
    input["message"] = "帮我做实验3";
    input["routed_tool"] = "harness:execute_plan";
    const Json::Value envelope = labAsk( input );
    REQUIRE_FALSE( envelope.get( "success", true ).asBool() );
    REQUIRE( errorCodeOf( envelope ) == error_codes::kTeachingRefusal );
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "a forged teacher claim cannot escalate under an exam ceiling", "[autonomy][gate]" )
{
    const ScopedEnv token( "SICNU_LAB_TEACHER_TOKEN", "s3cret-token" );
    installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"exam"})" );

    // Forged role without the host-injected credential degrades to student,
    // and the autonomy gate denies the execution intent.
    Json::Value forged( Json::objectValue );
    forged["role"] = "teacher";
    forged["message"] = "帮我做实验3";
    forged["routed_tool"] = "harness:execute_plan";
    const Json::Value envelope = labAsk( forged );
    REQUIRE_FALSE( envelope.get( "success", true ).asBool() );

    // A credentialed teacher still hits the exam ceiling at the gate.
    Json::Value credentialed( Json::objectValue );
    credentialed["role"] = "teacher";
    credentialed["teacher_token"] = "s3cret-token";
    credentialed["message"] = "帮我做实验3";
    credentialed["routed_tool"] = "harness:execute_plan";
    const Json::Value teacherEnvelope = labAsk( credentialed );
    REQUIRE_FALSE( teacherEnvelope.get( "success", true ).asBool() );
    REQUIRE( errorCodeOf( teacherEnvelope ) == autonomy_reason_codes::kModeCeiling );
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "the execution gate refuses before the workflow engine is touched", "[autonomy][gate]" )
{
  using namespace sicnu::agent::spatial_tools;
  SpatialToolRegistry &registry = SpatialToolRegistry::instance();
  registry.registerBuiltinTools();
  const auto tool = registry.find( "harness:execute_plan" );
  REQUIRE( tool.has_value() );

  Json::Value plan( Json::objectValue );
  plan["kind"] = "execution_plan";
  plan["schema_version"] = "2.0";
  plan["intent"] = "ndvi";
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operator_id"] = "rs:contrast_stretch";
  Json::Value steps( Json::arrayValue );
  steps.append( step );
  plan["steps"] = steps;
  Json::Value input( Json::objectValue );
  input["plan"] = plan;
  input["skip_preflight"] = true;

  // Restrictive course policy (exam): the direct registry execute — the
  // path MCP tools/call and the CLI take — refuses at the gate.
  AutonomyAuditLog::instance().clear();
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"exam"})" );
  const SpatialToolResult refused = ( *tool )->execute( input );
  REQUIRE_FALSE( refused.success );
  REQUIRE( refused.errorCode == autonomy_reason_codes::kAgentModeRequired );

  // A self-injected session block without the host credential is ignored:
  // it cannot re-scope the request into the lab domain (fail-safe, the
  // request keeps the course policy and is still refused).
  Json::Value selfInjected = input;
  selfInjected["role"] = "student";
  // `domain` is host-injected session CONTEXT (outside the policy block);
  // without the credential the whole block is ignored.
  selfInjected["domain"] = "lab";
  const SpatialToolResult forged = ( *tool )->execute( selfInjected );
  REQUIRE_FALSE( forged.success );
  REQUIRE( forged.errorCode == autonomy_reason_codes::kAgentModeRequired );

  // With the host credential, the session layer applies: a student session
  // in the lab domain hits the structural teaching rule.
  const ScopedEnv token( "SICNU_LAB_TEACHER_TOKEN", "s3cret-token" );
  Json::Value credentialed = selfInjected;
  credentialed["teacher_token"] = "s3cret-token";
  credentialed["domain"] = "lab";
  const SpatialToolResult studentRefused = ( *tool )->execute( credentialed );
  REQUIRE_FALSE( studentRefused.success );
  REQUIRE( studentRefused.errorCode == autonomy_reason_codes::kLabStudentExecution );

  // The gate decisions are audited (three records from this case).
  const std::vector<AutonomyAuditRecord> records = AutonomyAuditLog::instance().records();
  REQUIRE( records.size() == 3 );
  REQUIRE( records[ 0 ].toolId == "harness:execute_plan" );
  REQUIRE( records[ 0 ].riskClass == riskClassForToolId( "harness:execute_plan" ) );
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "the research default keeps pre-autonomy flows behavior-compatible", "[autonomy][gate]" )
{
    using namespace sicnu::agent::spatial_tools;
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
    SpatialToolRegistry &registry = SpatialToolRegistry::instance();
    registry.registerBuiltinTools();
    const auto tool = registry.find( "harness:execute_plan" );
    REQUIRE( tool.has_value() );

    // A structurally invalid plan (unknown operator) must fail with the
    // compile error — NOT an autonomy refusal: the gate is inert for the
    // research default.
    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator_id"] = "rs:definitely_not_a_real_operator";
    Json::Value steps( Json::arrayValue );
    steps.append( step );
    Json::Value plan( Json::objectValue );
    plan["kind"] = "execution_plan";
    plan["schema_version"] = "2.0";
    plan["intent"] = "ndvi";
    plan["steps"] = steps;
    Json::Value input( Json::objectValue );
    input["plan"] = plan;
    input["skip_preflight"] = true;

    const SpatialToolResult result = ( *tool )->execute( input );
    REQUIRE_FALSE( result.success );
    REQUIRE( result.errorCode.rfind( "AUTONOMY_", 0 ) != 0 );
}

TEST_CASE( "the risk-class mirror and the role rule cannot drift", "[autonomy][gate]" )
{
    // Mirror floor: the autonomy module's risk vocabulary is the harness
    // vocabulary, string for string.
    REQUIRE( std::string( autonomy_risk_classes::kReadOnly ) == risk_classes::kReadOnly );
    REQUIRE( std::string( autonomy_risk_classes::kModifiesDisplay ) == risk_classes::kModifiesDisplay );
    REQUIRE( std::string( autonomy_risk_classes::kCreatesArtifact ) == risk_classes::kCreatesArtifact );
    REQUIRE( std::string( autonomy_risk_classes::kModifiesProject ) == risk_classes::kModifiesProject );
    REQUIRE( std::string( autonomy_risk_classes::kDestructive ) == risk_classes::kDestructive );
    REQUIRE( std::string( autonomy_risk_classes::kExternalProcess ) == risk_classes::kExternalProcess );
    REQUIRE( std::string( autonomy_risk_classes::kNetwork ) == risk_classes::kNetwork );

    // The plan executor is classified by the harness risk table, and that
    // classification lands on autonomous execution.
    const std::string risk = riskClassForToolId( "harness:execute_plan" );
    const ActionClassification classification = classifyActionRisk( risk );
    REQUIRE( classification.known );
    REQUIRE( classification.capability == assistance_capabilities::kAutonomousExecution );

    // Role rule floor: the engine's instructor test is the harness's
    // fail-closed role normalization, input for input.
    for ( const std::string &role : { "", "student", "teacher", "admin", "teacher ", "Teacher",
                                      "ADMIN" } )
    {
        REQUIRE( isInstructorRole( role ) == ( normalizeLabRole( role ) != "student" ) );
    }
}

TEST_CASE( "autonomy reason codes are part of the closed error taxonomy", "[autonomy][gate]" )
{
    static const char *const kCodes[] = {
        autonomy_reason_codes::kUnknownCapability,
        autonomy_reason_codes::kLevelTooLow,
        autonomy_reason_codes::kDowngraded,
        autonomy_reason_codes::kModeCeiling,
        autonomy_reason_codes::kCourseCap,
        autonomy_reason_codes::kOverrideDenied,
        autonomy_reason_codes::kLabStudentExecution,
        autonomy_reason_codes::kAgentModeRequired,
    };
    for ( const char *code : kCodes )
    {
        INFO( code );
        REQUIRE( isKnownErrorCode( code ) );
        REQUIRE( errorCategoryForCode( code ) == "validation" );
        REQUIRE( retryClassForCode( code ) == RetryClass::None );
    }
    // AUTONOMY_ALLOWED is a decision, not an error.
    REQUIRE_FALSE( isKnownErrorCode( autonomy_reason_codes::kAllowed ) );
}

TEST_CASE( "the status tool projects the policy without a live session", "[autonomy][gate]" )
{
    using namespace sicnu::agent::spatial_tools;
    SpatialToolRegistry &registry = SpatialToolRegistry::instance();
    registry.registerBuiltinTools();
    const auto tool = registry.find( "harness:autonomy_status" );
    REQUIRE( tool.has_value() );

    installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"exam"})" );
    Json::Value input( Json::objectValue );
    input["role"] = "student";
    const SpatialToolResult result = ( *tool )->execute( input );
    REQUIRE( result.success );
    REQUIRE( result.output[ "schema" ].asString() == kAutonomyStatusSchema );
    REQUIRE( result.output[ "level" ].asString() == "L2" );

    bool forbiddenExecution = false;
    for ( const Json::Value &entry : result.output[ "forbidden" ] )
        if ( entry[ "capability" ].asString() == assistance_capabilities::kAutonomousExecution )
        {
            forbiddenExecution = true;
            REQUIRE( entry[ "reason_code" ].asString() ==
                     autonomy_reason_codes::kLabStudentExecution );
            REQUIRE_FALSE( entry[ "reason_zh" ].asString().empty() );
        }
    REQUIRE( forbiddenExecution );
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "the labspec layer constrains the lab it belongs to", "[autonomy][gate]" )
{
  // A lab document's optional "autonomy" block is the labspec layer: it sits
  // above the course policy and below teacher/session.
  QTemporaryDir labDir;
  REQUIRE( labDir.isValid() );
  {
    QFile spec( labDir.path() + "/labZZ_autonomy_probe.lab.json" );
    REQUIRE( spec.open( QIODevice::WriteOnly | QIODevice::Text ) );
    spec.write( R"({
      "spec_version": 2,
      "id": "labZZ_autonomy_probe",
      "title_zh": "自治策略探针实验",
      "steps": [
        { "title_zh": "第一步", "description_zh": "加载数据。", "action": "addRasterLayer" }
      ],
      "autonomy": { "schema": "sicnu.autonomy-policy/1", "level": "L1", "mode": "practice" }
    })" );
    spec.close();
  }

  LabSpecCatalog &catalog = LabSpecCatalog::instance();
  const std::string previousDirectory = catalog.directory();
  catalog.setDirectory( labDir.path().toStdString() );
  REQUIRE( catalog.reload() >= 1 );

  // Course policy would allow next-step help; this lab's block caps the
  // effective level at L1, so a next-step request downgrades to concept_hint
  // — the labspec layer overrides the course level.
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L5","mode":"practice"})" );
  Json::Value input( Json::objectValue );
  input["role"] = "student";
  input["lab_id"] = "labZZ_autonomy_probe";
  input["current_step"] = 1;
  input["message"] = "我卡在第1步了，下一步做什么？";
  const Json::Value envelope = labAsk( input );
  REQUIRE( envelope.get( "success", false ).asBool() );
  REQUIRE( envelope[ "result" ][ "autonomy" ][ "decision" ].asString() == "downgrade" );
  REQUIRE( envelope[ "result" ][ "autonomy" ][ "effective_level" ].asString() == "L1" );
  REQUIRE( envelope[ "result" ][ "autonomy" ][ "downgrade_to" ].asString() ==
           assistance_capabilities::kConceptHint );
  REQUIRE( envelope[ "result" ][ "intent" ].asString() == "lab_concept" );

  catalog.setDirectory( previousDirectory );
  catalog.reload();
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "a credentialed session layer outranks the course policy; a forged one cannot",
           "[autonomy][gate]" )
{
  const ScopedEnv token( "SICNU_LAB_TEACHER_TOKEN", "s3cret-token" );
  // Course says exam/L2; the host-injected, credential-authenticated session
  // layer (teacher-authorized raise for this session) wins for the request it
  // is injected into. Lifting the exam ceiling also requires the session to
  // declare its mode — the course mode stays binding otherwise.
  installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"exam"})" );
  Json::Value input( Json::objectValue );
  input["role"] = "teacher";
  input["teacher_token"] = "s3cret-token";
  input["message"] = "我卡在第3步了，下一步做什么？";
  Json::Value session( Json::objectValue );
  session["schema"] = "sicnu.autonomy-policy/1";
  session["level"] = "L3";
  session["mode"] = "instructor";
  input["autonomy"] = session;
  const Json::Value envelope = labAsk( input );
  REQUIRE( envelope.get( "success", false ).asBool() );
  REQUIRE( envelope[ "result" ][ "autonomy" ][ "decision" ].asString() == "allow" );
  REQUIRE( envelope[ "result" ][ "autonomy" ][ "effective_level" ].asString() == "L3" );

  // The same self-injected block WITHOUT the credential is ignored: the
  // session keeps the course exam policy (effective L2), so the request is
  // downgraded — not served at the forged L3.
  Json::Value forged( Json::objectValue );
  forged["role"] = "student";
  forged["message"] = "我卡在第3步了，下一步做什么？";
  forged["autonomy"] = session;
  const Json::Value forgedEnvelope = labAsk( forged );
  REQUIRE( forgedEnvelope.get( "success", false ).asBool() );
  REQUIRE( forgedEnvelope[ "result" ][ "autonomy" ][ "effective_level" ].asString() == "L2" );
  REQUIRE( forgedEnvelope[ "result" ][ "autonomy" ][ "decision" ].asString() == "downgrade" );
  AutonomyPolicyHolder::instance().installCoursePolicy(
      AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "fake action providers exercise the whole matrix without a model", "[autonomy][gate]" )
{
    installPolicy( R"({"schema":"sicnu.autonomy-policy/1","level":"L2","mode":"practice"})" );

    // Assistance corpus: every turn decides deterministically.
    for ( const FakeAssistanceTurn &turn : fakeAssistanceTurns() )
    {
        const AutonomyDecision decision = decideFakeAssistance(
            AutonomyPolicyHolder::instance().coursePolicy(), turn );
        if ( turn.role == "student" && turn.intent == "lab_execute" )
        {
            REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
            REQUIRE( decision.reasonCode == autonomy_reason_codes::kLabStudentExecution );
        }
        else if ( turn.role == "student" && turn.intent == "lab_hint" )
        {
            REQUIRE( decision.kind == AutonomyDecisionKind::Downgrade );
        }
    }

    // Action corpus: queries pass, artifact-producing actions are denied for
    // students, and an unresolved risk class fails closed.
    for ( const FakeActionTurn &turn : fakeActionTurns() )
    {
        const AutonomyDecision decision =
            decideFakeAction( AutonomyPolicyHolder::instance().coursePolicy(), turn );
        if ( turn.riskClass.empty() )
        {
            REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
            REQUIRE( decision.reasonCode == autonomy_reason_codes::kUnknownCapability );
        }
        else if ( turn.riskClass == autonomy_risk_classes::kReadOnly )
        {
            REQUIRE( decision.kind == AutonomyDecisionKind::Allow );
        }
        else if ( turn.role == "student" )
        {
            REQUIRE( decision.kind == AutonomyDecisionKind::Deny );
        }
    }

    // Every decision above is audited with a typed reason.
    const std::vector<AutonomyAuditRecord> records = AutonomyAuditLog::instance().records();
    for ( const AutonomyAuditRecord &record : records )
        REQUIRE( isKnownAutonomyReasonCode( record.reasonCode ) );
    AutonomyAuditLog::instance().clear();
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
}

TEST_CASE( "skip_preflight can no longer bypass a blocked intent preflight",
           "[autonomy][gate][review2][preflight]" )
{
    // RED on master: skip_preflight=true skipped preflightIntent entirely,
    // so a plan whose inputs do not even resolve compiled and submitted with
    // executed=true — any caller could override a blocked verdict,
    // contradicting the scientific_preflight.h contract ("blocked plans are
    // refused; the LLM cannot override"). The gate is unconditional for
    // typed intents now; plans without an intent keep their natural bypass.
    using namespace sicnu::agent::spatial_tools;
    AutonomyPolicyHolder::instance().installCoursePolicy(
        AutonomyPolicyHolder::researchDefaultPolicy() );
    SpatialToolRegistry &registry = SpatialToolRegistry::instance();
    registry.registerBuiltinTools();
    const auto tool = registry.find( "harness:execute_plan" );
    REQUIRE( tool.has_value() );

    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator_id"] = "rs:contrast_stretch";
    Json::Value steps( Json::arrayValue );
    steps.append( step );
    Json::Value slot( Json::objectValue );
    slot["name"] = "primary";
    slot["ref"] = "/definitely/not/here.tif";
    Json::Value inputs( Json::arrayValue );
    inputs.append( slot );
    Json::Value plan( Json::objectValue );
    plan["kind"] = "execution_plan";
    plan["schema_version"] = "2.0";
    plan["intent"] = "preprocess";
    plan["inputs"] = inputs;
    plan["steps"] = steps;
    Json::Value input( Json::objectValue );
    input["plan"] = plan;
    input["skip_preflight"] = true;

    const SpatialToolResult result = ( *tool )->execute( input );
    REQUIRE( result.success );
    REQUIRE( result.output["executed"].asBool() == false );
    REQUIRE( result.output["preflight"]["verdict"].asString() == "blocked" );
}
