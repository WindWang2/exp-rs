// tests/test_r29_t03_production_driver.cpp
//
// R29-T03 regressions for the production session assembly:
//
//  WP08 — the unified productionAgentOpsDriver() carries the LIVE repair
//  capability provider (data/agent/capabilities through
//  liveRepairCapabilityProvider), so a GRID_MISMATCH diagnostic plans a
//  REAL rs:resample-class candidate instead of the null-provider
//  no_safe_repair every host forced before. The no-provider assembly stays
//  the honest fail-closed contrast case; the approval channel stays
//  human-only (the bridge requires approval for science-changing repairs —
//  unchanged by the provider, only what it may PROPOSE widened).
//
//  WP05 — scientific:agent_session refs accept EXPLICIT typed objects
//  {"path":…} / {"asset_id":…} that remove the id/path ambiguity the legacy
//  string heuristic cannot (dotted asset ids, paths with spaces/CJK,
//  absolute paths); malformed typed refs are typed refusals at the MCP gate,
//  and every accepted form reaches the driver as ONE resolved string per
//  slot (the same identity planning/preflight/execution bind downstream).
//
// Run: ctest -R r29_t03_production_driver

#include <catch2/catch_test_macros.hpp>

// fake_seams.h FIRST: its FakeScenario declares a `slots` member, and any
// Qt object header (mcp_server.h pulls QObject) defines the `slots` macro
// — after that the member declaration cannot parse.
#include "agent_loop/fake_seams.h"
#include "agent/mcp_server.h"
#include "agent/tools/agent_session_adapter.h"
#include "agent_ops/ops_driver.h"
#include "agent_ops/operations_coordinator.h"
#include "agent_ops/ops_types.h"
#include "app/agent_ops/agent_ops_control_center_dock.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QApplication>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVariantMap>

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

/// Local capture harness (same pattern as tests/test_mcp_server.cpp, which
/// cannot be included — it has its own main via Catch2WithMain).
class R29TestMcpServer : public McpServer
{
  public:
    QVariant lastResponseId;
    QVariantMap lastResponseResult;
    QString lastErrorText;
    bool handshakeDone = false;

    void testHandleRequest( const QVariantMap &req ) { handleRequest( req ); }

    void sendResponse( const QVariant &id, const QVariantMap &result ) override
    {
        lastResponseId = id;
        lastResponseResult = result;
    }
    void sendError( const QVariant &id, int code, const QString &message ) override
    {
        ( void )id;
        ( void )code;
        lastErrorText = message;
    }
    void sendError( const QVariant &id, int code, const QString &message,
                    const QVariantMap &data ) override
    {
        ( void )id;
        ( void )code;
        ( void )data;
        lastErrorText = message;
    }
};

/// A failed-session diagnostic with a typed GRID_MISMATCH preflight finding
/// (the shape the live BridgedDiagnoser produces; GRID_MISMATCH routes to
/// the grid_align requirement kind in repair_requirement.cpp).
sicnu::agent_ops::OpDiagnostic gridMismatchDiagnostic()
{
    using sicnu::agent_ops::OpDiagnostic;
    OpDiagnostic diag;
    diag.code = "ops.preflight.FIXABLE";
    diag.rootCauseCode = "PREFLIGHT_FIXABLE";
    diag.repairable = true;
    diag.advisoryNext = sicnu::agent_ops::recovery_action::kRepair;
    diag.confidence = 0.9;
    Json::Value report( Json::objectValue );
    report["verdict"] = "fixable";
    Json::Value issues( Json::arrayValue );
    Json::Value issue( Json::objectValue );
    issue["code"] = "GRID_MISMATCH";
    issue["severity"] = "error";
    issue["message"] = "grid does not match the reference grid";
    issues.append( issue );
    report["issues"] = issues;
    diag.sources["preflight"] = report;
    return diag;
}

/// Drives one scientific:agent_session tools/call and returns the tool
/// result (isError + errorCode path) or the envelope text (driver path).
struct SessionCallOutcome
{
    bool isError = false;
    QString errorCode;
    QString text;
    QVariantMap doc; ///< the envelope parsed (empty when not a JSON object)
};

SessionCallOutcome sessionCall( R29TestMcpServer &server, int id, const QVariantMap &args )
{
    // MCP gate: initialize handshake before tools/call (same sequence as
    // tests/test_mcp_server.cpp's agent_session cases) — once per server.
    if ( !server.handshakeDone )
    {
        QVariantMap initReq;
        initReq[QStringLiteral( "id" )] = id + 1000;
        initReq[QStringLiteral( "method" )] = QStringLiteral( "initialize" );
        QVariantMap initParams;
        initParams[QStringLiteral( "protocolVersion" )] = QStringLiteral( "2024-11-05" );
        initReq[QStringLiteral( "params" )] = initParams;
        server.testHandleRequest( initReq );
        QVariantMap notifReq;
        notifReq[QStringLiteral( "method" )] = QStringLiteral( "notifications/initialized" );
        server.testHandleRequest( notifReq );
        server.handshakeDone = true;
    }

    QVariantMap req;
    req[QStringLiteral( "id" )] = id;
    req[QStringLiteral( "method" )] = QStringLiteral( "tools/call" );
    QVariantMap params;
    params[QStringLiteral( "name" )] = QStringLiteral( "scientific:agent_session" );
    params[QStringLiteral( "arguments" )] = args;
    req[QStringLiteral( "params" )] = params;
    server.testHandleRequest( req );

    SessionCallOutcome out;
    out.isError = server.lastResponseResult.value( QStringLiteral( "isError" ) ).toBool();
    out.errorCode = server.lastResponseResult.value( QStringLiteral( "errorCode" ) ).toString();
    const QVariantList content = server.lastResponseResult.value( QStringLiteral( "content" ) ).toList();
    if ( !content.isEmpty() )
        out.text = content.first().toMap().value( QStringLiteral( "text" ) ).toString();
    out.doc = QJsonDocument::fromJson( out.text.toUtf8() ).toVariant().toMap();
    return out;
}

QVariantMap runArgsWithRefs( const QVariantMap &refs )
{
    QVariantMap args;
    args[QStringLiteral( "action" )] = QStringLiteral( "run" );
    args[QStringLiteral( "goal" )] = QStringLiteral( "compute NDVI for the scene" );
    args[QStringLiteral( "refs" )] = refs;
    return args;
}

} // namespace

// ---------------------------------------------------------------------------
// WP08 — production driver + live repair provider
// ---------------------------------------------------------------------------

TEST_CASE( "r29_t03 production driver: one assembly with the live repair provider",
           "[r29_t03][agent_ops][production_driver]" )
{
    auto *driver = sicnu::agent::productionAgentOpsDriver();
    REQUIRE( driver != nullptr );
    // One process-lifetime assembly (the MCP host, CLI `session` and the
    // desktop Control Center dock all receive this SAME driver).
    REQUIRE( sicnu::agent::productionAgentOpsDriver() == driver );

    const auto &deps = driver->coordinator().dependencies();
    // On a source checkout the shipped knowledge (data/agent/capabilities)
    // loads and the coordinator runs on the live provider — the WP08
    // contract. (Fail-closed null stays legal only when the knowledge layer
    // itself cannot load; that case is exercised by the contrast test
    // below with an explicitly provider-less coordinator.)
    CHECK( deps.repairCapabilityProvider != nullptr );
}

TEST_CASE( "r29_t03 live provider plans a real grid_align candidate (WP08)",
           "[r29_t03][agent_ops][production_driver]" )
{
    auto *driver = sicnu::agent::productionAgentOpsDriver();
    REQUIRE( driver != nullptr );
    REQUIRE( driver->coordinator().dependencies().repairCapabilityProvider != nullptr );

    auto diag = gridMismatchDiagnostic();
    sicnu::agent_ops::RecoveryContext ctx;
    ctx.intent = "ndvi";
    auto decision = driver->coordinator().evaluateRecovery( diag, ctx );

    REQUIRE( decision.repairPlan.isObject() );
    CHECK( decision.repairPlan["status"].asString() == "planned" );
    // The candidate is a REAL operator from the shipped capability
    // knowledge (rs:resample / rs:align family: preprocess), not a
    // fabricated one: exactly one selected action carrying an operator_id.
    REQUIRE( decision.repairPlan["selected"].isArray() );
    CHECK( decision.repairPlan["selected"].size() >= 1 );
    bool sawOperator = false;
    for ( const auto &action : decision.repairPlan["selected"] )
    {
        if ( action.isObject() && action.isMember( "operator_id" ) &&
             !action["operator_id"].asString().empty() )
            sawOperator = true;
    }
    CHECK( sawOperator );
    // The plan carries the digest the human approval binds to (#1733): the
    // provider widened what the bridge may PROPOSE, not who may APPROVE.
    CHECK( decision.repairPlan["provenance"]["findings_digest"].isString() );
}

TEST_CASE( "r29_t03 without a provider the bridge still refuses honestly (contrast)",
           "[r29_t03][agent_ops][production_driver]" )
{
    // The pre-WP08 assembly (null provider) is the fail-closed contrast:
    // no candidates are fabricated, the typed no_safe_repair names
    // no_provider. The provider injection must not be needed for SAFETY —
    // only for capability.
    sicnu::agent_ops::OperationsCoordinator::Dependencies deps;
    sicnu::agent_ops::OperationsCoordinator coord( deps );
    auto diag = gridMismatchDiagnostic();
    sicnu::agent_ops::RecoveryContext ctx;
    ctx.intent = "ndvi";
    auto decision = coord.evaluateRecovery( diag, ctx );
    REQUIRE( decision.repairPlan.isObject() );
    CHECK( decision.repairPlan["status"].asString() == "no_safe_repair" );
    // Typed cause, not prose.
    bool sawNoProvider = false;
    for ( const auto &entry : decision.repairPlan["unresolved"] )
        if ( entry.isObject() && entry["cause"].asString() == "no_provider" )
            sawNoProvider = true;
    CHECK( sawNoProvider );
}

// ---------------------------------------------------------------------------
// WP09 — the desktop Control Center dock runs on the SAME production driver
// ---------------------------------------------------------------------------

TEST_CASE( "r29_t03 control center dock wires the production driver and survives close",
           "[r29_t03][agent_ops][dock]" )
{
    int argc = 0;
    qputenv( "QT_QPA_PLATFORM", "offscreen");
    QApplication *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
    std::unique_ptr<QApplication> owned;
    if ( !app )
    {
        owned = std::make_unique<QApplication>( argc, nullptr );
        app = owned.get();
    }

    {
        ::sicnu::app::agent_ops::AgentOpsControlCenterDock dock;
        auto *driver = sicnu::agent::productionAgentOpsDriver();
        dock.setDriver( driver );
        CHECK( dock.driver() == driver );
        // The dock's coordinator resolves THROUGH the driver (the same
        // OperationsCoordinator the MCP host and CLI drive) — no second
        // state machine.
        CHECK( dock.coordinator() == &driver->coordinator() );
        // No worker running, no session launched: closing the dock now is
        // the "task running while the window closes" trivial edge; the
        // destructor's cancel+join contract is covered by
        // test_agent_ops_ui_smoke's worker lifecycle cases.
        CHECK_FALSE( dock.isWorkerRunning() );
    } // destructor: cancel + join exactly once (#1540/#1742) — must not hang.
    SUCCEED( "dock constructed on the production driver and destroyed cleanly" );
}

// ---------------------------------------------------------------------------
// WP05 — explicit typed refs at the scientific:agent_session gate
// ---------------------------------------------------------------------------

TEST_CASE( "r29_t03 typed refs: explicit path/asset_id objects resolve and refuse typed",
           "[r29_t03][agent_ops][refs]" )
{
    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );
    qputenv( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );

    // Empty-seams driver: a ref that PASSES the MCP gate reaches the driver
    // and settles as the typed SEAMS_UNAVAILABLE envelope (wire parity with
    // the CLI); a ref refused at the gate is an isError tool result with
    // the gate's errorCode instead.
    static sicnu::agent_ops::OpsDriver emptyDriver{ sicnu::agent_ops::OpsDriver::Options{} };
    R29TestMcpServer server;
    server.setAgentOpsDriver( &emptyDriver );

    const auto gateRefused = [&]( const QVariantMap &refs, const char *code ) {
        auto out = sessionCall( server, 100, runArgsWithRefs( refs ) );
        CHECK( out.isError );
        CHECK( out.errorCode == QLatin1String( code ) );
    };
    const auto reachedDriver = [&]( const QVariantMap &refs ) {
        auto out = sessionCall( server, 101, runArgsWithRefs( refs ) );
        CHECK_FALSE( out.isError );
        CHECK( out.text.contains( QStringLiteral( "SEAMS_UNAVAILABLE" ) ) );
    };

    SECTION( "explicit path objects are anchored and contained" )
    {
        QVariantMap ref;
        ref[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "path" ), QStringLiteral( "inner/scene.tif" ) } };
        reachedDriver( ref );

        QVariantMap outside;
        outside[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "path" ), QStringLiteral( "../outside.tif" ) } };
        gateRefused( outside, "PATH_OUTSIDE_WORKSPACE" );

        QVariantMap absOutside;
        absOutside[QStringLiteral( "main" )] =
            QVariantMap{ { QStringLiteral( "path" ), QStringLiteral( "/etc/passwd" ) } };
        gateRefused( absOutside, "PATH_OUTSIDE_WORKSPACE" );

        QVariantMap absInside;
        absInside[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "path" ), workspace.filePath( QStringLiteral( "abs.tif" ) ) } };
        reachedDriver( absInside );

        // Paths the legacy heuristic misreads as ids (spaces + CJK) are
        // unambiguous in the explicit form — always a path here.
        QVariantMap cjk;
        cjk[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "path" ), QStringLiteral( "中文 目录/scene with spaces.tif" ) } };
        reachedDriver( cjk );
    }

    SECTION( "explicit asset_id objects are never anchored" )
    {
        // Dotted asset id: the LEGACY heuristic calls this path-shaped
        // (dot, no space) — the explicit form keeps it an id.
        QVariantMap dotted;
        dotted[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "asset_id" ), QStringLiteral( "LC08.sr.2020-044-034" ) } };
        reachedDriver( dotted );

        QVariantMap uuid;
        uuid[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "asset_id" ),
              QStringLiteral( "550e8400-e29b-41d4-a716-446655440000" ) } };
        reachedDriver( uuid );
    }

    SECTION( "malformed typed refs are typed refusals at the gate" )
    {
        QVariantMap both;
        both[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "path" ), QStringLiteral( "a.tif" ) },
            { QStringLiteral( "asset_id" ), QStringLiteral( "x" ) } };
        gateRefused( both, "INVALID_PARAMETER" );

        QVariantMap neither;
        neither[QStringLiteral( "main" )] = QVariantMap{
            { QStringLiteral( "wrong" ), QStringLiteral( "x" ) } };
        gateRefused( neither, "INVALID_PARAMETER" );

        QVariantMap emptyPath;
        emptyPath[QStringLiteral( "main" )] =
            QVariantMap{ { QStringLiteral( "path" ), QString() } };
        gateRefused( emptyPath, "INVALID_PARAMETER" );

        QVariantMap nonString;
        nonString[QStringLiteral( "main" )] =
            QVariantMap{ { QStringLiteral( "path" ), 42 } };
        gateRefused( nonString, "INVALID_PARAMETER" );

        QVariantMap emptyId;
        emptyId[QStringLiteral( "main" )] =
            QVariantMap{ { QStringLiteral( "asset_id" ), QStringLiteral( "  " ) } };
        gateRefused( emptyId, "INVALID_PARAMETER" );
    }

    SECTION( "legacy string form is preserved verbatim" )
    {
        // The #1826-preserved heuristic: separator/dotted strings anchor,
        // id-shaped strings pass through.
        QVariantMap outside;
        outside[QStringLiteral( "main" )] = QStringLiteral( "../outside.tif" );
        gateRefused( outside, "PATH_OUTSIDE_WORKSPACE" );

        QVariantMap inside;
        inside[QStringLiteral( "main" )] = QStringLiteral( "inner/scene.tif" );
        reachedDriver( inside );
    }

    qunsetenv( "SICNU_MCP_WORKSPACE" );
}

// ---------------------------------------------------------------------------
// R1 review regressions — status identity and query pollution
// ---------------------------------------------------------------------------

TEST_CASE( "r29_t03 R1: status honors session_id; queries never masquerade as status",
           "[r29_t03][agent_ops][status_identity]" )
{
    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );
    qputenv( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );

    using namespace sicnu::agent_loop;
    // FAST fake seams: dry-run sessions complete synchronously with a full
    // envelope that echoes its own session_id (the discriminator).
    FakeScenario scenario;
    FakeSeams seams( scenario );
    sicnu::agent_ops::OperationsCoordinator::Dependencies deps;
    deps.seams.data = &seams.dataProvider();
    deps.seams.planner = &seams.planner();
    deps.seams.preflight = &seams.preflight();
    deps.seams.executor = &seams.executor();
    deps.seams.verifier = &seams.verifier();
    deps.seams.diagnoser = &seams.diagnoser();
    sicnu::agent_ops::OpsDriver::Options options;
    options.deps = std::move( deps );
    static sicnu::agent_ops::OpsDriver fastDriver( std::move( options ) );

    R29TestMcpServer server;
    server.setAgentOpsDriver( &fastDriver );

    auto runFor = [&]( const char *id ) -> QVariantMap {
        QVariantMap args;
        args[QStringLiteral( "action" )] = QStringLiteral( "run" );
        args[QStringLiteral( "goal" )] = QStringLiteral( "compute NDVI for the scene" );
        args[QStringLiteral( "mode" )] = QStringLiteral( "dry_run" );
        args[QStringLiteral( "session_id" )] = QLatin1String( id );
        args[QStringLiteral( "journal_directory" )] =
            workspace.filePath( QStringLiteral( "journals" ) );
        return args;
    };

    // Two named sessions complete (fast, synchronous envelopes).
    auto a = sessionCall( server, 300, runFor( "r29-r1-a" ) );
    auto b = sessionCall( server, 301, runFor( "r29-r1-b" ) );
    REQUIRE_FALSE( a.isError );
    REQUIRE_FALSE( b.isError );

    // R1-P1A: status for session A returns A's envelope — never B's.
    QVariantMap byId;
    byId[QStringLiteral( "action" )] = QStringLiteral( "status" );
    byId[QStringLiteral( "session_id" )] = QStringLiteral( "r29-r1-a" );
    auto statusA = sessionCall( server, 302, byId );
    REQUIRE_FALSE( statusA.isError );
    CHECK( statusA.doc.value( QStringLiteral( "session_id" ) ).toString() ==
           QStringLiteral( "r29-r1-a" ) );
    CHECK( statusA.doc.value( QStringLiteral( "action" ) ).toString() ==
           QStringLiteral( "run" ) );

    // status without an id serves the LAST LAUNCH's envelope.
    QVariantMap noId;
    noId[QStringLiteral( "action" )] = QStringLiteral( "status" );
    auto statusLast = sessionCall( server, 303, noId );
    REQUIRE_FALSE( statusLast.isError );
    CHECK( statusLast.doc.value( QStringLiteral( "session_id" ) ).toString() ==
           QStringLiteral( "r29-r1-b" ) );

    // R1-P1B: a query (timeline) must NOT become what status serves.
    QVariantMap timeline;
    timeline[QStringLiteral( "action" )] = QStringLiteral( "timeline" );
    auto tl = sessionCall( server, 304, timeline );
    REQUIRE_FALSE( tl.isError );
    CHECK( tl.doc.value( QStringLiteral( "action" ) ).toString() ==
           QStringLiteral( "timeline" ) );
    auto statusAfterQuery = sessionCall( server, 305, noId );
    CHECK( statusAfterQuery.doc.value( QStringLiteral( "action" ) ).toString() ==
           QStringLiteral( "run" ) ); // the launch envelope, not the timeline doc
    CHECK( statusAfterQuery.doc.value( QStringLiteral( "session_id" ) ).toString() ==
           QStringLiteral( "r29-r1-b" ) );

    // Unknown session id: typed refusal pointing at reconcile (a session
    // from a previous host process lives in its journal, not memory).
    QVariantMap unknown;
    unknown[QStringLiteral( "action" )] = QStringLiteral( "status" );
    unknown[QStringLiteral( "session_id" )] = QStringLiteral( "from-a-previous-host" );
    auto statusUnknown = sessionCall( server, 306, unknown );
    REQUIRE_FALSE( statusUnknown.isError );
    CHECK( statusUnknown.doc.value( QStringLiteral( "error" ) ).toString() ==
           QStringLiteral( "UNKNOWN_SESSION" ) );
    CHECK( statusUnknown.text.contains( QStringLiteral( "reconcile" ) ) );

    qunsetenv( "SICNU_MCP_WORKSPACE" );
}

// ---------------------------------------------------------------------------
// WP06 — control availability during a long session run (async semantics)
// ---------------------------------------------------------------------------

namespace {

/// An executor whose run NEVER finishes on its own: poll honors the loop's
/// bounded wait in cancel-checked slices and only reports finished once
/// cancel() fired — a deterministic minutes-scale session for the MCP host
/// without touching real rasters.
class SlowUntilCancelledExecutor final : public sicnu::agent_loop::IExecutor
{
  public:
    sicnu::agent_loop::ExecutionStart begin( const sicnu::agent_loop::PlanDraft & ) override
    {
        // Each new run starts uncancelled (latches are per-run in the real
        // engine: cancelRun(pipelineId)).
        mCancelled = false;
        sicnu::agent_loop::ExecutionStart start;
        start.runId = "slow-run";
        start.started = true;
        return start;
    }
    sicnu::agent_loop::ExecutionOutcome poll( const sicnu::agent_loop::ExecutionStart &start,
                                              long long timeoutMs ) override
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
        while ( std::chrono::steady_clock::now() < deadline )
        {
            if ( mCancelled.load() )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        sicnu::agent_loop::ExecutionOutcome outcome;
        outcome.runId = start.runId;
        if ( mCancelled.load() )
        {
            outcome.finished = true;
            outcome.succeeded = false;
            outcome.state = "cancelled";
            outcome.errorCode = "CANCELLED";
        }
        return outcome; // not finished on timeout
    }
    void cancel( const sicnu::agent_loop::ExecutionStart & ) override { mCancelled = true; }
    bool cancelled() const { return mCancelled.load(); }

  private:
    std::atomic<bool> mCancelled{ false };
};

} // namespace

TEST_CASE( "r29_t03 long session: running envelope, live control, settled status",
           "[r29_t03][agent_ops][session_control]" )
{
    QTemporaryDir workspace;
    REQUIRE( workspace.isValid() );
    qputenv( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() );
    // Journals live INSIDE the workspace (the containment gate would refuse
    // an absolute path from another temp tree — that refusal is its own
    // covered case in the refs suite above).
    const QString journals = workspace.filePath( QStringLiteral( "journals" ) );

    using namespace sicnu::agent_loop;

    FakeScenario scenario; // plan→preflight→(execute)→verify all succeed
    FakeSeams seams( scenario );
    SlowUntilCancelledExecutor slowExecutor;
    sicnu::agent_ops::OperationsCoordinator::Dependencies deps;
    deps.seams.data = &seams.dataProvider();
    deps.seams.planner = &seams.planner();
    deps.seams.preflight = &seams.preflight();
    deps.seams.executor = &slowExecutor;
    deps.seams.verifier = &seams.verifier();
    deps.seams.diagnoser = &seams.diagnoser();
    sicnu::agent_ops::OpsDriver::Options slowOptions;
    slowOptions.deps = std::move( deps );
    static sicnu::agent_ops::OpsDriver slowDriver( std::move( slowOptions ) );

    R29TestMcpServer server;
    server.setAgentOpsDriver( &slowDriver );

    // 1. The launch returns a typed RUNNING envelope instead of blocking
    //    the MCP host for the whole session.
    QVariantMap runArgs;
    runArgs[QStringLiteral( "action" )] = QStringLiteral( "run" );
    runArgs[QStringLiteral( "goal" )] = QStringLiteral( "compute NDVI for the scene" );
    runArgs[QStringLiteral( "mode" )] = QStringLiteral( "execute_with_verify" );
    runArgs[QStringLiteral( "journal_directory" )] = journals;
    auto launched = sessionCall( server, 200, runArgs );
    CHECK_FALSE( launched.isError );
    CHECK( launched.text.contains( QStringLiteral( "\"running\"" ) ) );
    CHECK( launched.text.contains( QStringLiteral( "session_id" ) ) );
    // The generated id is legal for journals and traceable.
    const QRegularExpression idRe( QStringLiteral( "\"session_id\"\\s*:\\s*\"(mcp-[0-9-]+)\"" ) );
    const auto idMatch = idRe.match( launched.text );
    REQUIRE( idMatch.hasMatch() );

    // 2. Control DURING the run: cancel answers immediately (the main loop
    //    is not blocked) and reaches the actual execution.
    QVariantMap cancelArgs;
    cancelArgs[QStringLiteral( "action" )] = QStringLiteral( "cancel" );
    const auto cancelStart = std::chrono::steady_clock::now();
    auto cancelled = sessionCall( server, 201, cancelArgs );
    const auto cancelElapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                               cancelStart );
    CHECK_FALSE( cancelled.isError );
    CHECK( cancelled.doc.value( "ok" ) == true );
    CHECK( cancelElapsed.count() < 500 ); // answered while the run is live
    CHECK( cancelled.text.contains( idMatch.captured( 1 ) ) ); // active session named

    // 3. The run actually stops (cancel reached the executor through the
    //    coordinator's cross-thread atomic and the loop's cancel-aware
    //    sliced poll) — poll status until the final envelope settles.
    bool settled = false;
    QString finalText;
    QVariantMap statusArgs;
    statusArgs[QStringLiteral( "action" )] = QStringLiteral( "status" );
    for ( int i = 0; i < 400 && !settled; ++i )
    {
        auto status = sessionCall( server, 202 + i, statusArgs );
        REQUIRE_FALSE( status.isError );
        if ( !status.text.contains( QStringLiteral( "\"running\"" ) ) )
        {
            settled = true;
            finalText = status.text;
        }
        else
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
        }
    }
    REQUIRE( settled );
    CHECK( slowExecutor.cancelled() );
    // The settled envelope is the session's terminal document (cancelled
    // run — not a green completion).
    CHECK( finalText.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) );

    // 4. A second launch after the settle is accepted again once the
    //    latched cancel is cleared (documented semantics: cancel latches
    //    until clear_cancel arms the coordinator for new work).
    QVariantMap clearArgs;
    clearArgs[QStringLiteral( "action" )] = QStringLiteral( "clear_cancel" );
    sessionCall( server, 499, clearArgs );
    auto second = sessionCall( server, 500, runArgs );
    CHECK_FALSE( second.isError );
    const bool secondRunning = second.text.contains( QStringLiteral( "\"running\"" ) );
    CHECK( secondRunning );
    if ( secondRunning )
    {
        sessionCall( server, 501, cancelArgs );
        for ( int i = 0; i < 200; ++i )
        {
            auto status = sessionCall( server, 600 + i, statusArgs );
            if ( !status.text.contains( QStringLiteral( "\"running\"" ) ) )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
        }
    }

    qunsetenv( "SICNU_MCP_WORKSPACE" );
}
