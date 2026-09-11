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

/// Builds the wire document for a suggested action. Unknown keys are passed
/// through with "resolved": false (visible drift, no fabrication).
Json::Value resolvedSuggestedAction( const std::string &key, Json::Value arguments );

} // namespace sicnu::agent::harness
