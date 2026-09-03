// src/agent/spatial_tools/temporal_workspace_tools.h
// temporal:* workspace discovery tools — the agent-facing surface of the
// TemporalCollection-as-workspace-record integration (ADR-temporal):
//
//   discover what collections exist (list_collections)
//   → inspect one (get_collection)
//   → register / re-register one (register_collection)
//   → remove one (remove_collection)
//
// All responses are compact by default: the collection record summary is a
// handful of fields parsed from the stored descriptor (no raster I/O); scene
// detail stays behind temporal:list_scenes, which pages.
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// temporal:list_collections — paged listing of the workspace's temporal
/// collection records (id, name, scene count, time range, platforms).
class TemporalListCollectionsTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:list_collections"; }
    std::string displayName() const override { return "List Temporal Collections"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:get_collection — one workspace record's summary by id or name
/// (scene count, time range, platforms, bound-scene ratio, duplicate policy).
class TemporalGetCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:get_collection"; }
    std::string displayName() const override { return "Get Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:register_collection — binds scene assets and persists a
/// collection into the workspace (from a descriptor file or an inline scene
/// list). Idempotent for an identical (name + descriptor) re-registration.
class TemporalRegisterCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:register_collection"; }
    std::string displayName() const override { return "Register Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:remove_collection — removes a workspace record (scene assets are
/// never deleted: the descriptor only references them).
class TemporalRemoveCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:remove_collection"; }
    std::string displayName() const override { return "Remove Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:ingest_stac — minimal STAC ingestion: consumes an ALREADY-FETCHED
/// search response (file path or inline JSON — never network from a tool),
/// applies client-side filters (bbox / datetime / property / limit), builds a
/// TemporalCollection and registers it in the workspace. The remote scene
/// hrefs (COG candidates under /vsicurl/) become the scenes' pixel owners.
class TemporalIngestStacTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:ingest_stac"; }
    std::string displayName() const override { return "Ingest STAC Search Result"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

} // namespace sicnu::agent::spatial_tools
