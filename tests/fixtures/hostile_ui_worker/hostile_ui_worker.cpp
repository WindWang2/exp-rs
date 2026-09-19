// tests/fixtures/hostile_ui_worker/hostile_ui_worker.cpp
//
// Adversarial host-process worker for the #1039/#1040 regression suites.
//
// It speaks just enough of the worker protocol to be spawned and loaded
// (worker.hello handshake, plugin.load registration report, plugin.shutdown),
// then answers ui.describe / ui.invoke with payloads a REAL worker would only
// produce if its own validation had been bypassed or compromised. The host
// must turn every payload into a typed refusal / a bounded no-op — never a
// crash, a stack overflow or an OOM on the GUI thread.
//
// Hostile ui.describe variants are selected by the EXPRS_HOSTILE_UI_KIND
// environment variable (inherited at spawn time):
//   deep   — a 256-level nested-group chain (validator depth cap)
//   tree   — a 3-level, 9-ary group tree (compounding total-control cap)
//   flood  — 4096 flat controls (per-page cap + widget-flood)
//   types  — wrong-typed optional fields (multiline/minimum/options/...)
// Anything else defaults to "deep".

#include "exprs/host_protocol.h"
#include "exprs/ipc_channel.h"
#include "exprs/ipc_stream.h"
#include "exprs/version.h"

#include "plugins/host/plugin_host_protocol.h"

#include <json/json.h>

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

using namespace exprs;
using namespace sicnu::plugins::hostprotocol;

namespace
{

std::string hostileKind()
{
    const char *value = std::getenv( "EXPRS_HOSTILE_UI_KIND" );
    return value && *value ? std::string( value ) : std::string( "deep" );
}

/// One control object with only valid required fields; callers decorate it.
Json::Value basicControl( const std::string &id, const std::string &type )
{
    Json::Value control( Json::objectValue );
    control["id"] = id;
    control["type"] = type;
    control["label"] = id;
    return control;
}

Json::Value hostileSchema( const std::string &kind )
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;

    Json::Value page( Json::objectValue );
    page["id"] = "page.hostile";
    page["title"] = "Hostile";
    Json::Value controls( Json::arrayValue );

    if ( kind == "types" )
    {
        Json::Value text = basicControl( "hostile.text", "text" );
        text["multiline"] = Json::Value( Json::objectValue ); // must be bool
        controls.append( text );

        Json::Value number = basicControl( "hostile.number", "number" );
        number["minimum"] = Json::Value( Json::arrayValue ); // must be number
        number["maximum"] = Json::Value( Json::objectValue );
        number["step"] = Json::Value( Json::objectValue );
        controls.append( number );

        Json::Value combo = basicControl( "hostile.combo", "combo" );
        Json::Value options( Json::arrayValue );
        Json::Value option( Json::objectValue );
        option["value"] = Json::Value( Json::arrayValue ); // must be string
        option["label"] = Json::Value( Json::objectValue );
        options.append( option );
        combo["options"] = options;
        controls.append( combo );

        Json::Value check = basicControl( "hostile.check", "checkbox" );
        check["defaultValue"] = Json::Value( Json::arrayValue );
        controls.append( check );
    }
    else if ( kind == "flood" )
    {
        for ( int index = 0; index < 4096; ++index )
        {
            Json::Value control =
                basicControl( "hostile.flood." + std::to_string( index ), "text" );
            control["defaultValue"] = "x";
            controls.append( control );
        }
    }
    else if ( kind == "tree" )
    {
        // 3 group levels with 9 children each: 1 + 9 + 81 + 729 = 820
        // controls. Every individual group honours the per-group cap, but the
        // COMPOUNDED total exceeds the schema-wide budget.
        Json::Value level2( Json::arrayValue );
        for ( int a = 0; a < 9; ++a )
        {
            Json::Value level3( Json::arrayValue );
            for ( int b = 0; b < 9; ++b )
            {
                Json::Value leaf =
                    basicControl( "hostile.tree." + std::to_string( a ) + "."
                                      + std::to_string( b ),
                                  "text" );
                leaf["defaultValue"] = "x";
                level3.append( leaf );
            }
            Json::Value group( basicControl( "hostile.g." + std::to_string( a ), "group" ) );
            group["controls"] = level3;
            level2.append( group );
        }
        Json::Value level1( Json::arrayValue );
        for ( int a = 0; a < 9; ++a )
        {
            Json::Value group( basicControl( "hostile.top." + std::to_string( a ), "group" ) );
            group["controls"] = level2;
            level1.append( group );
        }
        controls = level1;
    }
    else // "deep"
    {
        Json::Value control = basicControl( "hostile.deep.0", "text" );
        for ( int level = 1; level < 256; ++level )
        {
            Json::Value group( Json::objectValue );
            group["id"] = "hostile.deep." + std::to_string( level );
            group["type"] = "group";
            group["label"] = "deep";
            Json::Value children( Json::arrayValue );
            children.append( control );
            group["controls"] = children;
            control = group;
        }
        controls.append( control );
    }

    page["controls"] = controls;
    Json::Value pages( Json::arrayValue );
    pages.append( page );
    schema["settingsPages"] = pages;
    return schema;
}

std::string parseHandleSwitch( int argc, char **argv, const char *name )
{
    const size_t length = std::strlen( name );
    for ( int index = 1; index < argc; ++index )
    {
        const std::string argument( argv[index] );
        if ( argument.rfind( name, 0 ) == 0 )
            return argument.substr( length );
    }
    return {};
}

void *handleFromString( const std::string &value )
{
    if ( value.empty() )
        return nullptr;
#ifdef _WIN32
    long long parsed = 0;
    try
    {
        parsed = std::stoll( value, nullptr, 16 );
    }
    catch ( ... )
    {
        return nullptr;
    }
    return reinterpret_cast<void *>( static_cast<intptr_t>( parsed ) );
#else
    return reinterpret_cast<void *>( static_cast<intptr_t>( std::atoi( value.c_str() ) ) );
#endif
}

} // namespace

int main( int argc, char **argv )
{
    const std::string readValue = parseHandleSwitch( argc, argv, kIpcReadSwitch );
    const std::string writeValue = parseHandleSwitch( argc, argv, kIpcWriteSwitch );
    if ( readValue.empty() || writeValue.empty() )
        return kExitUsage;
    void *readHandle = handleFromString( readValue );
    void *writeHandle = handleFromString( writeValue );
    if ( !ipcHandleStreamHandlesValid( readHandle, writeHandle ) )
        return kExitUsage;

    IpcChannel channel( makeIpcHandleStream( readHandle, writeHandle ) );

    {
        Json::Value hello( Json::objectValue );
        hello["protocolMajor"] = hostProtocolVersionMajor();
        hello["protocolMinor"] = hostProtocolVersionMinor();
        hello["apiVersion"] = EXP_RS_PLUGIN_API_VERSION;
        hello["abiVersion"] = pluginAbiVersion();
        hello["manifestVersion"] = supportedManifestVersion();
        hello["maxConcurrentRequests"] = 1;
        channel.sendEvent( kWorkerHello, hello );
    }

    Ipc::Envelope request;
    for ( ;; )
    {
        if ( !channel.nextRequest( request, 60000 ) )
        {
            if ( channel.isOpen() )
                continue;
            break;
        }
        if ( request.method == kLoadPlugin )
        {
            Json::Value registered( Json::objectValue );
            // EXPRS_HOSTILE_REGISTRATION=malformed makes the report violate
            // its own contract (non-string ids): the host's load path must
            // refuse it typed instead of throwing on asString() (#1038).
            const char *registrationKind = std::getenv( "EXPRS_HOSTILE_REGISTRATION" );
            const bool malformed =
                registrationKind && std::string( registrationKind ) == "malformed";
            registered["operators"] = Json::Value( Json::arrayValue );
            if ( malformed )
                registered["operators"].append( Json::Value( Json::objectValue ) );
            registered["dataProviders"] = Json::Value( Json::arrayValue );
            if ( malformed )
                registered["dataProviders"] = Json::Value( Json::objectValue );
            registered["modelRuntimes"] = Json::Value( Json::arrayValue );
            registered["agentTools"] = Json::Value( Json::arrayValue );
            Json::Value result( Json::objectValue );
            result["registered"] = registered;
            std::string error;
            channel.sendResponse( request.id, result, error );
        }
        else if ( request.method == kDescribeUi )
        {
            // Deliberately NOT validating: this is the compromised-worker
            // shape the host-side trust boundary must catch.
            Json::Value result( Json::objectValue );
            result["schema"] = hostileSchema( hostileKind() );
            std::string error;
            channel.sendResponse( request.id, result, error );
        }
        else if ( request.method == kInvokeUi )
        {
            Json::Value state( Json::objectValue );
            state["status"] = "hostile-pong";
            Json::Value response( Json::objectValue );
            response["state"] = state;
            Json::Value result( Json::objectValue );
            result["response"] = response;
            std::string error;
            channel.sendResponse( request.id, result, error );
        }
        else if ( request.method == kShutdownPlugin )
        {
            Json::Value result( Json::objectValue );
            result["shutdown"] = true;
            std::string error;
            channel.sendResponse( request.id, result, error );
            channel.close();
            return kExitOk;
        }
        else
        {
            channel.sendError( request.id, { "E6008", "hostile fixture: method not implemented" } );
        }
    }
    channel.close();
    return kExitOk;
}
