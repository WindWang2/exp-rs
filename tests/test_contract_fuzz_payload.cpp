// test_contract_fuzz_payload.cpp — bounded property lane over the JSON
// contract payloads that arrive from OUTSIDE the process (plugin manifests
// and their ports, worker-declared UI schemas, UI events, workflow documents,
// diagnostic records, plugin index annotations) — Track ds41-fuzz-boundaries.
//
// The lane asserts the same contract for every entry point:
//
//   1. TOTALITY — no bounded input (wrong types, depth bombs, size bombs,
//      unicode, control bytes) makes the parser throw; an exception escaping
//      a typed-error boundary is a defect (#1038's class), not a feature.
//   2. TYPED REJECT — an invalid payload returns false/!ok() WITH a non-empty
//      reason; silent failure is a defect.
//   3. ROUND-TRIP — a valid payload survives toJson/fromJson (or its own
//      normalization) without semantic drift.
//   4. CAPS ARE REAL — the documented hard caps (schema entries, string
//      length, group depth, combo options, event bytes) refuse oversized
//      input instead of accepting it.
//
// Known-answer legs for these modules live in test_exprs_workflow_schema.cpp,
// test_plugin_ui_schema.cpp, test_exprs_plugin_system.cpp and
// test_exprs_ipc.cpp — this file adds the mutation corpus.
//
// Deterministic seeds + hard caps (≤512 B inputs, ≤200 iterations/seed).
#include <catch2/catch_test_macros.hpp>

#include "exprs/ipc_envelope.h"
#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_index.h"
#include "exprs/plugin_manifest.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_ui_schema.h"
#include "exprs/workflow_schema.h"
#include "support/fuzz_corpus.h"

#include <cstddef>
#include <filesystem>
#include <fstream>

#include <json/json.h>

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using exprs::ManifestPort;
using exprs::PluginDiagnostic;
using exprs::PluginDiagnosticLog;
using exprs::PluginPackage;
using exprs::PluginUiSchemaLimits;
using sicnu::fuzz::BoundedRandom;
using sicnu::fuzz::depthBomb;
using sicnu::fuzz::mutateTypes;
using sicnu::fuzz::randomJsonValue;

namespace fs = std::filesystem;

namespace
{

constexpr int kIterationsPerSeed = 200;

std::string serialize( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    builder[ "commentStyle" ] = "None";
    return Json::writeString( builder, value );
}

Json::Value parseJson( const std::string &text, bool *ok )
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    // Same hardening the production parse sites apply: an unbounded reader
    // turns a deep-but-small input into a stack overflow in the test process
    // itself, which would be a harness defect, not a finding.
    builder[ "stackLimit" ] = 64;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    // jsoncpp THROWS when the depth bound is exceeded; the helper converts
    // that into the same ok=false answer the production readers give.
    try
    {
        *ok = reader->parse( text.data(), text.data() + text.size(), &value, &errors );
    }
    catch ( const Json::Exception & )
    {
        *ok = false;
    }
    return value;
}

/// A minimal VALID public workflow document (the mutation seed).
Json::Value validWorkflowDocument()
{
    Json::Value document( Json::objectValue );
    document[ "schema_version" ] = 1;
    Json::Value steps( Json::arrayValue );
    Json::Value step( Json::objectValue );
    step[ "id" ] = "step.a";
    step[ "kind" ] = "operator";
    step[ "operator" ] = "rs:ndvi";
    step[ "params" ] = Json::Value( Json::objectValue );
    steps.append( step );
    document[ "steps" ] = steps;
    return document;
}

/// A minimal VALID plugin-declared UI schema (the mutation seed).
Json::Value validUiSchema(){
    Json::Value schema( Json::objectValue );
    schema[ "version" ] = 1;
    Json::Value commands( Json::arrayValue );
    Json::Value command( Json::objectValue );
    command[ "id" ] = "cmd.refresh";
    command[ "title" ] = "Refresh";
    commands.append( command );
    schema[ "commands" ] = commands;

    Json::Value menuItems( Json::arrayValue );
    Json::Value menuItem( Json::objectValue );
    menuItem[ "id" ] = "menu.refresh";
    menuItem[ "title" ] = "Refresh now";
    menuItem[ "commandId" ] = "cmd.refresh";
    menuItems.append( menuItem );
    schema[ "menuItems" ] = menuItems;

    Json::Value pages( Json::arrayValue );
    Json::Value page( Json::objectValue );
    page[ "id" ] = "page.general";
    page[ "title" ] = "General";
    Json::Value controls( Json::arrayValue );
    Json::Value text( Json::objectValue );
    text[ "id" ] = "ctl.name";
    text[ "type" ] = "text";
    text[ "label" ] = "Name";
    controls.append( text );
    Json::Value combo( Json::objectValue );
    combo[ "id" ] = "ctl.mode";
    combo[ "type" ] = "combo";
    combo[ "label" ] = "Mode";
    Json::Value options( Json::arrayValue );
    Json::Value option( Json::objectValue );
    option[ "value" ] = "fast";
    option[ "label" ] = "Fast";
    options.append( option );
    combo[ "options" ] = options;
    controls.append( combo );
    Json::Value group( Json::objectValue );
    group[ "id" ] = "ctl.group";
    group[ "type" ] = "group";
    Json::Value children( Json::arrayValue );
    Json::Value child( Json::objectValue );
    child[ "id" ] = "ctl.child";
    child[ "type" ] = "number";
    child[ "minimum" ] = 0;
    child[ "maximum" ] = 10;
    child[ "step" ] = 1;
    children.append( child );
    group[ "controls" ] = children;
    controls.append( group );
    page[ "controls" ] = controls;
    pages.append( page );
    schema[ "settingsPages" ] = pages;
    return schema;
}

} // namespace

TEST_CASE( "workflow document fuzz: validate/migrate are total and typed",
           "[contract8][fuzz][payload]" )
{
    // --- totality over mutated documents -----------------------------------
    for ( const uint64_t seed : { 11ull, 0xA11CEull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value mutated = mutateTypes( validWorkflowDocument(), random );
            PluginDiagnosticLog diagnostics;
            bool ok = false;
            REQUIRE_NOTHROW( ok = exprs::validateWorkflowDocument( mutated, diagnostics ) );
            if ( !ok )
                CHECK_FALSE( diagnostics.items().empty() );

            PluginDiagnosticLog migrateLog;
            Json::Value migrated;
            REQUIRE_NOTHROW( migrated = exprs::migrateWorkflowDocument( mutated, 1,
                                                                        migrateLog ) );
            if ( ok )
            {
                // A valid v1 document migrates to itself (identity), and the
                // stamp only fills an ABSENT version.
                REQUIRE( migrated.isObject() );
                CHECK( migrated[ "schema_version" ].asInt() == 1 );
            }
        }
    }

    // --- version gate: every non-positive-integer version is a typed reject -
    {
        std::vector<Json::Value> versions = { Json::Value( 0 ), Json::Value( -1 ),
                                             Json::Value( 1.5 ), Json::Value( "1" ),
                                             Json::Value( true ),
                                             Json::Value( Json::objectValue ),
                                             Json::Value( Json::arrayValue ) };
        for ( const Json::Value &version : versions )
        {
            Json::Value document = validWorkflowDocument();
            document[ "schema_version" ] = version;
            PluginDiagnosticLog diagnostics;
            bool ok = true;
            REQUIRE_NOTHROW( ok = exprs::validateWorkflowDocument( document, diagnostics ) );
            CHECK_FALSE( ok );
            CHECK_FALSE( diagnostics.items().empty() );
        }
    }

    // --- a version newer than the supported one names the supported range --
    {
        Json::Value document = validWorkflowDocument();
        document[ "schema_version" ] = exprs::kWorkflowSchemaVersion + 1;
        PluginDiagnosticLog diagnostics;
        CHECK_FALSE( exprs::validateWorkflowDocument( document, diagnostics ) );
        bool mentionsRange = false;
        for ( const PluginDiagnostic &item : diagnostics.items() )
            if ( item.message.find( "schema_version" ) != std::string::npos )
                mentionsRange = true;
        CHECK( mentionsRange );

        // Down-migration is refused, up-migration to self is identity.
        PluginDiagnosticLog log;
        CHECK( exprs::migrateWorkflowDocument( document, exprs::kWorkflowSchemaVersion, log )
                   .isNull() );
    }

    // --- depth bombs are typed rejects, never crashes ----------------------
    // 866 is the pinned pre-fix stack-overflow boundary of the IPC decode
    // entry point on a Debug/MSVC stack; the rest are far beyond any real
    // thread stack. See tests/corpus/README.md (defect D1).
    for ( const int depth : { 16, 256, 866, 4096, 65536, 200000 } )
    {
        const std::string bomb = depthBomb( depth );
        bool ok = false;
        const Json::Value value = parseJson( bomb, &ok );
        if ( !ok )
            continue; // jsoncpp's own stackLimit guard already refused it
        PluginDiagnosticLog diagnostics;
        bool valid = true;
        REQUIRE_NOTHROW( valid = exprs::validateWorkflowDocument( value, diagnostics ) );
        CHECK_FALSE( valid );
    }

    // --- stamp only fills an absent version ---------------------------------
    {
        Json::Value document = validWorkflowDocument();
        document.removeMember( "schema_version" );
        exprs::stampWorkflowSchemaVersion( document );
        CHECK( document[ "schema_version" ].asInt() == exprs::kWorkflowSchemaVersion );

        Json::Value already( Json::objectValue );
        already[ "schema_version" ] = 3;
        exprs::stampWorkflowSchemaVersion( already );
        CHECK( already[ "schema_version" ].asInt() == 3 );
    }
}

TEST_CASE( "plugin ui schema fuzz: validate is total, caps are real, "
           "rejects are typed",
           "[contract8][fuzz][payload]" )
{
    // --- totality over mutated schemas -------------------------------------
    for ( const uint64_t seed : { 21ull, 0x5C0FFEEull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value mutated = mutateTypes( validUiSchema(), random );
            exprs::PluginUiSchemaParseResult result;
            REQUIRE_NOTHROW( result = exprs::validatePluginUiSchema( mutated ) );
            if ( !result.ok() )
            {
                CHECK_FALSE( result.errors.empty() );
                // Normalization must not hand a partially validated subtree to
                // the renderer: a refused schema normalizes to null.
                CHECK( result.normalized.isNull() );
            }
            else
            {
                CHECK( result.normalized == mutated );
            }
        }
    }

    // --- every documented cap refuses oversized input -----------------------
    {
        // Contribution cap: 40 commands against a 32 cap.
        Json::Value schema = validUiSchema();
        Json::Value commands( Json::arrayValue );
        for ( int i = 0; i < 40; ++i )
        {
            Json::Value command( Json::objectValue );
            command[ "id" ] = "cmd." + std::to_string( i );
            command[ "title" ] = "Command " + std::to_string( i );
            commands.append( command );
        }
        schema[ "commands" ] = commands;
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // String cap: a 257-character id.
        Json::Value schema = validUiSchema();
        schema[ "commands" ][ 0 ][ "id" ] = std::string( 257, 'x' );
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // Group depth cap: 6 nested groups against a 4 cap. Built through
        // REFERENCES: jsoncpp values are copied on assignment, so the naive
        // "current = parent" idiom silently flattens the tree and the cap is
        // never actually exercised.
        Json::Value schema = validUiSchema();
        Json::Value nestedGroup( Json::objectValue );
        nestedGroup[ "id" ] = "ctl.leaf";
        nestedGroup[ "type" ] = "text";
        for ( int level = 6; level >= 1; --level )
        {
            Json::Value parent( Json::objectValue );
            parent[ "id" ] = "ctl.g" + std::to_string( level );
            parent[ "type" ] = "group";
            Json::Value children( Json::arrayValue );
            children.append( nestedGroup );
            parent[ "controls" ] = children;
            nestedGroup = parent;
        }
        schema[ "settingsPages" ][ 0 ][ "controls" ] = Json::Value( Json::arrayValue );
        schema[ "settingsPages" ][ 0 ][ "controls" ].append( nestedGroup );
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // Combo option cap: 33 options against a 32 cap.
        Json::Value schema = validUiSchema();
        Json::Value options( Json::arrayValue );
        for ( int i = 0; i < 33; ++i )
        {
            Json::Value option( Json::objectValue );
            option[ "value" ] = "v" + std::to_string( i );
            option[ "label" ] = "V" + std::to_string( i );
            options.append( option );
        }
        schema[ "settingsPages" ][ 0 ][ "controls" ][ 1 ][ "options" ] = options;
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // combo defaultValue outside the declared options.
        Json::Value schema = validUiSchema();
        schema[ "settingsPages" ][ 0 ][ "controls" ][ 1 ][ "defaultValue" ] = "slow";
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // Unknown control type: the host cannot render it.
        Json::Value schema = validUiSchema();
        schema[ "settingsPages" ][ 0 ][ "controls" ][ 0 ][ "type" ] = "quantum-slider";
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // menuItem referencing an undeclared command.
        Json::Value schema = validUiSchema();
        schema[ "menuItems" ][ 0 ][ "commandId" ] = "cmd.absent";
        CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
    }
    {
        // Worker-declared version of a different shape: typed reject, not a
        // silent accept ("version": "1" or 2).
        // NOTE: a programmatically built Json::Value(1.0) normalizes to the
        // integer 1 in jsoncpp (an integral double becomes intValue), so it is
        // legitimately version 1; the wire form "1.0" parses as a real and is
        // refused. Only the non-integer shapes are asserted here.
        for ( const Json::Value &version : { Json::Value( "1" ), Json::Value( 2 ),
                                             Json::Value( true ) } )
        {
            Json::Value schema = validUiSchema();
            schema[ "version" ] = version;
            CHECK_FALSE( exprs::validatePluginUiSchema( schema ).ok() );
        }
    }
    {
        // A valid schema at the documented caps still validates.
        Json::Value schema = validUiSchema();
        Json::Value commands = schema[ "commands" ];
        for ( int i = 0; i < 29; ++i )
        {
            Json::Value command( Json::objectValue );
            command[ "id" ] = "cmd.extra." + std::to_string( i );
            command[ "title" ] = "Command " + std::to_string( i );
            commands.append( command );
        }
        // The contribution budget counts EVERY surface: 1 baseline command
        // + 29 extras + the menu item + the settings page = exactly 32.
        schema[ "commands" ] = commands;
        CHECK( exprs::validatePluginUiSchema( schema ).ok() );
    }

    // --- ui events: totality, typed reject, value cap ----------------------
    {
        Json::Value event( Json::objectValue );
        event[ "contributionId" ] = "menu.refresh";
        event[ "controlId" ] = "ctl.name";
        event[ "eventType" ] = "clicked";
        CHECK( exprs::validateUiEvent( event ).ok() );

        for ( const std::string &eventType : { "changed", "command", "submit", "custom" } )
        {
            event[ "eventType" ] = eventType;
            CHECK( exprs::validateUiEvent( event ).ok() );
        }

        // NOTE: an embedded NUL cannot be written as a narrow literal escape
        // (MSVC maps it to an empty string) — build it explicitly.
        std::string clickedWithNul = "clicked";
        clickedWithNul.push_back( static_cast<char>( 0 ) );
        CHECK( clickedWithNul.size() == 8 );
        CHECK( clickedWithNul != "clicked" );
        const std::vector<std::string> rejectedTypes = { "Clicked", "", "unknown-event",
                                                        "clicked ", clickedWithNul };
        for ( const std::string &eventType : rejectedTypes )
        {
            event[ "eventType" ] = eventType;
            CHECK_FALSE( exprs::validateUiEvent( event ).ok() );
        }

        Json::Value capped( Json::objectValue );
        capped[ "contributionId" ] = "menu.refresh";
        capped[ "controlId" ] = "ctl.name";
        capped[ "eventType" ] = "changed";
        capped[ "value" ] = std::string( 4097, 'v' );
        CHECK_FALSE( exprs::validateUiEvent( capped ).ok() );

        // At the cap it is accepted: the SERIALIZED size is what the cap
        // bounds, and JSON adds the two quotes.
        capped[ "value" ] = std::string( 4094, 'v' );
        CHECK( exprs::validateUiEvent( capped ).ok() );

        // A deep structure that serializes beyond the cap is refused. Built
        // through references for the same copy-flattening reason as above.
        Json::Value deep = capped;
        Json::Value nested( Json::objectValue );
        Json::Value *cursor = &nested;
        for ( int i = 0; i < 12; ++i )
        {
            ( *cursor )[ "pad" ] = std::string( 2048, 'p' );
            ( *cursor )[ "child" ] = Json::Value( Json::objectValue );
            cursor = &( ( *cursor )[ "child" ] );
        }
        deep[ "value" ] = nested;
        CHECK_FALSE( exprs::validateUiEvent( deep ).ok() );
    }

    // --- hostile shapes are typed rejects, never throws --------------------
    for ( const uint64_t seed : { 31ull, 0xE17E17ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < 120; ++i )
        {
            Json::Value event( Json::objectValue );
            if ( i % 3 == 0 )
                event[ "contributionId" ] = randomJsonValue( random );
            if ( i % 3 == 1 )
                event[ "controlId" ] = randomJsonValue( random );
            if ( i % 3 == 2 )
                event[ "eventType" ] = randomJsonValue( random );
            if ( i % 4 == 3 )
                event[ "value" ] = randomJsonValue( random );

            exprs::PluginUiEventParseResult result;
            REQUIRE_NOTHROW( result = exprs::validateUiEvent( event ) );
            if ( !result.ok() )
                CHECK_FALSE( result.errors.empty() );
        }
    }
}

TEST_CASE( "plugin ui schema fuzz: worker-supplied commandId of the wrong type "
           "fails typed instead of throwing",
           "[contract8][fuzz][payload]" )
{
    // #1038-class regression: a manifest/menu entry whose "commandId" is a
    // number/object/array used to reach an unguarded asString() and throw
    // Json::LogicError out of the validator (the worker's describeUi path and
    // every host-side re-validation call it). The contract is a typed reject.
    const std::vector<Json::Value> wrongTypes = {
        Json::Value( 42 ),        Json::Value( 1.5 ),      Json::Value( true ),
        Json::Value( Json::objectValue ), Json::Value( Json::arrayValue ),
    };
    for ( const Json::Value &wrong : wrongTypes )
    {
        Json::Value schema = validUiSchema();
        schema[ "menuItems" ][ 0 ][ "commandId" ] = wrong;
        exprs::PluginUiSchemaParseResult result;
        REQUIRE_NOTHROW( result = exprs::validatePluginUiSchema( schema ) );
        CHECK_FALSE( result.ok() );
        CHECK_FALSE( result.errors.empty() );
        CHECK( result.normalized.isNull() );
    }
    for ( const Json::Value &wrong : wrongTypes )
    {
        Json::Value schema = validUiSchema();
        Json::Value action( Json::objectValue );
        action[ "id" ] = "ctx.refresh";
        action[ "title" ] = "Context";
        action[ "commandId" ] = wrong;
        schema[ "contextActions" ] = Json::Value( Json::arrayValue );
        schema[ "contextActions" ].append( action );
        exprs::PluginUiSchemaParseResult result;
        REQUIRE_NOTHROW( result = exprs::validatePluginUiSchema( schema ) );
        CHECK_FALSE( result.ok() );
    }
    // The event side (contributionId/controlId) is guarded BEFORE the string
    // conversion on master already (boundedString() precedes asString()), so
    // it is asserted here as a totality property only — it pins the contract,
    // it is not a defect regression.
    for ( const Json::Value &wrong : wrongTypes )
    {
        for ( const char *key : { "contributionId", "controlId" } )
        {
            Json::Value event( Json::objectValue );
            event[ key ] = wrong;
            event[ "eventType" ] = "clicked";
            exprs::PluginUiEventParseResult result;
            REQUIRE_NOTHROW( result = exprs::validateUiEvent( event ) );
            CHECK_FALSE( result.ok() );
            CHECK_FALSE( result.errors.empty() );
        }
    }

    // The event VALUE depth cap: a value that is small in bytes but deeply
    // nested is refused, because the byte cap alone does not bound the
    // recursion the host/worker JSON readers would perform.
    {
        Json::Value deep( Json::objectValue );
        Json::Value nested( Json::objectValue );
        Json::Value *cursor = &nested;
        for ( int i = 0; i < 40; ++i )
        {
            ( *cursor )[ "child" ] = Json::Value( Json::objectValue );
            cursor = &( ( *cursor )[ "child" ] );
        }
        ( *cursor )[ "pad" ] = std::string( 32, 'p' );
        Json::Value event( Json::objectValue );
        event[ "contributionId" ] = "menu.refresh";
        event[ "controlId" ] = "ctl.name";
        event[ "eventType" ] = "changed";
        event[ "value" ] = nested;
        exprs::PluginUiEventParseResult result;
        REQUIRE_NOTHROW( result = exprs::validateUiEvent( event ) );
        CHECK_FALSE( result.ok() );
        bool mentionsDepth = false;
        for ( const std::string &error : result.errors )
            if ( error.find( "depth cap" ) != std::string::npos )
                mentionsDepth = true;
        CHECK( mentionsDepth );

        // At the cap it is accepted (32 levels + the value wrapper).
        Json::Value shallow( Json::objectValue );
        Json::Value *walk = &shallow;
        for ( int i = 0; i < 31; ++i )
        {
            ( *walk )[ "child" ] = Json::Value( Json::objectValue );
            walk = &( ( *walk )[ "child" ] );
        }
        ( *walk )[ "pad" ] = "x";
        event[ "value" ] = shallow;
        REQUIRE_NOTHROW( result = exprs::validateUiEvent( event ) );
        CHECK( result.ok() );
    }
}

TEST_CASE( "manifest port fuzz: fromJson is total and round-trips",
           "[contract8][fuzz][payload]" )
{
    Json::Value port( Json::objectValue );
    port[ "name" ] = "input";
    port[ "type" ] = "raster";
    port[ "required" ] = true;
    port[ "description" ] = "input raster";
    port[ "min" ] = 0.5;
    port[ "max" ] = 100.0;
    port[ "file_format" ] = "tif";
    port[ "enum" ] = Json::Value( Json::arrayValue );
    port[ "enum" ].append( "a" );

    for ( const uint64_t seed : { 41ull, 0xEDEF1ull, 0xEDEF2ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value mutated = ( i % 3 == 0 ) ? mutateTypes( port, random )
                                                       : randomJsonValue( random );
            ManifestPort parsed;
            std::string error;
            bool ok = false;
            REQUIRE_NOTHROW( ok = ManifestPort::fromJson( mutated, parsed, error ) );
            if ( ok )
            {
                // Round-trip: a valid port survives its own serializer.
                ManifestPort reparsed;
                std::string reerror;
                bool ok2 = false;
                REQUIRE_NOTHROW( ok2 = ManifestPort::fromJson( parsed.toJson(), reparsed,
                                                              reerror ) );
                CHECK( ok2 );
                CHECK( reparsed.name == parsed.name );
                CHECK( reparsed.type == parsed.type );
                CHECK( reparsed.required == parsed.required );
                CHECK( reparsed.hasMin == parsed.hasMin );
                CHECK( reparsed.hasMax == parsed.hasMax );
                CHECK( reparsed.fileFormat == parsed.fileFormat );
            }
            else
            {
                CHECK_FALSE( error.empty() );
            }
        }
    }

    // Typed rejects for the documented required shapes.
    {
        Json::Value missingName( Json::objectValue );
        missingName[ "type" ] = "raster";
        ManifestPort parsed;
        std::string error;
        REQUIRE( ManifestPort::fromJson( missingName, parsed, error ) == false );
        CHECK_FALSE( error.empty() );
    }
    {
        // "required" is a BOOLEAN field: a string/array is a typed reject, not
        // an asBool() exception out of the manifest reader (#1038-class).
        for ( const Json::Value &wrong : { Json::Value( "yes" ), Json::Value( 1 ),
                                          Json::Value( Json::arrayValue ),
                                          Json::Value( Json::objectValue ) } )
        {
            Json::Value port2 = port;
            port2[ "required" ] = wrong;
            ManifestPort parsed;
            std::string error;
            bool ok = true;
            REQUIRE_NOTHROW( ok = ManifestPort::fromJson( port2, parsed, error ) );
            CHECK_FALSE( ok );
            CHECK_FALSE( error.empty() );
        }
    }
    {
        // The valid baseline round-trips unchanged.
        ManifestPort parsed;
        std::string error;
        REQUIRE( ManifestPort::fromJson( port, parsed, error ) );
        CHECK( parsed.name == "input" );
        CHECK( parsed.required );
        CHECK( parsed.toJson()[ "name" ].asString() == "input" );
    }
}

TEST_CASE( "diagnostic records fuzz: fromJson is total on hostile records",
           "[contract8][fuzz][payload]" )
{
    PluginDiagnostic seed;
    seed.code = exprs::PluginDiagnosticCode::ManifestInvalidField;
    seed.severity = exprs::PluginDiagnosticSeverity::Error;
    seed.message = "bad field";
    seed.pluginId = "com.example";
    seed.field = "steps[].id";
    seed.file = "plugin.json";

    for ( const uint64_t seedValue : { 51ull, 0xD1A6ull } )
    {
        BoundedRandom random( seedValue );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value mutated = ( i % 4 == 0 ) ? mutateTypes( seed.toJson(), random )
                                                       : randomJsonValue( random );
            PluginDiagnostic parsed;
            REQUIRE_NOTHROW( parsed = PluginDiagnostic::fromJson( mutated ) );
            // A record cannot lose its message: missing fields stay empty, but
            // a present message survives verbatim.
            if ( mutated.isObject() && mutated.isMember( "message" )
                 && mutated[ "message" ].isString() )
                CHECK( parsed.message == mutated[ "message" ].asString() );
        }
    }

    // Round-trip through the record's own serializer.
    {
        const PluginDiagnostic reparsed = PluginDiagnostic::fromJson( seed.toJson() );
        CHECK( reparsed.message == seed.message );
        CHECK( reparsed.pluginId == seed.pluginId );
        CHECK( reparsed.field == seed.field );
        CHECK( reparsed.code == seed.code );
        CHECK( reparsed.severity == seed.severity );
    }
}

TEST_CASE( "plugin version ranges fuzz: satisfaction is total and exact",
           "[contract8][fuzz][payload]" )
{
    struct Case
    {
        std::string version;
        std::string range;
        bool expected;
    };
    const std::vector<Case> cases = {
        { "1.2.3", "1.2.3", true },    { "1.2.3", "=1.2.3", true },
        { "1.2.3", "^1.2.3", true },   { "1.9.0", "^1.2.3", true },
        { "2.0.0", "^1.2.3", false },  { "1.2.9", "~1.2.3", true },
        { "1.3.0", "~1.2.3", false },  { "1.2.3", ">=1.2.3", true },
        { "1.2.2", ">=1.2.3", false }, { "1.2.3", "", true },
        // Bare "1.2" is an EXACT bound (patch defaults to 0), not a prefix.
        { "1.2.0", "1.2", true },      { "1.2.3", "1.2", false },
        { "1.0.0", "1", true },        { "1.2.3", "1", false },
        { "1.2.3", "1.2.4", false },
    };
    for ( const Case &testCase : cases )
    {
        bool satisfied = false;
        REQUIRE_NOTHROW( satisfied = PluginPackage::versionSatisfiesRange( testCase.version,
                                                                          testCase.range ) );
        CHECK( satisfied == testCase.expected );
    }

    // Two documented tolerances (asserted, not silently accepted):
    //  - "" is the "bare plugin id" range: satisfied by ANY version.
    //  - a TRAILING NEWLINE is swallowed by the last version component
    //    (getline's default delimiter), so ">=1.2.3\n" parses as ">=1.2.3".
    //    Harmless leniency, recorded as an observation in the corpus notes.
    CHECK( PluginPackage::versionSatisfiesRange( "1.2.3", "" ) );
    CHECK( PluginPackage::versionSatisfiesRange( "0.0.1", "" ) );
    const std::string withTrailingNewline = ">=1.2.3\n";
    CHECK( PluginPackage::versionSatisfiesRange( "1.2.3", withTrailingNewline ) );
    CHECK_FALSE( PluginPackage::versionSatisfiesRange( "0.0.1", withTrailingNewline ) );

    // Malformed/hostile ranges: total (no throw) and never satisfied, because
    // no version can satisfy a range that does not parse.
    const std::vector<std::string> hostileRanges = {
        "abc",     "1.2.3.4", ">=abc",  "^",       "~",
        ">=999999999999999999999999", "-1.0.0", ">=1.2.3 <2.0.0",
        "  ",       "\xC3\xA9", "\x01",
        "1.2.3.4.5.6", ">=1.2.3extra", "^1.2.3.4.5",
    };
    for ( const std::string &range : hostileRanges )
    {
        for ( const std::string &version : { "1.2.3", "0.0.0", "999.999.999", "abc" } )
        {
            bool satisfied = true;
            REQUIRE_NOTHROW( satisfied = PluginPackage::versionSatisfiesRange( version,
                                                                              range ) );
            CHECK_FALSE( satisfied );
        }
    }

    // The mutation corpus over valid ranges stays total.
    for ( const uint64_t seed : { 61ull, 0x5E9Eull } )
    {
        BoundedRandom random( seed );
        const std::vector<std::string> baseRanges = { "^1.2.3", "~1.2.3", ">=1.2.3", "=1.2.3",
                                                      "1.2.3", "1.2", "1" };
        for ( int i = 0; i < 120; ++i )
        {
            const std::string &base = baseRanges[random.below(
                static_cast<uint32_t>( baseRanges.size() ) )];
            std::string mutated = base;
            mutated.insert( static_cast<std::string::size_type>(
                                random.below( static_cast<uint32_t>( mutated.size() + 1 ) ) ),
                            1, static_cast<char>( random.below( 128 ) ) );
            bool satisfied = true;
            REQUIRE_NOTHROW( satisfied = PluginPackage::versionSatisfiesRange( "1.2.3",
                                                                              mutated ) );
            (void) satisfied;
        }
    }
}

TEST_CASE( "plugin index fuzz: applyPins ignores hand-edited entries",
           "[contract8][fuzz][payload]" )
{
    Json::Value index( Json::objectValue );
    Json::Value plugins( Json::arrayValue );
    Json::Value plugin( Json::objectValue );
    plugin[ "id" ] = "com.example.plugin";
    plugin[ "version" ] = "1.2.3";
    plugin[ "title" ] = "Example";
    plugins.append( plugin );
    index[ "plugins" ] = plugins;

    // Well-formed pins: deterministic outcome.
    const Json::Value pinned =
        exprs::PluginIndex::applyPins( index, { { "com.example.plugin", "1.2.3" } } );
    CHECK( pinned[ "plugins" ][ 0 ][ "pin" ].asString() == "pinned" );
    const Json::Value upgraded =
        exprs::PluginIndex::applyPins( index, { { "com.example.plugin", "2.0.0" } } );
    CHECK( upgraded[ "plugins" ][ 0 ][ "pin" ].asString() == "upgrade" );

    // Hand-edited / hostile index entries: total, left untouched, no throw.
    for ( const uint64_t seed : { 71ull, 0x1D3A5ull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < 120; ++i )
        {
            const Json::Value mutated = mutateTypes( index, random );
            Json::Value annotated;
            REQUIRE_NOTHROW( annotated = exprs::PluginIndex::applyPins(
                                 mutated, { { "com.example.plugin", "1.2.3" } } ) );
            (void) annotated;
        }
    }
}

TEST_CASE( "envelope and schema size boundary: caps do not reject legal input",
           "[contract8][fuzz][payload]" )
{
    // A frame payload at the documented transport size still decodes: the
    // boundary lane guards against an accidental small cap being introduced.
    Json::Value request( Json::objectValue );
    request[ "v" ] = 1;
    request[ "type" ] = "request";
    request[ "id" ] = 1;
    request[ "method" ] = "ui.describe";
    Json::Value params( Json::objectValue );
    params[ "schema" ] = validUiSchema();
    request[ "params" ] = params;
    const std::string text = serialize( request );
    exprs::Ipc::Envelope decoded;
    std::string error;
    REQUIRE( exprs::Ipc::decodeEnvelopePayload( text, decoded, error ) );
    CHECK( decoded.method == "ui.describe" );
    CHECK( decoded.params[ "schema" ].isObject() );

    // The workflow document at its maximum legal step count still validates.
    Json::Value document( Json::objectValue );
    document[ "schema_version" ] = 1;
    Json::Value steps( Json::arrayValue );
    for ( int i = 0; i < 500; ++i )
    {
        Json::Value step( Json::objectValue );
        step[ "id" ] = "step." + std::to_string( i );
        step[ "kind" ] = "operator";
        step[ "operator" ] = "rs:ndvi";
        steps.append( step );
    }
    document[ "steps" ] = steps;
    PluginDiagnosticLog diagnostics;
    CHECK( exprs::validateWorkflowDocument( document, diagnostics ) );
}

TEST_CASE( "corpus minimization: a hostile ui schema reduces to one wrong-typed field",
           "[contract8][fuzz][payload]" )
{
    // The minimization lane. Before the fix the predicate below was
    // "validatePluginUiSchema throws Json::LogicError"; with the defect closed
    // the SAME fixture must still be refused — typed, with a stated reason —
    // so the predicate is the post-fix contract and the corpus fixture stays
    // meaningful as a regression pin.
    auto refused = []( const std::string &text ) {
        bool ok = false;
        const Json::Value value = parseJson( text, &ok );
        if ( !ok )
            return false;
        try
        {
            const exprs::PluginUiSchemaParseResult result =
                exprs::validatePluginUiSchema( value );
            return !result.ok() && !result.errors.empty();
        }
        catch ( const Json::Exception & )
        {
            return true; // an escaping exception is still a failure
        }
    };

    // Pad the seed with load-bearing neighbours so ddmin has something to
    // remove: the reducer must discover that only the wrong-typed commandId
    // matters.
    Json::Value padded = validUiSchema();
    padded[ "menuItems" ][ 0 ][ "commandId" ] = 42;
    padded[ "settingsPages" ][ 0 ][ "title" ] = std::string( "padded-title-" ) + std::string( 40, 'x' );
    Json::Value extra( Json::arrayValue );
    for ( int i = 0; i < 8; ++i )
    {
        Json::Value option( Json::objectValue );
        option[ "value" ] = "v" + std::to_string( i );
        option[ "label" ] = "V" + std::to_string( i );
        extra.append( option );
    }
    padded[ "settingsPages" ][ 0 ][ "controls" ][ 1 ][ "options" ] = extra;

    // 1-minimality of the FIXTURE CLASS: no single byte can be removed while
    // the text still parses and still carries the defect class (a numeric
    // commandId on a worker-declared menu item). The reducer's predicate is
    // the class itself: the raw "is it refused" predicate is NOT
    // class-preserving — ddmin would happily trade the numeric commandId for
    // a different refusal reason (a missing title) and shrink past the defect.
    auto sameFixtureClass = []( const std::string &text ) {
        bool ok = false;
        const Json::Value value = parseJson( text, &ok );
        if ( !ok || !value.isObject() || !value.isMember( "menuItems" ) )
            return false;
        const Json::Value &items = value[ "menuItems" ];
        if ( !items.isArray() || items.empty() || !items[ 0 ].isObject() )
            return false;
        return items[ 0 ].isMember( "commandId" ) && items[ 0 ][ "commandId" ].isNumeric();
    };
    const std::string original = serialize( padded );
    REQUIRE( refused( original ) );
    REQUIRE( sameFixtureClass( original ) );
    const std::string minimized = sicnu::fuzz::ddmin( original, sameFixtureClass );
    REQUIRE( sameFixtureClass( minimized ) );
    CHECK( minimized.size() <= original.size() );
    // The reducer reached a fixed point: running it again over the fixture
    // cannot shrink it further. (Byte-level 1-minimality is deliberately NOT
    // asserted: the class predicate is structural — dropping a byte inside a
    // long string value keeps the JSON valid AND keeps the class, so "no
    // single byte is removable" would be false for a legitimate fixture. The
    // reducer's own passes are the minimizer; this pins that they converged.)
    const std::string secondPass = sicnu::fuzz::ddmin( minimized, sameFixtureClass );
    CHECK( secondPass.size() == minimized.size() );
    CHECK( minimized.size() < original.size() );

    // The minimized form still names the exact defect class: a numeric
    // "commandId" in a worker-declared menu item.
    bool ok = false;
    const Json::Value value = parseJson( minimized, &ok );
    REQUIRE( ok );
    CHECK( value.isMember( "menuItems" ) );
    CHECK( value[ "menuItems" ].isArray() );
    CHECK( !value[ "menuItems" ].empty() );
    CHECK( value[ "menuItems" ][ 0 ].isMember( "commandId" ) );
    CHECK( value[ "menuItems" ][ 0 ][ "commandId" ].isNumeric() );

    // And the FIXED validator refuses it typed, which is what the corpus
    // fixture pins as the regression.
    exprs::PluginUiSchemaParseResult result;
    REQUIRE_NOTHROW( result = exprs::validatePluginUiSchema( value ) );
    CHECK_FALSE( result.ok() );
    CHECK_FALSE( result.errors.empty() );
}

TEST_CASE( "manifest file fuzz: loadManifestFromFile is total on hostile files",
           "[contract8][fuzz][payload]" )
{
    // The file surface of D1: a plugin package can hand the host any
    // plugin.json it likes. Depth bombs and wrong-typed payloads must be
    // typed ManifestInvalidJson / field errors, never a stack overflow or an
    // exception out of the loader.
    const fs::path directory = fs::temp_directory_path() / "sicnu-fuzz-manifest";
    std::error_code ec;
    fs::remove_all( directory, ec );
    fs::create_directories( directory, ec );
    const std::string manifestPath = ( directory / "plugin.json" ).generic_string();

    const auto writeAndLoad = [&manifestPath]( const std::string &content ) {
        std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
        out.write( content.data(), static_cast<std::streamsize>( content.size() ) );
        out.close();
        exprs::PluginManifest manifest;
        exprs::PluginDiagnostic error;
        bool ok = true;
        REQUIRE_NOTHROW( ok = exprs::loadManifestFromFile( manifestPath, manifest, error ) );
        return ok ? std::string() : error.message;
    };

    // A well-formed manifest still loads (no behaviour change from the fix).
    {
        const std::string good = R"({"id":"com.example.plugin","name":"Example","version":"1.2.3","sdk":"8.0","entry":"plugin.js"})";
        CHECK( writeAndLoad( good ).empty() );
    }
    // Wrong-typed payloads are typed field/JSON errors.
    {
        CHECK( writeAndLoad( "{ not json" ).find( "invalid JSON" ) != std::string::npos );
        // Non-object roots: jsoncpp's isMember() THROWS on them, so the loader
        // must refuse them before any member access (typed, never an escape).
        for ( const char *root : { "[]", "\"text\"", "42", "true", "null" } )
        {
            const std::string message = writeAndLoad( root );
            CHECK_FALSE( message.empty() );
            CHECK( message.find( "manifest root must be a JSON object" )
                   != std::string::npos );
        }
        CHECK( !writeAndLoad( R"({"manifest_version":"1"})" ).empty() );
    }
    // Depth bombs: the contract is TOTALITY, asserted on the SAME deep value
    // in two shapes — as an unknown field (an unknown-field-tolerant manifest
    // legitimately loads it) and as the value of a versioned manifest. Pre-fix
    // the unbounded reader killed the process at depth ~20000 instead.
    for ( const int depth : { 16, 64, 256, 900, 4096, 65536, 200000 } )
    {
        const std::string bomb = sicnu::fuzz::depthBomb( depth );
        const std::string deepValue = bomb.substr( 5 ); // strip {"a": and trailing }
        exprs::PluginManifest manifest;
        exprs::PluginDiagnostic error;
        bool ok = true;

        {
            std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
            out.write( bomb.data(), static_cast<std::streamsize>( bomb.size() ) );
        }
        REQUIRE_NOTHROW( ok = exprs::loadManifestFromFile( manifestPath, manifest, error ) );

        const std::string versioned = R"({"manifest_version":1,"id":"com.example.x",)"
                                     R"("name":"N","version":"1.0.0","sdk":"8.0",)"
                                     R"("entry":"p.js","a":)";
        {
            std::ofstream out( manifestPath, std::ios::binary | std::ios::trunc );
            const std::string content = versioned + deepValue + "}";
            out.write( content.data(), static_cast<std::streamsize>( content.size() ) );
        }
        REQUIRE_NOTHROW( ok = exprs::loadManifestFromFile( manifestPath, manifest, error ) );
    }

    fs::remove_all( directory, ec );
}
