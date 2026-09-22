// src/recipes/recipe_diagnostics.h
#pragma once

//
// RS14-20 typed diagnostic vocabulary for the LabSpec→ScientificRecipe
// compiler, validator and registry.
//
// Every non-compilable or suspicious element of a lab document produces one
// RecipeDiagnostic — nothing is silently dropped ("可解释失败" contract).
// Codes are a closed vocabulary: adding a code means appending here, never
// inventing a string at the call site.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::recipes {

/// Diagnostic severity: error blocks validation, warning is advisory, info
/// records an intentional boundary (e.g. a human-only step).
enum class DiagnosticSeverity { Error, Warning, Info };

const char *diagnosticSeverityString( DiagnosticSeverity severity );

/// Closed diagnostic-code vocabulary.
namespace diag_codes {
// --- compiler ---------------------------------------------------------------
/// Lab document carries no executable steps at all (e.g. a step-less v2
/// wrapper without a resolvable registry source).
inline constexpr const char *kNoSteps = "no_steps";
/// lab-registry entry exists but its `source` file is missing/invalid.
inline constexpr const char *kUnresolvedRegistrySource = "unresolved_registry_source";
/// Step references an operator id the IOperatorCatalog does not know.
inline constexpr const char *kUnknownOperator = "unknown_operator";
/// A param value violates the lab's own param_ranges (min/max/values).
inline constexpr const char *kParamOutOfRange = "param_out_of_range";
/// Manual step (no operator_id, no action) — human-only boundary marker.
inline constexpr const char *kManualStep = "manual_step";
/// UI-verb step (action, no operator_id) — human/agent-UI boundary marker.
inline constexpr const char *kUiActionStep = "ui_action_step";
/// A data prerequisite/asset has an empty or non-string path.
inline constexpr const char *kMissingAssetPath = "missing_asset_path";
/// Operator step with an empty params object (legal, but worth noting).
inline constexpr const char *kEmptyParams = "empty_params";
/// Document's spec_version/schema is not a recognized LabSpec contract.
inline constexpr const char *kUnrecognizedSpecVersion = "unrecognized_spec_version";
/// Two stages would share an id; the compiler renames deterministically.
inline constexpr const char *kDuplicateStageId = "duplicate_stage_id";
// --- validator ----------------------------------------------------------------
/// Recipe document schema field missing or wrong type.
inline constexpr const char *kMissingField = "missing_field";
/// `schema` is not "sicnu.scientific_recipe/1".
inline constexpr const char *kSchemaMismatch = "schema_mismatch";
/// `recipe_id` does not match the lab.<id> pattern.
inline constexpr const char *kBadRecipeId = "bad_recipe_id";
/// Stage kind outside the closed vocabulary.
inline constexpr const char *kBadStageKind = "bad_stage_kind";
/// Verifier-hook kind outside the closed vocabulary.
inline constexpr const char *kBadHookKind = "bad_hook_kind";
/// `depends_on` references an unknown or later stage id.
inline constexpr const char *kBadDependsOn = "bad_depends_on";
/// recipe_id duplicated inside a registry scan.
inline constexpr const char *kDuplicateRecipeId = "duplicate_recipe_id";
/// Unknown top-level or nested key (closed-schema discipline).
inline constexpr const char *kUnknownKey = "unknown_key";
/// teaching_origin block absent or incomplete on a compiled recipe.
inline constexpr const char *kMissingTeachingOrigin = "missing_teaching_origin";
} // namespace diag_codes

/// One typed finding. `stageId`/`field` locate the element; `message` is a
/// stable English diagnostic (localization is a presentation-layer concern).
struct RecipeDiagnostic
{
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Warning;
  std::string stageId;
  std::string field;
  std::string message;

  Json::Value toJson() const;
};

using RecipeDiagnostics = std::vector<RecipeDiagnostic>;

/// True when any diagnostic has severity Error.
bool hasErrors( const RecipeDiagnostics &diagnostics );

/// Stable `{code,severity,stage_id?,field?,message}` array — embedded into
/// compiled recipes under `compilation.diagnostics`.
Json::Value diagnosticsToJson( const RecipeDiagnostics &diagnostics );

/// One-line "code[severity] stage.field: message" strings for logs/reports.
std::vector<std::string> diagnosticStrings( const RecipeDiagnostics &diagnostics );

} // namespace sicnu::recipes
