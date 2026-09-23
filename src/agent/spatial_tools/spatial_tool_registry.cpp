// src/agent/spatial_tools/spatial_tool_registry.cpp
//
// SpatialToolRegistry implementation, split out of spatial_tool.cpp so the
// registry (instance/register/find/tools/size + the MeteredTool output
// budget decorator) compiles without the full built-in tool include closure.
// registerBuiltinTools() stays in spatial_tool.cpp with its tool headers.
#include "spatial_tool.h"

#include "../contracts/spatial_contracts.h"

#include <algorithm>
#include <mutex>

namespace sicnu::agent::spatial_tools {

std::string validateAgainstRequired( const Json::Value &input, const Json::Value &schema )
{
  if ( !schema.isObject() || !schema.isMember( "required" ) || !schema["required"].isArray() )
    return std::string();

  if ( !input.isObject() )
    return "Tool input must be a JSON object";

  for ( const auto &key : schema["required"] )
  {
    if ( !key.isString() )
      continue;
    if ( !input.isMember( key.asString() ) )
      return "Missing required parameter: " + key.asString();
  }

  // Declared-type check (#620): validating only `required` let a
  // {"path": {"a": 1}} input reach asString() and escape as an untyped
  // -32000 instead of a structured INVALID_PARAMETER.
  const Json::Value &properties = schema.isMember( "properties" ) && schema["properties"].isObject()
                                      ? schema["properties"]
                                      : Json::Value::nullSingleton();
  if ( properties.isNull() )
    return std::string();
  for ( const auto &name : input.getMemberNames() )
  {
    if ( !properties.isMember( name ) )
      continue;
    const Json::Value &decl = properties[name];
    if ( !decl.isObject() || !decl.isMember( "type" ) || !decl["type"].isString() )
      continue;
    const std::string type = decl["type"].asString();
    const Json::Value &value = input[name];
    bool ok = true;
    if ( type == "string" )
      ok = value.isString();
    else if ( type == "integer" )
      ok = value.isIntegral();
    else if ( type == "number" )
      ok = value.isNumeric();
    else if ( type == "boolean" )
      ok = value.isBool();
    else if ( type == "array" )
      ok = value.isArray();
    else if ( type == "object" )
      ok = value.isObject();
    if ( !ok )
      return "Parameter '" + name + "' must be of type " + type;
  }
  return std::string();
}


SpatialToolRegistry &SpatialToolRegistry::instance()
{
  static SpatialToolRegistry registry;
  return registry;
}

namespace {

/// Harness 7.0 (mission Area I): runtime output meter. Every tool result
/// passing through the registry is measured against
/// contracts::kMaxToolOutputBytes; oversized outputs are compacted
/// schema-aware — the largest array-valued member is trimmed first (the
/// shape survives, the envelope stays parseable) and the truncation is
/// declared, never silent. This turns the 3.0 advisory cap into an enforced
/// one at the single choke point every agent-facing tool passes through.
class MeteredTool final : public SpatialTool
{
  public:
    explicit MeteredTool( SpatialToolPtr inner ) : mInner( std::move( inner ) ) {}

    std::string name() const override { return mInner->name(); }
    std::string displayName() const override { return mInner->displayName(); }
    std::string description() const override { return mInner->description(); }
    std::vector<std::string> tags() const override { return mInner->tags(); }
    Json::Value inputSchema() const override { return mInner->inputSchema(); }
    Json::Value outputSchema() const override { return mInner->outputSchema(); }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult result = mInner->execute( input );
      if ( result.success )
      {
        compactIfOversized( result.output );
      }
      else if ( result.error.size() > sicnu::agent::contracts::kMaxToolOutputBytes )
      {
        // Failure envelopes are bounded by the error taxonomy in practice;
        // a runaway operator log is clamped instead of passed through.
        result.error.erase( sicnu::agent::contracts::kMaxToolOutputBytes );
        result.error += "[truncated: error exceeded the 512 KiB tool budget]";
      }
      return result;
    }

  private:
    /// Trims `member` (an array) to the largest prefix whose serialized size
    /// fits the budget. Deterministic: halving from the full length.
    void trimArray( Json::Value &output, const std::string &member, size_t budget,
                    Json::UInt64 originalBytes ) const
    {
      int count = static_cast<int>( output[member].size() );
      while ( count >= 1 )
      {
        Json::Value trimmed( Json::arrayValue );
        for ( int i = 0; i < count; ++i )
          trimmed.append( output[member][i] );
        Json::Value candidate = output;
        candidate[member] = trimmed;
        candidate["truncated"] = true;
        candidate["truncated_field"] = member;
        candidate["original_bytes"] = originalBytes;
        if ( sicnu::agent::contracts::serializedSize( candidate ) <= budget )
        {
          output[member] = trimmed;
          output["truncated"] = true;
          output["truncated_field"] = member;
          output["original_bytes"] = originalBytes;
          return;
        }
        count /= 2;
      }
    }

    void compactIfOversized( Json::Value &output ) const
    {
      using sicnu::agent::contracts::kMaxToolOutputBytes;
      using sicnu::agent::contracts::serializedSize;
      if ( serializedSize( output ) <= kMaxToolOutputBytes )
        return;
      const size_t budget = kMaxToolOutputBytes - 512; // room for markers
      const Json::UInt64 originalBytes = static_cast<Json::UInt64>( serializedSize( output ) );

      // Prefer trimming the largest array-valued member.
      std::string largest;
      size_t largestSize = 0;
      for ( const std::string &key : output.getMemberNames() )
      {
        if ( !output[key].isArray() )
          continue;
        const size_t size = serializedSize( output[key] );
        if ( size > largestSize )
        {
          largestSize = size;
          largest = key;
        }
      }
      if ( !largest.empty() )
      {
        trimArray( output, largest, budget, originalBytes );
        if ( serializedSize( output ) <= kMaxToolOutputBytes )
          return;
      }
      // Last resort: an honest compact envelope replaces the payload.
      Json::Value compact( Json::objectValue );
      compact["truncated"] = true;
      compact["truncated_field"] = largest.empty() ? "*" : largest;
      compact["original_bytes"] = originalBytes;
      compact["note"] = "output exceeded the 512 KiB tool budget and was elided; "
                        "re-query with a narrower filter or pagination";
      output = compact;
    }

    SpatialToolPtr mInner;
};

} // namespace

bool SpatialToolRegistry::registerTool( SpatialToolPtr tool )
{
  if ( !tool || tool->name().empty() )
    return false;

  std::lock_guard<std::mutex> lock( mMutex );
  // Every registration is metered (Harness 7.0 Area I): callers get the
  // decorator transparently from find(), so the 512 KiB cap holds no matter
  // which surface executes the tool. The name is captured and the wrapper
  // built BEFORE the emplace — the emplace arguments' evaluation order is
  // unspecified, and moving the pointer away before name() reads it was a
  // null dereference (found as a segfault in the #725 facets test).
  const std::string name = tool->name();
  const SpatialToolPtr metered = std::make_shared<MeteredTool>( std::move( tool ) );
  return mTools.emplace( name, metered ).second;
}

bool SpatialToolRegistry::unregisterTool( const std::string &name )
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.erase( name ) > 0;
}

bool SpatialToolRegistry::RegistrationToken::arm( SpatialToolPtr tool )
{
  release();
  if ( !tool || tool->name().empty() )
    return false;
  // Capture the name before the register call moves the tool away.
  const std::string name = tool->name();
  if ( !SpatialToolRegistry::instance().registerTool( std::move( tool ) ) )
    return false;
  mName = name;
  return true;
}

void SpatialToolRegistry::RegistrationToken::release()
{
  if ( mName.empty() )
    return;
  SpatialToolRegistry::instance().unregisterTool( mName );
  mName.clear();
}
std::optional<SpatialToolPtr> SpatialToolRegistry::find( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  const auto it = mTools.find( name );
  if ( it == mTools.end() )
    return std::nullopt;
  return it->second;
}

std::vector<SpatialToolPtr> SpatialToolRegistry::tools() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::vector<SpatialToolPtr> result;
  result.reserve( mTools.size() );
  for ( const auto &[name, tool] : mTools )
    result.push_back( tool );
  return result;
}

size_t SpatialToolRegistry::size() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.size();
}


} // namespace sicnu::agent::spatial_tools
