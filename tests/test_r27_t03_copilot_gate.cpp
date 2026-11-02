// tests/test_r27_t03_copilot_gate.cpp — #1763 (AS-014)
//
// The GUI Copilot single-call gate, tested at the policy header level (the
// same seam the dock calls): containment + remote default-deny on every
// string argument of a single tool call, free-text slots exempt, and
// write-shaped calls flagged for the human approval card. The MCP surface
// had this policy; the copilot dispatcher did not — prompt injection via
// tool results could drive arbitrary-path read/write or /vsicurl fetches.
#include "agent/copilot_tool_gate.h"

#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::agent::copilot_gate;

namespace
{
struct EnvGuard
{
    QByteArray workspace = qgetenv( "SICNU_MCP_WORKSPACE" );
    bool hadWorkspace = qEnvironmentVariableIsSet( "SICNU_MCP_WORKSPACE" );
    QByteArray remote = qgetenv( "SICNU_MCP_ALLOW_REMOTE" );
    bool hadRemote = qEnvironmentVariableIsSet( "SICNU_MCP_ALLOW_REMOTE" );
    ~EnvGuard()
    {
        if ( hadWorkspace )
            qputenv( "SICNU_MCP_WORKSPACE", workspace );
        else
            qunsetenv( "SICNU_MCP_WORKSPACE" );
        if ( hadRemote )
            qputenv( "SICNU_MCP_ALLOW_REMOTE", remote );
        else
            qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    }
};

Json::Value envelope( const char *name, const Json::Value &parameters )
{
    Json::Value doc( Json::objectValue );
    doc["name"] = name;
    doc["parameters"] = parameters;
    return doc;
}
} // namespace

TEST_CASE( "copilot single calls are contained and remote-denied (#1763)",
           "[r27t03][copilot][p2]" )
{
    EnvGuard guard;
    qunsetenv( "SICNU_MCP_ALLOW_REMOTE" );
    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );
    qputenv( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );

    // Arbitrary-path read / write shapes must be refused.
    {
        Json::Value params( Json::objectValue );
        params["input_path"] = "/etc/passwd";
        const auto gate = gateSingleCall( envelope( "rs:ndvi", params ) );
        INFO( "input_path=/etc/passwd" );
        CHECK_FALSE( gate.refusalReason.isEmpty() );
        CHECK_FALSE( gate.needsApproval );
    }
    {
        Json::Value params( Json::objectValue );
        params["input_path"] = "/vsicurl/http://169.254.169.254/latest/meta-data/";
        const auto gate = gateSingleCall( envelope( "rs:ndvi", params ) );
        CHECK_FALSE( gate.refusalReason.isEmpty() );
    }
    {
        Json::Value params( Json::objectValue );
        params["input"] = "relative.tif";
        params["output_path"] = "/outside_workspace/result.tif";
        const auto gate = gateSingleCall( envelope( "rs:resample", params ) );
        CHECK_FALSE( gate.refusalReason.isEmpty() );
    }
    {
        // Nested structures are walked, not just top-level strings.
        Json::Value nested( Json::objectValue );
        nested["files"] = Json::Value( Json::arrayValue );
        Json::Value entry( Json::objectValue );
        entry["path"] = "GTIFF_DIR:1:/etc/shadow";
        nested["files"].append( entry );
        const auto gate = gateSingleCall( envelope( "gdal:merge", nested ) );
        CHECK_FALSE( gate.refusalReason.isEmpty() );
    }

    // In-workspace reads stay allowed and are NOT write-shaped.
    {
        Json::Value params( Json::objectValue );
        params["input_path"] = "data/scene.tif";
        const auto gate = gateSingleCall( envelope( "rs:ndvi", params ) );
        CHECK( gate.refusalReason.isEmpty() );
        CHECK_FALSE( gate.needsApproval );
    }

    // Free-text slots are prose, never path-scanned (a note that QUOTES an
    // absolute path must not poison the call).
    {
        Json::Value params( Json::objectValue );
        params["note"] = "the operator denied /etc/hosts access yesterday";
        params["input_path"] = "scene.tif";
        const auto gate = gateSingleCall( envelope( "rs:ndvi", params ) );
        CHECK( gate.refusalReason.isEmpty() );
    }

    // A write slot names an artifact: the call needs human approval even
    // when the target is INSIDE the workspace (the Confirm card shows it).
    {
        Json::Value params( Json::objectValue );
        params["input_path"] = "scene.tif";
        params["output_path"] = "ndvi_result.tif";
        const auto gate = gateSingleCall( envelope( "rs:ndvi", params ) );
        CHECK( gate.refusalReason.isEmpty() );
        CHECK( gate.needsApproval );
    }
}
