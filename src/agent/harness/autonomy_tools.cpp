// src/agent/harness/autonomy_tools.cpp
#include "autonomy_tools.h"

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"
#include "spatial_tools/spatial_tool.h"

#include <string>
#include <vector>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

/// Read-only projection of the effective policy. The session layer
/// (role/domain/policy overrides) is host-injected — the input schema omits
/// it so a composing model is never invited to claim authority.
class AutonomyStatusTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:autonomy_status"; }
    std::string displayName() const override { return "Autonomy Status"; }
    std::string description() const override
    {
      return "Read-only projection of the effective teaching-autonomy policy "
             "(sicnu.autonomy-status/1): current L0..L5 level, mode, and per "
             "capability whether it is allowed, limited (downgraded), or "
             "forbidden — each with a typed reason code. Decides nothing; "
             "the same engine backs the live gates.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "autonomy", "policy", "teaching", "status" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value role( Json::objectValue );
      role["type"] = "string";
      role["description"] = "Session role (host-injected; student by default).";
      props["role"] = role;
      Json::Value domain( Json::objectValue );
      domain["type"] = "string";
      domain["description"] = "Surface domain: \"lab\" (teaching) or a research domain.";
      props["domain"] = domain;
      return objectSchema( std::move( props ), Json::Value() );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["allowed"] = Json::Value( Json::objectValue );
      props["limited"] = Json::Value( Json::objectValue );
      props["forbidden"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const Json::Value &role = input[ "role" ];
      const Json::Value &domain = input[ "domain" ];
      const std::string sessionRole = role.isString() ? role.asString() : std::string();
      const std::string surfaceDomain =
          domain.isString() && !domain.asString().empty() ? domain.asString() : std::string( "lab" );

      std::vector<sicnu::agent::autonomy::AutonomyPolicyLayer> layers;
      const Json::Value &session = input[ "autonomy" ];
      if ( session.isObject() )
      {
        const sicnu::agent::autonomy::AutonomyPolicyParseResult parsed =
            sicnu::agent::autonomy::parseAutonomyPolicy( session );
        if ( parsed.ok )
          layers.emplace_back( sicnu::agent::autonomy::AutonomyPolicyLayer{
              sicnu::agent::autonomy::policy_sources::kSession, parsed.policy } );
      }
      const sicnu::agent::autonomy::AutonomyPolicy policy =
          sicnu::agent::autonomy::AutonomyPolicyHolder::instance().effectivePolicy( layers );

      return SpatialToolResult::ok(
          sicnu::agent::autonomy::autonomyStatusProjection( policy, sessionRole, surfaceDomain ) );
    }
};

} // namespace

void registerAutonomyTools()
{
  SpatialToolRegistry::instance().registerTool( std::make_shared<AutonomyStatusTool>() );
}

} // namespace sicnu::agent::harness
