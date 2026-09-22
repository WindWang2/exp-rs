// src/agent/harness/autonomy_tools.cpp
#include "autonomy_tools.h"

#include "agent/autonomy/autonomy_holder.h"
#include "agent/autonomy/autonomy_policy.h"
#include "agent/autonomy/autonomy_projection.h"
#include "harness_actions.h"
#include "lab_copilot.h"
#include "spatial_tools/spatial_tool.h"

#include <string>
#include <vector>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

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
      // Authority-bearing fields (role / domain / session policy) are
      // deliberately OMITTED — same rule as the lab tools: the composing
      // model is never invited to claim them. The host injects them.
      return objectSchema( Json::Value( Json::objectValue ), Json::Value() );
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
      // Same session-context hardening as the execution gate (plan_tools):
      // a non-elevated role passes through, an elevated claim needs the
      // host-injected credential, and the domain is honored only with the
      // credential — a forged context must not re-scope the projection the
      // UI renders (it cannot raise levels, but it must not lie either).
      std::string sessionRole;
      if ( input.isMember( "role" ) && input[ "role" ].isString() )
        sessionRole = normalizeLabRole( input[ "role" ].asString() );
      if ( labRoleMayUseTeacherSurfaces( sessionRole ) && !teacherCredentialValid( input ) )
        sessionRole = "student";
      std::string surfaceDomain( "lab" );
      if ( teacherCredentialValid( input ) && input.isMember( "domain" ) &&
           input[ "domain" ].isString() && !input[ "domain" ].asString().empty() )
        surfaceDomain = input[ "domain" ].asString();

      std::vector<sicnu::agent::autonomy::AutonomyPolicyLayer> layers;
      const Json::Value &session = input[ "autonomy" ];
      // The session layer is privileged (it can raise a level) and follows
      // the teacher-credential gate; a forged block cannot inflate the
      // projection the UI renders.
      if ( session.isObject() && teacherCredentialValid( input ) )
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
