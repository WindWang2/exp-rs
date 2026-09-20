// tests/test_capability_surface_parity.cpp
//
// Track D1 (ds41-capability-help-sync) surface parity gate for the ALGORITHM /
// CAPABILITY discovery chain. The tool-surface projection (meta tools +
// data-platform tools + AgentToolCatalog) is already gated by
// test_surface_parity; this file closes the sibling gap: the capability chain
//
//   live registry descriptors (single source of truth)
//     -> sidecar A  data/processing/algorithm_meta/*.json        (MCP `catalog`)
//     -> sidecar B  data/processing/algorithm_meta/capability/*.json
//     -> help       operator.<domain> topics (src/help/**)
//     -> MCP        list_algorithms / get_algorithm_schema
//     -> CLI        `algorithms list` / `--schema`
//
// must present ONE id universe and ONE schema vocabulary. Before this gate
// each surface projected independently: nothing compared CLI `algorithms
// list`, MCP `list_algorithms`, sidecar A ids, sidecar B ids, or the help
// topic set against the registry (cli-mcp-agent-surface-11 gated only the
// TOOL surface and recorded this gap as a known limitation).
//
// Rules of the gate:
//   * The rs: slice must match EXACTLY across registry / sidecar A (task-
//     declaring set) / sidecar B / help operator topics / MCP list_algorithms.
//   * Non-rs families (gdal:/otb:/qgis:/native:/io:/custom_tools:, plugins)
//     are a documented superset on the full-engine CLI process: the gate
//     asserts the rs: slice exactly and requires every CLI rs: id to resolve
//     live, never that the two universes are equal.
//   * The MCP `catalog` block must appear exactly for sidecar-A ids — a new
//     task-declaring operator that never regenerated its sidecar loses its
//     catalog on the agent surface, and that must fail here.
//   * Schema parity is checked on declared key samples (name/type/default/
//     enum), not byte equality: MCP projects the descriptor's input schema and
//     stamps facts (determinism grade) the raw operator schema does not carry;
//     that documented divergence is asserted, not ignored.
//   * Every exemption is an enumerated (id, reason) pair in this file. No
//     magic counts.

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include "agent/harness/capability_catalog.h"
#include "agent/mcp_server.h"
#include "help/adapters/operator_help_source.h"
#include "help/help_composition.h"
#include "help/help_id.h"
#include "help/help_registry.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef SICNU_SOURCE_DIR
#error "SICNU_SOURCE_DIR must point at the repo source tree"
#endif
#ifndef SICNU_TEST_CAPABILITY_CLI
#error "SICNU_TEST_CAPABILITY_CLI must point at the sicnu_geo_rs_cli binary"
#endif

using namespace sicnu;
using namespace sicnu::help;

namespace {

std::string canonicalJson( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    const std::string raw = Json::writeString( builder, value );
    const QJsonDocument doc = QJsonDocument::fromJson( QByteArray::fromStdString( raw ) );
    return std::string( doc.toJson( QJsonDocument::Compact ).constData() );
}

std::string canonicalJson( const QVariant &value )
{
    const QJsonDocument doc = QJsonDocument::fromVariant( value );
    return std::string( doc.toJson( QJsonDocument::Compact ).constData() );
}

struct Bootstrap
{
    AlgorithmMetaStore &store;
    agent::harness::CapabilityCatalog &catalog;
    std::vector<std::string> registryRs;          ///< every rs: operator name
    std::vector<std::string> taskDeclaring;       ///< descriptors declaring a task family
    std::set<std::string> sidecarA;               ///< algorithm_meta/*.json ids
    std::set<std::string> sidecarB;               ///< capability/rs-*.json ids

    explicit Bootstrap()
      : store( processing::AlgorithmMetaStore::instance() ),
        catalog( agent::harness::CapabilityCatalog::instance() )
    {
        operators::RSOperatorRegistry::instance();
        operators::rs::initBuiltinRsOperators();
        operators::rs::installRsOperatorProvider();
        processing::AtomicAlgorithmRegistry::instance().initialize();

        REQUIRE( store.loadDefaults() > 0 );
        for ( const auto &entry : store.entries() )
          sidecarA.insert( entry.id );

        catalog.setDirectory( std::string( SICNU_SOURCE_DIR ) +
                              "/data/processing/algorithm_meta/capability" );
        catalog.reload();
        REQUIRE( catalog.loadProblems().empty() );
        for ( const std::string &id : catalog.entryIds() )
          sidecarB.insert( id );

        auto &registry = operators::RSOperatorRegistry::instance();
        for ( const std::string &name : registry.operatorNames() )
          if ( name.rfind( "rs:", 0 ) == 0 )
            registryRs.push_back( name );
        std::sort( registryRs.begin(), registryRs.end() );

        for ( const auto &desc : processing::AtomicAlgorithmRegistry::instance().listDescriptors() )
          if ( desc.id.rfind( "rs:", 0 ) == 0 && !desc.agentMetadata.taskFamily.empty() )
            taskDeclaring.push_back( desc.id );
        std::sort( taskDeclaring.begin(), taskDeclaring.end() );

        adapters::OperatorHelpSource helpSource;
        const auto report = help::composeHelpSystem( help::globalHelpRegistry(), nullptr, &helpSource );
        REQUIRE( report.ok() );
    }
};

/// MCP probe server: same protected-handler access pattern as
/// test_surface_parity's SurfaceProbeServer (mcp_server.cpp is compiled into
/// this test binary).
class CapabilityProbeServer : public McpServer
{
  public:
    QVariantMap lastResponseResult;
    int lastErrorCode = -1;
    QString lastErrorMessage;

    void request( const QVariantMap &req ) { handleRequest( req ); }

    void sendResponse( const QVariant &, const QVariantMap &result ) override
    {
        lastResponseResult = result;
        lastErrorCode = 0;
    }
    void sendError( const QVariant &, int code, const QString &message ) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendError( const QVariant &, int code, const QString &message, const QVariantMap & ) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

    /// tools/call probe; returns true when the call succeeded (no isError).
    bool callTool( const QString &name, const QVariantMap &args )
    {
        QVariantMap params;
        params[ QStringLiteral( "name" ) ] = name;
        params[ QStringLiteral( "arguments" ) ] = args;
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 1 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ), params } } );
        if ( lastErrorCode != 0 )
          return false;
        return !lastResponseResult.value( QStringLiteral( "isError" ) ).toBool();
    }

    QVariant toolOutput() const
    {
        const QVariantList content =
          lastResponseResult.value( QStringLiteral( "content" ) ).toList();
        if ( content.isEmpty() )
          return {};
        const QVariantMap first = content.first().toMap();
        const QString text = first.value( QStringLiteral( "text" ) ).toString();
        const QJsonDocument doc = QJsonDocument::fromJson( text.toUtf8() );
        return doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
    }
};

CapabilityProbeServer &mcp()
{
    static CapabilityProbeServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_capability_surface_parity";
        static char *argv[] = { arg0 };
        if ( !QCoreApplication::instance() )
          new QCoreApplication( argc, argv );
        auto *s = new CapabilityProbeServer();
        s->request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 0 },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{ { QStringLiteral( "protocolVersion" ),
                                                          QStringLiteral( "2024-11-05" ) } } } } );
        return s;
    }();
    return *instance;
}

/// Runs the real CLI binary as a subprocess (full engine init, so it sees
/// plugin/provider algorithms the in-process registry does not) and returns
/// the parsed `data` object. OpenCV's INFO banners pollute stdout on Windows
/// debug builds; the log level is pinned so the payload parses cleanly.
QVariantMap runCli( const QStringList &args )
{
    QProcess cli;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert( QStringLiteral( "OPENCV_LOG_LEVEL" ), QStringLiteral( "warning" ) );
    env.insert( QStringLiteral( "QT_QPA_PLATFORM" ), QStringLiteral( "offscreen" ) );
    cli.setProcessEnvironment( env );
    cli.start( QString::fromUtf8( SICNU_TEST_CAPABILITY_CLI ), args );
    REQUIRE( cli.waitForStarted( 10000 ) );
    REQUIRE( cli.waitForFinished( 180000 ) );
    REQUIRE( cli.exitStatus() == QProcess::NormalExit );
    INFO( "cli args: " << args.join( ' ' ).toStdString()
                       << " stderr: " << cli.readAllStandardError().toStdString() );
    REQUIRE( cli.exitCode() == 0 );
    const QByteArray raw = cli.readAllStandardOutput();
    const int jsonStart = raw.indexOf( '{' );
    REQUIRE( jsonStart >= 0 );
    const QJsonDocument doc = QJsonDocument::fromJson( raw.mid( jsonStart ) );
    REQUIRE( doc.isObject() );
    return doc.object().toVariantMap();
}

std::set<std::string> idSet( const std::vector<std::string> &ids )
{
    return { ids.begin(), ids.end() };
}

namespace {

/// Normalized parameter fact set: name -> "type|default|enum" signature under
/// one serializer, so a jsoncpp-side schema and a QVariant-side schema can be
/// compared without trusting either writer's number formatting.
std::map<std::string, std::string> paramSignatures( const QVariant &schema )
{
    std::map<std::string, std::string> out;
    const QVariantMap props = schema.toMap().value( QStringLiteral( "properties" ) ).toMap();
    for ( auto it = props.constBegin(); it != props.constEnd(); ++it )
    {
        const QVariantMap p = it.value().toMap();
        QString sig = p.value( QStringLiteral( "type" ) ).toString();
        const QVariant def = p.value( QStringLiteral( "default" ) );
        if ( def.isValid() )
          sig += QStringLiteral( "|default=" ) + canonicalJson( def );
        const QVariant en = p.value( QStringLiteral( "enum" ) );
        if ( en.isValid() )
          sig += QStringLiteral( "|enum=" ) + canonicalJson( en );
        out[ it.key().toStdString() ] = sig.toStdString();
    }
    return out;
}

std::map<std::string, std::string> paramSignatures( const Json::Value &schema )
{
    return paramSignatures( QJsonDocument::fromJson(
      QByteArray::fromStdString( canonicalJson( schema ) ) ).object().toVariantMap() );
}

} // namespace

TEST_CASE( "Capability chain: registry, sidecars, help and MCP present one rs: universe",
           "[capability][surface_parity]" )
{
    Bootstrap boot;

    const std::set<std::string> registry = idSet( boot.registryRs );
    const std::set<std::string> taskSet = idSet( boot.taskDeclaring );

    // Sidecar A == exactly the task-declaring descriptor set.
    for ( const std::string &id : taskSet )
      if ( !boot.sidecarA.count( id ) )
        FAIL( "task-declaring operator has no sidecar A: " + id +
              " — regenerate with: sicnu_geo_rs_cli --export-catalog data/processing/algorithm_meta" );
    for ( const std::string &id : boot.sidecarA )
      if ( !taskSet.count( id ) )
        FAIL( "sidecar A names a non-task-declaring id: " + id );

    // Sidecar B == exactly the registered rs: universe.
    for ( const std::string &id : registry )
      if ( !boot.sidecarB.count( id ) )
        FAIL( "operator missing capability sidecar: " + id +
              " — regenerate with: capability_knowledge_tool gen-meta <repo-root>" );
    for ( const std::string &id : boot.sidecarB )
      if ( !registry.count( id ) )
        FAIL( "capability sidecar names an unregistered operator: " + id );

    // Help operator topics cover every registered rs: operator. The base topic
    // is derived from the registry by construction (operator_help_provider),
    // so absence means the composition or the id grammar drifted.
    for ( const std::string &id : registry )
    {
        const QString helpId = QStringLiteral( "operator.%1" ).arg(
          help::HelpId::domainForOperatorId( QString::fromStdString( id ) ) );
        INFO( "operator: " << id );
        REQUIRE( help::globalHelpRegistry().find( helpId ) != nullptr );
    }

    // MCP list_algorithms: the rs: slice must match the registry exactly, and
    // the `catalog` block must appear exactly for sidecar-A ids.
    CapabilityProbeServer &server = mcp();
    REQUIRE( server.callTool( QStringLiteral( "list_algorithms" ),
                              QVariantMap{ { QStringLiteral( "limit" ), 2000 } } ) );
    const QVariantMap listed = server.toolOutput();
    const QVariantList entries = listed.value( QStringLiteral( "algorithms" ) ).toList();
    REQUIRE_FALSE( entries.isEmpty() );

    std::set<std::string> mcpRs;
    std::set<std::string> mcpCatalog;
    for ( const QVariant &entryValue : entries )
    {
        const QVariantMap entry = entryValue.toMap();
        const QString id = entry.value( QStringLiteral( "id" ) ).toString();
        if ( id.startsWith( QStringLiteral( "rs:" ) ) )
          mcpRs.insert( id.toStdString() );
        if ( entry.contains( QStringLiteral( "catalog" ) ) )
          mcpCatalog.insert( id.toStdString() );
    }

    for ( const std::string &id : registry )
      if ( !mcpRs.count( id ) )
        FAIL( "operator missing from MCP list_algorithms: " + id );
    for ( const std::string &id : mcpRs )
      if ( !registry.count( id ) )
        FAIL( "MCP list_algorithms serves an unregistered rs: id: " + id );

    for ( const std::string &id : boot.sidecarA )
      if ( !mcpCatalog.count( id ) )
        FAIL( "sidecar-A id has no MCP catalog block: " + id );
    for ( const std::string &id : mcpCatalog )
      if ( !boot.sidecarA.count( id ) )
        FAIL( "MCP catalog block for an id without sidecar A: " + id );
}

TEST_CASE( "CLI algorithms list agrees with the registry rs: slice (real subprocess)",
           "[capability][surface_parity][cli]" )
{
    Bootstrap boot;
    const std::set<std::string> registry = idSet( boot.registryRs );

    const QVariantMap data = runCli( { QStringLiteral( "algorithms" ),
                                        QStringLiteral( "list" ),
                                        QStringLiteral( "--json" ) } );
    // The CLI 3.0 envelope is {api_version, command, data:[...], ok}; `data`
    // is the algorithm array itself.
    const QVariantList listed = data.value( QStringLiteral( "data" ) ).toList();
    REQUIRE_FALSE( listed.isEmpty() );

    std::set<std::string> cliRs;
    for ( const QVariant &entryValue : listed )
    {
        const QString id = entryValue.toMap().value( QStringLiteral( "id" ) ).toString();
        if ( id.startsWith( QStringLiteral( "rs:" ) ) )
          cliRs.insert( id.toStdString() );
    }

    for ( const std::string &id : registry )
      if ( !cliRs.count( id ) )
        FAIL( "operator missing from CLI algorithms list: " + id );
    for ( const std::string &id : cliRs )
      if ( !registry.count( id ) )
        FAIL( "CLI algorithms list serves an unregistered rs: id: " + id );

    // The CLI's full-engine process legitimately lists MORE (gdal:/otb:/qgis:/
    // native:/io:/custom_tools: provider algorithms, plugin operators); the
    // gate pins the rs: slice exactly and counts the rest as information.
    WARN( "CLI listed " << listed.size() << " algorithms (" << cliRs.size()
                        << " rs:)" );
}

TEST_CASE( "get_algorithm_schema matches the descriptor and the CLI raw schema on key samples",
           "[capability][surface_parity][schema]" )
{
    Bootstrap boot;
    CapabilityProbeServer &server = mcp();

    // Key samples: a flagship spectral index (band roles), a model-task
    // adapter, a tolerance-graded SAR operator, a classic classifier, and a
    // raster-spatial operator. Determinism is stamped by MCP but not by the
    // raw operator schema — asserted as a documented divergence below.
    static const std::vector<std::string> kSamples = {
        "rs:spectral_index", "rs:change", "rs:sar_speckle",
        "rs:supervised_classification", "rs:terrain_viewshed" };

    auto &registry = processing::AtomicAlgorithmRegistry::instance();
    for ( const std::string &id : kSamples )
    {
        INFO( "sample: " << id );
        auto adapter = registry.findAdapter( id );
        REQUIRE( adapter != nullptr );

        // 1. MCP get_algorithm_schema's input_schema == the descriptor's own
        //    projection (the MCP surface must not alias the descriptor).
        REQUIRE( server.callTool( QStringLiteral( "get_algorithm_schema" ),
                                  QVariantMap{ { QStringLiteral( "algorithm_id" ),
                                                 QString::fromStdString( id ) } } ) );
        const QVariantMap schemaOut = server.toolOutput();
        const QVariant inputSchema = schemaOut.value( QStringLiteral( "input_schema" ) );
        REQUIRE( inputSchema.isValid() );
        const auto descriptorParams = paramSignatures( adapter->descriptor().toInputSchema() );
        const auto projectedParams = paramSignatures( inputSchema );
        REQUIRE( projectedParams == descriptorParams );

        // 2. Every parameter of the RAW operator schema appears with the same
        //    type/default/enum in the MCP projection, and vice versa: the
        //    projection may never rename, drop or retype a declared parameter.
        //    `--schema` prints the raw operator schema without the CLI 3.0
        //    envelope (legacy flag path in main_cli.cpp).
        auto op = operators::RSOperatorRegistry::instance().create( id );
        REQUIRE( op != nullptr );
        const QVariantMap rawSchema =
          runCli( { QStringLiteral( "--schema" ), QString::fromStdString( id ) } );
        const auto rawParams = paramSignatures( rawSchema );
        REQUIRE_FALSE( rawParams.empty() );
        REQUIRE( projectedParams == rawParams );
    }

    // Documented divergence (census G10): the raw CLI schema does not stamp
    // the ADR 0124 determinism grade; the MCP schema does. Pin the raw shape
    // (parameters carry name/type/default, no determinism key) so neither side
    // silently gains the other's field set.
    const QVariantMap cliSchema =
      runCli( { QStringLiteral( "--schema" ), QStringLiteral( "rs:sar_speckle" ) } );
    CHECK( cliSchema.contains( QStringLiteral( "properties" ) ) );
    CHECK_FALSE( cliSchema.contains( QStringLiteral( "determinism" ) ) );
    CHECK_FALSE( cliSchema.contains( QStringLiteral( "x-determinism" ) ) );
}
