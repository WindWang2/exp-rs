// src/agent/harness/harness_actions.cpp
#include "harness_actions.h"

#include <json/json.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace sicnu::agent::harness {

const std::vector<HarnessActionSpec> &harnessActionTable()
{
  static const std::vector<HarnessActionSpec> kTable = {
    // Dataset-level facts: the agent re-inspects; a human opens the
    // dataset/experiment workbench.
    { "check_dataset", "spatial:understand", "workbench.datasetExperiment", "", "tool" },
    { "reinspect_dataset", "spatial:understand", "workbench.datasetExperiment", "", "tool" },
    { "check_training", "spatial:understand", "workbench.datasetExperiment", "", "tool" },
    // Band-level facts: raster inspection only (no dedicated workbench page).
    { "inspect_bands", "spatial:raster_inspect", "", "", "tool" },
    // Model choice: the selection tool is the agent surface; the model
    // workbench is the human one.
    { "select_model", "spatial:select_model", "workbench.model", "", "tool" },
    // Temporal collections: typed preflight + the temporal workbench.
    { "check_collection", "temporal:preflight_collection", "workbench.temporal", "", "tool" },
    { "select_matching_polarization", "spatial:understand", "workbench.datasetExperiment", "", "tool" },
    // Safe preparations: proposed as recipe searches — the harness proposes
    // the preparation CLASS, the agent picks the concrete recipe through the
    // recipe tools (no operator invented here).
    { "calibrate_consistently", "harness:search_recipes", "",
      "{\"query\": \"sar calibration\"}", "tool" },
    { "reproject_to_reference", "harness:search_recipes", "",
      "{\"query\": \"reproject\"}", "tool" },
    { "align_to_reference", "harness:search_recipes", "",
      "{\"query\": \"reproject\"}", "tool" },
    { "normalize_radiometry", "harness:search_recipes", "",
      "{\"query\": \"radiometric calibration reflectance\"}", "tool" },
    // Workflow-preflight repair actions (the other makeRepairSuggestion
    // producer): path problems resolve like dataset checks; the rest edit
    // the workflow document the agent is holding.
    { "fix_input_path", "spatial:understand", "workbench.datasetExperiment", "", "tool" },
    { "fix_schema", "", "", "", "author" },
    { "break_cycle", "", "", "", "author" },
    { "fill_params", "", "", "", "author" },
    { "rename_output", "", "", "", "author" },
    // Cartography quality repairs execute through the cartography repair
    // tool (the same loop harness map confirmation drives).
    { "add_source_note", "cartography:repair", "", "", "tool" },
    { "add_title", "cartography:repair", "", "", "tool" },
    // Plan-authoring actions: Pi edits the plan document it is holding.
    // Where a supporting lookup exists it is attached as the tool.
    { "rename_input", "", "", "", "author" },
    { "rename_step", "", "", "", "author" },
    { "set_operator", "spatial:search_capabilities", "", "", "tool" },
    { "search_capabilities", "spatial:search_capabilities", "", "", "tool" },
    { "fix_wiring", "", "", "", "author" },
    { "fix_outputs", "", "", "", "author" },
    { "fix_pins", "", "", "", "author" },
    { "set_verification", "", "", "", "author" },
    { "set_role", "", "", "", "author" },
    { "set_cleanup", "", "", "", "author" },
    // Legacy dotted spellings still emitted by older producers; mapped to the
    // canonical (colon) tool ids so old keys stay resolvable.
    { "harness.plan", "harness:plan", "", "", "tool" },
    { "harness.preflight", "harness:preflight", "", "", "tool" },
    { "harness.recipe_search", "harness:search_recipes", "", "", "tool" },
    { "harness:plan", "harness:plan", "", "", "tool" },
    { "resume_run", "harness:run_status", "", "", "tool" },
    { "spatial.understand", "spatial:understand", "", "", "tool" },
    { "temporal.preflight_collection", "temporal:preflight_collection", "", "", "tool" },
    { "project.search", "project:search", "", "", "tool" },
    { "spatial.workspace_summary", "spatial:workspace_summary", "", "", "tool" },
  };
  return kTable;
}

bool harnessActionKnown( const std::string &key )
{
  const auto &table = harnessActionTable();
  return std::any_of( table.begin(), table.end(),
                      [ &key ]( const HarnessActionSpec &spec ) { return spec.key == key; } );
}

std::string normalizeLabRole( const std::string &role )
{
  if ( role == "teacher" || role == "admin" )
    return role;
  return "student";
}

bool isStudentRole( const std::string &role )
{
  return normalizeLabRole( role ) == "student";
}

bool actionProducesArtifact( const std::string &key )
{
  // Explicitly artifact-producing keys, sorted. resume_run drives a run to
  // its completed (artifact-delivering) terminal state.
  static const char *const kArtifactKeys[] = { "resume_run" };
  for ( const char *candidate : kArtifactKeys )
    if ( key == candidate )
      return true;

  // Anything that resolves to the plan executor is artifact-producing by
  // construction, present or future — the classification cannot drift behind
  // new table rows.
  const auto &table = harnessActionTable();
  const auto it = std::find_if( table.begin(), table.end(),
                                [ &key ]( const HarnessActionSpec &spec ) { return spec.key == key; } );
  return it != table.end() && it->tool == "harness:execute_plan";
}

bool teachingGateBlocks( const TeachingContext &context, const std::string &key )
{
  return context.intentDomain == "lab" && isStudentRole( context.role ) &&
         actionProducesArtifact( key );
}

Json::Value resolvedSuggestedActionForRole( const std::string &key, Json::Value arguments,
                                            const TeachingContext &context )
{
  if ( teachingGateBlocks( context, key ) )
  {
    // Structural withholding: the resolution carries no tool and no
    // workbench command, so no dispatcher can execute it. The typed code
    // (TEACHING_REFUSAL) is what Pi reads — this is a contract refusal, not
    // a prose apology.
    Json::Value doc( Json::objectValue );
    doc["action"] = key;
    doc["arguments"] = arguments.isObject() ? std::move( arguments ) : Json::Value( Json::objectValue );
    doc["resolved"] = false;
    doc["withheld"] = true;
    doc["withheld_by"] = "teaching_constraint";
    doc["reason_code"] = "TEACHING_REFUSAL";
    return doc;
  }
  return resolvedSuggestedAction( key, std::move( arguments ) );
}

Json::Value resolvedSuggestedAction( const std::string &key, Json::Value arguments )
{
  Json::Value doc( Json::objectValue );
  doc["action"] = key;
  doc["arguments"] = arguments.isObject() ? std::move( arguments ) : Json::Value( Json::objectValue );

  const auto &table = harnessActionTable();
  const auto it = std::find_if( table.begin(), table.end(),
                                [ &key ]( const HarnessActionSpec &spec ) { return spec.key == key; } );
  if ( it == table.end() )
  {
    doc["resolved"] = false;
    return doc;
  }

  if ( !it->tool.empty() )
    doc["tool"] = it->tool;
  if ( !it->workbenchCommand.empty() )
    doc["workbench_command"] = it->workbenchCommand;
  doc["kind"] = it->kind.empty() ? std::string( "tool" ) : it->kind;
  if ( doc["arguments"].empty() && !it->defaultArgumentsJson.empty() )
  {
    Json::Value defaults;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( reader->parse( it->defaultArgumentsJson.data(),
                        it->defaultArgumentsJson.data() + it->defaultArgumentsJson.size(),
                        &defaults, &errors ) &&
         defaults.isObject() )
      doc["arguments"] = defaults;
  }
  doc["resolved"] = true;
  return doc;
}

} // namespace sicnu::agent::harness
