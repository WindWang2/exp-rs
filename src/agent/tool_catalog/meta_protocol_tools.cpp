// src/agent/tool_catalog/meta_protocol_tools.cpp
//
// The meta tool table itself. Moved byte-for-byte from mcp_server.cpp —
// names, descriptions, property types, and required-input sets are the
// client protocol contract (ADR 0022) and must not drift during the move.

#include "meta_protocol_tools.h"

#include <stdexcept>

namespace sicnu::agent::tool_catalog::meta_protocol {

namespace {

const std::vector<MetaToolDef> &table()
{
    static const std::vector<MetaToolDef> kMetaTools = {
        { "list_algorithms",
          "List available remote sensing and GIS processing algorithms (canonical "
          "catalog: rs: operators + provider algorithms). Compact discovery layer — "
          "id, name, group, tags, outputs, memory policy. Paginated: pass "
          "cursor=nextCursor until it is -1. Use get_algorithm_schema for the full "
          "parameter schema of a specific algorithm.",
          { { "limit", "integer", "Page size (1-500, default 50).", false },
            { "cursor", "integer", "Offset from the previous page's nextCursor (default 0).", false } } },
        { "search_algorithms",
          "Search/filter the canonical algorithm catalog by free text, group, tag, "
          "purpose text, task family, modality, input or output type, or large-raster "
          "safety. All filters match declared descriptor metadata (AND across fields, "
          "comma lists are ANY-of, case-insensitive). Returns the same compact entries "
          "as list_algorithms; on zero hits the result carries a 'hints' object with "
          "the declared filter vocabulary and closest id suggestions.",
          { { "query", "string", "Free-text filter; space-separated tokens are ANDed and matched (case-insensitive) against id, name, group, tags, purpose and description. Empty = no text filter.", false },
            { "group", "string", "Exact group filter (e.g. 'spectral', 'change detection'). Optional.", false },
            { "tag", "string", "Tag filter, comma-separated for ANY-of (e.g. 'sar' or 'sar,insar'); matches declared algorithm tags exactly. Optional.", false },
            { "purpose", "string", "Substring filter on the declared agent purpose text. Optional.", false },
            { "task", "string", "Exact task-family filter (e.g. 'preprocess', 'spectral', 'temporal', 'classification'). Optional.", false },
            { "modality", "string", "Modality filter, comma-separated for ANY-of (e.g. 'optical', 'sar', 'dem'); matches declared input-port contracts. Optional.", false },
            { "input_type", "string", "Input data type filter — exact, case-insensitive (Raster/Vector/Table/Numeric/Integer/String/Boolean/Enum/BoundingBox/Crs/Json). Optional.", false },
            { "output_type", "string", "Output data type filter — same vocabulary as input_type. Optional.", false },
            { "large_raster_safe", "boolean", "When true, only streaming/multipass operators. Optional.", false },
            { "limit", "integer", "Page size (1-500, default 50).", false },
            { "cursor", "integer", "Offset from the previous page's nextCursor (default 0).", false } } },
        { "get_algorithm_schema",
          "Get the detailed input parameter JSON Schema, real output ports, and agent "
          "metadata for a specific algorithm.",
          { { "algorithm_id", "string", "Unique ID of the algorithm, e.g., 'rs:spectral_index'", true } } },
        { "preflight_algorithm",
          "Validate parameters and dataset compatibility WITHOUT executing: schema "
          "validation, raster dataset probes (size/bands/CRS/radiometric state), "
          "same-grid/CRS/band/radiometric checks, and a dynamic resource (RAM) "
          "estimate. Use before execute_algorithm to plan a run.",
          { { "algorithm_id", "string", "ID of the algorithm to preflight", true },
            { "parameters", "object", "Planned parameter name-value pairs", false } } },
        { "execute_algorithm",
          "Asynchronously run a processing algorithm with the specified parameters.",
          { { "algorithm_id", "string", "ID of the algorithm to execute", true },
            { "parameters", "object", "Parameter name-value pairs for the algorithm", false } } },
        { "get_execution_status",
          "Get progress, execution status, results, and the committed asset id of an "
          "ongoing or completed algorithm execution.",
          { { "execution_id", "string", "The execution ID returned by execute_algorithm", true } } },
        { "cancel_execution",
          "Cancel an actively running algorithm execution.",
          { { "execution_id", "string", "The execution ID of the run to cancel", true } } },
        { "list_operators",
          "List registered RSOperator algorithms (legacy Agent surface: rs:/opencv:/gdal:/otb:). "
          "Paginated: pass cursor=nextCursor until it is -1.",
          { { "limit", "integer", "Page size (1-500, default 50).", false },
            { "cursor", "integer", "Offset from the previous page's nextCursor (default 0).", false } } },
        { "get_operator_schema",
          "Get JSON Schema and metadata for an RSOperator (e.g. 'rs:spectral_index').",
          { { "operator_id", "string", "Operator id, e.g. 'rs:spectral_index'", true } } },
        { "execute_operator",
          "Asynchronously run an RSOperator with JSON parameters. Returns execution_id.",
          { { "operator_id", "string", "Operator id to execute", true },
            { "parameters", "object", "JSON parameter object", false } } },
        { "list_layers",
          "List all raster and vector layers loaded in the current QGIS project.",
          {} },
        { "describe_dataset",
          "Get detailed layer metadata, including spatial extent, coordinate reference system (CRS), and band/field details.",
          { { "layer_id", "string", "Name or ID of the layer to describe", true } } },
        { "get_lineage",
          "Query a Data Manager asset's processing provenance and lineage: the "
          "deriving algorithm + parameters when the asset was produced, its input "
          "assets (derivedFrom), and any assets derived from it (derivedOutputsOf).",
          { { "asset_id", "string", "Data Manager asset id (UUID) to query", true } } },
        { "list_interaction_tools",
          "List all interactive GIS tools (view controls, layer navigation, canvas ROI).",
          {} },
        { "get_interaction_schema",
          "Get JSON Schema and parameters for an interaction tool (e.g. 'view:set_extent', 'roi:set').",
          { { "tool_name", "string", "Interaction tool name (e.g. 'view:get_state', 'view:set_extent', 'roi:set')", true } } },
        { "list_tools",
          "List all unified agent tools (Processing algorithms, Interaction/Canvas tools, Data tools). "
          "Returns category, name, and description per tool; input schemas are omitted by default "
          "(pull a candidate's full schema via get_tool_schema). Pass compact=false to embed every schema.",
          { { "category", "string", "Optional category filter: 'Processing', 'Interaction', 'Data', 'Custom'.", false },
            { "compact", "boolean", "Set false to embed per-entry input schemas (default: omitted).", false },
            { "limit", "integer", "Max entries per page (offset pagination). 0 = all. Optional.", false },
            { "cursor", "integer", "Offset of the first entry to return; pass nextCursor from the previous page. Optional.", false } } },
        { "search_tools",
          "Search unified agent tools by free text (e.g. 'show raster', 'roi', 'spectral'), group, tag, "
          "input/output type, or capability facets (task family, modality, band roles, temporal, "
          "deterministic, large-raster safety, GPU, memory policy, cost class). Ranked by relevance; "
          "pull a candidate's full schema via get_tool_schema.",
          { { "query", "string", "Free-text filter matched against name, group, purpose, tags, and description.", false },
            { "group", "string", "Exact or substring group filter. Optional.", false },
            { "tag", "string", "Tag filter. Optional.", false },
            { "input_type", "string", "Input data type filter. Optional.", false },
            { "output_type", "string", "Output data type filter. Optional.", false },
            { "task", "string", "Task-family facet, e.g. 'classification', 'temporal', 'inference'. Optional.", false },
            { "modality", "string", "Modality facet (matched against the tool's rs-contract dataKind/group), e.g. 'optical'. Optional.", false },
            { "band_roles", "string", "Comma-separated band roles ('red,nir'); matches when any port declares any role. Optional.", false },
            { "temporal", "boolean", "Temporal-capability facet (task family or group carries temporal). Optional.", false },
            { "deterministic", "boolean", "Determinism facet (same inputs imply same outputs). Optional.", false },
            { "gpu", "boolean", "GPU-acceleration-capable facet. Optional.", false },
            { "memory_policy", "string", "Exact memory-policy facet: 'streaming', 'multipass_streaming', 'full_raster', ... Optional.", false },
            { "cost_class", "string", "Cost-class facet substring, e.g. 'O(tile)'. Optional.", false },
            { "large_raster_safe", "boolean", "Restrict to streaming/multipass tools safe for large rasters. Optional.", false },
            { "compact", "boolean", "Set false to embed per-entry input schemas (default: omitted).", false },
            { "limit", "integer", "Max entries per page (offset pagination). 0 = all. Optional.", false },
            { "cursor", "integer", "Offset of the first entry to return; pass nextCursor from the previous page. Optional.", false } } },
        { "get_tool_schema",
          "Get parameter JSON Schema and metadata for any registered tool in the unified Agent Tool Catalog.",
          { { "tool_id", "string", "Unique ID of the tool, e.g. 'rs:spectral_index', 'canvas:draw_roi', 'data:list_layers'", true } } },
        { "run_workflow",
          "Submit an agent-generated spatial workflow (DAG) as pipeline JSON and execute it "
          "through the Task Center: steps reference registered operators (e.g. "
          "'rs:spectral_index'), connections declare the execution order, and upstream "
          "outputs flow into downstream inputs. Returns the pipeline id and one "
          "execution_id per step — poll them with get_execution_status or use "
          "get_workflow_status for the aggregate view.",
          { { "pipeline", "object", "Pipeline definition: {id, name, steps: [{id, title, operator, params, inputs: [{fromStepId, fromPort, toPort}]}]}. A JSON string is also accepted.", true },
            { "auto_load", "boolean", "Auto-load finished outputs as layers (headless MCP default false).", false },
            { "experiment_db", "string", "Optional. Path of an ExperimentStore database; when set, this run is auto-recorded into it as a truthful experiment run (created/running/completed/failed/cancelled/interrupted) through the authoritative workflow lifecycle. Requires experiment_id. Recording binds to THIS submission only.", false },
            { "experiment_id", "string", "Target experiment id for auto-recording (created if missing). Required with experiment_db.", false },
            { "experiment_name", "string", "Name used when auto-recording creates the experiment. Optional.", false },
            { "experiment_objective", "string", "Research question recorded for the experiment. Optional.", false },
            { "dataset_db", "string", "Optional DatasetStore path for pin verification (dataset fingerprint/split fingerprint auto-fill).", false },
            { "dataset_version", "string", "Optional dataset version id pin (must exist in dataset_db when provided).", false },
            { "split_manifest", "string", "Optional split manifest id pin.", false },
            { "model_id", "string", "Optional model id pin.", false },
            { "model_digest", "string", "Optional model content digest pin.", false },
            { "seed", "integer", "Optional seed pin (non-negative).", false } } },
        { "get_workflow_status",
          "Get the aggregate status of a workflow submitted with run_workflow: overall "
          "state plus per-step execution ids, statuses, and progress.",
          { { "pipeline_id", "integer", "Pipeline id returned by run_workflow.", true } } },
        { "resume_workflow",
          "Resume an interrupted workflow run from its last checkpoint: steps that "
          "already completed are skipped and the run continues from the first "
          "non-terminal step. The run_id comes from get_workflow_status "
          "(run_state = interrupted) after a crash or cancellation. Returns the new "
          "pipeline id for status polling.",
          { { "run_id", "string", "Workflow run id of the interrupted run.", true } } },
        // Surface-11 (E): large-result handle. Tool results never inline raw
        // arrays beyond the bounded envelope; agents read file artifacts
        // through this paged, digested reader instead.
        // Surface-11 parity fix: get_tool_help was always dispatchable via
        // tools/call but absent from every tools/list — the exact drift the
        // union projection exists to eliminate. Appended (wire order of the
        // pre-existing rows is unchanged).
        { "get_tool_help",
          "Get bounded help for a tool: char-capped summary, key parameters, "
          "and related diagnostics from the shared help knowledge base. Covers "
          "processing operators (pass e.g. 'rs:spectral_index'); protocol and "
          "data-platform tools report their own descriptions via tools/list.",
          { { "tool_id", "string", "Tool id to look up help for, e.g. 'rs:spectral_index'.", true } } },
        { "artifact_read",
          "Read a bounded slice of a file artifact (raster sidecar, JSON result, "
          "CSV, log) produced by a tool execution. Returns the slice plus the "
          "whole-file sha256 and a nextOffset cursor for continuation — never "
          "inline large payloads in tool results; page through them here. "
          "Paths follow the server's workspace rule (paths must stay inside "
          "the server workspace: SICNU_MCP_WORKSPACE, default the server's "
          "working directory).",
          { { "path", "string", "Path of the artifact file to read.", true },
            { "offset", "integer", "Byte offset to start reading from (default 0).", false },
            { "length", "integer", "Max bytes to read; clamped to 262144 (256 KiB). Default: the cap.", false },
            { "encoding", "string", "'text' (default, must be valid UTF-8) or 'base64' for binary slices.", false } } },
        // Agent ops driver (R3): one tool over the shared session surface —
        // run/resume/cancel/status/export parity with the CLI `session`
        // command and the workbench panel. The host must inject an
        // OpsDriver; without one the call fails closed (typed
        // AGENT_OPS_UNAVAILABLE), never a fake session.
        { "scientific:agent_session",
          "Drive a scientific agent session through the OperationsCoordinator "
          "(agent_loop is the only state machine). Actions: run (goal[,intent,"
          "mode,journal_directory,session_id,domain,role,refs]), resume "
          "(journal_directory,session_id,goal), reconcile (journal_directory,"
          "session_id — inspect a journal without resuming, incl. "
          "duplicate-submit hazards), status/timeline/export "
          "(session_id), pause/cancel/clear_pause/clear_cancel, "
          "approve_repair, actions.",
          { { "action", "string", "One of the session surface actions.", true },
            { "goal", "string", "Session goal (run/resume).", false },
            { "intent", "string", "Intent hint, e.g. 'ndvi'.", false },
            { "mode", "string", "'dry_run' | 'plan_only' | 'execute_with_verify'.", false },
            { "journal_directory", "string", "Directory holding session journals.", false },
            { "session_id", "string", "Session id for resume/reconcile/status/export.", false },
            { "refs", "object", "Slot -> reference map (paths / asset ids) for run.", false },
            { "domain", "string", "Autonomy domain (default 'research').", false },
            { "role", "string", "Autonomy role.", false },
            { "approve", "boolean", "approve_repair: record one-shot pending approval.", false } } },
    };
    return kMetaTools;
}

} // namespace

const std::vector<MetaToolDef> &defs()
{
    return table();
}

Json::Value schema( const MetaToolDef &def )
{
    Json::Value schema = Json::Value( Json::objectValue );
    schema["type"] = "object";
    Json::Value properties = Json::Value( Json::objectValue );
    Json::Value required = Json::Value( Json::arrayValue );
    for ( const MetaToolInput &input : def.inputs )
    {
        Json::Value prop = Json::Value( Json::objectValue );
        prop["type"] = input.type;
        prop["description"] = input.description;
        properties[input.name] = prop;
        if ( input.required )
            required.append( input.name );
    }
    schema["properties"] = properties;
    if ( !required.empty() )
        schema["required"] = required;
    return schema;
}

Json::Value schemaFor( const std::string &name )
{
    for ( const MetaToolDef &def : table() )
    {
        if ( name == def.name )
            return schema( def );
    }
    return Json::Value( Json::nullValue );
}

bool contains( const std::string &name )
{
    for ( const MetaToolDef &def : table() )
    {
        if ( name == def.name )
            return true;
    }
    return false;
}

} // namespace sicnu::agent::tool_catalog::meta_protocol
