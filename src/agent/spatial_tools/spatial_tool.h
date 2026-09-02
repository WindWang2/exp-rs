// src/agent/spatial_tools/spatial_tool.h
#pragma once

#include <json/json.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::agent::spatial_tools {

/**
 * Result of a spatial tool execution. On success `output` conforms to the
 * tool's outputSchema; on failure `error` carries a user-facing message,
 * accompanied by machine-readable error codes and categorization for agent
 * planning (ADR 0122 / Harness deepening).
 */
struct SpatialToolResult {
  bool success = false;
  Json::Value output;
  std::string error;
  std::string errorCode;     ///< Machine-readable code, e.g. "NOT_FOUND", "INVALID_PARAMETER"
  std::string errorCategory; ///< Error family: "validation" | "io" | "runtime"
  bool retryable = false;    ///< Whether Pi/agent can retry the operation with adjusted parameters

  static SpatialToolResult ok( Json::Value output )
  {
    SpatialToolResult r;
    r.success = true;
    r.output = std::move( output );
    return r;
  }

  static SpatialToolResult failure( std::string error,
                                    std::string errorCode = "",
                                    std::string errorCategory = "",
                                    bool retryable = false )
  {
    SpatialToolResult r;
    r.success = false;
    r.error = std::move( error );
    r.errorCode = std::move( errorCode );
    r.errorCategory = std::move( errorCategory );
    r.retryable = retryable;
    return r;
  }

  Json::Value toJson() const
  {
    Json::Value val( Json::objectValue );
    val["success"] = success;
    if ( success )
    {
      val["result"] = output;
    }
    else
    {
      Json::Value errObj( Json::objectValue );
      errObj["message"] = error;
      if ( !errorCode.empty() )
        errObj["code"] = errorCode;
      if ( !errorCategory.empty() )
        errObj["category"] = errorCategory;
      errObj["retryable"] = retryable;
      val["error"] = errObj;
    }
    return val;
  }
};

/**
 * SpatialTool — the executable agent-facing spatial capability contract
 * (ADR 0122). Unlike the descriptor-only AgentTool, a SpatialTool owns both
 * its schema and its synchronous execution. Tools in this registry are
 * expected to be fast and read-only (inspection, catalogs, queries);
 * long-running algorithm work stays in the Task Center.
 */
class SpatialTool {
  public:
    virtual ~SpatialTool() = default;

    /// Canonical id, e.g. "spatial:raster_inspect"
    virtual std::string name() const = 0;

    /// Human-readable title
    virtual std::string displayName() const = 0;

    /// Detailed capability description surfaced to agents
    virtual std::string description() const = 0;

    /// Semantic tags for discovery
    virtual std::vector<std::string> tags() const = 0;

    /// JSON Schema ({type: "object", properties, required}) for execute() input
    virtual Json::Value inputSchema() const = 0;

    /// JSON Schema describing the shape of a successful execute() output
    virtual Json::Value outputSchema() const = 0;

    /// Executes the tool. Must be thread-safe and must not block for long.
    virtual SpatialToolResult execute( const Json::Value &input ) = 0;
};

inline QString requireStringField( const Json::Value &input, const char *key, std::string *error )
{
  if ( !input.isObject() || !input.isMember( key ) || !input[key].isString() )
  {
    *error = std::string( "missing string parameter '" ) + key + "'";
    return {};
  }
  return QString::fromStdString( input[key].asString() );
}
};

using SpatialToolPtr = std::shared_ptr<SpatialTool>;

/**
 * Validates that the required properties declared by a JSON Schema
 * (draft-style "required" array) are present in an input object. Returns an
 * empty string when valid, otherwise a human-readable rejection message.
 */
std::string validateAgainstRequired( const Json::Value &input, const Json::Value &schema );

/**
 * Process-wide registry of SpatialTool instances (ADR 0122). Mirrors the
 * AgentToolCatalog/InteractionToolRegistry singleton idiom.
 */
class SpatialToolRegistry {
  public:
    static SpatialToolRegistry &instance();

    /// Registers a tool; returns false and keeps the existing registration
    /// when a tool with the same name is already present.
    bool registerTool( SpatialToolPtr tool );

    /// Registers the built-in spatial tools (raster/vector inspection,
    /// model catalog). Idempotent.
    void registerBuiltinTools();

    /// Removes every registration and re-registers the built-ins.
    void reset();

    std::optional<SpatialToolPtr> find( const std::string &name ) const;

    std::vector<SpatialToolPtr> tools() const;

    size_t size() const;

  private:
    SpatialToolRegistry() = default;

    mutable std::mutex mMutex;
    std::unordered_map<std::string, SpatialToolPtr> mTools;
};

} // namespace sicnu::agent::spatial_tools
