// tests/test_plugin_channel_parity_r4.cpp — dual-channel diagnostic parity
// (track R4 WP-E). The platform loads native plugins through TWO channels:
//
//   in-process    PluginRegistry -> PluginLoader (dlopen in this process)
//   host-process  PluginRegistry -> PluginHostProcessRuntime (worker spawn,
//                 the plugin maps inside the worker)
//
// For the SAME bad-plugin sample both channels must report a TYPED failure
// (no silent false), with a non-empty message, and the two codes must map
// onto each other through the table below (documented in full in
// .planning/plugin-lifecycle-exprs-r4/ROLLBACK_CONTRACT.md, appendix A).
// A channel that goes silent — or drifts to an unrelated code — is a
// regression this suite catches.
//
// Mapping table (contract after the D-6 closure — the host-process runtime
// now forwards the worker's typed load log from IpcError.data instead of
// folding every failure into LibraryLoadFailed):
//   sample                in-process            host-process
//   --------------------  --------------------  ------------------------
//   garbage payload       LibraryLoadFailed     LibraryLoadFailed
//                         (dlopen refuses)      (worker refuses; the
//                                                typed log is forwarded)
//   entrypoint missing    EntrypointMissing     EntrypointMissing
//                         (validation stage,    (validation stage,
//                         channel-independent)   channel-independent)
//   id mismatch           InitializationFailed  InitializationFailed
//                         (loader:448)          (the worker's loader
//                                                refuses with the same
//                                                typed code; the host
//                                                re-emits it verbatim)
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"
#include "support/exprs_test_env.h"
#include "exprs/plugin_snapshot.h"
#include "plugins/host/plugin_host_process_runtime.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace exprs;
namespace {
/// Binary-wide pid-unique user plugin root: without this redirect every
/// setEnabled() below persists the REAL $HOME/sicnu_geo_rs/plugins.index.json
/// — parallel case processes race that shared file and tests write into the
/// developer's profile (issue #1364 cross-process trampling class).
const exprs_test::UserRootRedirect kUserRootRedirected;
} // namespace


#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built hello fixture plugin dir"
#endif
#ifndef SICNU_TEST_PARITY_WORKER
#error "SICNU_TEST_PARITY_WORKER must point at the plugin host worker binary"
#endif

namespace {

#ifdef _WIN32
const char *kHelloEntrypoint = "libhello_plugin.dll";
#else
const char *kHelloEntrypoint = "libhello_plugin.so";
#endif

/// The observed parity contract as data — one row per bad-plugin sample.
/// A change here means one channel's error surface drifted; the table and
/// ROLLBACK_CONTRACT.md appendix A must be updated together.
struct ParityRow
{
    const char *sample;
    PluginDiagnosticCode inProcess;
    PluginDiagnosticCode hostProcess;
};

const ParityRow kParityTable[] = {
    { "garbage payload", PluginDiagnosticCode::LibraryLoadFailed,
      PluginDiagnosticCode::LibraryLoadFailed },
    { "entrypoint missing", PluginDiagnosticCode::EntrypointMissing,
      PluginDiagnosticCode::EntrypointMissing },
    { "id mismatch", PluginDiagnosticCode::InitializationFailed,
      PluginDiagnosticCode::InitializationFailed },
};

bool sawCodeFor( const std::vector<PluginDiagnostic> &items, const std::string &pluginId,
                 PluginDiagnosticCode code )
{
    for ( const PluginDiagnostic &item : items )
        if ( item.pluginId == pluginId && item.code == code )
            return true;
    return false;
}

bool sawAnyCodeFor( const std::vector<PluginDiagnostic> &items, const std::string &pluginId )
{
    for ( const PluginDiagnostic &item : items )
        if ( item.pluginId == pluginId && item.code != PluginDiagnosticCode::None
             && item.severity == PluginDiagnosticSeverity::Error )
            return true;
    return false;
}

/// A scratch plugin tree for ONE sample in ONE channel. The sample is
/// identified by its own plugin id so diagnostics never cross. The root is
/// pid-unique: ctest's PRE_TEST discovery runs each case as its own process,
/// and a shared fixed name would let two parallel case processes (or a
/// rerun) trample each other's fixtures mid-attempt.
struct ParityFixture
{
    const std::string root;
    const std::string pluginDir;
    const std::string id;
    std::string recordId; // the DECLARED manifest id (differs for the
                          // id-mismatch sample; set by writeManifest)

    ParityFixture( const std::string &base, const char *pluginId )
        : root( ( std::filesystem::temp_directory_path()
                  / ( base + "." + std::to_string( snapshotOwnerPid() ) ) )
                    .generic_string() )
        , pluginDir( root + "/" + pluginId )
        , id( pluginId )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        std::filesystem::create_directories( pluginDir, ec );
    }
    ~ParityFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
    }

    void writeManifest( const std::string &entrypoint, bool hostProcess,
                        const std::string &manifestId = "" )
    {
        recordId = manifestId.empty() ? id : manifestId;
        std::ofstream manifest( pluginDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" << ( manifestId.empty() ? id : manifestId ) << R"(",
            "name": "Parity Fixture",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,)"
                 << ( hostProcess ? "\n            \"runtime\": \"host-process\"," : "" )
                 << R"(
            "entrypoint": ")" << entrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:parity", "display_name": "Parity", "group": "test" }]
        })";
    }
};

/// Minimal recording sink — BOTH load paths refuse with RegistrationFailed
/// before doing any work when no sink is installed (registry preflight), so
/// the parity attempts must have one.
class ParitySink : public PluginContributionSink
{
public:
    bool registerOperatorFactory( const std::string &, const std::string &,
                                  std::function<std::unique_ptr<sicnu::operators::RSOperator>()> ) override
    {
        return true;
    }
    bool registerDataProvider( const std::string &, const std::string &,
                               std::shared_ptr<IPluginDataProviderV1> ) override
    {
        return true;
    }
    bool registerModelRuntime( const std::string &, const std::string &,
                               PluginModelRuntimeFactoryV1 ) override
    {
        return true;
    }
    bool registerAgentTool( const std::string &, const std::string &,
                            std::shared_ptr<IPluginAgentToolV1> ) override
    {
        return true;
    }
};

struct AttemptOutcome
{
    std::vector<PluginDiagnostic> globalItems;
    bool recordHasCode = false; // expected code found on the RECORD's log
};

/// Drives ONE registry load attempt against a prepared fixture dir. The
/// verdicts are captured BEFORE teardown: the next attempt's configure()
/// re-scans a different root and replaces the registry's records.
AttemptOutcome attemptLoad( ParityFixture &fixture, bool hostProcess,
                            sicnu::plugins::PluginHostProcessRuntime *runtime,
                            const std::string &tempDir, ParitySink &sink,
                            PluginDiagnosticCode expectedRecordCode )
{
    PluginRegistry &registry = PluginRegistry::instance();
    registry.unloadAll();
    registry.setContributionSink( &sink );
    if ( hostProcess )
        registry.setHostProcessRuntime( runtime );
    PluginRegistryOptions options;
    options.roots = { fixture.root };
    options.tempDirectory = tempDir;
    options.policy.allowThirdPartyNative = true;
    registry.configure( options );
    const std::string &subjectId = fixture.recordId.empty() ? fixture.id : fixture.recordId;
    registry.setEnabled( subjectId, true );
    registry.load( subjectId ); // failure is the subject — diagnostics below

    AttemptOutcome outcome;
    outcome.globalItems = registry.diagnostics().items();
    if ( const PluginRecord *record = registry.record( subjectId ) )
        for ( const PluginDiagnostic &item : record->diagnostics.items() )
            if ( item.code == expectedRecordCode )
                outcome.recordHasCode = true;
    registry.unloadAll();
    if ( hostProcess )
        registry.setHostProcessRuntime( nullptr );
    registry.setContributionSink( nullptr );
    return outcome;
}

/// Removes the shared per-case temp dir on ANY exit path (a failed REQUIRE
/// must not leak /tmp/exprs_parity_temp into the next run).
struct TempDirCleanup
{
    std::string path;
    ~TempDirCleanup()
    {
        std::error_code ec;
        std::filesystem::remove_all( path, ec );
    }
};

} // namespace

TEST_CASE( "garbage payload is reported typed by BOTH channels",
           "[plugin][parity][r4]" )
{
    sicnu::plugins::PluginHostProcessRuntime::Options options;
    options.workerPath = SICNU_TEST_PARITY_WORKER;
    options.handshakeTimeoutMs = 15000;
    sicnu::plugins::PluginHostProcessRuntime runtime( options );
    const std::string tempDir =
        ( std::filesystem::temp_directory_path()
          / ( "exprs_parity_temp." + std::to_string( snapshotOwnerPid() ) ) )
            .generic_string();
    std::filesystem::create_directories( tempDir );
    TempDirCleanup tempCleanup{ tempDir };

    // Identical bad sample, one per channel: a regular file that is not a
    // library (passes containment + validation, fails at map time).
    ParityFixture inProc( "exprs_parity_inproc_garbage", "org.test.parity.inproc" );
    inProc.writeManifest( "libgarbage.so", false );
    {
        std::ofstream garbage( inProc.pluginDir + "/libgarbage.so", std::ios::binary );
        garbage << "this is not a shared library";
    }
    ParityFixture hostProc( "exprs_parity_host_garbage", "org.test.parity.host" );
    hostProc.writeManifest( "libgarbage.so", true );
    {
        std::ofstream garbage( hostProc.pluginDir + "/libgarbage.so", std::ios::binary );
        garbage << "this is not a shared library";
    }

    ParitySink sink;
    const AttemptOutcome in =
        attemptLoad( inProc, false, &runtime, tempDir, sink,
                     kParityTable[ 0 ].inProcess );
    const AttemptOutcome host =
        attemptLoad( hostProc, true, &runtime, tempDir, sink,
                     kParityTable[ 0 ].hostProcess );

    // Neither channel may go silent, and both must hit the mapped code.
    REQUIRE( sawAnyCodeFor( in.globalItems, inProc.id ) );
    REQUIRE( sawAnyCodeFor( host.globalItems, hostProc.id ) );
    REQUIRE( sawCodeFor( in.globalItems, inProc.id,
                         kParityTable[ 0 ].inProcess ) );
    REQUIRE( sawCodeFor( host.globalItems, hostProc.id,
                         kParityTable[ 0 ].hostProcess ) );
}

TEST_CASE( "entrypoint missing is reported typed by BOTH channels",
           "[plugin][parity][r4]" )
{
    sicnu::plugins::PluginHostProcessRuntime::Options options;
    options.workerPath = SICNU_TEST_PARITY_WORKER;
    options.handshakeTimeoutMs = 15000;
    sicnu::plugins::PluginHostProcessRuntime runtime( options );
    const std::string tempDir =
        ( std::filesystem::temp_directory_path()
          / ( "exprs_parity_temp." + std::to_string( snapshotOwnerPid() ) ) )
            .generic_string();
    std::filesystem::create_directories( tempDir );
    TempDirCleanup tempCleanup{ tempDir };

    // The named entrypoint file does not exist — the refusal belongs to the
    // validation stage, which is channel-INDEPENDENT, so the codes match
    // exactly.
    ParityFixture inProc( "exprs_parity_inproc_missing", "org.test.parity.missing.ip" );
    inProc.writeManifest( "libdoes_not_exist.so", false );
    ParityFixture hostProc( "exprs_parity_host_missing", "org.test.parity.missing.hp" );
    hostProc.writeManifest( "libdoes_not_exist.so", true );

    ParitySink sink;
    const AttemptOutcome in =
        attemptLoad( inProc, false, &runtime, tempDir, sink,
                     kParityTable[ 1 ].inProcess );
    const AttemptOutcome host =
        attemptLoad( hostProc, true, &runtime, tempDir, sink,
                     kParityTable[ 1 ].hostProcess );

    // The entrypoint-missing refusal is the scan-time validation verdict,
    // recorded on the RECORD's diagnostics in both channels — provably
    // channel-independent.
    REQUIRE( in.recordHasCode );
    REQUIRE( host.recordHasCode );
}

TEST_CASE( "manifest/binary id mismatch is reported typed by BOTH channels",
           "[plugin][parity][r4]" )
{
    sicnu::plugins::PluginHostProcessRuntime::Options options;
    options.workerPath = SICNU_TEST_PARITY_WORKER;
    options.handshakeTimeoutMs = 15000;
    sicnu::plugins::PluginHostProcessRuntime runtime( options );
    const std::string tempDir =
        ( std::filesystem::temp_directory_path()
          / ( "exprs_parity_temp." + std::to_string( snapshotOwnerPid() ) ) )
            .generic_string();
    std::filesystem::create_directories( tempDir );
    TempDirCleanup tempCleanup{ tempDir };

    // Both manifests declare an id the PAYLOAD does not report: in-process,
    // the loader's pluginId() == manifest id check refuses; host-process,
    // the same loader check runs inside the worker. Copy the hello fixture
    // binary in as the payload for both.
    ParityFixture inProc( "exprs_parity_inproc_ids", "org.test.parity.declared" );
    inProc.writeManifest( kHelloEntrypoint, false );
    std::error_code ec;
    std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                    + kHelloEntrypoint,
                                inProc.pluginDir + "/" + kHelloEntrypoint, ec );
    REQUIRE( !ec );

    ParityFixture hostProc( "exprs_parity_host_ids", "org.test.parity.declared.hp" );
    hostProc.writeManifest( kHelloEntrypoint, true, "org.test.parity.declared" );
    std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                    + kHelloEntrypoint,
                                hostProc.pluginDir + "/" + kHelloEntrypoint, ec );
    REQUIRE( !ec );

    ParitySink sink;
    const AttemptOutcome in =
        attemptLoad( inProc, false, &runtime, tempDir, sink,
                     kParityTable[ 2 ].inProcess );
    const AttemptOutcome host =
        attemptLoad( hostProc, true, &runtime, tempDir, sink,
                     kParityTable[ 2 ].hostProcess );

    // The record id is the DECLARED one — refusals are attributed to it.
    REQUIRE( sawCodeFor( in.globalItems, "org.test.parity.declared",
                         kParityTable[ 2 ].inProcess ) );
    REQUIRE( sawCodeFor( host.globalItems, "org.test.parity.declared",
                         kParityTable[ 2 ].hostProcess ) );
}
