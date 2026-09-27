// tests/test_mission_runtime_parity.cpp
//
// Mission Runtime 13.0 — surface parity + wiring gates (WP2/WP5).
//
// Oracles covered (.planning/glm53-mission-runtime-13/ORACLES.md):
//   O9 surface parity — GUI model row, the mission:timeline tool payload and
//      the shared projection are the same bytes; every advertised surface id
//      has a real tool object with the same description; every wiring the
//      surface needs (registry entry, provider prefix, MCP dispatch branch,
//      built-in registration, Pi category, CMake sources, test targets) is
//      proven present by parsing the REAL source files — deleting one turns
//      this gate red (that is the "delete a wiring" potency oracle).
//   + stub parity for the three QGIS-dependent symbols the out-of-tree
//      harness cannot compile (see stubs/mission_gate_stubs.cpp).

#include "agent/spatial_tools/mission_tools.h"
#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_timeline_model.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::app;

namespace
{

QString repoFile( const QString &relative )
{
    return QStringLiteral( SICNU_MISSION_SOURCE_DIR ) + QLatin1Char( '/' ) + relative;
}

QString readRepoFile( const QString &relative )
{
    QFile f( repoFile( relative ) );
    if ( !f.open( QIODevice::ReadOnly | QIODevice::Text ) )
        return {};
    return QString::fromUtf8( f.readAll() );
}

/// True when @p needle appears in the file (whitespace-normalised).
bool fileContains( const QString &relative, const QString &needle )
{
    const QString text = readRepoFile( relative );
    const QString squeezed = text.simplified();
    return squeezed.contains( needle.simplified() );
}

MissionTask makeTask( const QString &id, MissionStage stage, MissionTaskStatus status )
{
    MissionTask t;
    t.id = id;
    t.stage = stage;
    t.title = QStringLiteral( "Task %1" ).arg( id );
    t.status = status;
    t.capabilityId = QStringLiteral( "rs:spectral_index" );
    t.inputRefIds = { QStringLiteral( "layer-a" ) };
    t.outputRefIds = { QStringLiteral( "artifact-1" ) };
    return t;
}

} // namespace

// ── O9: one projection, three surfaces ───────────────────────────────────

TEST_CASE( "the desktop model and the agent tools render one projection", "[mission][parity]" )
{
    MissionTimeline timeline;
    timeline.setMissionId( QStringLiteral( "mission-parity" ) );
    timeline.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Import,
                                MissionTaskStatus::Succeeded ) );
    timeline.addTask( makeTask( QStringLiteral( "t2" ), MissionStage::Analyze,
                                MissionTaskStatus::Pending ) );

    MissionTimelineModel model;
    model.setTimeline( timeline );

    // GUI row bytes == the shared task projection bytes.
    const QByteArray guiRow = missionCanonicalJson( model.projectionAt( 0 ) );
    const QByteArray sharedRow =
        missionCanonicalJson( missionTaskProjectionJson( *timeline.task( QStringLiteral( "t1" ) ) ) );
    CHECK( guiRow == sharedRow );

    // The agent tool payload carries the same rows (parsed back to QJsonObject
    // because jsoncpp re-sorts keys; the values are identical).
    sicnu::agent::spatial_tools::MissionTimelineTool tool;
    Json::Value schema = tool.outputSchema();
    CHECK( schema.isObject() );
    // Feed the tool the same timeline through the shared projection function
    // it uses: the payload of a real invocation is asserted in
    // test_mission_tools.cpp; here the byte-contract of the shared function
    // is locked against the model.
    const QJsonObject projection = missionTimelineProjectionJson( timeline, 32, 0 );
    const QJsonArray tasks = projection.value( QStringLiteral( "tasks" ) ).toArray();
    REQUIRE( tasks.size() == 2 );
    for ( int row = 0; row < tasks.size(); ++row )
    {
        const QByteArray rowBytes = missionCanonicalJson( model.projectionAt( row ) );
        const QByteArray payloadBytes = missionCanonicalJson( tasks.at( row ).toObject() );
        CHECK( rowBytes == payloadBytes );
    }
}

// ── every advertised surface id has a real tool object ───────────────────

TEST_CASE( "every mission tool id in the registry has a real tool object", "[mission][parity]" )
{
    const QStringList toolIds = missionSurfaceIds( MissionSurface::AgentTool );
    CHECK( toolIds.size() == 3 );

    const std::vector<sicnu::agent::spatial_tools::SpatialTool *> tools = {
        new sicnu::agent::spatial_tools::MissionContextTool(),
        new sicnu::agent::spatial_tools::MissionTimelineTool(),
        new sicnu::agent::spatial_tools::MissionAdvanceTool(),
    };
    QStringList toolNames;
    for ( const auto *tool : tools )
        toolNames.push_back( QString::fromStdString( tool->name() ) );

    // No registry id without a tool, and no tool without a registry id.
    for ( const QString &id : toolIds )
        CHECK( toolNames.contains( id ) );
    for ( const QString &name : toolNames )
        CHECK( toolIds.contains( name ) );

    // The advertised description is the tool's own description (one truth).
    for ( const auto *tool : tools )
    {
        const QString id = QString::fromStdString( tool->name() );
        const MissionSurfaceEntry *entry = nullptr;
        for ( const MissionSurfaceEntry &e : missionSurfaceRegistry() )
            if ( e.id == id )
                entry = &e;
        REQUIRE( entry != nullptr );
        CHECK( entry->description == QString::fromStdString( tool->description() ) );
    }

    for ( auto *tool : tools )
        delete tool;
}

// ── the surface registry stays clean (12.0 gates, still load-bearing) ────

TEST_CASE( "mission surface ids remain unique and phantom-free", "[mission][parity]" )
{
    CHECK( missionDuplicateSurfaceIds().isEmpty() );
    CHECK( missionPhantomSurfaceIds().isEmpty() );
    CHECK( missionDuplicatePiToolNames().isEmpty() );

    // The declared app commands are registered in the desktop shell.
    const QStringList commandIds = missionSurfaceIds( MissionSurface::AppCommand );
    CHECK( commandIds.contains( QStringLiteral( "mission.timeline.show" ) ) );
    CHECK( commandIds.contains( QStringLiteral( "mission.task.retry" ) ) );
    CHECK( commandIds.contains( QStringLiteral( "mission.task.resume" ) ) );
    for ( const QString &id : commandIds )
    {
        // App commands must not use the agent-tool namespace separator.
        CHECK_FALSE( id.contains( QLatin1Char( ':' ) ) );
        CHECK( fileContains( QStringLiteral( "src/app/workbench/command_defs.cpp" ),
                             QStringLiteral( "\"%1\"" ).arg( id ) ) );
    }
}

// ── wiring gates: deleting one turns this red ────────────────────────────

TEST_CASE( "the mission family is wired into every surface", "[mission][parity]" )
{
    // 1. MCP allow-list prefix (surface_registry.cpp).
    CHECK( fileContains( QStringLiteral( "src/agent/tool_catalog/surface_registry.cpp" ),
                         QStringLiteral( "\"mission:\"" ) ) );

    // 2. The MCP tools/call dispatch chain routes the family. Since the
    // registry-truth routing refactor (#1250) the per-family prefix literal
    // is gone: the dispatch consults the surface allow-list (pinned by gate
    // 1) through isToolIdAllowed, so THAT is the wiring to pin here.
    CHECK( fileContains( QStringLiteral( "src/agent/mcp_server.cpp" ),
                         QStringLiteral( "isToolIdAllowed(toolName)" ) ) );

    // 3. The catalog provider advertises the family prefix.
    CHECK( fileContains( QStringLiteral( "src/agent/spatial_tools/spatial_tool_provider.cpp" ),
                         QStringLiteral( "\"mission:\"" ) ) );

    // 4. The built-in registration installs the tools.
    CHECK( fileContains( QStringLiteral( "src/agent/spatial_tools/spatial_tool.cpp" ),
                         QStringLiteral( "registerMissionTools();" ) ) );

    // 5. Pi bridges the category — the exact default category list literal
    // (a bare "mission" would be satisfiable by a comment).
    CHECK( fileContains( QStringLiteral( "pi/exp-rs-spatial.ts" ),
                         QStringLiteral( "\"meta,spatial,data,temporal,cartography,symbology,"
                                         "workflow,workspace,layout,harness,mission\"" ) ) );

    // 6. The new sources are registered in the target that compiles them —
    // and NOT in the other one (duplicate definitions across the executable
    // and the DLL are a Windows link hazard). The checks name the source on a
    // line that also names the workbench directory, so a comment that merely
    // mentions the file cannot satisfy them.
    const auto compiledInto = []( const QString &cmake, const QString &source ) {
        const QRegularExpression re(
            QStringLiteral( "workbench/%1|spatial_tools/%1" ).arg( source ) );
        return re.match( readRepoFile( cmake ).simplified() ).hasMatch();
    };
    // sicnu_agent owns the QGIS-free value model + tools.
    for ( const QString &source : { QStringLiteral( "mission_tools.cpp" ),
                                    QStringLiteral( "spatial_tool_registry.cpp" ),
                                    QStringLiteral( "mission_stage.cpp" ),
                                    QStringLiteral( "mission_projection.cpp" ),
                                    QStringLiteral( "mission_run_authority.cpp" ) } )
    {
        INFO( source.toStdString() + " must be compiled by sicnu_agent" );
        CHECK( compiledInto( QStringLiteral( "src/agent/CMakeLists.txt" ), source ) );
    }
    // The executable owns the persistence chain + shell surface only.
    for ( const QString &source : { QStringLiteral( "mission_timeline_bridge.cpp" ),
                                    QStringLiteral( "mission_runtime_store.cpp" ),
                                    QStringLiteral( "mission_tool_authority.cpp" ),
                                    QStringLiteral( "mission_timeline_panel.cpp" ),
                                    QStringLiteral( "mission_run_resolver.cpp" ) } )
    {
        INFO( source.toStdString() + " must be compiled by sicnu_geo_rs" );
        CHECK( compiledInto( QStringLiteral( "src/app/CMakeLists.txt" ), source ) );
    }
    // …and the exe must NOT compile the agent-owned ones (no duplicates).
    for ( const QString &source : { QStringLiteral( "mission_stage.cpp" ),
                                    QStringLiteral( "mission_projection.cpp" ),
                                    QStringLiteral( "mission_run_authority.cpp" ),
                                    QStringLiteral( "mission_tools.cpp" ) } )
    {
        INFO( source.toStdString() + " must NOT be compiled twice (exe + DLL)" );
        CHECK_FALSE( compiledInto( QStringLiteral( "src/app/CMakeLists.txt" ), source ) );
    }
    // The executable links the agent library that carries them.
    CHECK( fileContains( QStringLiteral( "src/app/CMakeLists.txt" ),
                         QStringLiteral( "sicnu_agent" ) ) );

    // 7. The gate targets are registered for the real build.
    CHECK( fileContains( QStringLiteral( "tests/CMakeLists.txt" ),
                         QStringLiteral( "test_mission_runtime_persistence" ) ) );
    CHECK( fileContains( QStringLiteral( "tests/CMakeLists.txt" ),
                         QStringLiteral( "test_mission_tools" ) ) );
    CHECK( fileContains( QStringLiteral( "tests/CMakeLists.txt" ),
                         QStringLiteral( "test_mission_runtime_parity" ) ) );
    CHECK( fileContains( QStringLiteral( "tests/CMakeLists.txt" ),
                         QStringLiteral( "test_mission_runtime_scale" ) ) );
}

// ── compile-substitute: shell symbols the harness cannot compile ─────────

TEST_CASE( "shell hunks reference only declared ContextRules predicates", "[mission][parity]" )
{
    // command_defs.cpp cannot be compiled in this environment (it pulls the
    // whole QGIS/GUI closure), so this gate substitutes for the compiler on
    // exactly the failure class an undeclared predicate causes: every
    // ContextRules::<name> the shell references must be DECLARED in
    // selection_context.h (and defined in selection_context.cpp).
    const QString defs = readRepoFile( QStringLiteral( "src/app/workbench/command_defs.cpp" ) );
    const QString header = readRepoFile( QStringLiteral( "src/app/workbench/selection_context.h" ) );
    const QString impl = readRepoFile( QStringLiteral( "src/app/workbench/selection_context.cpp" ) );
    REQUIRE_FALSE( defs.isEmpty() );
    REQUIRE_FALSE( header.isEmpty() );

    const QRegularExpression useRe( QStringLiteral( "ContextRules::([A-Za-z_]+)" ) );
    QSet<QString> used;
    auto it = useRe.globalMatch( defs );
    while ( it.hasNext() )
        used.insert( it.next().captured( 1 ) );
    CHECK( used.size() >= 8 ); // the shell uses a healthy number of rules (not vacuous)

    // Declaration shape: "<type> NAME( const SelectionContextSnapshot" — the
    // return type differs (bool predicates vs the QString reason helper).
    const auto declaredIn = []( const QString &source, const QString &name ) {
        const QRegularExpression re(
            QStringLiteral( "\\b%1\\s*\\(\\s*const SelectionContextSnapshot" ).arg( name ) );
        return re.match( source ).hasMatch();
    };
    for ( const QString &name : used )
    {
        INFO( "ContextRules::" + name.toStdString() + " must be declared in selection_context.h" );
        CHECK( declaredIn( header, name ) );
        // …and defined in the implementation TU.
        INFO( "ContextRules::" + name.toStdString() + " must be defined in selection_context.cpp" );
        CHECK( declaredIn( impl, name ) );
    }

    // The mission commands the registry declares must exist as handlers on
    // the window (main_window.h declares the slots the handlers call).
    const QString window = readRepoFile( QStringLiteral( "src/app/main_window.h" ) );
    for ( const QString &slot : { QStringLiteral( "showMissionTimelinePanel" ),
                                  QStringLiteral( "retrySelectedMissionTask" ),
                                  QStringLiteral( "resumeSelectedMissionTask" ),
                                  QStringLiteral( "refreshMissionRuntime" ) } )
    {
        INFO( "main_window.h must declare " + slot.toStdString() );
        CHECK( window.contains( slot ) );
    }
    // …and the shell file must define them.
    const QString shell = readRepoFile( QStringLiteral( "src/app/main_window_workbench.cpp" ) );
    for ( const QString &slot : { QStringLiteral( "void QgisDesktopWindow::showMissionTimelinePanel" ),
                                  QStringLiteral( "void QgisDesktopWindow::retrySelectedMissionTask" ),
                                  QStringLiteral( "void QgisDesktopWindow::resumeSelectedMissionTask" ),
                                  QStringLiteral( "void QgisDesktopWindow::refreshMissionRuntime" ) } )
    {
        INFO( "main_window_workbench.cpp must define " + slot.toStdString() );
        CHECK( shell.contains( slot ) );
    }

    // Every mission API a shell TU uses must have its declaring header in
    // that TU's include list — the exact failure class of a missing include
    // (the compiler is not available for these files here).
    const auto usesAndIncludes = []( const QString &relative, const QString &header,
                                     const QString &symbol ) {
        const QString text = readRepoFile( relative );
        return text.contains( symbol ) && text.contains( QStringLiteral( "#include \"%1\"" ).arg( header ) );
    };
    // main.cpp installs the mission tool host (headless --mcp branch).
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main.cpp" ),
                            QStringLiteral( "workbench/mission_tool_host_install.h" ),
                            QStringLiteral( "installMissionToolHost" ) ) );
    // main_window_workbench.cpp resolves run authority and owns the runtime.
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_workbench.cpp" ),
                            QStringLiteral( "workbench/mission_run_resolver.h" ),
                            QStringLiteral( "resolveMissionRunStatus" ) ) );
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_workbench.cpp" ),
                            QStringLiteral( "workbench/mission_runtime_store.h" ),
                            QStringLiteral( "MissionRuntimeState" ) ) );
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_workbench.cpp" ),
                            QStringLiteral( "workbench/mission_timeline_panel.h" ),
                            QStringLiteral( "MissionTimelinePanel" ) ) );
    // main_window_connections.cpp loads/saves the runtime and reconciles.
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_connections.cpp" ),
                            QStringLiteral( "workbench/mission_runtime_store.h" ),
                            QStringLiteral( "loadMissionRuntime" ) ) );
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_connections.cpp" ),
                            QStringLiteral( "workbench/mission_run_resolver.h" ),
                            QStringLiteral( "resolveMissionRunStatus" ) ) );
    CHECK( usesAndIncludes( QStringLiteral( "src/app/main_window_connections.cpp" ),
                            QStringLiteral( "workbench/mission_run_authority.h" ),
                            QStringLiteral( "reconcileRunAuthority" ) ) );

    // The save path must reload the authority before persisting (an agent
    // commit between saves must never be reverted by a stale cache), and the
    // open path must propagate a failed load (the poison guard). These are
    // structural pins on logic the harness cannot execute here.
    const QString connections =
        readRepoFile( QStringLiteral( "src/app/main_window_connections.cpp" ) );
    const int onWrite = connections.indexOf(
        QStringLiteral( "void QgisDesktopWindow::onProjectWrite" ) );
    REQUIRE( onWrite > 0 );
    const QString writeBody = connections.mid( onWrite );
    CHECK( writeBody.indexOf( QStringLiteral( "loadMissionRuntime" ) )
           < writeBody.indexOf( QStringLiteral( "saveMissionRuntime" ) ) );
    CHECK( writeBody.contains( QStringLiteral( "authorityCorrupt" ) ) );
    // The write path must contain exactly ONE save call, and it must sit
    // after the reload-check refusal branch (a save that runs when the
    // authority cannot be re-read publishes the window's stale cache over a
    // corrupt-but-recoverable artifact — the #1148 merge-residue duplicate
    // did exactly that, invisibly to the load<save ordering pin above).
    const int saveAt = writeBody.indexOf( QStringLiteral( "saveMissionRuntime" ) );
    CHECK( writeBody.indexOf( QStringLiteral( "saveMissionRuntime" ), saveAt + 1 ) < 0 );
    CHECK( saveAt > writeBody.indexOf( QStringLiteral( "disk.authorityCorrupt" ) ) );
    // #1149: "no authority at the target path" is first-publication, not an
    // adopt-an-empty-timeline order — the adopt assignment must be guarded by
    // an empty-authority check (Save-As / moved project / removed sidecar
    // must never wipe the live task space).
    const int adoptAt = writeBody.indexOf( QStringLiteral( "m_missionRuntime.timeline = disk.timeline" ) );
    REQUIRE( adoptAt > 0 );
    CHECK( writeBody.lastIndexOf( QStringLiteral( "!disk.authorityLoaded" ),
                                  adoptAt ) >= 0 );
    const int onRead = connections.indexOf(
        QStringLiteral( "void QgisDesktopWindow::onProjectRead" ) );
    REQUIRE( onRead > 0 );
    const QString readBody = connections.mid( onRead, onWrite - onRead );
    CHECK( readBody.contains( QStringLiteral( "m_missionRuntime = runtime" ) ) );
    CHECK( readBody.contains( QStringLiteral( "reconcileRunAuthority" ) ) );
}

// ── stub parity: the harness stubs match the real QGIS-bound sources ─────

TEST_CASE( "the harness stubs match the real object identity source", "[mission][parity]" )
{
    // The real token mapping, extracted from object_identity.cpp.
    const QString identity = readRepoFile( QStringLiteral( "src/app/workbench/object_identity.cpp" ) );
    const QRegularExpression tokenRe(
        QStringLiteral( "case\\s+ObjectKind::(\\w+):\\s*\\n\\s*return\\s+QStringLiteral\\(\\s*\"(\\w+)\"\\s*\\)" ) );
    QMap<QString, QString> expected;
    auto it = tokenRe.globalMatch( identity );
    while ( it.hasNext() )
    {
        const QRegularExpressionMatch m = it.next();
        expected.insert( m.captured( 1 ), m.captured( 2 ) );
    }
    REQUIRE( expected.size() == 7 );

    // The stub's observable mapping (it is the one the harness links).
    const struct
    {
        ObjectKind kind;
        const char *token;
    } kCases[] = {
        { ObjectKind::Layer, "layer" },
        { ObjectKind::Asset, "asset" },
        { ObjectKind::Result, "result" },
        { ObjectKind::Dataset, "dataset" },
        { ObjectKind::ExperimentRun, "experiment_run" },
        { ObjectKind::Model, "model" },
        { ObjectKind::WorkflowRun, "workflow_run" },
    };
    for ( const auto &c : kCases )
    {
        const QString kindName = QString::fromUtf8(
            [&] {
                switch ( c.kind )
                {
                    case ObjectKind::None: return "None";
                    case ObjectKind::Layer: return "Layer";
                    case ObjectKind::Asset: return "Asset";
                    case ObjectKind::Result: return "Result";
                    case ObjectKind::Dataset: return "Dataset";
                    case ObjectKind::ExperimentRun: return "ExperimentRun";
                    case ObjectKind::Model: return "Model";
                    case ObjectKind::WorkflowRun: return "WorkflowRun";
                }
                return "None";
            }() );
        CHECK( objectKindToken( c.kind ) == QString::fromUtf8( c.token ) );
        CHECK( expected.value( kindName ) == QString::fromUtf8( c.token ) );
    }

    // The primary-object priority order in the real source.
    const QStringList order = {
        QStringLiteral( "selectedWorkflowRunIds" ), QStringLiteral( "selectedExperimentIds" ),
        QStringLiteral( "selectedDatasetIds" ),    QStringLiteral( "selectedModelIds" ),
        QStringLiteral( "selectedResultIds" ),     QStringLiteral( "selectedAssetIds" ),
    };
    int lastAt = -1;
    for ( const QString &key : order )
    {
        const int at = identity.indexOf(
            QStringLiteral( "if ( !snapshot.%1.isEmpty() )" ).arg( key ) );
        REQUIRE( at > lastAt ); // the real source keeps this order
        lastAt = at;
    }
}
