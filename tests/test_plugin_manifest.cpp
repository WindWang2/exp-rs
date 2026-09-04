// tests/test_plugin_manifest.cpp — Manifest v1 parsing + validator contract
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include <fstream>

using namespace exprs;

namespace {
std::string apiVersionLiteral()
{
    return std::string( "\"" ) + EXP_RS_PLUGIN_API_VERSION + "\"";
}

std::string replaceApi( std::string text )
{
    const std::string from = "PLACEHOLDER";
    const std::string to = apiVersionLiteral();
    const size_t position = text.find( from );
    if ( position != std::string::npos )
        text.replace( position, from.size(), to );
    return text;
}

std::string writeTemp( const std::string &name, const std::string &content )
{
    const std::string path = "/tmp/exprs_test_" + name;
    std::ofstream output( path, std::ios::trunc );
    output << content;
    return path;
}

PluginManifest parseOk( const std::string &json )
{
    Json::Value root;
    Json::Reader reader;
    REQUIRE( reader.parse( json, root, false ) );
    PluginManifest manifest;
    PluginDiagnostic error;
    REQUIRE( PluginManifest::fromJson( root, manifest, error ) );
    return manifest;
}

PluginValidationRequest validRequest()
{
    PluginValidationRequest request;
    request.pluginDir = {};
    request.hostApi = pluginApiVersion();
    request.hostAbi = pluginAbiVersion();
    return request;
}
} // namespace

TEST_CASE( "manifest v1 round trip", "[plugin][manifest]" )
{
    std::string json = R"({
        "manifest_version": 1,
        "id": "org.example.demo",
        "name": "Demo",
        "version": "1.2.3",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "capabilities": ["operator"],
        "permissions": ["filesystem_read"],
        "operators": [{
            "id": "demo:stats",
            "display_name": "Demo Stats",
            "group": "demo",
            "inputs": [{ "name": "values", "type": "json", "required": true }],
            "outputs": []
        }]
    })";
    json = replaceApi( json );
    PluginManifest manifest = parseOk( json );
    REQUIRE( manifest.id == "org.example.demo" );
    REQUIRE( manifest.manifestVersion == 1 );
    REQUIRE( manifest.operators.size() == 1 );
    REQUIRE( manifest.operators[0].inputs.size() == 1 );
    REQUIRE( manifest.operators[0].inputs[0].type == "json" );

    // toJson -> fromJson round trip preserves everything required.
    Json::Value written = manifest.toJson();
    PluginManifest restored;
    PluginDiagnostic error;
    REQUIRE( PluginManifest::fromJson( written, restored, error ) );
    REQUIRE( restored.id == manifest.id );
    REQUIRE( restored.operators.size() == manifest.operators.size() );
    REQUIRE( restored.operators[0].id == "demo:stats" );
}

TEST_CASE( "manifest load from file reports structured errors", "[plugin][manifest]" )
{
    PluginManifest manifest;
    PluginDiagnostic error;

    SECTION( "unreadable" )
    {
        REQUIRE_FALSE( loadManifestFromFile( "/nonexistent/path/plugin.json", manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestUnreadable );
    }
    SECTION( "invalid json" )
    {
        const std::string path = writeTemp( "bad.json", "{ not json" );
        REQUIRE_FALSE( loadManifestFromFile( path, manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidJson );
    }
    SECTION( "unknown manifest version" )
    {
        const std::string path = writeTemp( "v2.json", R"({"manifest_version": 2})" );
        REQUIRE_FALSE( loadManifestFromFile( path, manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestUnknownVersion );
    }
}

TEST_CASE( "validator enforces the manifest contract", "[plugin][validator]" )
{
    auto validate = []( const std::string &jsonText ) {
        PluginManifest manifest = parseOk( replaceApi( jsonText ) );
        PluginDiagnosticLog log;
        const bool ok = PluginManifestValidator::validate( manifest, validRequest(), log );
        return std::make_tuple( ok, log );
    };

    const std::string base = R"({
        "manifest_version": 1,
        "id": "org.example.valid",
        "name": "Valid",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"],
        "operators": [{ "id": "demo:x", "display_name": "X",
                        "external": { "argv": ["true"] } }]
    })";

    SECTION( "valid manifest passes" )
    {
        auto [ok, log] = validate( base );
        REQUIRE( ok );
        REQUIRE_FALSE( log.hasErrors() );
    }
    SECTION( "bad plugin id" )
    {
        auto [ok, log] = validate( R"({
            "manifest_version": 1, "id": "Not_A_DNS", "name": "X", "version": "1.0.0",
            "api_version": PLACEHOLDER, "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": []
        })" );
        REQUIRE_FALSE( ok );
        bool sawId = false;
        for ( const auto &item : log.items() )
            sawId = sawId || item.code == PluginDiagnosticCode::ManifestInvalidField;
        REQUIRE( sawId );
    }
    SECTION( "api version mismatch" )
    {
        auto [ok, log] = validate( R"({
            "manifest_version": 1, "id": "org.example.a", "name": "A", "version": "1.0.0",
            "api_version": "99.0", "abi_version": 1,
            "entrypoint_kind": "manifest", "operators": []
        })" );
        REQUIRE_FALSE( ok );
        REQUIRE( log.hasErrors() );
        bool sawApi = false;
        for ( const auto &item : log.items() )
            sawApi = sawApi || item.code == PluginDiagnosticCode::ApiVersionMismatch;
        REQUIRE( sawApi );
    }
    SECTION( "abi mismatch" )
    {
        auto [ok, log] = validate( R"({
            "manifest_version": 1, "id": "org.example.b", "name": "B", "version": "1.0.0",
            "api_version": PLACEHOLDER, "abi_version": 42,
            "entrypoint_kind": "manifest", "operators": []
        })" );
        REQUIRE_FALSE( ok );
    }
    SECTION( "manifest plugin operator without external section" )
    {
        auto [ok, log] = validate( R"({
            "manifest_version": 1, "id": "org.example.c", "name": "C", "version": "1.0.0",
            "api_version": PLACEHOLDER, "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [{ "id": "demo:y", "display_name": "Y" }]
        })" );
        REQUIRE_FALSE( ok );
    }
    SECTION( "duplicate operator ids" )
    {
        auto [ok, log] = validate( R"({
            "manifest_version": 1, "id": "org.example.d", "name": "D", "version": "1.0.0",
            "api_version": PLACEHOLDER, "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [
                { "id": "demo:x", "display_name": "X", "external": { "argv": ["true"] } },
                { "id": "demo:x", "display_name": "X2", "external": { "argv": ["true"] } }
            ]
        })" );
        REQUIRE_FALSE( ok );
    }
}

TEST_CASE( "permission parsing and capability implication", "[plugin][permissions]" )
{
    std::vector<std::string> warnings;
    Json::Value permissions( Json::arrayValue );
    permissions.append( "filesystem_read" );
    permissions.append( "not_a_permission" );
    const auto parsed = parsePermissions( permissions, warnings );
    REQUIRE( parsed.size() == 1 );
    REQUIRE( parsed[0] == PluginPermission::FilesystemRead );
    REQUIRE( warnings.size() == 1 );

    const auto implied = requiredPermissionsForCapability( "data_provider" );
    REQUIRE( implied.size() == 1 );
    REQUIRE( implied[0] == PluginPermission::FilesystemRead );
}

TEST_CASE( "api compatibility rule", "[plugin][version]" )
{
    REQUIRE( isPluginApiCompatible( { 3, 0 }, { 3, 0 } ) );
    REQUIRE( isPluginApiCompatible( { 3, 1 }, { 3, 0 } ) );
    REQUIRE_FALSE( isPluginApiCompatible( { 3, 0 }, { 3, 1 } ) );
    REQUIRE_FALSE( isPluginApiCompatible( { 3, 0 }, { 4, 0 } ) );
}
