// tests/test_plugin_ui_schema.cpp — declarative out-of-process UI schema
// contract (plugin-platform 8.0). The validator is the fail-closed gate in
// front of the host renderer: everything it rejects never renders.
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_ui_schema.h"

#include <json/json.h>

using namespace exprs;

namespace {

Json::Value textControl( const char *id = "name" )
{
    Json::Value control( Json::objectValue );
    control["id"] = id;
    control["type"] = "text";
    control["label"] = "Name";
    control["defaultValue"] = "world";
    return control;
}

Json::Value minimalPage( const char *id = "page.main" )
{
    Json::Value page( Json::objectValue );
    page["id"] = id;
    page["title"] = "Page";
    Json::Value controls( Json::arrayValue );
    controls.append( textControl() );
    page["controls"] = controls;
    return page;
}

Json::Value schemaWithPage( const Json::Value &page )
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;
    Json::Value pages( Json::arrayValue );
    pages.append( page );
    schema["settingsPages"] = pages;
    return schema;
}

} // namespace

TEST_CASE( "a well-formed schema validates and echoes", "[plugin][uischema]" )
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;

    Json::Value commands( Json::arrayValue );
    Json::Value command( Json::objectValue );
    command["id"] = "fixture.refresh";
    command["title"] = "Refresh";
    commands.append( command );
    schema["commands"] = commands;

    Json::Value menuItems( Json::arrayValue );
    Json::Value menuItem( Json::objectValue );
    menuItem["id"] = "menu.refresh";
    menuItem["title"] = "Refresh now";
    menuItem["commandId"] = "fixture.refresh";
    menuItems.append( menuItem );
    schema["menuItems"] = menuItems;

    Json::Value docks( Json::arrayValue );
    docks.append( minimalPage( "dock.status" ) );
    schema["dockPanels"] = docks;

    Json::Value help( Json::arrayValue );
    Json::Value topic( Json::objectValue );
    topic["id"] = "help.topic";
    topic["title"] = "Topic";
    help.append( topic );
    schema["helpTopics"] = help;

    const auto result = validatePluginUiSchema( schema );
    REQUIRE( result.ok() );
    REQUIRE( result.normalized["commands"][0]["id"].asString() == "fixture.refresh" );
}

TEST_CASE( "every control type of the v1 surface validates", "[plugin][uischema]" )
{
    Json::Value page( Json::objectValue );
    page["id"] = "page.all";
    page["title"] = "All";
    Json::Value controls( Json::arrayValue );
    for ( const char *type : { "label", "text", "number", "checkbox", "combo", "slider",
                               "button" } )
    {
        Json::Value control( Json::objectValue );
        control["id"] = std::string( "c-" ) + type;
        control["type"] = type;
        control["label"] = type;
        if ( std::string( type ) == "combo" )
        {
            Json::Value options( Json::arrayValue );
            Json::Value option( Json::objectValue );
            option["value"] = "a";
            option["label"] = "A";
            options.append( option );
            control["options"] = options;
        }
        controls.append( control );
    }
    page["controls"] = controls;
    REQUIRE( validatePluginUiSchema( schemaWithPage( page ) ).ok() );
}

TEST_CASE( "structurally broken schemas fail closed", "[plugin][uischema]" )
{
    CHECK_FALSE( validatePluginUiSchema( Json::Value( Json::arrayValue ) ).ok() );

    Json::Value wrongVersion( Json::objectValue );
    wrongVersion["version"] = 2;
    CHECK_FALSE( validatePluginUiSchema( wrongVersion ).ok() );

    // Unknown control type: the host cannot render it.
    Json::Value exotic( Json::objectValue );
    exotic["id"] = "c.bad";
    exotic["type"] = "holo";
    exotic["label"] = "x";
    Json::Value page = minimalPage();
    page["controls"] = Json::Value( Json::arrayValue );
    page["controls"].append( exotic );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( page ) ).ok() );

    // Duplicate ids on one page.
    Json::Value duplicated = minimalPage();
    duplicated["controls"][0]["id"] = "dup";
    Json::Value second = textControl( "dup" );
    duplicated["controls"].append( second );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( duplicated ) ).ok() );

    // Numbers with inverted ranges and bad defaults.
    Json::Value number = textControl( "n" );
    number["type"] = "number";
    number["minimum"] = 10;
    number["maximum"] = 0;
    Json::Value numberPage = minimalPage();
    numberPage["controls"] = Json::Value( Json::arrayValue );
    numberPage["controls"].append( number );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( numberPage ) ).ok() );
}

TEST_CASE( "command references must resolve to declared commands", "[plugin][uischema]" )
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;
    Json::Value menuItems( Json::arrayValue );
    Json::Value item( Json::objectValue );
    item["id"] = "m";
    item["title"] = "M";
    item["commandId"] = "missing.command";
    menuItems.append( item );
    schema["menuItems"] = menuItems;
    CHECK_FALSE( validatePluginUiSchema( schema ).ok() );
}

TEST_CASE( "hard caps bound the schema surface", "[plugin][uischema]" )
{
    // Controls per page.
    Json::Value page( Json::objectValue );
    page["id"] = "page.flood";
    page["title"] = "Flood";
    Json::Value controls( Json::arrayValue );
    for ( int i = 0; i < 65; ++i )
    {
        Json::Value control = textControl( ( "c" + std::to_string( i ) ).c_str() );
        controls.append( control );
    }
    page["controls"] = controls;
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( page ) ).ok() );

    // Combo option cap.
    Json::Value combo = textControl( "combo" );
    combo["type"] = "combo";
    Json::Value options( Json::arrayValue );
    for ( int i = 0; i < 33; ++i )
    {
        Json::Value option( Json::objectValue );
        option["value"] = std::to_string( i );
        option["label"] = std::to_string( i );
        options.append( option );
    }
    combo["options"] = options;
    Json::Value comboPage = minimalPage();
    comboPage["controls"] = Json::Value( Json::arrayValue );
    comboPage["controls"].append( combo );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( comboPage ) ).ok() );

    // Group depth cap.
    Json::Value nested = textControl( "leaf" );
    for ( int depth = 0; depth < 6; ++depth )
    {
        Json::Value group( Json::objectValue );
        group["id"] = ( "g" + std::to_string( depth ) ).c_str();
        group["type"] = "group";
        group["label"] = "g";
        Json::Value children( Json::arrayValue );
        children.append( nested );
        group["controls"] = children;
        nested = group;
    }
    Json::Value deepPage = minimalPage();
    deepPage["controls"] = Json::Value( Json::arrayValue );
    deepPage["controls"].append( nested );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( deepPage ) ).ok() );

    // Unbounded strings.
    Json::Value longLabel = textControl();
    longLabel["label"] = std::string( 300, 'x' );
    Json::Value labelPage = minimalPage();
    labelPage["controls"] = Json::Value( Json::arrayValue );
    labelPage["controls"].append( longLabel );
    CHECK_FALSE( validatePluginUiSchema( schemaWithPage( labelPage ) ).ok() );
}

TEST_CASE( "group nesting validates within the depth budget", "[plugin][uischema]" )
{
    Json::Value leaf = textControl( "leaf" );
    for ( int depth = 0; depth < 3; ++depth )
    {
        Json::Value group( Json::objectValue );
        group["id"] = ( "g" + std::to_string( depth ) ).c_str();
        group["type"] = "group";
        group["label"] = "g";
        Json::Value children( Json::arrayValue );
        children.append( leaf );
        group["controls"] = children;
        leaf = group;
    }
    Json::Value page = minimalPage();
    page["controls"] = Json::Value( Json::arrayValue );
    page["controls"].append( leaf );
    REQUIRE( validatePluginUiSchema( schemaWithPage( page ) ).ok() );
}

// -- plugin-platform 9.0: accessibility metadata + host-side event validation --

TEST_CASE( "accessibility metadata is validated and preserved", "[plugin][uischema][p12]" )
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;
    Json::Value pages( Json::arrayValue );
    Json::Value page( Json::objectValue );
    page["id"] = "page.a11y";
    page["title"] = "Accessibility";
    Json::Value controls( Json::arrayValue );
    Json::Value button( Json::objectValue );
    button["id"] = "apply";
    button["type"] = "button";
    button["label"] = "Apply";
    button["description"] = "Applies the current settings";
    button["accessibilityLabel"] = "Apply settings button";
    controls.append( button );
    page["controls"] = controls;
    pages.append( page );
    schema["settingsPages"] = pages;

    const auto result = validatePluginUiSchema( schema );
    REQUIRE( result.ok() );
    REQUIRE( result.normalized["settingsPages"][0]["controls"][0]["accessibilityLabel"].asString()
             == "Apply settings button" );

    // Oversized metadata is refused (same cap as every other string).
    // jsoncpp values are value types: mutate through the TREE, not the
    // local copy, or the mutation is invisible to the validated schema.
    schema["settingsPages"][0]["controls"][0]["description"] = std::string( 512, 'x' );
    const auto refused = validatePluginUiSchema( schema );
    REQUIRE_FALSE( refused.ok() );
}

TEST_CASE( "ui events are validated host-side before the worker round-trip",
           "[plugin][uischema][p12]" )
{
    PluginUiSchemaLimits limits;

    // A well-formed event passes.
    Json::Value event( Json::objectValue );
    event["contributionId"] = "page.main";
    event["controlId"] = "apply";
    event["eventType"] = "clicked";
    event["value"] = true;
    auto result = validateUiEvent( event, limits );
    REQUIRE( result.ok() );

    // Non-object: refused.
    REQUIRE_FALSE( validateUiEvent( Json::Value( Json::arrayValue ), limits ).ok() );

    // Oversized identifier: refused.
    event["contributionId"] = std::string( 512, 'a' );
    REQUIRE_FALSE( validateUiEvent( event, limits ).ok() );
    event["contributionId"] = "page.main";

    // Unknown event type: refused (the host cannot render/dispatch it).
    event["eventType"] = "teleport";
    auto unknown = validateUiEvent( event, limits );
    REQUIRE_FALSE( unknown.ok() );
    REQUIRE( unknown.errors.front().find( "teleport" ) != std::string::npos );

    // Oversized serialized value: refused.
    event["eventType"] = "custom";
    event["value"] = std::string( 8192, 'v' );
    REQUIRE_FALSE( validateUiEvent( event, limits ).ok() );

    // Value within the cap: passes.
    event["value"] = std::string( 1024, 'v' );
    REQUIRE( validateUiEvent( event, limits ).ok() );

    // Identifier character whitelist applies to events too.
    event["controlId"] = "bad id with spaces";
    REQUIRE_FALSE( validateUiEvent( event, limits ).ok() );
}
