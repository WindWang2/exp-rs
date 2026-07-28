# ADR 0012: Atomic Algorithm Adapter & Agent Tool Calling Specification

- **Status**: Accepted
- **Date**: 2026-07-28
- **Authors**: SICNU GEO RS Architecture Team
- **Deciders**: AI Agent Integration Taskforce
- **Technical Story**: Issue #91 (Wayfinder Map: Atomizing Algorithm Modules & Agent Contract Standards)

---

## Context and Problem Statement

To enable LLM AI Agents to autonomously orchestrate remote sensing processing workflows, perform conversational tool calling, and interface with the Task Pipeline Visual Editor, algorithm modules in SICNU GEO RS must be atomized and standardized.

Previously:
1. `RSOperator` implementations provided parameters schema via `schema()` and metadata via `metadata()`, but lacked output port schemas and OpenAI/Qwen Tool Calling specification formats.
2. QGIS `QgsProcessingAlgorithm` and Python processing plugins used different parameter definition mechanisms (`QgsProcessingParameterDefinition`), creating a seam between algorithm providers and AI Agent tool invokers.
3. LLM AI Agents require rich semantic metadata (`purpose`, `prerequisites`, `workflowHints`, `limitations`) to accurately select tools and deduce parameter arguments without hallucination.

---

## Decision Drivers

- **Zero-Modification Adapter**: Must automatically wrap existing `RSOperator`, `QgsProcessingAlgorithm`, and Python plugin algorithms without modifying their core logic or breaking legacy callers.
- **Bi-directional JSON-Schema Validation**: Must generate valid JSON-Schema for both input parameters (`toInputSchema()`) and output datasets (`toOutputSchema()`) for upstream/downstream connection validation.
- **Native LLM Tool Call Export**: Must export algorithm descriptors directly into standard OpenAI / Qwen LLM Function Calling Format (`toToolCallDefinition()`).
- **GUI-Independent Core**: Must remain strictly inside `src/processing/framework/`, free of Qt GUI or QMainWindow dependencies.

---

## Technical Specification & Architecture

### 1. Unified Port Data Type Enum (`DataType`)

All algorithm ports (inputs and outputs) map to a strongly-typed `DataType` enum:

```cpp
namespace sicnu::processing {

enum class DataType {
  Any,          // Unconstrained payload
  Raster,       // Raster image file or layer path
  Vector,       // Vector shapefile/geojson/geopackage path
  Table,        // Attribute table or CSV file path
  Numeric,      // Floating-point numeric value
  Integer,      // Whole integer value
  String,       // Plain text string
  Boolean,      // Boolean flag (true/false)
  Enum,         // Enumerated string selection
  BoundingBox,  // Spatial extent [xmin, ymin, xmax, ymax]
  Crs,          // Coordinate Reference System EPSG/WKT
  Json          // Structured JSON object
};

} // namespace sicnu::processing
```

---

### 2. Port & Semantic Metadata Descriptors

```cpp
namespace sicnu::processing {

struct PortDescriptor {
  std::string name;
  std::string displayName;
  std::string description;
  DataType type = DataType::Any;
  bool required = true;
  std::string defaultValue;
  std::vector<std::string> enumOptions;

  Json::Value toJsonSchema() const;
};

struct AgentMetadata {
  std::string purpose;                    // Tool purpose & use case
  std::vector<std::string> tags;          // Vector search / indexing tags
  std::vector<std::string> prerequisites; // Preconditions (e.g. required bands, CRS)
  std::vector<std::string> workflowHints;// Pipeline hints (e.g. recommend atmospheric correction prior)
  std::vector<std::string> limitations;   // Known constraints & resource limits
  std::string llmPromptHint;             // LLM prompt guidance

  Json::Value toJson() const;
  static AgentMetadata fromJson( const Json::Value &val );
};

struct AlgorithmDescriptor {
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
```

---

### 3. OpenAI / LLM Tool Calling Export Format

`AlgorithmDescriptor::toToolCallDefinition()` automatically synthesizes high-density LLM system descriptions:

```json
{
  "type": "function",
  "function": {
    "name": "rs_spectral_index",
    "description": "Spectral Index: Derive vegetation, water, or built-up indices from imagery. 适用场景：Derive vegetation, water, or built-up indices from multispectral imagery. 前置条件：Input raster must have sufficient bands for the selected index.",
    "parameters": {
      "type": "object",
      "properties": {
        "input": {
          "type": "string",
          "x-ui-type": "raster",
          "description": "Input multi-band raster"
        },
        "index": {
          "type": "string",
          "enum": ["NDVI", "EVI", "SAVI", "NDWI", "NDBI", "MNDWI"],
          "description": "Spectral index to compute"
        }
      },
      "required": ["input", "index"]
    }
  }
}
```

---

### 4. Adapter Interface & Reflection Builder

```cpp
namespace sicnu::processing {

class AtomicAlgorithmAdapter {
public:
  virtual ~AtomicAlgorithmAdapter() = default;
  virtual std::string algorithmId() const = 0;
  virtual AlgorithmDescriptor descriptor() const = 0;
  virtual Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr ) = 0;
};

class AlgorithmDescriptorBuilder {
public:
  static AlgorithmDescriptor buildFromRsOperator( const operators::RSOperator &op );
};

} // namespace sicnu::processing
```

---

## Consequences

### Positive
- **LLM Tool Calling Ready**: Any registered C++ `RSOperator` or QGIS Processing algorithm can be exported directly to LLM context windows as function tool calls.
- **Workflow Pipeline Seam**: `AlgorithmDescriptor` input and output schemas seamlessly feed the Visual Workflow Editor (`PipelineScene` & `PipelineNodeItem`) for automatic port compatibility checks.
- **100% Tested**: Verified by `test_atomic_algorithm_adapter` (21 Catch2 assertions passed).

### Negative / Trade-offs
- Conversion from complex QGIS custom parameter widgets to simple JSON schema types abstracts away UI-specific widget logic (desirable for headless processing).
