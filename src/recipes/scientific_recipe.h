// src/recipes/scientific_recipe.h
#pragma once

//
// RS14-20 ScientificRecipe — the versioned, machine-readable "skill" artifact
// compiled from a lab (or authored by hand).
//
// Wire contract: `schema: "sicnu.scientific_recipe/1"`, recipe_id namespace
// `lab.<lab_id>` — deliberately disjoint from the harness_recipe namespace
// (`harness.*`, data/agent/recipes/). Normative JSON Schema:
// data/schemas/scientific_recipe.schema.json. The C++ validator
// (recipe_validator.cpp) is authoritative; the schema file mirrors it for
// external tooling.
//
// Recipes are declarative data: stages reference operator ids and verbatim
// param objects; nothing here executes anything.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::recipes {

/// Schema identifier (closed constant; bump → "sicnu.scientific_recipe/2").
inline constexpr const char *kRecipeSchemaId = "sicnu.scientific_recipe/1";
/// Compiler identity embedded in teaching_origin.compiled_by.
inline constexpr const char *kCompilerId = "sicnu.lab2recipe/1";
/// recipe_id must match `lab.` + [a-z0-9_]+ (the lab id verbatim).
inline constexpr const char *kRecipeIdPrefix = "lab.";

/// Stage kinds (closed vocabulary).
namespace stage_kinds {
/// Headless-executable operator invocation ({operator_id, params}).
inline constexpr const char *kOperator = "operator";
/// Requires a human (or a UI-driving agent): boundary field tells why
/// ("ui_action" | "manual" | "judgment").
inline constexpr const char *kHumanOnly = "human_only";
/// Reflection prompt (thinking question) — always human-only.
inline constexpr const char *kReflection = "reflection";
} // namespace stage_kinds

/// Verifier-hook kinds (closed vocabulary). Hooks are *descriptors*; the
/// grading/verifier layers evaluate them — this module never executes.
namespace hook_kinds {
/// An output artifact must exist at `target` (path) after the stage.
inline constexpr const char *kArtifactExists = "artifact_exists";
/// Soft evidence: the lab's authored completion hint text.
inline constexpr const char *kCompletionHint = "completion_hint";
/// Reference to a sicnu.lab.rules/1 grading-rules document (`ref` path).
inline constexpr const char *kLabRules = "lab_rules";
/// Reference to a grading/intent pipeline document (`ref` path).
inline constexpr const char *kGradingPipeline = "grading_pipeline";
/// D3 expected_results claim: `claim` text + `artifact` + `tolerance_note`.
inline constexpr const char *kExpectedClaim = "expected_claim";
} // namespace hook_kinds

/// human_only boundary reasons (closed vocabulary).
namespace boundaries {
inline constexpr const char *kUiAction = "ui_action";   ///< GUI-verb step
inline constexpr const char *kManual = "manual";        ///< hands-on non-UI step
inline constexpr const char *kReflection = "reflection";///< reflection stage
inline constexpr const char *kJudgment = "judgment";    ///< authored judgment call
} // namespace boundaries

/// True when `doc` is a JSON object carrying our schema id (cheap sniff —
/// full validation lives in the validator).
bool isScientificRecipe( const Json::Value &doc );

/// Canonical serialization: stable key order is jsoncpp's job (it sorts map
/// keys); we use the compact StyledWriter-free FastWriter replacement —
/// StreamWriterBuilder with 2-space indentation, UTF-8 passthrough — so
/// committed artifacts diff cleanly and compile runs are byte-reproducible.
std::string serializeRecipe( const Json::Value &recipe );

/// Parse JSON text; returns false on parse failure (error filled when given).
bool parseRecipeJson( const std::string &text, Json::Value &out, std::string *error = nullptr );

/// Deterministic stage id for a compiled stage: `s%02d_<slug>` where slug is
/// the ASCII-folded step title (or the D3 step id when present). Exported for
/// tests; the compiler is the only producer.
std::string makeStageId( int index, const std::string &title, const std::string &preferredId );

/// recipe_id for a lab id: `lab.<id>`.
std::string recipeIdForLab( const std::string &labId );

/// Semantic equivalence between two recipe documents — the contract a
/// compiled recipe shares with a hand-authored reference (and the drift gate
/// between a committed artifact and a fresh compile of the same lab).
///
/// Compared (order-sensitive where the domain is ordered):
///   recipe_id, goal_pattern.{intent,modality}, required asset paths (set),
///   stage kind sequence, per-stage operator_id + params (deep-equal),
///   human_only boundary sequence, reflection prompts (ordered),
///   verifier-hook kind+target+ref multiset per stage and at recipe scope,
///   evidence.{artifacts.paths, claims, grading_rules, grading_pipeline}.
/// Ignored on purpose: titles/notes/fingerprints (provenance, wording),
/// stage ids (authors may name differently), keyword lists (subsets allowed),
/// compilation stats. `diff` receives one English line per difference.
bool recipesEquivalent( const Json::Value &a, const Json::Value &b,
                        std::vector<std::string> *diff = nullptr );

} // namespace sicnu::recipes
