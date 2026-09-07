// src/agent/harness/tool_manifest.h
#pragma once

//
// Harness 4.0 canonical tool manifest (mission Phases 1 and 14).
//
// Every agent-callable tool carries one manifest answering, without prose:
//   * stable id + taxonomy classification (tool_taxonomy.h)
//   * risk class (read_only | creates_artifact | modifies_project |
//     destructive | external_process | network | modifies_display)
//   * side effects / idempotency
//   * resource hints (memory policy, cost class, large-raster safety, GPU)
//   * cancellability
//   * preconditions (structured, from AgentMetadata prerequisites)
//   * expected artifacts (kind + persistence of what a successful run yields)
//
// Sources of truth: `AgentMetadata` for Processing tools (operators already
// declare this in code); a declarative table for the inline SpatialTool
// families. The manifest is derived, never hand-maintained twice: there is no
// second registry to drift.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

/// Closed risk vocabulary (Phase 14). Ordered by increasing blast radius;
/// the order is documentation, the strings are the contract.
namespace risk_classes {
inline constexpr const char *kReadOnly = "read_only";
inline constexpr const char *kModifiesDisplay = "modifies_display";
inline constexpr const char *kCreatesArtifact = "creates_artifact";
inline constexpr const char *kModifiesProject = "modifies_project";
inline constexpr const char *kDestructive = "destructive";
inline constexpr const char *kExternalProcess = "external_process";
inline constexpr const char *kNetwork = "network";
} // namespace risk_classes

bool isKnownRiskClass( const std::string &riskClass );

/// One expected artifact of a successful tool run.
/// `{name, kind, persistence}` — kind: "raster"|"vector"|"model"|"map"|
/// "report"|"collection"|"none"; persistence: "committed_asset"|
/// "project_file"|"sidecar"|"ephemeral".
struct ExpectedArtifact {
  std::string name;
  std::string kind;
  std::string persistence;
};

/// One precondition: `{check, description}` — `check` is a stable token
/// (e.g. "input_exists", "model_ready", "grid_compatible").
struct Precondition {
  std::string check;
  std::string description;
};

struct ToolManifest {
  std::string toolId;
  std::string taxonomy;   ///< "domain.action" (tool_taxonomy.h)
  std::string riskClass;  ///< risk_classes::* (default read_only)
  bool sideEffects = false;
  bool idempotent = true;
  bool cancellable = false;
  std::string memoryPolicy;    ///< "" when undeclared
  std::string determinismGrade;///< "" when undeclared
  std::string costClass;       ///< "" when undeclared
  bool largeRasterSafe = false;
  bool gpuAccelerated = false;
  bool producesProvenance = false;
  std::vector<ExpectedArtifact> expectedArtifacts;
  std::vector<Precondition> preconditions;

  /// Bounded wire shape. Never exceeds ~40 lines per tool — safe to embed in
  /// catalog entries and get_tool_schema responses.
  Json::Value toJson() const;
};

/// Manifest for an id when nothing better is known: taxonomy + risk table.
/// Used by catalog surfacing for tools whose owning subsystem did not declare
/// richer metadata.
ToolManifest manifestForToolId( const std::string &toolId );

/// Risk classification for a tool id (namespace table + explicit overrides
/// for the mutating inline tools). Total over the id space.
std::string riskClassForToolId( const std::string &toolId );

/// True when a tool id is in the explicit mutating-tools table (the ones an
/// agent must treat as state-changing even though the namespace reads quiet).
bool isKnownMutatingTool( const std::string &toolId );

/// Builds the bounded `harness` metadata block that catalog surfaces embed.
/// Same as manifestForToolId(id).toJson(); free function for call-site brevity.
Json::Value harnessBlockForToolId( const std::string &toolId );

} // namespace sicnu::agent::harness
