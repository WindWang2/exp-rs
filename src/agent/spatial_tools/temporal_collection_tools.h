// src/agent/spatial_tools/temporal_collection_tools.h
// temporal:* agent tools — lightweight collection management for temporal
// remote sensing (goal §33–§35). These follow the SpatialTool model (ADR
// 0122): fast, direct, schema-carrying, no new agent loop. Heavy temporal
// computation stays in the rs:temporal_* operators executed via the normal
// search_tools → get_tool_schema → preflight → execute chain.
//
// Compact-by-default responses (goal §35): describe_collection summarizes a
// 100-scene collection in a few fields; list_scenes pages on request.
#pragma once

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// temporal:create_collection — build + validate a collection descriptor from
/// scene paths (times auto-resolved from metadata/filename or supplied) and
/// save it as a JSON sidecar that rs:temporal_* operators accept.
class TemporalCreateCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:create_collection"; }
    std::string displayName() const override { return "Create Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:describe_collection — compact summary (scene count, time range,
/// platforms, grid compatibility, radiometric state) with optional per-issue
/// detail. Never dumps every scene × every band.
class TemporalDescribeCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:describe_collection"; }
    std::string displayName() const override { return "Describe Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:list_scenes — paged chronological scene listing with time source,
/// platform, and band-role availability.
class TemporalListScenesTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:list_scenes"; }
    std::string displayName() const override { return "List Collection Scenes"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

/// temporal:preflight_collection — run the full scientific gate (time / grid /
/// roles / radiometry / validity) without executing any algorithm.
class TemporalPreflightCollectionTool : public SpatialTool {
  public:
    std::string name() const override { return "temporal:preflight_collection"; }
    std::string displayName() const override { return "Preflight Temporal Collection"; }
    std::string description() const override;
    std::vector<std::string> tags() const override;
    Json::Value inputSchema() const override;
    Json::Value outputSchema() const override;
    SpatialToolResult execute( const Json::Value &input ) override;
};

} // namespace sicnu::agent::spatial_tools
