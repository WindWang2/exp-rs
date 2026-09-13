// src/agent/harness/lab_tools.cpp
#include "lab_tools.h"

#include "harness_error.h"
#include "lab_copilot.h"

#include <json/json.h>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

void stringProperty( Json::Value &props, const char *name, const char *description )
{
  Json::Value prop( Json::objectValue );
  prop["type"] = "string";
  prop["description"] = description;
  props[name] = std::move( prop );
}

/// Turns a labAsk/labReference envelope into the tool result: answers ride
/// as success output; refusals are typed SpatialToolResult failures that
/// still carry the full structured envelope in `output` for auditing.
SpatialToolResult envelopeToResult( const Json::Value &envelope )
{
  if ( envelope.get( "success", false ).asBool() )
    return SpatialToolResult::ok( envelope["result"] );

  const Json::Value error = envelope["error"];
  SpatialToolResult result = SpatialToolResult::failure(
    error.get( "message", "" ).asString(), error.get( "code", "" ).asString(),
    error.get( "category", "" ).asString(), /*retryable=*/false );
  result.output = envelope;
  return result;
}

class LabAskTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:lab_ask"; }
    std::string displayName() const override { return "Lab Copilot (teaching mode)"; }
    std::string description() const override
    {
      return "Teaching copilot for lab work: diagnoses why your result looks wrong, hints the "
             "next step, explains concepts (Chinese-first). It never produces finished "
             "artifacts or grades for students — 宁可少帮，不可代做; attempts return the typed "
             "TEACHING_REFUSAL. role is session state (default student), not read from the "
             "message.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "lab", "teaching", "tutoring" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      stringProperty( props, "message", "The student's help-seeking message." );
      stringProperty( props, "lab_id", "Current lab id, when the session is anchored to a lab." );
      Json::Value step( Json::objectValue );
      step["type"] = "integer";
      step["description"] = "Current step number in the lab (1-based), when known.";
      props["current_step"] = std::move( step );
      Json::Value observation( Json::objectValue );
      observation["type"] = "object";
      observation["description"] =
        "Measured facts about the student's output (min/max, nodata_fraction, kappa, "
        "crs_pair, grid_pair, ...) for troubleshooting.";
      props["observation"] = std::move( observation );
      // Deliberately NOT in the schema: "role" and "teacher_token". They are
      // host-injected session credentials (V1 hardening) — the composing
      // model is never invited to claim a role; an absent role degrades to
      // student (fail-closed).
      Json::Value required( Json::arrayValue );
      required.append( "message" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["intent"] = Json::Value( Json::objectValue );
      props["role"] = Json::Value( Json::objectValue );
      props["answer_zh"] = Json::Value( Json::objectValue );
      props["suggested_actions"] = Json::Value( Json::arrayValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "message" ) || !input["message"].isString() ||
           input["message"].asString().empty() )
        return SpatialToolResult::failure( "message must be a non-empty string",
                                           error_codes::kInvalidParameter, "validation" );
      return envelopeToResult( labAsk( input ) );
    }
};

class LabReferenceTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:lab_reference"; }
    std::string displayName() const override { return "Lab Reference (teacher only)"; }
    std::string description() const override
    {
      return "Teacher surface: reference solutions for a lab and grade citations. Access "
             "requires the host-injected session role AND the host-configured teacher "
             "credential (SICNU_LAB_TEACHER_TOKEN); unconfigured hosts and students are "
             "refused with TEACHING_REFUSAL — the copilot may cite a score for a teacher, "
             "never compute-and-hand-out for a student.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "lab", "teaching", "teacher" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      stringProperty( props, "lab_id", "Lab id, e.g. lab04_change_detection." );
      Json::Value kind( Json::objectValue );
      kind["type"] = "string";
      kind["description"] = "reference_solution | grade_citation";
      props["kind"] = std::move( kind );
      // Deliberately NOT in the schema: "role" and "teacher_token" — host
      // injected for authenticated teacher sessions only (fail-closed when
      // absent: students and unconfigured hosts are refused).
      Json::Value required( Json::arrayValue );
      required.append( "lab_id" );
      required.append( "kind" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["role"] = Json::Value( Json::objectValue );
      props["kind"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "lab_id" ) || !input["lab_id"].isString() ||
           input["lab_id"].asString().empty() )
        return SpatialToolResult::failure( "lab_id must be a non-empty string",
                                           error_codes::kInvalidParameter, "validation" );
      if ( !input.isMember( "kind" ) || !input["kind"].isString() ||
           input["kind"].asString().empty() )
        return SpatialToolResult::failure( "kind must be a non-empty string",
                                           error_codes::kInvalidParameter, "validation" );
      return envelopeToResult( labReference( input ) );
    }
};

} // namespace

void registerLabTools()
{
  // Plain registration: registerTool() rejects duplicates, so this is
  // idempotent — and re-registers correctly after registry reset().
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<LabAskTool>() );
  registry.registerTool( std::make_shared<LabReferenceTool>() );
}

} // namespace sicnu::agent::harness
