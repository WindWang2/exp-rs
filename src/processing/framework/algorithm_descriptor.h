#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::processing {

enum class DataType {
  Any,
  Raster,
  Vector,
  Table,
  Numeric,
  Integer,
  String,
  Boolean,
  Enum,
  BoundingBox,
  Crs,
  Json
};

std::string dataTypeToString( DataType type );
DataType dataTypeFromString( const std::string &typeStr );

/// Maps a file extension (lower-case, no dot) to a data type, e.g. "tif" →
/// Raster, "csv" → Table, "shp"/"geojson" → Vector, "json" → Json.
DataType dataTypeFromFileFormat( const std::string &format );

struct PortDescriptor
{
  std::string name;
  std::string displayName;
  std::string description;
  DataType type = DataType::Any;
  bool required = true;
  std::string defaultValue;
  std::vector<std::string> enumOptions;

  /// Numeric range constraints (present only when hasMinimum/hasMaximum).
  bool hasMinimum = false;
  double minimum = 0.0;
  bool hasMaximum = false;
  double maximum = 0.0;

  /// True when the port accepts/emits an array of items (e.g. band list).
  bool isArray = false;
  DataType itemType = DataType::Any;

  /// Concrete output file format for produced files ("tif", "csv", "shp",
  /// "json", ...). Empty when the port is not a produced file.
  std::string fileFormat;

  /// Optional machine-readable remote-sensing data contract ("x-rs-contract"):
  /// dataKind / bands / gridRelation / radiometricState / categorical / noData
  /// / crs etc. Consumed by preflight and Agent planning (not free-form prose).
  Json::Value rsContract;

  Json::Value toJsonSchema() const;
};

struct AgentMetadata
{
  std::string purpose;
  std::vector<std::string> tags;
  std::vector<std::string> prerequisites;
  std::vector<std::string> workflowHints;
  std::vector<std::string> limitations;
  std::string llmPromptHint;
  /// Large-raster memory policy ("streaming", "multipass_streaming",
  /// "full_raster", "external_process", "unsupported_for_large_raster").
  std::string memoryPolicy;
  /// Numeric reproducibility under parallel or blocked execution (ADR 0124):
  /// "bit_exact" or "tolerance". Distinct from `deterministic`, which marks
  /// stochastic kernels; the grade governs floating-point accumulation order
  /// for otherwise deterministic kernels.
  std::string determinismGrade;
  /// Declared execution-resource estimate: tileWidth/tileHeight/
  /// estimatedRamBytes/temporaryDiskBytes (0 = unknown). Empty object = none.
  Json::Value execution;

  // --- Structured planning hints (AgentMetadata 2.0, consumed by preflight) --
  /// Same inputs ⇒ same outputs (default true for deterministic kernels).
  bool deterministic = true;
  /// Modifies inputs or external state beyond producing outputs.
  bool sideEffects = false;
  /// Safe to re-run with identical parameters without changing the result.
  bool idempotent = false;
  /// Complexity class hint, e.g. "O(tile)", "O(histogram)", "O(width*height)",
  /// "O(bands^3)". Informational.
  std::string costClass;
  /// Safe to run on large rasters (memory policy is streaming/multipass).
  bool largeRasterSafe = false;
  /// Cooperative cancellation honored during execution (preflight advisory).
  bool supportsCancellation = false;
  /// Execution produces provenance/lineage records for its outputs.
  bool producesProvenance = false;
  /// When non-empty, this operator is a compatibility facade/alias whose
  /// underlying atomic primitives are named here (comma-separated). Agents use
  /// it to prefer composable primitives over monolithic selectors.
  std::string facadeOf;

  // --- Capability fields absorbed from the algorithm_meta sidecars (#707):
  // --- the descriptor is the single source of truth; sidecars remain sparse
  // --- optional overrides that must agree with these values.
  /// Task family ("segmentation", "classification", "inference", ...).
  std::string taskFamily;
  /// The algorithm can use GPU acceleration (per-model resolution may still
  /// land on CPU — this is capability, not requirement).
  bool gpuAccelerated = false;
  /// Tri-state marker: gpuAccelerated is AUTHORITATIVE only when the
  /// descriptor explicitly declared it (review P2 — otherwise "descriptor
  /// wins" degenerated to "default false wins" and silently flipped honest
  /// sidecar gpu:true entries).
  bool gpuDeclared = false;
  /// Optional benchmark accuracy in [0, 1]; negative = unreported.
  double accuracy = -1.0;
  /// Human/agent-facing selection guidance.
  std::string notes;

  Json::Value toJson() const;
  static AgentMetadata fromJson( const Json::Value &val );
};

struct AlgorithmDescriptor
{
  std::string id;
  std::string displayName;
  std::string group;
  std::string description;

  std::vector<PortDescriptor> inputs;
  std::vector<PortDescriptor> outputs;

  AgentMetadata agentMetadata;

  Json::Value toInputSchema() const;
  Json::Value toOutputSchema() const;
  Json::Value toToolCallDefinition() const;
};

} // namespace sicnu::processing
