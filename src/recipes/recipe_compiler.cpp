// src/recipes/recipe_compiler.cpp
#include "recipes/recipe_compiler.h"

#include "recipes/provider_interfaces.h"
#include "recipes/scientific_recipe.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace sicnu::recipes {

namespace {

void addDiag( RecipeDiagnostics &diags, const char *code, DiagnosticSeverity severity,
              std::string message, std::string stageId = {}, std::string field = {} )
{
  diags.push_back( RecipeDiagnostic{ code, severity, std::move( stageId ),
                                     std::move( field ), std::move( message ) } );
}

/// lab id "lab02_spectral_analysis" → intent slug "spectral_analysis".
std::string intentFromLabId( const std::string &labId )
{
  std::string slug = labId;
  if ( slug.rfind( "lab", 0 ) == 0 )
  {
    // strip "labNN_" prefix (digits optional)
    std::size_t pos = 3;
    while ( pos < slug.size() && std::isdigit( static_cast<unsigned char>( slug[pos] ) ) )
      ++pos;
    if ( pos < slug.size() && slug[pos] == '_' )
      slug = slug.substr( pos + 1 );
  }
  return slug.empty() ? labId : slug;
}

/// Deterministic modality heuristic from id+title text (documented in the
/// schema: a coarse routing hint, not a scientific claim).
std::string inferModality( const LabDocument &lab )
{
  std::string hay = lab.id + " " + lab.title + " " + lab.titleZh + " " + lab.theme;
  std::transform( hay.begin(), hay.end(), hay.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  auto has = [&hay]( const char *kw ) { return hay.find( kw ) != std::string::npos; };
  if ( has( "sar" ) || has( "insar" ) ) return "sar";
  if ( has( "hyperspectral" ) || has( "高光谱" ) ) return "hyperspectral";
  if ( has( "temporal" ) || has( "phenology" ) || has( "时序" ) ) return "temporal";
  if ( has( "terrain" ) || has( "dem" ) || has( "地形" ) ) return "terrain";
  if ( has( "cartograph" ) || has( "制图" ) ) return "cartography";
  if ( has( "spectral" ) || has( "光谱" ) || has( "ndvi" ) || has( "fusion" ) ||
       has( "atmospheric" ) || has( "enhancement" ) || has( "pca" ) ||
       has( "mosaic" ) || has( "classification" ) || has( "obia" ) ||
       has( "georeferenc" ) || has( "change" ) )
    return "optical";
  return "general";
}

/// Lowercase alnum tokens (len>=3) from a title — en keyword set.
std::vector<std::string> englishKeywords( const LabDocument &lab )
{
  std::set<std::string> words;
  auto harvest = [&words]( const std::string &text )
  {
    std::string cur;
    for ( const unsigned char c : text )
    {
      if ( std::isalnum( c ) && c < 0x80 )
        cur.push_back( static_cast<char>( std::tolower( c ) ) );
      else if ( !cur.empty() )
      {
        if ( cur.size() >= 3 )
          words.insert( cur );
        cur.clear();
      }
    }
    if ( cur.size() >= 3 )
      words.insert( cur );
  };
  harvest( lab.title );
  harvest( intentFromLabId( lab.id ) );
  static const std::set<std::string> kStop = { "the", "and", "for", "with", "lab", "using" };
  std::vector<std::string> out;
  for ( const auto &w : words )
    if ( !kStop.count( w ) )
      out.push_back( w );
  return out;
}

/// zh keywords: glossary terms + title_zh (compact, human-meaningful).
Json::Value chineseKeywords( const LabDocument &lab )
{
  std::set<std::string> words;
  if ( !lab.titleZh.empty() )
    words.insert( lab.titleZh );
  for ( const auto &t : lab.glossaryTerms )
    if ( !t.empty() )
      words.insert( t );
  Json::Value out( Json::arrayValue );
  for ( const auto &w : words )
    out.append( w );
  return out;
}

/// True when `value` satisfies a param_ranges rule {min?,max?,values?}.
/// Returns failure reason ("" when ok).
std::string checkParamRange( const Json::Value &rule, const Json::Value &value )
{
  if ( rule.isMember( "values" ) && rule["values"].isArray() )
  {
    for ( const auto &allowed : rule["values"] )
      if ( allowed == value )
        return {};
    return "value not in allowed values list";
  }
  if ( value.isNumeric() )
  {
    const double v = value.asDouble();
    if ( rule.isMember( "min" ) && rule["min"].isNumeric() && v < rule["min"].asDouble() )
      return "below minimum " + std::to_string( rule["min"].asDouble() );
    if ( rule.isMember( "max" ) && rule["max"].isNumeric() && v > rule["max"].asDouble() )
      return "above maximum " + std::to_string( rule["max"].asDouble() );
  }
  return {};
}

/// Collect output-looking param values ("outputs/…") for artifact matching.
std::vector<std::string> outputParams( const Json::Value &params )
{
  std::vector<std::string> out;
  if ( !params.isObject() )
    return out;
  for ( const std::string &name : params.getMemberNames() )
    if ( params[name].isString() && params[name].asString().rfind( "outputs/", 0 ) == 0 )
      out.push_back( params[name].asString() );
  return out;
}

} // namespace

CompileResult compileLabToRecipe( const LabDocument &lab, const IOperatorCatalog &ops )
{
  CompileResult result;
  Json::Value recipe( Json::objectValue );
  recipe["schema"] = kRecipeSchemaId;
  recipe["recipe_id"] = recipeIdForLab( lab.id );
  recipe["title"] = lab.title;
  if ( !lab.titleZh.empty() )
    recipe["title_zh"] = lab.titleZh;

  // ---- goal pattern -------------------------------------------------------
  {
    Json::Value goal( Json::objectValue );
    goal["intent"] = intentFromLabId( lab.id );
    Json::Value en( Json::arrayValue );
    for ( const auto &w : englishKeywords( lab ) )
      en.append( w );
    goal["keywords_en"] = std::move( en );
    goal["keywords_zh"] = chineseKeywords( lab );
    goal["modality"] = inferModality( lab );
    recipe["goal_pattern"] = std::move( goal );
  }

  // ---- teaching origin ----------------------------------------------------
  {
    Json::Value origin( Json::objectValue );
    origin["kind"] = lab.format == LabFormat::D2 ? "labspec_d2" : "labspec_d3";
    origin["lab_id"] = lab.id;
    if ( lab.specVersion > 0 )
      origin["spec_version"] = lab.specVersion;
    if ( !lab.schemaTag.empty() )
      origin["source_schema"] = lab.schemaTag;
    if ( !lab.sourcePath.empty() )
      origin["source_path"] = lab.sourcePath;
    if ( !lab.sourceFingerprint.empty() )
      origin["source_fingerprint"] = lab.sourceFingerprint;
    if ( !lab.wrapperPath.empty() )
      origin["wrapper_path"] = lab.wrapperPath;
    origin["compiled_by"] = kCompilerId;
    if ( !lab.objective.empty() )
      origin["objective"] = lab.objective;
    if ( !lab.objectiveZh.empty() )
      origin["objective_zh"] = lab.objectiveZh;
    if ( !lab.theme.empty() )
      origin["theme"] = lab.theme;
    if ( !lab.audience.empty() )
      origin["audience"] = lab.audience;
    if ( lab.durationMinutes > 0 )
      origin["duration_minutes"] = lab.durationMinutes;
    Json::Value pk( Json::arrayValue );
    for ( const auto &k : lab.prerequisiteKnowledge )
      pk.append( k );
    if ( !pk.empty() )
      origin["prerequisite_knowledge"] = std::move( pk );
    Json::Value terms( Json::arrayValue );
    for ( const auto &t : lab.glossaryTerms )
      terms.append( t );
    if ( !terms.empty() )
      origin["glossary_terms"] = std::move( terms );
    Json::Value headings( Json::arrayValue );
    for ( const auto &h : lab.principleHeadings )
      headings.append( h );
    if ( !headings.empty() )
      origin["principle_headings"] = std::move( headings );
    Json::Value roles( Json::arrayValue );
    for ( const auto &r : lab.operatorRoles )
      roles.append( r );
    if ( !roles.empty() )
      origin["operator_roles"] = std::move( roles );
    recipe["teaching_origin"] = std::move( origin );
  }

  // ---- required assets ----------------------------------------------------
  {
    Json::Value assets( Json::arrayValue );
    int assetIndex = 0;
    for ( const auto &ref : lab.prerequisites )
    {
      if ( ref.path.empty() )
      {
        addDiag( result.diagnostics, diag_codes::kMissingAssetPath,
                 DiagnosticSeverity::Warning,
                 "prerequisite entry without a path was dropped" );
        continue;
      }
      Json::Value asset( Json::objectValue );
      asset["id"] = assetIndex == 0 ? "primary" : "asset_" + std::to_string( assetIndex );
      asset["kind"] = "file"; // honest default; executors refine from content
      Json::Value predicate( Json::objectValue );
      predicate["path"] = ref.path;
      predicate["must_exist"] = true;
      asset["predicate"] = std::move( predicate );
      if ( !ref.note.empty() )
        asset["note"] = ref.note;
      assets.append( std::move( asset ) );
      ++assetIndex;
    }
    recipe["required_assets"] = std::move( assets );
  }

  // ---- stages -------------------------------------------------------------
  {
    Json::Value stages( Json::arrayValue );
    std::set<std::string> usedIds;
    std::string previousId;
    int operatorCount = 0, humanCount = 0;

    for ( const auto &step : lab.steps )
    {
      Json::Value stage( Json::objectValue );
      std::string stageId = makeStageId( step.index, step.title, step.id );
      if ( usedIds.count( stageId ) )
      {
        const std::string base = stageId;
        int suffix = 2;
        while ( usedIds.count( stageId = base + "_" + std::to_string( suffix ) ) )
          ++suffix;
        addDiag( result.diagnostics, diag_codes::kDuplicateStageId,
                 DiagnosticSeverity::Warning,
                 "duplicate stage id renamed to " + stageId, stageId );
      }
      usedIds.insert( stageId );
      stage["id"] = stageId;
      stage["index"] = step.index;
      stage["title"] = step.title;
      if ( !step.titleZh.empty() && step.titleZh != step.title )
        stage["title_zh"] = step.titleZh;
      if ( !step.descriptionZh.empty() )
        stage["description_zh"] = step.descriptionZh;
      if ( !previousId.empty() )
        stage["depends_on"] = Json::Value( Json::arrayValue );
      if ( !previousId.empty() )
        stage["depends_on"].append( previousId );

      Json::Value hooks( Json::arrayValue );

      if ( step.hasOperator() )
      {
        stage["kind"] = stage_kinds::kOperator;
        stage["operator_id"] = step.operatorId;
        stage["params"] = step.params;
        ++operatorCount;

        if ( !ops.hasOperator( step.operatorId ) )
        {
          stage["operator_known"] = false;
          addDiag( result.diagnostics, diag_codes::kUnknownOperator,
                   DiagnosticSeverity::Warning,
                   "operator not in catalog: " + step.operatorId, stageId,
                   "operator_id" );
        }
        else if ( step.params.empty() )
        {
          addDiag( result.diagnostics, diag_codes::kEmptyParams,
                   DiagnosticSeverity::Info,
                   "operator step declares no params", stageId, "params" );
        }

        // Pedagogical param bounds → warnings when the authored step itself
        // violates them (authoring drift) — still compiled verbatim.
        if ( lab.paramRanges.isMember( step.operatorId ) &&
             lab.paramRanges[step.operatorId].isObject() )
        {
          const Json::Value &rules = lab.paramRanges[step.operatorId];
          for ( const std::string &param : rules.getMemberNames() )
            if ( step.params.isMember( param ) )
            {
              const std::string why = checkParamRange( rules[param], step.params[param] );
              if ( !why.empty() )
                addDiag( result.diagnostics, diag_codes::kParamOutOfRange,
                         DiagnosticSeverity::Warning,
                         "param '" + param + "': " + why, stageId, "params." + param );
            }
        }

        // artifact_exists hooks for outputs/… params (and when the path is a
        // declared expected artifact, kind is copied for the verifier).
        for ( const auto &out : outputParams( step.params ) )
        {
          Json::Value hook( Json::objectValue );
          hook["kind"] = hook_kinds::kArtifactExists;
          hook["target"] = out;
          for ( const auto &artifact : lab.expectedArtifacts )
            if ( artifact.path == out && !artifact.kind.empty() )
              hook["artifact_kind"] = artifact.kind;
          hooks.append( std::move( hook ) );
        }
      }
      else
      {
        stage["kind"] = stage_kinds::kHumanOnly;
        stage["human_only"] = true;
        ++humanCount;
        if ( step.isUiAction() )
        {
          stage["boundary"] = boundaries::kUiAction;
          stage["action"] = step.action;
          stage["reason"] = "UI-verb step (main-window slot '" + step.action +
                            "') — no headless equivalent compiled";
          addDiag( result.diagnostics, diag_codes::kUiActionStep,
                   DiagnosticSeverity::Info,
                   "step requires the interactive shell: action '" + step.action + "'",
                   stageId, "action" );
        }
        else
        {
          stage["boundary"] = boundaries::kManual;
          stage["reason"] = "manual step — no operator or UI action bound";
          addDiag( result.diagnostics, diag_codes::kManualStep,
                   DiagnosticSeverity::Info,
                   "step is manual (no operator_id/action)", stageId );
        }
      }

      if ( !step.teachingNote.empty() )
        stage["teaching_note"] = step.teachingNote;
      if ( !step.headlessNote.empty() )
        stage["headless_note"] = step.headlessNote;
      if ( !step.completionHint.empty() )
      {
        stage["completion_hint"] = step.completionHint;
        Json::Value hook( Json::objectValue );
        hook["kind"] = hook_kinds::kCompletionHint;
        hook["text"] = step.completionHint;
        hooks.append( std::move( hook ) );
      }
      if ( !hooks.empty() )
        stage["verifier_hooks"] = std::move( hooks );

      previousId = stageId;
      stages.append( std::move( stage ) );
    }

    // Reflection stages: thinking questions / D3 questions — human-only.
    int reflectionCount = 0;
    int qIndex = 0;
    for ( const auto &q : lab.questions )
    {
      if ( q.prompt.empty() )
      {
        addDiag( result.diagnostics, diag_codes::kMissingField,
                 DiagnosticSeverity::Info,
                 "question entry without a prompt was dropped", {},
                 "questions[]" );
        continue;
      }
      Json::Value stage( Json::objectValue );
      stage["id"] = "reflection_" + std::to_string( ++reflectionCount );
      stage["index"] = static_cast<int>( lab.steps.size() ) + qIndex;
      stage["kind"] = stage_kinds::kReflection;
      stage["human_only"] = true;
      stage["boundary"] = boundaries::kReflection;
      stage["prompt"] = q.prompt;
      if ( !q.hint.empty() )
        stage["hint"] = q.hint;
      if ( !previousId.empty() )
      {
        stage["depends_on"] = Json::Value( Json::arrayValue );
        stage["depends_on"].append( previousId );
      }
      previousId = stage["id"].asString();
      ++qIndex;
      stages.append( std::move( stage ) );
    }

    if ( lab.steps.empty() )
      addDiag( result.diagnostics, diag_codes::kNoSteps, DiagnosticSeverity::Error,
               "lab document has no executable steps (unresolved wrapper?)" );

    recipe["stages"] = std::move( stages );
    recipe["compilation"]["stages_total"] = static_cast<int>( lab.steps.size() ) + reflectionCount;
    recipe["compilation"]["stages_operator"] = operatorCount;
    recipe["compilation"]["stages_human_only"] = humanCount;
    recipe["compilation"]["stages_reflection"] = reflectionCount;
    const int nonReflection = operatorCount + humanCount;
    recipe["compilation"]["coverage"] =
      nonReflection > 0 ? static_cast<double>( operatorCount ) / nonReflection : 0.0;
  }

  // ---- preflight ------------------------------------------------------------
  {
    Json::Value preflight( Json::objectValue );
    std::set<std::string> requiredOps;
    for ( const auto &step : lab.steps )
      if ( step.hasOperator() )
        requiredOps.insert( step.operatorId );
    for ( const auto &role : lab.operatorRoles )
      requiredOps.insert( role.substr( 0, role.find( ' ' ) ) ); // "id — role"
    Json::Value opsArr( Json::arrayValue );
    for ( const auto &o : requiredOps )
      opsArr.append( o );
    preflight["required_operators"] = std::move( opsArr );

    Json::Value assetsArr( Json::arrayValue );
    for ( const auto &ref : lab.prerequisites )
      if ( !ref.path.empty() )
        assetsArr.append( ref.path );
    preflight["required_assets"] = std::move( assetsArr );

    if ( !lab.paramRanges.empty() )
      preflight["param_bounds"] = lab.paramRanges;

    recipe["preflight"] = std::move( preflight );
  }

  // ---- alternatives (param_ranges values lists) -----------------------------
  {
    Json::Value alternatives( Json::arrayValue );
    for ( const std::string &opId : lab.paramRanges.getMemberNames() )
    {
      const Json::Value &rules = lab.paramRanges[opId];
      if ( !rules.isObject() )
        continue;
      for ( const std::string &param : rules.getMemberNames() )
      {
        const Json::Value &rule = rules[param];
        if ( rule.isMember( "values" ) && rule["values"].isArray() &&
             rule["values"].size() > 1 )
        {
          Json::Value alt( Json::objectValue );
          alt["kind"] = "param_values";
          alt["operator_id"] = opId;
          alt["param"] = param;
          alt["choices"] = rule["values"];
          if ( rule.isMember( "note_zh" ) )
            alt["note_zh"] = rule["note_zh"];
          alternatives.append( std::move( alt ) );
        }
      }
    }
    if ( !alternatives.empty() )
      recipe["alternatives"] = std::move( alternatives );
  }

  // ---- evidence -------------------------------------------------------------
  {
    Json::Value evidence( Json::objectValue );
    if ( !lab.expectedArtifacts.empty() )
    {
      Json::Value artifacts( Json::arrayValue );
      for ( const auto &a : lab.expectedArtifacts )
      {
        Json::Value item( Json::objectValue );
        item["path"] = a.path;
        if ( !a.kind.empty() )
          item["kind"] = a.kind;
        if ( !a.noteZh.empty() )
          item["note_zh"] = a.noteZh;
        artifacts.append( std::move( item ) );
      }
      evidence["artifacts"] = std::move( artifacts );
    }
    if ( !lab.expectedResults.empty() )
    {
      Json::Value claims( Json::arrayValue );
      for ( const auto &e : lab.expectedResults )
      {
        Json::Value item( Json::objectValue );
        if ( !e.claim.empty() )
          item["claim"] = e.claim;
        if ( !e.artifact.empty() )
          item["artifact"] = e.artifact;
        if ( !e.toleranceNote.empty() )
          item["tolerance_note"] = e.toleranceNote;
        claims.append( std::move( item ) );
      }
      evidence["claims"] = std::move( claims );
    }
    if ( !lab.gradingRules.empty() )
      evidence["grading_rules"] = lab.gradingRules;
    if ( !lab.gradingPipeline.empty() )
      evidence["grading_pipeline"] = lab.gradingPipeline;
    if ( !lab.gradingIntentRef.empty() )
      evidence["grading_intent_ref"] = lab.gradingIntentRef;
    if ( !lab.dataSpecRef.empty() )
      evidence["data_spec"] = lab.dataSpecRef;
    if ( !lab.pipelineRunner.empty() )
      evidence["pipeline_runner"] = lab.pipelineRunner;
    if ( !evidence.empty() )
      recipe["evidence"] = std::move( evidence );
  }

  // ---- lab-scoped verifier hooks ------------------------------------------
  // Grading refs and expected_results claims are lab-level evidence, not
  // stage-scoped — they land on a top-level verifier_hooks array (same hook
  // schema as stage hooks). A verifier/grader consumes these to know what to
  // evaluate after the last stage; the recipe layer never executes them.
  {
    Json::Value hooks( Json::arrayValue );
    if ( !lab.gradingRules.empty() )
    {
      Json::Value hook( Json::objectValue );
      hook["kind"] = hook_kinds::kLabRules;
      hook["ref"] = lab.gradingRules;
      hooks.append( std::move( hook ) );
    }
    if ( !lab.gradingPipeline.empty() )
    {
      Json::Value hook( Json::objectValue );
      hook["kind"] = hook_kinds::kGradingPipeline;
      hook["ref"] = lab.gradingPipeline;
      hooks.append( std::move( hook ) );
    }
    for ( const auto &e : lab.expectedResults )
    {
      Json::Value hook( Json::objectValue );
      hook["kind"] = hook_kinds::kExpectedClaim;
      if ( !e.claim.empty() )
        hook["claim"] = e.claim;
      if ( !e.artifact.empty() )
        hook["artifact"] = e.artifact;
      if ( !e.toleranceNote.empty() )
        hook["tolerance_note"] = e.toleranceNote;
      hooks.append( std::move( hook ) );
      if ( !e.artifact.empty() )
      {
        Json::Value artifactHook( Json::objectValue );
        artifactHook["kind"] = hook_kinds::kArtifactExists;
        artifactHook["target"] = e.artifact;
        hooks.append( std::move( artifactHook ) );
      }
    }
    if ( !hooks.empty() )
      recipe["verifier_hooks"] = std::move( hooks );
  }

  recipe["compilation"]["diagnostics"] = Json::Value( Json::arrayValue );
  for ( const auto &d : result.diagnostics )
    recipe["compilation"]["diagnostics"].append( d.code );

  result.recipe = std::move( recipe );
  return result;
}

} // namespace sicnu::recipes
