// tests/test_plugin_capabilities.cpp — structured capability declarations &
// resource quotas (isolation runtime 5.0).
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_capabilities.h"
#include "exprs/plugin_quotas.h"

#include <json/json.h>

using namespace exprs;

TEST_CASE( "empty capabilities parse to deny-all defaults", "[plugin][capabilities]" )
{
    const auto result = parsePluginAccess( Json::Value( Json::objectValue ),
                                                 "C:/p/my-plugin", "C:/ws", "C:/t" );
    REQUIRE( result.ok() );
    REQUIRE( result.capabilities.fsReadRoots.empty() );
    REQUIRE( result.capabilities.fsWriteRoots.empty() );
    REQUIRE_FALSE( result.capabilities.network );
    REQUIRE_FALSE( result.capabilities.externalProcess );
    REQUIRE_FALSE( result.capabilities.destructive );
}

TEST_CASE( "path placeholders expand to canonical roots", "[plugin][capabilities]" )
{
    Json::Value fs( Json::objectValue );
    fs["read"] = Json::Value( Json::arrayValue );
    fs["read"].append( "${workspace}/inputs" );
    fs["read"].append( "${plugin}/data" );
    fs["write"] = Json::Value( Json::arrayValue );
    fs["write"].append( "${temp}/out" );
    Json::Value access( Json::objectValue );
    access["filesystem"] = fs;
    const auto result =
        parsePluginAccess( access, "C:/p/my-plugin", "C:/ws", "C:/t" );
    REQUIRE( result.ok() );
    REQUIRE( result.capabilities.fsReadRoots.size() == 2 );
    // Canonical form is lower-cased drive, forward slashes on Windows.
    for ( const std::string &root : result.capabilities.fsReadRoots )
        REQUIRE( ( root.find( "ws" ) != std::string::npos || root.find( "plugin" ) != std::string::npos ) );
    REQUIRE( result.capabilities.fsWriteRoots.size() == 1 );
}

TEST_CASE( "${workspace} without a configured workspace fails closed", "[plugin][capabilities]" )
{
    Json::Value fs( Json::objectValue );
    fs["read"] = Json::Value( Json::arrayValue );
    fs["read"].append( "${workspace}/inputs" );
    Json::Value access( Json::objectValue );
    access["filesystem"] = fs;
    const auto result = parsePluginAccess( access, "C:/p/my-plugin", "", "C:/t" );
    REQUIRE_FALSE( result.ok() );
    // Fail closed: no grant may materialize from an unresolvable root.
    REQUIRE( result.capabilities.fsReadRoots.empty() );
    REQUIRE( result.errors.size() == 1 );
    REQUIRE( result.errors[0].find( "workspace" ) != std::string::npos );
}

TEST_CASE( "capability flags parse with typed errors", "[plugin][capabilities]" )
{
    Json::Value caps( Json::objectValue );
    caps["network"] = true;
    caps["externalProcess"] = true;
    caps["gpu"] = "cuda";
    caps["workspace"] = [] {
        Json::Value v( Json::objectValue );
        v["mutate"] = true;
        return v;
    }();
    caps["project"] = [] {
        Json::Value v( Json::objectValue );
        v["mutate"] = false;
        return v;
    }();
    caps["modelProvider"] = [] {
        Json::Value v( Json::objectValue );
        Json::Value frameworks( Json::arrayValue );
        frameworks.append( "onnx" );
        v["frameworks"] = frameworks;
        return v;
    }();
    caps["ui"] = true;
    caps["destructive"] = false;

    const auto result = parsePluginAccess( caps, "C:/p", "C:/ws", "C:/t" );
    REQUIRE( result.ok() );
    REQUIRE( result.capabilities.network );
    REQUIRE( result.capabilities.externalProcess );
    REQUIRE( result.capabilities.gpuHint == "cuda" );
    REQUIRE( result.capabilities.workspaceMutation );
    REQUIRE_FALSE( result.capabilities.projectMutation );
    REQUIRE( result.capabilities.modelFrameworks == std::vector<std::string>{ "onnx" } );
    REQUIRE( result.capabilities.uiContribution );
    REQUIRE_FALSE( result.capabilities.destructive );
}

TEST_CASE( "wrong-typed capabilities are validation errors", "[plugin][capabilities]" )
{
    Json::Value caps( Json::objectValue );
    caps["network"] = "yes";
    caps["gpu"] = 3;
    const auto result = parsePluginAccess( caps, "C:/p", "C:/ws", "C:/t" );
    REQUIRE_FALSE( result.ok() );
    REQUIRE( result.errors.size() == 2 );
}

TEST_CASE( "quota manifest parsing warns on junk and keeps ceilings for clamping", "[plugin][quotas]" )
{
    PluginQuota ceilings = PluginQuota::fromEnvironment();
    ceilings.maxRequestConcurrency = 2;
    ceilings.maxResponseBytes = 1024L * 1024L;

    Json::Value quotas( Json::objectValue );
    quotas["maxRequestConcurrency"] = 32;       // above ceiling -> clamped
    quotas["maxResponseBytes"] = Json::Value( Json::Int64( 1024 ) * 1024 * 1024 ); // above ceiling -> clamped
    quotas["requestDeadlineMs"] = 60000;        // legal, below default ceiling
    quotas["workerMemoryBytes"] = 512LL * 1024LL * 1024LL;
    quotas["workerCpuRatePercent"] = 50;
    quotas["maxChildProcesses"] = 2;
    quotas["gpuHint"] = "cuda";

    PluginQuota quota;
    std::vector<std::string> warnings;
    quota.parseManifest( quotas, warnings );
    REQUIRE( warnings.empty() );
    quota.clampTo( ceilings );

    REQUIRE( quota.maxRequestConcurrency == 2 );
    REQUIRE( quota.maxResponseBytes == 1024L * 1024L );
    REQUIRE( quota.requestDeadlineMs == 60000 );
    REQUIRE( quota.workerMemoryBytes == 512LL * 1024LL * 1024LL );
    REQUIRE( quota.workerCpuRatePercent == 50 );
    REQUIRE( quota.maxChildProcesses == 2 );
    REQUIRE( quota.gpuHint == "cuda" );
}

TEST_CASE( "quota junk values produce warnings, not refusals", "[plugin][quotas]" )
{
    Json::Value quotas( Json::objectValue );
    quotas["maxRequestConcurrency"] = "many";
    quotas["gpuHint"] = 5;

    PluginQuota quota;
    std::vector<std::string> warnings;
    quota.parseManifest( quotas, warnings );
    REQUIRE( warnings.size() == 2 );
    // Defaults survived.
    REQUIRE( quota.maxRequestConcurrency == 4 );
}

TEST_CASE( "quota environment defaults parse", "[plugin][quotas]" )
{
    // fromEnvironment must never exceed sane bounds even with junk env.
    const PluginQuota quota = PluginQuota::fromEnvironment();
    REQUIRE( quota.maxRequestConcurrency > 0 );
    REQUIRE( quota.requestDeadlineMs > 0 );
    REQUIRE( quota.maxResponseBytes > 0 );
    REQUIRE( quota.workerCpuRatePercent >= 0 );
    REQUIRE( quota.workerCpuRatePercent <= 100 );
}
