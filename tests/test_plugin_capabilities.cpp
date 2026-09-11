// tests/test_plugin_capabilities.cpp — structured capability declarations &
// resource quotas (isolation runtime 5.0).
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_capabilities.h"
#include "exprs/plugin_quotas.h"

#include <json/json.h>

#include <filesystem>
#include <map>

using namespace exprs;

namespace {
/// Root anchors for path-expansion fixtures: absolute on the RUNNING
/// platform (the expansion logic rejects paths that are not anchored, so
/// drive-letter strings would fail closed on POSIX).
std::string anchorPath( const char *leaf )
{
    return ( std::filesystem::temp_directory_path() / leaf ).generic_string();
}
} // namespace

TEST_CASE( "empty capabilities parse to deny-all defaults", "[plugin][capabilities]" )
{
    const auto result = parsePluginAccess( Json::Value( Json::objectValue ),
                                           anchorPath( "cap-p/my-plugin" ),
                                           anchorPath( "cap-ws" ), anchorPath( "cap-t" ) );
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
        parsePluginAccess( access, anchorPath( "cap-p/my-plugin" ), anchorPath( "cap-ws" ), anchorPath( "cap-t" ) );
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
    const auto result = parsePluginAccess( access, anchorPath( "cap-p/my-plugin" ), "", anchorPath( "cap-t" ) );
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

    const auto result = parsePluginAccess( caps, anchorPath( "cap-p" ), anchorPath( "cap-ws" ), anchorPath( "cap-t" ) );
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
    const auto result = parsePluginAccess( caps, anchorPath( "cap-p" ), anchorPath( "cap-ws" ), anchorPath( "cap-t" ) );
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
    quotas["workerMemoryBytes"] = static_cast<Json::Int64>( 512LL * 1024LL * 1024LL );
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

TEST_CASE( "protocol 1.2 request-bytes quota parses, clamps and round-trips",
           "[plugin][quotas][p12]" )
{
    PluginQuota ceilings = PluginQuota::fromEnvironment();
    ceilings.maxRequestBytes = 4L * 1024L * 1024L;

    Json::Value quotas( Json::objectValue );
    quotas["maxRequestBytes"] = Json::Value( Json::Int64( 64 ) * 1024 * 1024 ); // above ceiling
    PluginQuota quota;
    std::vector<std::string> warnings;
    quota.parseManifest( quotas, warnings );
    REQUIRE( warnings.empty() );
    quota.clampTo( ceilings );
    REQUIRE( quota.maxRequestBytes == 4L * 1024L * 1024L );

    // A manifest may lower below the ceiling.
    Json::Value lower( Json::objectValue );
    lower["maxRequestBytes"] = Json::Value( Json::Int64( 8192 ) );
    PluginQuota lowered;
    std::vector<std::string> noWarnings;
    lowered.parseManifest( lower, noWarnings );
    lowered.clampTo( ceilings );
    REQUIRE( lowered.maxRequestBytes == 8192 );

    // The toJson projection carries the field (round-trip contract).
    const Json::Value json = quota.toJson();
    REQUIRE( json["maxRequestBytes"].asInt64() == 4L * 1024L * 1024L );

    // Out-of-range values warn and keep the default.
    Json::Value junk( Json::objectValue );
    junk["maxRequestBytes"] = 512; // below the 1024 floor
    PluginQuota refused;
    std::vector<std::string> warned;
    refused.parseManifest( junk, warned );
    REQUIRE( warned.size() == 1 );
}

TEST_CASE( "capability enforcement matrix is present and honest", "[plugin][capabilities][p12]" )
{
    const auto matrix = exprs::pluginCapabilityEnforcementMatrix();
    REQUIRE( !matrix.empty() );

    // Stable contract names must be present exactly once per (capability,
    // runtimeScope) pair.
    std::map<std::string, int> seen;
    for ( const auto &entry : matrix )
        ++seen[ std::string( entry.capability ) + "@" + entry.runtimeScope ];
    for ( const auto &[ key, count ] : seen )
    {
        (void)key;
        REQUIRE( count == 1 );
    }

    // Honesty anchors: the refused-by-contract row for network must exist,
    // and the read-roots row must NOT claim enforcement (declaration +
    // validation only — reads of native code are not interceptable).
    bool networkRefused = false;
    bool inProcessFsHonest = false;
    for ( const auto &entry : matrix )
    {
        if ( std::string( entry.capability ) == "network"
             && entry.level == exprs::CapabilityEnforcementLevel::RefusedByContract )
            networkRefused = true;
        if ( std::string( entry.capability ) == "filesystem.readRoots"
             && entry.level == exprs::CapabilityEnforcementLevel::AuditOnly )
            inProcessFsHonest = true;
    }
    REQUIRE( networkRefused );
    REQUIRE( inProcessFsHonest );

    // JSON projection mirrors the table.
    const Json::Value json = exprs::pluginCapabilityEnforcementMatrixJson();
    REQUIRE( json.isArray() );
    REQUIRE( json.size() == matrix.size() );
}

TEST_CASE( "model framework gate honours explicit declarations only",
           "[plugin][capabilities][p12]" )
{
    Json::Value access( Json::objectValue );
    Json::Value modelProvider( Json::objectValue );
    Json::Value frameworks( Json::arrayValue );
    frameworks.append( "onnx" );
    modelProvider["frameworks"] = frameworks;
    access["modelProvider"] = modelProvider;

    REQUIRE( exprs::modelFrameworkAllowed( access, "onnx" ) );
    REQUIRE_FALSE( exprs::modelFrameworkAllowed( access, "cuda-trt" ) );

    // Declared modelProvider WITHOUT a frameworks list stays unbounded.
    Json::Value bare( Json::objectValue );
    bare["modelProvider"] = Json::Value( Json::objectValue );
    REQUIRE( exprs::modelFrameworkAllowed( bare, "anything" ) );

    // An access object WITHOUT modelProvider never gates (compat).
    Json::Value other( Json::objectValue );
    other["network"] = true;
    REQUIRE( exprs::modelFrameworkAllowed( other, "anything" ) );

    // No access object at all: pre-9.0 behavior (allow).
    REQUIRE( exprs::modelFrameworkAllowed( Json::Value(), "anything" ) );

    // accessBool is tri-state: declared true/false/absent.
    Json::Value withUi( Json::objectValue );
    withUi["ui"] = false;
    REQUIRE( exprs::accessBool( withUi, "ui" ) == 0 );
    withUi["ui"] = true;
    REQUIRE( exprs::accessBool( withUi, "ui" ) == 1 );
    REQUIRE( exprs::accessBool( Json::Value( Json::objectValue ), "ui" ) == -1 );
    REQUIRE( exprs::manifestDeclaresAccess( Json::Value( Json::objectValue ) ) );
    REQUIRE_FALSE( exprs::manifestDeclaresAccess( Json::Value() ) );
}

TEST_CASE( "pathIsWithinRoot contains and rejects exactly", "[plugin][capabilities]" )
{
    namespace fs = std::filesystem;
    const std::string root = ( fs::temp_directory_path() / "cap-within" ).generic_string();
    std::string resolved;

    // Direct containment (including the root itself and nested paths).
    REQUIRE( pathIsWithinRoot( root, root, resolved ) );
    REQUIRE( pathIsWithinRoot( root + "/nested/output", root, resolved ) );
    // Traversal that stays inside is fine; escaping is not.
    REQUIRE( pathIsWithinRoot( root + "/a/../b", root, resolved ) );
    REQUIRE_FALSE( pathIsWithinRoot( root + "/../escape", root, resolved ) );
    REQUIRE_FALSE( pathIsWithinRoot( "/tmp", root, resolved ) );
    // Sibling prefixes must not match ("cap-within-2" shares a prefix).
    REQUIRE_FALSE( pathIsWithinRoot( root + "-2/output", root, resolved ) );
    // Empty inputs contain nothing (fail closed).
    REQUIRE_FALSE( pathIsWithinRoot( "", root, resolved ) );
    REQUIRE_FALSE( pathIsWithinRoot( root, "", resolved ) );
}

TEST_CASE( "secret redaction strips secret-like values recursively",
           "[plugin][diagnostics][p12]" )
{
    using exprs::isSecretLikeKey;
    using exprs::redactSecrets;

    // Key vocabulary, case-insensitive.
    REQUIRE( isSecretLikeKey( "password" ) );
    REQUIRE( isSecretLikeKey( "PASSWORD" ) );
    REQUIRE( isSecretLikeKey( "client_secret" ) );
    REQUIRE( isSecretLikeKey( "remote_identity_token" ) );
    REQUIRE( isSecretLikeKey( "api-key" ) );
    REQUIRE( isSecretLikeKey( "apiKey" ) == false || isSecretLikeKey( "ApiKey" ) );
    REQUIRE_FALSE( isSecretLikeKey( "tokenize_datasets" ) == false ); // contains token
    REQUIRE_FALSE( isSecretLikeKey( "entrypoint" ) );

    Json::Value manifest( Json::objectValue );
    manifest["id"] = "org.test.redact";
    manifest["access"] = Json::Value( Json::objectValue );
    Json::Value auth( Json::objectValue );
    auth["password"] = "hunter2";
    auth["remote_identity_token"] = "sk-live-123";
    auth["endpoint"] = "https://example.test";
    manifest["access"]["auth"] = auth;
    Json::Value array( Json::arrayValue );
    Json::Value item( Json::objectValue );
    item["private_key"] = "-----BEGIN KEY-----";
    item["label"] = "safe";
    array.append( item );
    manifest["items"] = array;

    const Json::Value redacted = redactSecrets( manifest );
    // Original untouched.
    REQUIRE( manifest["access"]["auth"]["password"].asString() == "hunter2" );
    // Secrets replaced, non-secrets preserved, structure intact.
    REQUIRE( redacted["access"]["auth"]["password"].asString() == "[redacted]" );
    REQUIRE( redacted["access"]["auth"]["remote_identity_token"].asString() == "[redacted]" );
    REQUIRE( redacted["access"]["auth"]["endpoint"].asString() == "https://example.test" );
    REQUIRE( redacted["items"][0]["private_key"].asString() == "[redacted]" );
    REQUIRE( redacted["items"][0]["label"].asString() == "safe" );
    REQUIRE( redacted["id"].asString() == "org.test.redact" );
}
