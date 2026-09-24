// test_spatial_tool_registration.cpp — scoped SpatialTool registrations
//
// The registry is process-wide and first-registration-wins; before the
// scoped-token convergence a session host (the main window) could never
// release its tools, pinning guarded-but-dead registrations for the process
// lifetime and silently shadowing any re-assembled shell's fresh tool.
// These cases pin the token/unregister semantics and the real-shaped
// window-shaped tools.
#include <catch2/catch_test_macros.hpp>

#include "agent/spatial_tools/spatial_tool.h"
#include "workbench/agent_context_tool.h"

#include <memory>
#include <utility>

using namespace sicnu::agent::spatial_tools;

namespace
{
/// Minimal fake tool (the fake side of the fake + real-shaped pairing).
class FakeTool : public SpatialTool
{
  public:
    explicit FakeTool( std::string name ) : mName( std::move( name ) ) {}
    std::string name() const override { return mName; }
    std::string displayName() const override { return mName; }
    std::string description() const override { return "fake"; }
    std::vector<std::string> tags() const override { return { "test" }; }
    Json::Value inputSchema() const override { return Json::Value( Json::objectValue ); }
    Json::Value outputSchema() const override { return Json::Value( Json::objectValue ); }
    SpatialToolResult execute( const Json::Value & ) override
    {
        SpatialToolResult result;
        result.success = true;
        result.output = Json::Value( "fake-ok" );
        return result;
    }
    std::string mName;
};

/// Uniquely-named tool factory: the registry is process-global, tests must
/// not fight other registrations.
int uniqueCounter = 0;
std::string uniqueName( const char *base )
{
    return std::string( base ) + "-" + std::to_string( ++uniqueCounter );
}
} // namespace

TEST_CASE( "SpatialToolRegistry: unregisterTool removes exactly the named registration", "[agent][spatial_registry]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const auto name = uniqueName( "test:unregister" );

  CHECK( registry.registerTool( std::make_shared<FakeTool>( name ) ) );
  REQUIRE( registry.find( name ).has_value() );

  CHECK( registry.unregisterTool( name ) );
  CHECK( !registry.find( name ).has_value() );

  // Removing again reports honestly: nothing was registered.
  CHECK( !registry.unregisterTool( name ) );

  // The name is free again — a re-assembly can register cleanly.
  CHECK( registry.registerTool( std::make_shared<FakeTool>( name ) ) );
  REQUIRE( registry.find( name ).has_value() );
  registry.unregisterTool( name );
}

TEST_CASE( "RegistrationToken: scoped arm/release with duplicate refusal", "[agent][spatial_registry]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const auto name = uniqueName( "test:token" );

  SpatialToolRegistry::RegistrationToken token;
  REQUIRE( token.name().empty() );
  REQUIRE( token.arm( std::make_shared<FakeTool>( name ) ) );
  REQUIRE( token.name() == name );
  REQUIRE( registry.find( name ).has_value() );

  // First-wins survives the token: a second arm of the same name is
  // refused, the refusing token owns nothing, and its destruction must not
  // release the original registration.
  {
    SpatialToolRegistry::RegistrationToken loser;
    CHECK( !loser.arm( std::make_shared<FakeTool>( name ) ) );
    CHECK( loser.name().empty() );
  }
  REQUIRE( registry.find( name ).has_value() );

  // Destruction of the armed token releases the registration.
  token.release();
  CHECK( token.name().empty() );
  CHECK( !registry.find( name ).has_value() );
  // Releasing an already-released token is a no-op.
  token.release();
}

TEST_CASE( "RegistrationToken: moving transfers the release duty exactly once", "[agent][spatial_registry]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const auto name = uniqueName( "test:moved" );

  SpatialToolRegistry::RegistrationToken source;
  REQUIRE( source.arm( std::make_shared<FakeTool>( name ) ) );

  SpatialToolRegistry::RegistrationToken target( std::move( source ) );
  CHECK( source.name().empty() ); // moved-from owns nothing
  CHECK( target.name() == name );
  REQUIRE( registry.find( name ).has_value() );

  // The moved-from token destructs harmlessly; the target's destruction
  // releases the registration — exactly once.
  {
    SpatialToolRegistry::RegistrationToken assigned;
    assigned = std::move( target );
    CHECK( target.name().empty() );
    CHECK( assigned.name() == name );
  }
  CHECK( !registry.find( name ).has_value() );
}

TEST_CASE( "RegistrationToken: an in-flight executor survives the release", "[agent][spatial_registry]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const auto name = uniqueName( "test:inflight" );

  SpatialToolRegistry::RegistrationToken token;
  REQUIRE( token.arm( std::make_shared<FakeTool>( name ) ) );

  // Look the tool up (an MCP dispatch would be executing here…)
  const auto lookedUp = registry.find( name );
  REQUIRE( lookedUp.has_value() );

  // …and the host tears the registration down mid-flight.
  token.release();
  CHECK( !registry.find( name ).has_value() );

  // The executor still holds its shared_ptr: execute() completes instead
  // of dangling.
  const auto result = ( *lookedUp )->execute( Json::Value() );
  CHECK( result.success );
  CHECK( result.output.asString() == "fake-ok" );
}

TEST_CASE( "WorkbenchContextTool: real-shaped scoped registration answers and retires", "[agent][spatial_registry]" )
{
  auto &registry = SpatialToolRegistry::instance();
  const auto name = std::string( "workbench:context" );

  // Whatever an earlier fixture left behind must not wedge this test:
  // release any stale registration of the well-known name, then clean up
  // our own at the end.
  registry.unregisterTool( name );

  bool providerCalled = false;
  SpatialToolRegistry::RegistrationToken token;
  REQUIRE( token.arm( std::make_shared<sicnu::app::WorkbenchContextTool>(
      [&providerCalled]() -> Json::Value {
        providerCalled = true;
        Json::Value payload( Json::objectValue );
        payload["phase"] = "map";
        return payload;
      } ) ) );

  const auto found = registry.find( name );
  REQUIRE( found.has_value() );
  const auto result = ( *found )->execute( Json::Value() );
  CHECK( result.success );
  CHECK( providerCalled );
  CHECK( result.output["phase"].asString() == "map" );

  // Teardown retires the tool: the registry no longer offers it (a later
  // shell assembles its own instead of silently reusing this one).
  token.release();
  CHECK( !registry.find( name ).has_value() );
}
