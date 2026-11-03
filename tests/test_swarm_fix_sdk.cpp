// tests/test_swarm_fix_sdk.cpp — swarm-review 2026-11-04 SDK-series regression
// guards (SWARM_REVIEW.md findings SDK-1 / SDK-2 / SDK-3).
//
//   SDK-1  a manifest_version outside the INT domain (e.g. 4294967296) is
//          integral for jsoncpp (isIntegral() true) but asInt() THROWS on it.
//          The old isIntegral()-only gate let that LogicError escape
//          loadManifestFromFile (the only try/catch there wrapped the parse),
//          through PluginDiscovery::scan -> registry -> startup, with no catch
//          anywhere on the chain (#1038 totality hole).
//   SDK-2  a host-process worker's registration report is UNTRUSTED input: a
//          forged non-string element ({"operators":[{}]}) made asString()
//          throw Json::LogicError inside PluginHostProcessRuntime::loadPlugin,
//          which has no catch — the host process died and the isolation
//          contract was breached. The worker side already had the peerString
//          gate (plugin_host_worker_main.cpp) and the proxies had the
//          infer()/describeUiSchema gates; the load path did not.
//   SDK-3  with SIGCHLD set to SIG_IGN the kernel auto-reaps children, so
//          waitpid() fails with ECHILD instead of returning a status. The
//          three-fd EOF fast path breaks out of the poll loop BEFORE the
//          WNOHANG liveness probe, so the post-loop blocking waitpid ran with
//          reaped==false and decoded its untouched (zero) status as exit code
//          0 — a FAILED tool reported as a success (and published).
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"
#include "exprs/host_protocol.h"
#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_discovery.h"
#include "exprs/plugin_interface.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_quotas.h"
#include "exprs/plugin_record.h"
#include "exprs/version.h"

#include "plugins/host/plugin_host_process_runtime.h"
#include "plugins/host/plugin_host_protocol.h"

#include "support/exprs_test_env.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <json/json.h>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace exprs;

namespace {

const exprs_test::ScratchGuard kScratch{ "swarm_fix_sdk" };

void writeTextFile( const std::string &path, const std::string &content )
{
    std::ofstream output( path, std::ios::trunc | std::ios::binary );
    output << content;
}

void replaceAll( std::string &text, const std::string &from, const std::string &to )
{
    for ( size_t position = text.find( from ); position != std::string::npos;
          position = text.find( from, position + to.size() ) )
        text.replace( position, from.size(), to );
}

std::string compactJson( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    std::ostringstream stream;
    writer->write( value, &stream );
    std::string text = stream.str();
    while ( !text.empty() && ( text.back() == '\n' || text.back() == '\r' ) )
        text.pop_back();
    return text;
}

std::string logText( const PluginDiagnosticLog &log )
{
    std::string text;
    for ( const PluginDiagnostic &item : log.items() )
    {
        text += PluginDiagnostic::codeString( item.code );
        text += ": ";
        text += item.message;
        text += "\n";
    }
    return text;
}

// ---------------------------------------------------------------------------
// SDK-1
// ---------------------------------------------------------------------------

TEST_CASE( "swarm-review SDK-1: out-of-int-domain manifest_version is refused typed",
           "[swarm][sdk][manifest]" )
{
    // ScratchGuard removes stale residue but never creates the root — the
    // sections below write manifests directly into it.
    std::error_code scratchEc;
    std::filesystem::create_directories( kScratch.path, scratchEc );
    REQUIRE_FALSE( scratchEc );

    // 4294967296 = 2^32: integral (isIntegral() true, jsoncpp stores it as
    // uintValue) but outside the int domain (isInt() false), so asInt()
    // THROWS Json::LogicError. The version gate must live on the int domain.
    const std::string document = R"JSON({ "manifest_version": 4294967296 })JSON";

    SECTION( "loadManifestFromFile refuses typed, never throws" )
    {
        const std::string path = kScratch.path + "/sdk1_huge_version.json";
        writeTextFile( path, document );
        PluginManifest manifest;
        PluginDiagnostic error;
        bool ok = true;
        REQUIRE_NOTHROW( ok = loadManifestFromFile( path, manifest, error ) );
        REQUIRE_FALSE( ok );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "manifest_version" );
        REQUIRE_FALSE( error.message.empty() );
    }

    SECTION( "PluginManifest::fromJson names the field instead of throwing" )
    {
        Json::Value root;
        Json::CharReaderBuilder builder;
        const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        std::string parseError;
        REQUIRE( reader->parse( document.data(), document.data() + document.size(), &root,
                                &parseError ) );
        PluginManifest manifest;
        PluginDiagnostic error;
        bool ok = true;
        REQUIRE_NOTHROW( ok = PluginManifest::fromJson( root, manifest, error ) );
        REQUIRE_FALSE( ok );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "manifest_version" );
    }

    SECTION( "discovery scan surfaces a Broken record, not an exception" )
    {
        // The startup path from the finding: PluginDiscovery::scan -> registry
        // -> GUI. A malformed manifest must become a Broken record with a
        // typed diagnostic (it used to throw out of scan).
        const std::string root = kScratch.path + "/sdk1_discovery_root";
        const std::string pluginDir = root + "/huge_version";
        std::error_code ec;
        std::filesystem::create_directories( pluginDir, ec );
        REQUIRE_FALSE( ec );
        writeTextFile( pluginDir + "/plugin.json", document );

        PluginDiscoveryOptions options;
        options.roots = { root };
        options.useCache = false;
        PluginDiagnosticLog log;
        std::vector<PluginRecord> records;
        REQUIRE_NOTHROW( records = PluginDiscovery::scan( options, log ) );
        REQUIRE( records.size() == 1 );
        REQUIRE( records[ 0 ].state == PluginState::Broken );
        // scan() attaches diagnostics to the RECORD (only
        // inspectDirectory() merges into the caller's log) — assert on the
        // surface scan actually populates.
        bool typedFieldError = false;
        for ( const PluginDiagnostic &item : records[ 0 ].diagnostics.items() )
        {
            if ( item.code == PluginDiagnosticCode::ManifestInvalidField
                 && item.field == "manifest_version" )
                typedFieldError = true;
        }
        REQUIRE( typedFieldError );
    }

    SECTION( "controls: in-domain versions keep their existing verdicts" )
    {
        // No over-rejection: version 1 loads, version 2 is the pre-existing
        // unknown-version refusal, a string stays the typed field error.
        const std::string v1 = kScratch.path + "/sdk1_v1.json";
        const std::string v2 = kScratch.path + "/sdk1_v2.json";
        const std::string vString = kScratch.path + "/sdk1_vstring.json";
        writeTextFile( v1, R"JSON({ "manifest_version": 1 })JSON" );
        writeTextFile( v2, R"JSON({ "manifest_version": 2 })JSON" );
        writeTextFile( vString, R"JSON({ "manifest_version": "1" })JSON" );

        PluginManifest manifest;
        PluginDiagnostic error;
        bool ok = false;
        REQUIRE_NOTHROW( ok = loadManifestFromFile( v1, manifest, error ) );
        REQUIRE( ok );

        REQUIRE_FALSE( loadManifestFromFile( v2, manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestUnknownVersion );

        REQUIRE_FALSE( loadManifestFromFile( vString, manifest, error ) );
        REQUIRE( error.code == PluginDiagnosticCode::ManifestInvalidField );
        REQUIRE( error.field == "manifest_version" );
    }
}

#ifndef _WIN32

// ---------------------------------------------------------------------------
// SDK-2 — forged host-process worker (python3, POSIX only)
// ---------------------------------------------------------------------------

/// A forged worker: it speaks just enough of the host-process protocol
/// (worker.hello, plugin.load, plugin.shutdown) to answer plugin.load with a
/// HOSTILE registration report, which is exactly the input SDK-2 gates. The
/// real worker never forges its own report, so the gate cannot be driven
/// through it — a hostile peer has to be simulated.
const char *kForgedWorkerScript = R"SWARMPY(#!@PYTHON@
"""swarm-review SDK-2 forged worker: hostile registration-report fixture."""
import json
import os
import struct
import sys

READ_FD = 3
WRITE_FD = 4
for argument in sys.argv[1:]:
    if argument.startswith("@READ_SWITCH@"):
        READ_FD = int(argument.split("=", 1)[1])
    elif argument.startswith("@WRITE_SWITCH@"):
        WRITE_FD = int(argument.split("=", 1)[1])

API_VERSION = "@API_VERSION@"
ABI_VERSION = @ABI_VERSION@
PROTOCOL_MAJOR = @PROTOCOL_MAJOR@
PROTOCOL_MINOR = @PROTOCOL_MINOR@
REGISTERED = @REGISTERED@


def write_frame(obj):
    payload = json.dumps(obj, separators=(",", ":")).encode("utf-8")
    view = memoryview(struct.pack("<I", len(payload)) + payload)
    while view:
        view = view[os.write(WRITE_FD, view):]


def read_exact(count):
    chunks = []
    remaining = count
    while remaining > 0:
        chunk = os.read(READ_FD, remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame():
    header = read_exact(4)
    if header is None:
        return None
    (length,) = struct.unpack("<I", header)
    body = read_exact(length)
    if body is None:
        return None
    return json.loads(body.decode("utf-8"))


write_frame({
    "v": PROTOCOL_MAJOR,
    "type": "event",
    "event": "worker.hello",
    "params": {
        "protocolMajor": PROTOCOL_MAJOR,
        "protocolMinor": PROTOCOL_MINOR,
        "apiVersion": API_VERSION,
        "abiVersion": ABI_VERSION,
        "maxConcurrentRequests": 1,
    },
})

while True:
    frame = read_frame()
    if frame is None:
        break
    if frame.get("type") != "request":
        continue
    method = frame.get("method", "")
    request_id = frame.get("id", 0)
    if method == "plugin.shutdown":
        write_frame({"v": PROTOCOL_MAJOR, "type": "response", "id": request_id,
                     "ok": True, "result": {}})
        break
    if method == "plugin.load":
        write_frame({"v": PROTOCOL_MAJOR, "type": "response", "id": request_id,
                     "ok": True, "result": {"registered": REGISTERED}})
        continue
    write_frame({"v": PROTOCOL_MAJOR, "type": "response", "id": request_id,
                 "ok": True, "result": {}})
)SWARMPY";

const char *kSwarmPluginId = "org.exprs.test.swarm-sdk2";

/// The interpreter the forged worker runs under. The test harness pins
/// PYTHONHOME to the interpreter CMake found (a conda build here) and
/// advertises it as SICNU_PYTHON_EXECUTABLE / PYTHONEXECUTABLE
/// (cmake/SicnuTestEnv.cmake, #730): a plain "/usr/bin/env python3" shebang
/// would then be a DIFFERENT interpreter that cannot find its stdlib under
/// that PYTHONHOME, and the fixture would die before its hello. Prefer the
/// pinned interpreter; fall back to PATH lookup.
std::string pythonInterpreter()
{
    for ( const char *variable : { "SICNU_PYTHON_EXECUTABLE", "PYTHONEXECUTABLE" } )
    {
        const char *value = std::getenv( variable );
        if ( value && *value && ::access( value, X_OK ) == 0 )
            return value;
    }
    if ( std::system( "command -v python3 >/dev/null 2>&1" ) == 0 )
        return "/usr/bin/env python3";
    return {};
}

bool python3Available()
{
    return !pythonInterpreter().empty();
}

class StubServices : public HostServicesV1
{
public:
    std::string tempDirectory() const override { return root; }
    std::string workspaceRoot() const override { return {}; }
    std::string dataDirectory() const override { return {}; }
    void log( const char *level, const std::string &message ) override
    {
        (void)level;
        (void)message;
    }
    std::string pluginDirectory() const override { return root; }

    std::string root;
};

class StubSink : public PluginContributionSink
{
public:
    bool registerOperatorFactory(
        const std::string &pluginId, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override
    {
        (void)pluginId;
        (void)factory;
        operatorIds.push_back( operatorId );
        return true;
    }
    bool registerDataProvider( const std::string &pluginId, const std::string &providerId,
                               std::shared_ptr<IPluginDataProviderV1> provider ) override
    {
        (void)pluginId;
        (void)provider;
        providerIds.push_back( providerId );
        return true;
    }
    bool registerModelRuntime( const std::string &pluginId, const std::string &framework,
                               PluginModelRuntimeFactoryV1 factory ) override
    {
        (void)pluginId;
        (void)factory;
        frameworks.push_back( framework );
        return true;
    }
    bool registerAgentTool( const std::string &pluginId, const std::string &toolId,
                            std::shared_ptr<IPluginAgentToolV1> tool ) override
    {
        (void)pluginId;
        (void)tool;
        toolIds.push_back( toolId );
        return true;
    }

    std::vector<std::string> operatorIds;
    std::vector<std::string> providerIds;
    std::vector<std::string> frameworks;
    std::vector<std::string> toolIds;
};

std::string writeForgedWorker( const std::string &directory, const Json::Value &registered )
{
    const std::string interpreter = pythonInterpreter();
    if ( interpreter.empty() )
        return {};
    std::string script = kForgedWorkerScript;
    replaceAll( script, "@PYTHON@", interpreter );
    replaceAll( script, "@API_VERSION@", EXP_RS_PLUGIN_API_VERSION );
    replaceAll( script, "@ABI_VERSION@", std::to_string( pluginAbiVersion() ) );
    replaceAll( script, "@PROTOCOL_MAJOR@",
                std::to_string( hostProtocolVersionMajor() ) );
    replaceAll( script, "@PROTOCOL_MINOR@",
                std::to_string( hostProtocolVersionMinor() ) );
    // The switch spellings come from the protocol header so the fixture cannot
    // drift from the launcher's argv contract.
    replaceAll( script, "@READ_SWITCH@", sicnu::plugins::hostprotocol::kIpcReadSwitch );
    replaceAll( script, "@WRITE_SWITCH@", sicnu::plugins::hostprotocol::kIpcWriteSwitch );
    replaceAll( script, "@REGISTERED@", compactJson( registered ) );
    const std::string path = directory + "/forged_worker.py";
    writeTextFile( path, script );
    ::chmod( path.c_str(), 0755 );
    return path;
}

struct ForgedLoadResult
{
    bool ok = false;
    PluginDiagnosticLog log;
    StubSink sink;
    std::unique_ptr<sicnu::plugins::PluginHostProcessRuntime> runtime;
    Json::Value snapshot;
};

ForgedLoadResult loadThroughForgedWorker( const std::string &workerPath,
                                          const std::string &pluginDir )
{
    ForgedLoadResult result;
    sicnu::plugins::PluginHostProcessRuntime::Options options;
    options.workerPath = workerPath;
    result.runtime =
        std::make_unique<sicnu::plugins::PluginHostProcessRuntime>( std::move( options ) );

    PluginRecord record;
    record.directory = pluginDir;
    record.manifestPath = pluginDir + "/plugin.json";
    record.state = PluginState::Validated;
    {
        Json::Value manifest( Json::objectValue );
        manifest[ "manifest_version" ] = 1;
        manifest[ "id" ] = kSwarmPluginId;
        manifest[ "name" ] = "Swarm SDK-2 Fixture";
        manifest[ "version" ] = "1.0.0";
        manifest[ "api_version" ] = EXP_RS_PLUGIN_API_VERSION;
        manifest[ "abi_version" ] = pluginAbiVersion();
        manifest[ "runtime" ] = "host-process";
        manifest[ "entrypoint" ] = "libswarm_sdk2_fixture.so";
        manifest[ "entrypoint_kind" ] = "native";
        PluginDiagnostic error;
        if ( !PluginManifest::fromJson( manifest, record.manifest, error ) )
            FAIL( "the SDK-2 fixture manifest must parse: " << error.message );
    }

    StubServices services;
    services.root = pluginDir;
    result.ok = result.runtime->loadPlugin( record, services, result.sink, result.log );
    result.snapshot = result.runtime->diagnosticsSnapshot();
    return result;
}

TEST_CASE( "swarm-review SDK-2: the forged-worker harness loads a well-typed report",
           "[swarm][sdk][hostprocess]" )
{
    if ( !python3Available() )
        SKIP( "python3 is unavailable on this lane; the forged-worker harness cannot run" );

    const std::string pluginDir = kScratch.path + "/sdk2_control";
    std::error_code ec;
    std::filesystem::create_directories( pluginDir, ec );
    REQUIRE_FALSE( ec );

    // Harness control: without this case a broken forged worker would make the
    // hostile cases pass for the wrong reason (e.g. a spawn failure).
    Json::Value registered( Json::objectValue );
    registered[ "operators" ] = Json::Value( Json::arrayValue );
    registered[ "operators" ].append( "test:swarm-echo" );
    const std::string workerPath = writeForgedWorker( pluginDir, registered );

    ForgedLoadResult result = loadThroughForgedWorker( workerPath, pluginDir );
    INFO( "control load log:\n" << logText( result.log ) );
    REQUIRE( result.ok );
    REQUIRE( result.sink.operatorIds == std::vector<std::string>{ "test:swarm-echo" } );
    REQUIRE( result.snapshot[ "plugins" ].isMember( kSwarmPluginId ) );

    PluginDiagnosticLog unloadLog;
    REQUIRE( result.runtime->unloadPlugin( kSwarmPluginId, unloadLog ) );
    REQUIRE_FALSE( result.runtime->diagnosticsSnapshot()[ "plugins" ].isMember(
        kSwarmPluginId ) );
}

TEST_CASE( "swarm-review SDK-2: a forged non-string operator id fails the load typed",
           "[swarm][sdk][hostprocess]" )
{
    if ( !python3Available() )
        SKIP( "python3 is unavailable on this lane; the forged-worker harness cannot run" );

    const std::string pluginDir = kScratch.path + "/sdk2_hostile_operator";
    std::error_code ec;
    std::filesystem::create_directories( pluginDir, ec );
    REQUIRE_FALSE( ec );

    Json::Value registered( Json::objectValue );
    registered[ "operators" ] = Json::Value( Json::arrayValue );
    registered[ "operators" ].append( Json::Value( Json::objectValue ) ); // forged {}
    const std::string workerPath = writeForgedWorker( pluginDir, registered );

    // Pre-fix this call threw Json::LogicError out of loadPlugin (no catch on
    // the host side) and terminated the host process — the isolation contract
    // breached by the very input it is supposed to contain. Reaching the
    // assertions below at all is the regression proof; the typed refusal and
    // the torn-down session pin the contract.
    ForgedLoadResult result = loadThroughForgedWorker( workerPath, pluginDir );
    INFO( "hostile load log:\n" << logText( result.log ) );
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.sink.operatorIds.empty() );
    bool typedRefusal = false;
    for ( const PluginDiagnostic &item : result.log.items() )
    {
        if ( item.code == PluginDiagnosticCode::IpcProtocolError
             && item.message.find( "operators[]" ) != std::string::npos
             && item.message.find( "not a string" ) != std::string::npos )
            typedRefusal = true;
    }
    REQUIRE( typedRefusal );
    REQUIRE_FALSE( result.snapshot[ "plugins" ].isMember( kSwarmPluginId ) );
    REQUIRE_FALSE( result.runtime->isWorkerAlive( kSwarmPluginId ) );
}

TEST_CASE( "swarm-review SDK-2: a forged non-string data-provider id fails the load typed",
           "[swarm][sdk][hostprocess]" )
{
    if ( !python3Available() )
        SKIP( "python3 is unavailable on this lane; the forged-worker harness cannot run" );

    const std::string pluginDir = kScratch.path + "/sdk2_hostile_provider";
    std::error_code ec;
    std::filesystem::create_directories( pluginDir, ec );
    REQUIRE_FALSE( ec );

    Json::Value registered( Json::objectValue );
    registered[ "dataProviders" ] = Json::Value( Json::arrayValue );
    registered[ "dataProviders" ].append( 42 ); // forged number
    const std::string workerPath = writeForgedWorker( pluginDir, registered );

    ForgedLoadResult result = loadThroughForgedWorker( workerPath, pluginDir );
    REQUIRE_FALSE( result.ok );
    REQUIRE( result.sink.operatorIds.empty() );
    bool typedRefusal = false;
    for ( const PluginDiagnostic &item : result.log.items() )
    {
        if ( item.code == PluginDiagnosticCode::IpcProtocolError
             && item.message.find( "dataProviders[]" ) != std::string::npos
             && item.message.find( "not a string" ) != std::string::npos )
            typedRefusal = true;
    }
    REQUIRE( typedRefusal );
    REQUIRE_FALSE( result.snapshot[ "plugins" ].isMember( kSwarmPluginId ) );
    REQUIRE_FALSE( result.runtime->isWorkerAlive( kSwarmPluginId ) );
}

TEST_CASE( "swarm-review SDK-2: a forged non-object registration report fails the load typed",
           "[swarm][sdk][hostprocess]" )
{
    if ( !python3Available() )
        SKIP( "python3 is unavailable on this lane; the forged-worker harness cannot run" );

    const std::string pluginDir = kScratch.path + "/sdk2_hostile_report_shape";
    std::error_code ec;
    std::filesystem::create_directories( pluginDir, ec );
    REQUIRE_FALSE( ec );

    // Same class, different shape: jsoncpp's find() (behind operator[]) ASSERTS
    // on a non-object, non-null value, so a forged "registered": 42 threw out of
    // the member indexing even before any element cast ran.
    Json::Value registered( 42 );
    const std::string workerPath = writeForgedWorker( pluginDir, registered );

    ForgedLoadResult result = loadThroughForgedWorker( workerPath, pluginDir );
    INFO( "hostile report-shape log:\n" << logText( result.log ) );
    REQUIRE_FALSE( result.ok );
    bool typedRefusal = false;
    for ( const PluginDiagnostic &item : result.log.items() )
    {
        if ( item.code == PluginDiagnosticCode::IpcProtocolError
             && item.message.find( "registered" ) != std::string::npos
             && item.message.find( "not an object" ) != std::string::npos )
            typedRefusal = true;
    }
    REQUIRE( typedRefusal );
    REQUIRE_FALSE( result.snapshot[ "plugins" ].isMember( kSwarmPluginId ) );
    REQUIRE_FALSE( result.runtime->isWorkerAlive( kSwarmPluginId ) );
}

// ---------------------------------------------------------------------------
// SDK-3 — auto-reaped (SIGCHLD = SIG_IGN) external process exit reporting
// ---------------------------------------------------------------------------

/// swarm-review SDK-3: the review's scenario is an embedding process that
/// ignores SIGCHLD. SA_NOCLDWAIT is the explicit portable spelling of the same
/// auto-reap behaviour (and the one the host-process suite already uses).
struct SigchldAutoReapGuard
{
    struct sigaction previous;
    SigchldAutoReapGuard()
    {
        struct sigaction action = {};
        action.sa_handler = SIG_IGN;
        sigemptyset( &action.sa_mask );
        action.sa_flags = SA_NOCLDWAIT;
        ::sigaction( SIGCHLD, &action, &previous );
    }
    ~SigchldAutoReapGuard() { ::sigaction( SIGCHLD, &previous, nullptr ); }
};

TEST_CASE( "swarm-review SDK-3: an auto-reaped failing tool is never exit code 0",
           "[swarm][sdk][external]" )
{
    SigchldAutoReapGuard const sigchldGuard;

    // The child closes stdout/stderr (so all three pipe ends reach EOF right
    // after exec — the exec-error pipe is CLOEXEC) and only THEN sleeps and
    // exits 3. The poll loop therefore breaks on the three-fd fast path while
    // the child is provably still alive, i.e. BEFORE the WNOHANG liveness
    // probe can observe its death: reaped stays false and the post-loop
    // BLOCKING waitpid runs. With SIGCHLD ignored that waitpid fails with
    // ECHILD once the child is auto-reaped, and the pre-fix code decoded its
    // untouched zero status as WIFEXITED(0) — exit code 0 for a tool that
    // failed with 3, i.e. a failed tool published as a success.
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "exec 1>&- 2>&-; sleep 0.5; exit 3" };
    request.timeoutSeconds = 30;

    ExternalProcessResult result;
    REQUIRE_NOTHROW( result = ExternalProcess::run( request ) );
    REQUIRE( result.started );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE( result.exitCode == -1 );
    REQUIRE( result.exitSignal == 0 );
    REQUIRE_FALSE( result.timedOut );
    REQUIRE_FALSE( result.cancelled );
    // Honest unknown status: the message must say so, and must not claim a
    // spawn failure (nothing failed to start).
    REQUIRE( result.error.find( "auto-reaped" ) != std::string::npos );
}

TEST_CASE( "swarm-review SDK-3: control — the same tool reports exit code 3 without auto-reap",
           "[swarm][sdk][external]" )
{
    // Pins the attribution: the -1 above comes from the auto-reap guard, not
    // from the tool or the fast path. The normal reap still decodes the real
    // status, so a correctly reaped failure is still reported as 3.
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "exec 1>&- 2>&-; exit 3" };
    request.timeoutSeconds = 30;
    const ExternalProcessResult result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE( result.exitCode == 3 );
    REQUIRE( result.error.empty() );
}

#endif // !_WIN32

} // namespace
