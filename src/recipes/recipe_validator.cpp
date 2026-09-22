// src/recipes/recipe_validator.cpp
#include "recipes/recipe_validator.h"

#include "recipes/scientific_recipe.h"

#include <algorithm>
#include <set>

namespace sicnu::recipes {

namespace {

void err( RecipeDiagnostics &diags, const char *code, const std::string &message,
          const std::string &field = {}, const std::string &stageId = {} )
{
  diags.push_back( RecipeDiagnostic{ code, DiagnosticSeverity::Error, stageId, field, message } );
}

void warn( RecipeDiagnostics &diags, const char *code, const std::string &message,
           const std::string &field = {}, const std::string &stageId = {} )
{
  diags.push_back( RecipeDiagnostic{ code, DiagnosticSeverity::Warning, stageId, field, message } );
}

const std::set<std::string> &rootKeys()
{
  static const std::set<std::string> keys = {
    "schema", "recipe_id", "title", "title_zh", "description",
    "goal_pattern", "teaching_origin", "required_assets", "stages",
    "verifier_hooks", "preflight", "alternatives", "evidence", "compilation"
  };
  return keys;
}

const std::set<std::string> &stageKeys()
{
  static const std::set<std::string> keys = {
    "id", "index", "kind", "title", "title_zh", "description_zh",
    "operator_id", "params", "operator_known", "action", "boundary",
    "human_only", "reason", "prompt", "hint", "teaching_note",
    "headless_note", "completion_hint", "depends_on", "verifier_hooks"
  };
  return keys;
}

const std::set<std::string> &hookKeys()
{
  static const std::set<std::string> keys = {
    "kind", "target", "artifact_kind", "text", "ref", "claim", "artifact",
    "tolerance_note"
  };
  return keys;
}

bool isStageKind( const std::string &kind )
{
  return kind == stage_kinds::kOperator || kind == stage_kinds::kHumanOnly ||
         kind == stage_kinds::kReflection;
}

bool isHookKind( const std::string &kind )
{
  return kind == hook_kinds::kArtifactExists || kind == hook_kinds::kCompletionHint ||
         kind == hook_kinds::kLabRules || kind == hook_kinds::kGradingPipeline ||
         kind == hook_kinds::kExpectedClaim;
}

bool isBoundary( const std::string &b )
{
  return b == boundaries::kUiAction || b == boundaries::kManual ||
         b == boundaries::kReflection || b == boundaries::kJudgment;
}

/// "lab." + [a-z0-9_]+
bool validRecipeId( const std::string &id )
{
  if ( id.rfind( kRecipeIdPrefix, 0 ) != 0 )
    return false;
  const std::string rest = id.substr( 4 );
  if ( rest.empty() )
    return false;
  return std::all_of( rest.begin(), rest.end(), []( char c ) {
    return ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
  } );
}

void checkUnknownKeys( const Json::Value &obj, const std::set<std::string> &allowed,
                       const std::string &where, RecipeDiagnostics &diags )
{
  if ( !obj.isObject() )
    return;
  for ( const std::string &key : obj.getMemberNames() )
    if ( !allowed.count( key ) )
      warn( diags, diag_codes::kUnknownKey, "unknown key '" + key + "' in " + where, key );
}

/// Shared hook-array check for stage-level and lab-level verifier_hooks.
void checkHooks( const Json::Value &stage, const std::string &ctx,
                 const std::string &stageId, RecipeDiagnostics &diags )
{
  if ( !stage.isMember( "verifier_hooks" ) )
    return;
  if ( !stage["verifier_hooks"].isArray() )
  {
    err( diags, diag_codes::kBadHookKind,
         ctx + ": " + stageId + " verifier_hooks must be an array",
         "verifier_hooks", stageId );
    return;
  }
  for ( const auto &hook : stage["verifier_hooks"] )
  {
    if ( !hook.isObject() || !isHookKind( hook.get( "kind", "" ).asString() ) )
      err( diags, diag_codes::kBadHookKind,
           ctx + ": " + stageId + " hook with unknown kind", "verifier_hooks",
           stageId );
    else
      checkUnknownKeys( hook, hookKeys(), ctx + " " + stageId + " hook", diags );
  }
}

} // namespace

RecipeDiagnostics validateRecipe( const Json::Value &recipe )
{
  RecipeDiagnostics diags;
  if ( !recipe.isObject() )
  {
    err( diags, diag_codes::kMissingField, "recipe document must be an object" );
    return diags;
  }

  const std::string id = recipe.get( "recipe_id", "" ).asString();
  const std::string ctx = id.empty() ? "recipe" : "recipe " + id;

  if ( recipe.get( "schema", "" ).asString() != kRecipeSchemaId )
    err( diags, diag_codes::kSchemaMismatch,
         ctx + ": schema must be '" + kRecipeSchemaId + "'", "schema" );
  if ( id.empty() || !validRecipeId( id ) )
    err( diags, diag_codes::kBadRecipeId,
         ctx + ": recipe_id must match 'lab.<id>'", "recipe_id" );
  if ( !recipe["title"].isString() || recipe["title"].asString().empty() )
    err( diags, diag_codes::kMissingField, ctx + ": missing title", "title" );

  checkUnknownKeys( recipe, rootKeys(), ctx, diags );

  // ---- goal_pattern ---------------------------------------------------------
  if ( !recipe.isMember( "goal_pattern" ) || !recipe["goal_pattern"].isObject() )
    err( diags, diag_codes::kMissingField, ctx + ": missing goal_pattern", "goal_pattern" );
  else if ( !recipe["goal_pattern"]["intent"].isString() ||
            recipe["goal_pattern"]["intent"].asString().empty() )
    err( diags, diag_codes::kMissingField, ctx + ": goal_pattern.intent required",
         "goal_pattern.intent" );

  // ---- teaching_origin ------------------------------------------------------
  if ( !recipe.isMember( "teaching_origin" ) || !recipe["teaching_origin"].isObject() )
    warn( diags, diag_codes::kMissingTeachingOrigin,
          ctx + ": no teaching_origin — recipe is not traceable to a lab", "teaching_origin" );
  else
  {
    const Json::Value &origin = recipe["teaching_origin"];
    if ( !origin["lab_id"].isString() || origin["lab_id"].asString().empty() )
      warn( diags, diag_codes::kMissingTeachingOrigin,
            ctx + ": teaching_origin.lab_id missing", "teaching_origin.lab_id" );
  }

  // ---- stages ---------------------------------------------------------------
  if ( !recipe.isMember( "stages" ) || !recipe["stages"].isArray() ||
       recipe["stages"].empty() )
    err( diags, diag_codes::kMissingField, ctx + ": stages must be a non-empty array",
         "stages" );
  else
  {
    std::set<std::string> seen;
    bool hasOperator = false;
    for ( const auto &stage : recipe["stages"] )
    {
      if ( !stage.isObject() )
      {
        err( diags, diag_codes::kMissingField, ctx + ": stage must be an object", "stages" );
        continue;
      }
      const std::string sid = stage.get( "id", "" ).asString();
      const bool sidFresh = !sid.empty() && !seen.count( sid );
      if ( sid.empty() )
        err( diags, diag_codes::kMissingField, ctx + ": stage without id", "stages[].id" );
      else if ( !sidFresh )
        err( diags, diag_codes::kDuplicateStageId, ctx + ": duplicate stage id " + sid,
             "stages[].id", sid );

      const std::string kind = stage.get( "kind", "" ).asString();
      if ( !isStageKind( kind ) )
        err( diags, diag_codes::kBadStageKind,
             ctx + ": stage " + sid + " has unknown kind '" + kind + "'", "kind", sid );

      checkUnknownKeys( stage, stageKeys(), ctx + " stage " + sid, diags );

      if ( kind == stage_kinds::kOperator )
      {
        hasOperator = true;
        if ( !stage["operator_id"].isString() || stage["operator_id"].asString().empty() )
          err( diags, diag_codes::kMissingField,
               ctx + ": operator stage " + sid + " missing operator_id", "operator_id", sid );
      }
      if ( kind == stage_kinds::kHumanOnly || stage.get( "human_only", false ).asBool() )
      {
        if ( stage.isMember( "boundary" ) && !isBoundary( stage["boundary"].asString() ) )
          err( diags, diag_codes::kBadStageKind,
               ctx + ": stage " + sid + " unknown boundary '" +
                 stage["boundary"].asString() + "'",
               "boundary", sid );
      }
      if ( kind == stage_kinds::kReflection &&
           ( !stage["prompt"].isString() || stage["prompt"].asString().empty() ) )
        err( diags, diag_codes::kMissingField,
             ctx + ": reflection stage " + sid + " missing prompt", "prompt", sid );

      // depends_on must reference strictly earlier stage ids (self/forward
      // references are both invalid — sid is only inserted after this check).
      if ( stage.isMember( "depends_on" ) )
      {
        if ( !stage["depends_on"].isArray() )
          err( diags, diag_codes::kBadDependsOn,
               ctx + ": stage " + sid + " depends_on must be an array", "depends_on", sid );
        else
          for ( const auto &dep : stage["depends_on"] )
            if ( !dep.isString() || !seen.count( dep.asString() ) )
              err( diags, diag_codes::kBadDependsOn,
                   ctx + ": stage " + sid + " depends on unknown/later stage '" +
                     dep.asString() + "'",
                   "depends_on", sid );
      }
      if ( !sid.empty() )
        seen.insert( sid );

      checkHooks( stage, ctx, "stage " + sid, diags );
    }
    if ( !hasOperator )
      warn( diags, diag_codes::kNoSteps,
            ctx + ": recipe has no operator stages — nothing agent-executable",
            "stages" );
  }

  // ---- lab-scoped hooks (same shape as stage hooks) --------------------------
  checkHooks( recipe, ctx, "recipe", diags );

  // ---- preflight --------------------------------------------------------------
  if ( recipe.isMember( "preflight" ) )
  {
    const Json::Value &pf = recipe["preflight"];
    if ( pf.isMember( "required_operators" ) && !pf["required_operators"].isArray() )
      err( diags, diag_codes::kMissingField,
           ctx + ": preflight.required_operators must be an array",
           "preflight.required_operators" );
    if ( pf.isMember( "required_assets" ) && !pf["required_assets"].isArray() )
      err( diags, diag_codes::kMissingField,
           ctx + ": preflight.required_assets must be an array",
           "preflight.required_assets" );
  }

  // ---- compilation ------------------------------------------------------------
  if ( recipe.isMember( "compilation" ) && recipe["compilation"].isObject() &&
       recipe["compilation"].isMember( "diagnostics" ) &&
       recipe["compilation"]["diagnostics"].isArray() )
  {
    for ( const auto &code : recipe["compilation"]["diagnostics"] )
      if ( code.isString() && code.asString() == diag_codes::kNoSteps )
        err( diags, diag_codes::kNoSteps,
             ctx + ": compiled with no_steps error — recipe is a stub", "compilation" );
  }

  return diags;
}

bool recipeIsValid( const Json::Value &recipe, RecipeDiagnostics *diags )
{
  RecipeDiagnostics local = validateRecipe( recipe );
  if ( diags )
    *diags = local;
  return !hasErrors( local );
}

} // namespace sicnu::recipes
