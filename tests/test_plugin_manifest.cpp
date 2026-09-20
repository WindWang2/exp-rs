// tests/test_plugin_manifest.cpp — Manifest v1 parsing + validator contract
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include <filesystem>
#include <fstream>
#include <functional>

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

TEST_CASE( "exhaustive manifest round trip: every declared field survives",
           "[plugin][manifest][p12]" )
{
    // Mechanical 9.0 audit: a manifest with EVERY field populated must
    // survive toJson -> fromJson -> toJson with a byte-identical second
    // projection. (8.0 found three fields silently dropped by toJson; this
    // test exists so a fourth cannot land unnoticed.)
    std::string json = R"({
        "manifest_version": 1,
        "id": "org.example.full",
        "name": "Full",
        "version": "9.1.2",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "description": "every field",
        "vendor": "Example GmbH",
        "license": "MIT",
        "platforms": ["linux", "windows"],
        "entrypoint": "libfull.so",
        "entrypoint_kind": "native",
        "runtime": "host-process",
        "capabilities": ["operator", "data_provider", "model_runtime", "agent_tool", "ui"],
        "permissions": ["filesystem_read", "filesystem_write", "network"],
        "dependencies": ["org.other@^1.0"],
        "python": { "module": "full", "package": "full_pkg" },
        "access": {
            "filesystem": { "read": ["${workspace}/inputs"], "write": ["${temp}"] },
            "network": true,
            "externalProcess": false,
            "gpu": { "hint": "cuda" },
            "workspace": { "mutate": false },
            "project": { "mutate": false },
            "modelProvider": { "frameworks": ["onnx"] },
            "ui": true,
            "destructive": false
        },
        "quotas": {
            "maxRequestConcurrency": 2,
            "requestDeadlineMs": 60000,
            "maxResponseBytes": 1048576,
            "maxRequestBytes": 1048576,
            "workerMemoryBytes": 536870912,
            "workerCpuRatePercent": 50,
            "maxChildProcesses": 2,
            "gpuHint": "cuda"
        },
        "package": {
            "checksums": { "libfull.so": "deadbeef" },
            "sbom": { "path": "sbom.json", "format": "cyclonedx" },
            "signature": { "algorithm": "ed25519", "value": "cafe" }
        },
        "conformance": {
            "cancelTarget": "full:cancel",
            "concurrencyTarget": "full:gate",
            "crashTarget": "full:crash",
            "uiSchema": true,
            "dataProviderTarget": "full:store",
            "agentToolTarget": "full:tool",
            "modelFrameworkTarget": "onnx"
        },
        "operators": [{
            "id": "full:stats",
            "display_name": "Full Stats",
            "group": "full",
            "description": "operator with everything",
            "memory_policy": "streaming",
            "determinism_grade": "tolerance",
            "supports_cancel": true,
            "schema": { "type": "object" },
            "metadata": { "agent": true },
            "inputs": [{ "name": "values", "type": "json", "required": true,
                         "description": "in", "default": [1, 2],
                         "min": 0.0, "max": 9.0, "file_format": "tif" }],
            "outputs": [{ "name": "out", "type": "raster", "required": false }]
        }, {
            "id": "full:tool-run",
            "display_name": "External Tool",
            "group": "full",
            "description": "pure-manifest external tool",
            "external": {
                "argv": ["${plugin_dir}/tools/run.sh", "--in", "${input}"],
                "environment": { "FULL_MODE": "1" },
                "inherit_environment": false,
                "working_directory": "workdir",
                "timeout_seconds": 120,
                "stdout_limit_bytes": 1024,
                "stderr_limit_bytes": 1024
            }
        }],
        "data_providers": [{
            "id": "full:store",
            "display_name": "Full Store",
            "description": "store",
            "schemes": ["isodb://"],
            "capabilities": { "maxPageSize": 100 }
        }],
        "model_runtimes": [{
            "framework": "onnx",
            "display_name": "ONNX Full",
            "description": "runtime",
            "gpu": true
        }],
        "agent_tools": [{
            "id": "full:tool",
            "display_name": "Full Tool",
            "category": "demo",
            "description": "tool",
            "input_schema": { "type": "object" },
            "output_schema": { "type": "object" }
        }],
        "ui": { "dock": true, "menu_actions": true, "settings_page": true,
                "dock_title": "Full", "settings_page_title": "Full Settings" },
        "cartography": { "layout_items": true, "layout_item_ids": ["full:item"] }
    })";
    json = replaceApi( json );

    PluginManifest manifest = parseOk( json );
    REQUIRE( manifest.runtime == PluginRuntimeKind::HostProcess );
    REQUIRE( manifest.operators.size() == 2 );
    REQUIRE( manifest.operators[1].hasExternalTool );
    REQUIRE( manifest.dataProviders.size() == 1 );
    REQUIRE( manifest.modelRuntimes.size() == 1 );
    REQUIRE( manifest.agentTools.size() == 1 );
    REQUIRE( manifest.hasUi );
    REQUIRE( manifest.hasCartography );
    REQUIRE( manifest.python.module == "full" );
    REQUIRE_FALSE( manifest.access.isNull() );
    REQUIRE_FALSE( manifest.quotas.isNull() );
    REQUIRE_FALSE( manifest.package.isNull() );
    REQUIRE_FALSE( manifest.conformance.isNull() );

    const Json::Value first = manifest.toJson();
    PluginManifest restored;
    PluginDiagnostic error;
    REQUIRE( PluginManifest::fromJson( first, restored, error ) );
    const Json::Value second = restored.toJson();

    // Idempotence: re-projecting a parsed manifest must be stable.
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    REQUIRE( Json::writeString( builder, first ) == Json::writeString( builder, second ) );

    // COMPLETENESS (the actual regression class): every leaf path present in
    // the ORIGINAL hand-written document must also exist in `first`. A
    // toJson() that drops a field makes first lack it — the idempotence
    // check above cannot see that, this walk can. (jsoncpp object members
    // are key-sorted, so equal documents also serialize identically.)
    std::function<void( const Json::Value &, const Json::Value &, const std::string & )>
        requirePathsSurvive = [&]( const Json::Value &original, const Json::Value &projected,
                                   const std::string &path ) {
            if ( original.isObject() )
            {
                REQUIRE( projected.isObject() );
                for ( const std::string &key : original.getMemberNames() )
                {
                    const std::string childPath = path + "/" + key;
                    INFO( "manifest path dropped by toJson(): " << childPath );
                    REQUIRE( projected.isMember( key ) );
                    requirePathsSurvive( original[ key ], projected[ key ], childPath );
                }
            }
            else if ( original.isArray() )
            {
                REQUIRE( projected.isArray() );
                REQUIRE( projected.size() == original.size() );
                for ( Json::ArrayIndex i = 0; i < original.size(); ++i )
                    requirePathsSurvive( original[ i ], projected[ i ],
                                         path + "[" + std::to_string( i ) + "]" );
            }
            // Leaves: existence already asserted by the parent walk.
        };
    {
        Json::Value originalDocument;
        Json::Reader reader;
        REQUIRE( reader.parse( json, originalDocument, false ) );
        requirePathsSurvive( originalDocument, first, "" );
    }

    // Spot anchors (fail with a message a human can act on):
    REQUIRE( restored.conformance["dataProviderTarget"].asString() == "full:store" );
    REQUIRE( restored.package["checksums"]["libfull.so"].asString() == "deadbeef" );
    REQUIRE( restored.quotas["maxRequestBytes"].asInt64() == 1048576 );
    REQUIRE( restored.access["externalProcess"].asBool() == false );
    REQUIRE( restored.operators[1].external.argv.size() == 3 );
    REQUIRE( restored.dataProviders[0].schemes == std::vector<std::string>{ "isodb://" } );
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
    SECTION( "wrong-typed manifest version is a typed field error (issue #1038)" )
    {
        const std::string path = writeTemp( "bad-version.json", R"({"manifest_version":"1"})" );
        REQUIRE_FALSE( loadManifestFromFile( path, manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "manifest_version" );
    }
}

TEST_CASE( "wrong-typed manifest fields fail typed, never throw (issue #1038)",
           "[plugin][manifest]" )
{
    // A malformed plugin.json used to throw Json::LogicError out of
    // PluginManifest::fromJson -> PluginDiscovery::scan -> application death
    // at startup/refresh. Every listed conversion is type-guarded and the
    // whole parse sits behind a totality boundary.
    auto parse = []( const std::string &jsonText ) {
        Json::Value root;
        Json::Reader reader;
        REQUIRE( reader.parse( jsonText, root, false ) );
        PluginManifest manifest;
        PluginDiagnostic error;
        bool ok = false;
        REQUIRE_NOTHROW( ok = PluginManifest::fromJson( root, manifest, error ) );
        return std::make_tuple( ok, error );
    };

    SECTION( "manifest_version as a string" )
    {
        auto [ok, error] = parse( R"({"manifest_version":"1"})" );
        REQUIRE_FALSE( ok );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "manifest_version" );
        REQUIRE_FALSE( error.message.empty() );
    }
    SECTION( "id as an array" )
    {
        auto [ok, error] = parse( R"({"id":[]})" );
        REQUIRE_FALSE( ok );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "id" );
    }
    SECTION( "abi_version as a string" )
    {
        auto [ok, error] = parse( R"({"abi_version":"1"})" );
        REQUIRE_FALSE( ok );
        REQUIRE( error.field == "abi_version" );
    }
    SECTION( "entrypoint_kind as an object" )
    {
        auto [ok, error] = parse( R"({"entrypoint_kind":{}})" );
        REQUIRE_FALSE( ok );
        REQUIRE( error.field == "entrypoint_kind" );
    }
    SECTION( "runtime as an array" )
    {
        auto [ok, error] = parse( R"({"runtime":[]})" );
        REQUIRE_FALSE( ok );
        REQUIRE( error.field == "runtime" );
    }
    SECTION( "residual sub-parser type error is converted, not thrown" )
    {
        // supports_cancel: [] throws inside ManifestOperator::fromJson; the
        // fromJson totality boundary must answer false with a diagnostic.
        auto [ok, error] = parse( R"({
            "id": "org.example.bad",
            "operators": [{ "id": "bad:op", "display_name": "Bad",
                            "supports_cancel": [] }]
        })" );
        REQUIRE_FALSE( ok );
        REQUIRE_FALSE( error.message.empty() );
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

TEST_CASE( "host API range negotiation rejects before any code runs", "[plugin][version][p12]" )
{
    // Bound parsing: exactly "MAJOR.MINOR" numeric; everything else fails.
    PluginApiVersion parsed{};
    REQUIRE( parseApiVersion( "3.0", parsed ) );
    REQUIRE( parsed.major == 3 );
    REQUIRE( parsed.minor == 0 );
    REQUIRE( parseApiVersion( "10.42", parsed ) );
    REQUIRE( parsed.major == 10 );
    REQUIRE( parsed.minor == 42 );
    REQUIRE_FALSE( parseApiVersion( "", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "3", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "3.0.1", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "3.", parsed ) );
    REQUIRE_FALSE( parseApiVersion( ".0", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "3.x", parsed ) );

    std::string error;
    std::string offendingField;
    // Inside the declared range (both bounds inclusive).
    REQUIRE( isHostApiWithinRange( { 3, 0 }, "3.0", "3.2", error, offendingField ) );
    REQUIRE( isHostApiWithinRange( { 3, 1 }, "3.0", "3.2", error, offendingField ) );
    REQUIRE( isHostApiWithinRange( { 3, 2 }, "3.0", "3.2", error, offendingField ) );
    REQUIRE( isHostApiWithinRange( { 3, 1 }, "3.0", "", error, offendingField ) );
    REQUIRE( isHostApiWithinRange( { 3, 1 }, "", "3.2", error, offendingField ) );
    REQUIRE( isHostApiWithinRange( { 3, 1 }, "", "", error, offendingField ) );

    // Below the minimum / above the maximum: rejected, with the offending
    // bound surfaced as `expected` so a diagnostic can name the field.
    REQUIRE_FALSE( isHostApiWithinRange( { 2, 9 }, "3.0", "", error, offendingField ) );
    REQUIRE( offendingField == "min_host_api" );
    REQUIRE( error.find( "below" ) != std::string::npos );
    REQUIRE_FALSE( isHostApiWithinRange( { 3, 3 }, "", "3.2", error, offendingField ) );
    REQUIRE( offendingField == "max_host_api" );
    REQUIRE( error.find( "above" ) != std::string::npos );
    REQUIRE_FALSE( isHostApiWithinRange( { 4, 0 }, "3.0", "3.2", error, offendingField ) );

    // A malformed bound fails closed (never an exception).
    REQUIRE_FALSE( isHostApiWithinRange( { 3, 0 }, "abc", "", error, offendingField ) );
    REQUIRE( error.find( "min_host_api" ) != std::string::npos );
    REQUIRE_FALSE( isHostApiWithinRange( { 3, 0 }, "", "3.0.0", error, offendingField ) );
    REQUIRE( error.find( "max_host_api" ) != std::string::npos );

    // Validator end-to-end: the range is enforced through the typed
    // compatibility gate (E2001), before any entrypoint is touched.
    PluginManifest manifest = parseOk( replaceApi( R"({
        "manifest_version": 1,
        "id": "org.test.range",
        "name": "Range",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "min_host_api": "3.0",
        "max_host_api": "3.0",
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })" ) );
    REQUIRE( manifest.minHostApi == "3.0" );
    REQUIRE( manifest.maxHostApi == "3.0" );

    PluginDiagnosticLog log;
    PluginManifest current = manifest;
    REQUIRE( PluginManifestValidator::validate( current, validRequest(), log ) );
    REQUIRE_FALSE( log.hasErrorsFor( "org.test.range" ) );

    // Round-trip: the bounds survive toJson()/fromJson() (load params and the
    // discovery index both travel through them).
    const Json::Value json = current.toJson();
    REQUIRE( json.isMember( "min_host_api" ) );
    PluginManifest reparsed;
    PluginDiagnostic parseError;
    REQUIRE( PluginManifest::fromJson( json, reparsed, parseError ) );
    REQUIRE( reparsed.minHostApi == "3.0" );
    REQUIRE( reparsed.maxHostApi == "3.0" );

    // Host inside the declared range passes; a host outside it is refused
    // typed before execution (old plugin refuses, never silently runs).
    PluginValidationRequest outOfRange = validRequest();
    outOfRange.hostApi = { 3, 5 };
    PluginDiagnosticLog rejectedLog;
    PluginManifest rejected = manifest;
    REQUIRE_FALSE( PluginManifestValidator::validate( rejected, outOfRange, rejectedLog ) );
    bool sawRangeError = false;
    for ( const PluginDiagnostic &item : rejectedLog.items() )
    {
        if ( item.pluginId == "org.test.range"
             && item.code == PluginDiagnosticCode::ApiVersionMismatch )
            sawRangeError = true;
    }
    REQUIRE( sawRangeError );

    // The declared minimum itself is INSIDE the range (inclusive bounds):
    // host 3.0 against [3.0, 3.0] validates.
    PluginValidationRequest atMinimum = validRequest();
    atMinimum.hostApi = { 3, 0 };
    PluginDiagnosticLog atMinimumLog;
    PluginManifest atMin = manifest;
    REQUIRE( PluginManifestValidator::validate( atMin, atMinimum, atMinimumLog ) );
    REQUIRE_FALSE( atMinimumLog.hasErrorsFor( "org.test.range" ) );

    // A host OLDER than the declared minimum (same API major, so the plain
    // gate passes) is refused by the RANGE gate, and the diagnostic names the
    // min bound as the offending field.
    PluginManifest minOnly = parseOk( replaceApi( R"({
        "manifest_version": 1,
        "id": "org.test.range",
        "name": "Range",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "min_host_api": ")" + std::to_string( pluginApiVersion().major ) + "."
               + std::to_string( pluginApiVersion().minor + 5 ) + R"(",
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })" ) );
    PluginValidationRequest tooOld = validRequest();
    tooOld.hostApi = { pluginApiVersion().major, pluginApiVersion().minor };
    PluginDiagnosticLog tooOldLog;
    REQUIRE_FALSE( PluginManifestValidator::validate( minOnly, tooOld, tooOldLog ) );
    bool sawMinField = false;
    for ( const PluginDiagnostic &item : tooOldLog.items() )
    {
        if ( item.pluginId == "org.test.range"
             && item.code == PluginDiagnosticCode::ApiVersionMismatch
             && item.field == "min_host_api" )
            sawMinField = true;
    }
    REQUIRE( sawMinField );

    // A malformed max bound is attributed to max_host_api (not guessed as
    // the min field), and an inverted range (min above max) fails typed.
    PluginManifest badMax = parseOk( replaceApi( R"({
        "manifest_version": 1,
        "id": "org.test.badmax",
        "name": "BadMax",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "max_host_api": "3.x",
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })" ) );
    PluginDiagnosticLog badMaxLog;
    REQUIRE_FALSE( PluginManifestValidator::validate( badMax, validRequest(), badMaxLog ) );
    bool sawMaxField = false;
    for ( const PluginDiagnostic &item : badMaxLog.items() )
    {
        if ( item.pluginId == "org.test.badmax"
             && item.code == PluginDiagnosticCode::ManifestInvalidField
             && item.field == "max_host_api" )
            sawMaxField = true;
    }
    REQUIRE( sawMaxField );

    PluginManifest inverted = parseOk( replaceApi( R"({
        "manifest_version": 1,
        "id": "org.test.inverted",
        "name": "Inverted",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "min_host_api": "4.0",
        "max_host_api": "3.0",
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })" ) );
    PluginDiagnosticLog invertedLog;
    REQUIRE_FALSE(
        PluginManifestValidator::validate( inverted, validRequest(), invertedLog ) );
    bool sawInverted = false;
    for ( const PluginDiagnostic &item : invertedLog.items() )
    {
        if ( item.pluginId == "org.test.inverted"
             && item.message.find( "empty (min above max)" ) != std::string::npos )
            sawInverted = true;
    }
    REQUIRE( sawInverted );

    // Signed / spaced bounds are rejected (digit-only parsing).
    REQUIRE_FALSE( parseApiVersion( "-1.0", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "+3.0", parsed ) );
    REQUIRE_FALSE( parseApiVersion( " 3.0", parsed ) );
    REQUIRE_FALSE( parseApiVersion( "3.0 ", parsed ) );
}

TEST_CASE( "manifests without the optional range keep the old semantics",
           "[plugin][version][p12]" )
{
    // A manifest that declares no min/max keeps the plain rule: no new
    // diagnostics, and the round-trip carries no empty fields either.
    PluginManifest manifest = parseOk( replaceApi( R"({
        "manifest_version": 1,
        "id": "org.test.norange",
        "name": "NoRange",
        "version": "1.0.0",
        "api_version": PLACEHOLDER,
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })" ) );
    REQUIRE( manifest.minHostApi.empty() );
    REQUIRE( manifest.maxHostApi.empty() );
    PluginDiagnosticLog log;
    REQUIRE( PluginManifestValidator::validate( manifest, validRequest(), log ) );
    REQUIRE_FALSE( log.hasErrorsFor( "org.test.norange" ) );
    REQUIRE_FALSE( manifest.toJson().isMember( "min_host_api" ) );
    REQUIRE_FALSE( manifest.toJson().isMember( "max_host_api" ) );
}

TEST_CASE( "wrong-typed host API bounds fail typed", "[plugin][version][p12]" )
{
    // Issue #1038 pattern: a wrong-typed declaration must be a typed field
    // error, never an exception out of discovery.
    Json::Value root;
    Json::Reader reader;
    REQUIRE( reader.parse( R"({
        "manifest_version": 1,
        "id": "org.test.badrange",
        "name": "BadRange",
        "version": "1.0.0",
        "api_version": "3.0",
        "min_host_api": 3,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator"]
    })", root, false ) );
    PluginManifest manifest;
    PluginDiagnostic error;
    REQUIRE_FALSE( PluginManifest::fromJson( root, manifest, error ) );
    REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
    REQUIRE( error.field == "min_host_api" );
}

TEST_CASE( "entrypoint containment (issue #756)", "[plugin][validator][containment]" )
{
    namespace fs = std::filesystem;
    const std::string root = "/tmp/exprs_test_containment";
    fs::remove_all( root );
    fs::create_directories( root + "/org.test.containment/sub" );
    const std::string pluginDir = root + "/org.test.containment";

    // A legal payload inside the plugin dir, and a decoy outside it.
    { std::ofstream output( pluginDir + "/sub/libreal.so", std::ios::binary ); output << "payload"; }
    { std::ofstream output( root + "/libescape.so", std::ios::binary ); output << "escape"; }
    std::error_code linkError;
    fs::create_symlink( root + "/libescape.so", fs::path( pluginDir + "/sub/liblink.so" ),
                        linkError );

    auto manifestWithEntrypoint = []( const std::string &entrypoint ) {
        // Minimal native-kind manifest struct (not parsed from JSON).
        PluginManifest manifest;
        manifest.manifestVersion = 1;
        manifest.id = "org.test.containment";
        manifest.name = "Containment";
        manifest.version = "1.0.0";
        manifest.apiVersion = std::string( EXP_RS_PLUGIN_API_VERSION );
        manifest.abiVersion = pluginAbiVersion();
        manifest.entrypoint = entrypoint;
        manifest.entrypointKind = PluginEntrypointKind::Native;
        return manifest;
    };

    auto validateInDir = []( const PluginManifest &manifest ) {
        PluginValidationRequest request;
        request.pluginDir = "/tmp/exprs_test_containment/org.test.containment";
        PluginDiagnosticLog log;
        const bool ok = PluginManifestValidator::validate( manifest, request, log );
        return std::make_tuple( ok, log );
    };

    auto hasCode = []( const PluginDiagnosticLog &log, PluginDiagnosticCode code ) {
        for ( const auto &item : log.items() )
        {
            if ( item.code == code && item.severity == PluginDiagnosticSeverity::Error )
                return true;
        }
        return false;
    };

    SECTION( "nested legal path validates and resolves inside the root" )
    {
        auto [ok, log] = validateInDir( manifestWithEntrypoint( "sub/libreal.so" ) );
        REQUIRE( ok );
        REQUIRE_FALSE( hasCode( log, PluginDiagnosticCode::EntrypointOutsideRoot ) );
    }
    SECTION( ".. escape is rejected" )
    {
        auto [ok, log] = validateInDir( manifestWithEntrypoint( "../libescape.so" ) );
        REQUIRE_FALSE( ok );
        REQUIRE( hasCode( log, PluginDiagnosticCode::EntrypointOutsideRoot ) );
    }
    SECTION( "absolute path is rejected" )
    {
        auto [ok, log] =
            validateInDir( manifestWithEntrypoint( "/tmp/exprs_test_containment/libescape.so" ) );
        REQUIRE_FALSE( ok );
        REQUIRE( hasCode( log, PluginDiagnosticCode::EntrypointOutsideRoot ) );
    }
    SECTION( "symlink escape is rejected" )
    {
        if ( linkError )
        {
            WARN( "symlinks unsupported on this platform; section skipped" );
            return;
        }
        auto [ok, log] = validateInDir( manifestWithEntrypoint( "sub/liblink.so" ) );
        REQUIRE_FALSE( ok );
        REQUIRE( hasCode( log, PluginDiagnosticCode::EntrypointOutsideRoot ) );
    }
    SECTION( "missing file is reported as missing, not an escape" )
    {
        auto [ok, log] = validateInDir( manifestWithEntrypoint( "sub/libabsent.so" ) );
        REQUIRE_FALSE( ok );
        REQUIRE( hasCode( log, PluginDiagnosticCode::EntrypointMissing ) );
    }
    SECTION( "python.package lexical escape is rejected" )
    {
        PluginManifest manifest = manifestWithEntrypoint( {} );
        manifest.entrypointKind = PluginEntrypointKind::Python;
        manifest.python.module = "some_module";
        manifest.python.package = "../../outside";
        auto [ok, log] = validateInDir( manifest );
        REQUIRE_FALSE( ok );
        REQUIRE( hasCode( log, PluginDiagnosticCode::EntrypointOutsideRoot ) );
    }
    fs::remove_all( root );
}
