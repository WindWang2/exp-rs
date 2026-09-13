// src/agent/harness/harness_actions.h
#pragma once

//
// Harness 9.0 (issue #881): the closed suggested-action vocabulary.
//
// Every action the harness attaches to a blocker/warning/advice MUST resolve
// to a surface the caller can actually invoke:
//   * `tool`              — a registered SpatialTool id (agent-executable), or
//   * `workbench_command` — a registered workbench command id (UI surface).
// An action key with NEITHER resolution must not exist; the drift floor in
// tests/test_harness9_contracts.cpp cross-checks the table against the live
// SpatialToolRegistry and the workbench command definitions.
//
// Wire shape (issued by resolvedSuggestedAction / suggestedAction):
//   { "action": <stable key>, "arguments": {...}, "kind": "tool"|"workbench"|"author",
//     "tool": <id>?, "workbench_command": <id>?, "resolved": bool }
// `action` keeps the historical key so diagnostics/help lookups stay stable;
// `tool`/`workbench_command` are what a dispatcher executes, and "author"
// actions instruct Pi to edit the plan document it is holding. Unknown keys
// pass through with "resolved": false — drift is visible on the wire, never
// silently invented.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

struct HarnessActionSpec
{
    std::string key;              ///< stable semantic identifier
    std::string tool;             ///< registered tool id, empty when none
    std::string workbenchCommand; ///< registered command id, empty when none
    std::string defaultArgumentsJson; ///< pre-filled tool arguments (JSON object text)
    /// "tool" — invoke the tool; "workbench" — open the UI surface;
    /// "author" — Pi edits the plan document it is holding. Empty = "tool".
    std::string kind;
};

/// The closed action table (sorted by key; deterministic order for drift).
const std::vector<HarnessActionSpec> &harnessActionTable();

/// True when `key` is part of the vocabulary.
bool harnessActionKnown( const std::string &key );

//
// D9: the teaching constraint (hard, structural — not a prompt).
//
// In the lab intent domain the harness is a tutor: it may diagnose, hint,
// and explain, but it must never put a finished artifact into a student's
// hands (宁可少帮，不可代做). The enforcement point is HERE, in the closed
// action vocabulary: when the requester is a student, artifact-producing
// keys do not resolve. There is no code path that hands the underlying
// tool/workbench surface across the gate — a bypass attempt surfaces as a
// typed TEACHING_REFUSAL (see harness_error.h), not a soft apology.
//

/// Teaching roles for the lab domain. "teacher" and "admin" share full
/// reach; everything else — including an empty string — degrades to
/// "student", the safe default. Role is session state supplied by the
/// caller; it is NEVER parsed from message content.
std::string normalizeLabRole( const std::string &role );

/// True for "student" (and anything that normalized to it).
bool isStudentRole( const std::string &role );

/// Who/where a suggested action is being resolved for.
struct TeachingContext
{
    /// "lab" while serving the teaching surface; anything else leaves the
    /// gate inert (research flows resolve exactly as before).
    std::string intentDomain;
    /// Session role, already normalized or raw ("" → student).
    std::string role;
};

/// True when resolving `key` would put a final artifact / a completed run
/// into the requester's hands — the "do it for them" surface. Closed
/// classification: a key produces artifacts when it is explicitly listed or
/// when its resolved tool drives plan execution.
bool actionProducesArtifact( const std::string &key );

/// The gate itself: student + lab domain + artifact action ⇒ withheld.
bool teachingGateBlocks( const TeachingContext &context, const std::string &key );

/// Role-gated twin of resolvedSuggestedAction. For a blocked resolution the
/// returned document carries the action key (for auditability) plus
///   resolved: false, withheld: true, withheld_by: "teaching_constraint",
///   reason_code: "TEACHING_REFUSAL"
/// and NO tool / workbench_command surface. Unblocked inputs resolve exactly
/// like resolvedSuggestedAction.
Json::Value resolvedSuggestedActionForRole( const std::string &key, Json::Value arguments,
                                            const TeachingContext &context );

/// Builds the wire document for a suggested action. Unknown keys are passed
/// through with "resolved": false (visible drift, no fabrication).
Json::Value resolvedSuggestedAction( const std::string &key, Json::Value arguments );

} // namespace sicnu::agent::harness
