// tests/test_r18_c_surface_pagination.cpp — R18-C C01: protocol authority at
// the discovery surface.
// 1. tools/list documents numeric cursors (its own nextCursor emits them);
//    a non-numeric cursor used to silently rewind to page 0 — a client bug
//    then re-processes the whole listing unnoticed. It must be a typed
//    -32602 refusal, mirroring the get_workflow_status run_id gate.
// 2. Cross-surface schema parity: get_tool_schema(name).schema must equal
//    tools/list{includeSchemas}.inputSchema[name] for EVERY tool — two
//    discovery surfaces, one authority, no unexplained drift.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "agent/mcp_server.h"

static const bool kFixtureWorkspaceInstalled = [] {
    if ( qEnvironmentVariableIsEmpty( "SICNU_MCP_WORKSPACE" ) )
        qputenv( "SICNU_MCP_WORKSPACE", "/" );
    return true;
}();

namespace
{

class ProtocolServer : public McpServer
{
public:
    QVariantMap lastResponseResult;
    int lastErrorCode = 0;
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
    void sendError( const QVariant &, int code, const QString &message,
                    const QVariantMap & ) override
    {
        lastErrorCode = code;
        lastErrorMessage = message;
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

    QVariantMap call( const QVariant &id, const QString &tool, const QVariantMap &arguments )
    {
        request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), id },
            { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "name" ), tool },
                           { QStringLiteral( "arguments" ), arguments } } } } );
        return lastResponseResult;
    }
};

ProtocolServer &server()
{
    static ProtocolServer *instance = [] {
        static int argc = 1;
        static char arg0[] = "test_r18_c_surface_pagination";
        static char *argv[] = { arg0 };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );
        ProtocolServer *s = new ProtocolServer();
        s->request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 0 },
            { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
            { QStringLiteral( "params" ), QVariantMap{} } } );
        return s;
    }();
    return *instance;
}

/// The tools/call result payload rides content[0].text as compact JSON.
QJsonObject payloadOf( const QVariantMap &callResult )
{
    return QJsonDocument::fromJson(
               callResult.value( QStringLiteral( "content" ) ).toList().value( 0 ).toMap()
                   .value( QStringLiteral( "text" ) ).toString().toUtf8() )
        .object();
}

/// Semantic-schema equality for the parity gate: in the MCP tool-parameters
/// context an absent `type` means `object` (normalizedInputSchema()
/// materializes both it and an empty `properties:{}` that raw registrations
/// may omit). Everything else must match exactly — real drift (different
/// properties, types, required lists) still fails.
QJsonObject semanticSchema( QJsonObject schema )
{
    if ( schema.value( QStringLiteral( "properties" ) ).toArray().isEmpty()
         && schema.value( QStringLiteral( "properties" ) ).toObject().isEmpty() )
        schema.remove( QStringLiteral( "properties" ) );
    if ( !schema.contains( QStringLiteral( "type" ) ) )
        schema[ QStringLiteral( "type" ) ] = QStringLiteral( "object" );
    return schema;
}

} // namespace

TEST_CASE( "tools/list rejects a non-numeric cursor instead of silently rewinding",
           "[agent][mcp][surface][r18c]" )
{
    ProtocolServer &s = server();

    SECTION( "garbage cursor is a typed -32602" )
    {
        s.request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 1 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/list" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "limit" ), 5 },
                           { QStringLiteral( "cursor" ), QStringLiteral( "abc" ) } } } } );
        CHECK( s.lastErrorCode == -32602 );
        CHECK( s.lastErrorMessage.contains( QStringLiteral( "cursor" ) ) );
    }

    SECTION( "trailing-garbage cursor is refused too" )
    {
        s.request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 2 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/list" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "cursor" ), QStringLiteral( "5x" ) } } } } );
        CHECK( s.lastErrorCode == -32602 );
    }

    SECTION( "a numeric cursor still pages" )
    {
        s.request( QVariantMap{
            { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
            { QStringLiteral( "id" ), 3 },
            { QStringLiteral( "method" ), QStringLiteral( "tools/list" ) },
            { QStringLiteral( "params" ),
              QVariantMap{ { QStringLiteral( "limit" ), 3 },
                           { QStringLiteral( "cursor" ), QStringLiteral( "3" ) } } } } );
        REQUIRE( s.lastErrorCode == 0 );
        CHECK( s.lastResponseResult.value( QStringLiteral( "cursor" ) ).toInt() == 3 );
        CHECK( s.lastResponseResult.value( QStringLiteral( "count" ) ).toInt() == 3 );
        CHECK( s.lastResponseResult.contains( QStringLiteral( "nextCursor" ) ) );
    }
}

TEST_CASE( "the list_tools meta tool refuses a non-numeric cursor too",
           "[agent][mcp][surface][r18c]" )
{
    ProtocolServer &s = server();
    // Same rule as the tools/list method handler: a garbage cursor must not
    // silently rewind to page 0 — it is a typed tool-error refusal.
    const QVariantMap result =
        s.call( 20, QStringLiteral( "list_tools" ),
                QVariantMap{ { QStringLiteral( "cursor" ), QStringLiteral( "abc" ) } } );
    REQUIRE( result.value( QStringLiteral( "isError" ) ).toBool() == true );
    const QString text = result.value( QStringLiteral( "content" ) )
                             .toList()
                             .value( 0 )
                             .toMap()
                             .value( QStringLiteral( "text" ) )
                             .toString();
    CHECK( text.contains( QStringLiteral( "cursor" ) ) );
}

TEST_CASE( "get_tool_schema and tools/list agree on every tool's schema",
           "[agent][mcp][surface][r18c]" )
{
    ProtocolServer &s = server();

    // Full listing with embedded schemas.
    s.request( QVariantMap{
        { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
        { QStringLiteral( "id" ), 10 },
        { QStringLiteral( "method" ), QStringLiteral( "tools/list" ) },
        { QStringLiteral( "params" ),
          QVariantMap{ { QStringLiteral( "includeSchemas" ), true } } } } );
    REQUIRE( s.lastErrorCode == 0 );
    const QVariantList tools =
        s.lastResponseResult.value( QStringLiteral( "tools" ) ).toList();
    REQUIRE( tools.size() > 100 );

    int checked = 0;
    for ( const QVariant &toolVar : tools )
    {
        const QVariantMap tool = toolVar.toMap();
        const QString name = tool.value( QStringLiteral( "name" ) ).toString();
        CAPTURE( name );
        REQUIRE( tool.contains( QStringLiteral( "inputSchema" ) ) );

        const QVariantMap schemaResult =
            s.call( 11, QStringLiteral( "get_tool_schema" ),
                    QVariantMap{ { QStringLiteral( "tool_id" ), name } } );
        REQUIRE( schemaResult.value( QStringLiteral( "isError" ) ).toBool() == false );
        const QJsonObject payload = payloadOf( schemaResult );
        REQUIRE( payload.contains( QStringLiteral( "schema" ) ) );

        const QJsonObject listSchema =
            QJsonDocument::fromJson( QJsonDocument::fromVariant(
                                         tool.value( QStringLiteral( "inputSchema" ) ).toMap() )
                                         .toJson() )
                .object();
        const QJsonObject detailSchema = payload.value( QStringLiteral( "schema" ) ).toObject();
        if ( semanticSchema( listSchema ) != semanticSchema( detailSchema ) )
        {
            FAIL( QString( "schema drift for %1:\n  list:   %2\n  detail: %3" )
                      .arg( name,
                            QString::fromUtf8( QJsonDocument( listSchema ).toJson(
                                QJsonDocument::Compact ) ),
                            QString::fromUtf8( QJsonDocument( detailSchema ).toJson(
                                QJsonDocument::Compact ) ) )
                      .toStdString() );
        }
        ++checked;
    }
    CHECK( checked == tools.size() );
}
