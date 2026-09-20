// tests/test_plugin_ui_schema_host.cpp — declarative UI schema renderer
// (plugin-platform 8.0): schema -> host-owned widgets, events through a
// fake delegate, state applied back, release detaches and deletes.
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/plugin_ui_schema_host.h"

#include <QApplication>
#include <QPointer>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <atomic>
#include <chrono>
#include <json/json.h>
#include <thread>

using namespace sicnu::plugins;

namespace {

/// Qt app fixture (offscreen platform via ctest environment).
struct AppFixture
{
    AppFixture()
    {
        static int argc = 1;
        static char argument[] = "test_plugin_ui_schema_host";
        static char *argv[] = { argument };
        if ( !QApplication::instance() )
            new QApplication( argc, argv );
    }
};

Json::Value event( const char *contributionId, const char *controlId, const char *type,
                   const Json::Value &value = Json::Value() )
{
    Json::Value event( Json::objectValue );
    event["contributionId"] = contributionId;
    event["controlId"] = controlId;
    event["eventType"] = type;
    if ( !value.isNull() )
        event["value"] = value;
    return event;
}

/// Fake plugin: records delivered events, answers clicks with a state patch.
/// It answers with the PRODUCTION envelope shape ({ok, response:{...}}), the
/// same object PluginHostProcessRuntime::invokeUi returns — the renderer must
/// unwrap one level (#1040; a bare {state} fake masked the bug).
class FakeDelegate : public UiInvokeDelegate
{
public:
    std::atomic<int> delivered{ 0 };
    Json::Value lastEvent;

    Json::Value invoke( const Json::Value &event ) override
    {
        lastEvent = event;
        delivered.fetch_add( 1 );
        Json::Value response( Json::objectValue );
        response["ok"] = true;
        if ( event.get( "eventType", "" ).asString() == "clicked" )
        {
            Json::Value providerResponse( Json::objectValue );
            Json::Value state( Json::objectValue );
            state["name"] = "clicked-response";
            providerResponse["state"] = state;
            response["response"] = providerResponse;
        }
        return response;
    }
};

/// Hostile plugin: every answer carries a wrong-typed state value. Pre-fix
/// the combo applyValue called asString() on it from the queued GUI lambda
/// (uncaught Json::LogicError -> terminate, issue #1039).
class HostileStateDelegate : public UiInvokeDelegate
{
public:
    std::atomic<int> delivered{ 0 };

    Json::Value invoke( const Json::Value &event ) override
    {
        (void)event;
        delivered.fetch_add( 1 );
        Json::Value state( Json::objectValue );
        state["mode"] = Json::Value( Json::arrayValue );
        state["name"] = Json::Value( Json::objectValue );
        Json::Value providerResponse( Json::objectValue );
        providerResponse["state"] = state;
        Json::Value response( Json::objectValue );
        response["ok"] = true;
        response["response"] = providerResponse;
        return response;
    }
};

/// Minimal valid schema: one settings page (text + checkbox + button) and
/// one dock (label + button) plus a command/menu pair.
Json::Value buildSchema()
{
    Json::Value schema( Json::objectValue );
    schema["version"] = 1;

    Json::Value commands( Json::arrayValue );
    Json::Value command( Json::objectValue );
    command["id"] = "fixture.cmd";
    command["title"] = "Command";
    commands.append( command );
    schema["commands"] = commands;

    Json::Value menuItems( Json::arrayValue );
    Json::Value item( Json::objectValue );
    item["id"] = "menu.item";
    item["title"] = "Menu";
    item["commandId"] = "fixture.cmd";
    menuItems.append( item );
    schema["menuItems"] = menuItems;

    Json::Value pages( Json::arrayValue );
    Json::Value page( Json::objectValue );
    page["id"] = "page.main";
    page["title"] = "Main";
    Json::Value controls( Json::arrayValue );
    Json::Value text( Json::objectValue );
    text["id"] = "name";
    text["type"] = "text";
    text["label"] = "Name";
    text["defaultValue"] = "hello";
    controls.append( text );
    Json::Value check( Json::objectValue );
    check["id"] = "enabled";
    check["type"] = "checkbox";
    check["label"] = "Enabled";
    check["defaultValue"] = false;
    controls.append( check );
    Json::Value button( Json::objectValue );
    button["id"] = "apply";
    button["type"] = "button";
    button["label"] = "Apply";
    controls.append( button );
    page["controls"] = controls;
    pages.append( page );
    schema["settingsPages"] = pages;
    return schema;
}

/// Shell sink that owns what the renderer attaches (mirrors the app shell).
class TestShellSink : public UiShellSink
{
public:
    QList<QAction *> actions;
    QWidget *settingsPage = nullptr;
    QWidget *dock = nullptr;

    void attachDock( const QString &, const QString &, QWidget *widget ) override
    {
        dock = widget;
    }
    void attachMenuActions( const QString &, const QList<QAction *> &list ) override
    {
        actions.append( list );
    }
    void attachSettingsPage( const QString &, const QString &, QWidget *page ) override
    {
        settingsPage = page;
    }
    void releaseUi( const QString & ) override
    {
        delete settingsPage;
        settingsPage = nullptr;
        delete dock;
        dock = nullptr;
        qDeleteAll( actions );
        actions.clear();
    }
};

} // namespace

TEST_CASE( "declarative schema renders into host-owned widgets", "[uischemahost]" )
{
    AppFixture fixture;
    auto &renderer = *PluginUiSchemaRenderer::instance();
    TestShellSink sink;
    renderer.setShellSink( &sink );

    auto delegate = std::make_unique<FakeDelegate>();
    FakeDelegate *delegatePtr = delegate.get();
    QString error;
    REQUIRE( renderer.attachPluginSchema( "org.test.ui", buildSchema(), std::move( delegate ),
                                          error ) );
    REQUIRE( renderer.hasPluginUi( "org.test.ui" ) );
    REQUIRE( sink.settingsPage != nullptr );
    REQUIRE( sink.dock == nullptr ); // schema declared no dock
    REQUIRE( sink.actions.size() == 1 );

    // Host-owned controls carry the schema defaults.
    auto *page = sink.settingsPage;
    auto *lineEdit = page->findChild<QLineEdit *>();
    REQUIRE( lineEdit != nullptr );
    REQUIRE( lineEdit->text() == "hello" );
    auto *checkbox = page->findChild<QCheckBox *>();
    REQUIRE( checkbox != nullptr );
    REQUIRE_FALSE( checkbox->isChecked() );

    // User interaction becomes a bounded event for the plugin.
    lineEdit->setText( "changed-by-user" );
    emit lineEdit->editingFinished();
    auto *button = page->findChild<QPushButton *>();
    REQUIRE( button != nullptr );
    button->click();

    // The delivery thread is asynchronous: pump the event loop until the
    // plugin answered (bounded wait keeps the test deterministic).
    for ( int i = 0; i < 100 && delegatePtr->delivered.load() < 2; ++i )
    {
        QApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( delegatePtr->delivered.load() >= 2 );
    REQUIRE( delegatePtr->lastEvent["controlId"].asString() == "apply" );
    REQUIRE( delegatePtr->lastEvent["eventType"].asString() == "clicked" );

    // The plugin's state patch lands back on the HOST-owned widget.
    for ( int i = 0; i < 100 && lineEdit->text() != "clicked-response"; ++i )
    {
        QApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( lineEdit->text() == "clicked-response" );

    // Release detaches and deletes; no widget survives its plugin.
    renderer.releasePluginUi( "org.test.ui" );
    REQUIRE_FALSE( renderer.hasPluginUi( "org.test.ui" ) );
    REQUIRE( sink.settingsPage == nullptr );
    REQUIRE( sink.actions.isEmpty() );
    renderer.setShellSink( nullptr );
}

TEST_CASE( "renderer refuses a schema with no attachable surface", "[uischemahost]" )
{
    AppFixture fixture;
    auto &renderer = *PluginUiSchemaRenderer::instance();
    TestShellSink sink;
    renderer.setShellSink( &sink );

    Json::Value schema( Json::objectValue );
    schema["version"] = 1; // nothing attachable
    QString error;
    REQUIRE_FALSE( renderer.attachPluginSchema( "org.test.empty", schema,
                                                std::make_unique<FakeDelegate>(), error ) );
    renderer.setShellSink( nullptr );
}

TEST_CASE( "renderer refuses unvalidated hostile schemas host-side (issue #1039)",
           "[uischemahost]" )
{
    AppFixture fixture;
    auto &renderer = *PluginUiSchemaRenderer::instance();
    TestShellSink sink;
    renderer.setShellSink( &sink );

    SECTION( "wrong-typed control fields never reach buildControls" )
    {
        // Pre-fix: control.get("multiline", false).asBool() on an object and
        // .asDouble() on a string threw Json::LogicError through the GUI
        // thread. The host-side re-validation must refuse the whole schema.
        Json::Value schema( Json::objectValue );
        schema["version"] = 1;
        Json::Value page( Json::objectValue );
        page["id"] = "page.hostile";
        page["title"] = "Hostile";
        Json::Value controls( Json::arrayValue );
        Json::Value text( Json::objectValue );
        text["id"] = "name";
        text["type"] = "text";
        text["multiline"] = Json::Value( Json::objectValue );
        controls.append( text );
        Json::Value number( Json::objectValue );
        number["id"] = "level";
        number["type"] = "number";
        number["minimum"] = "oops";
        controls.append( number );
        page["controls"] = controls;
        Json::Value pages( Json::arrayValue );
        pages.append( page );
        schema["settingsPages"] = pages;

        QString error;
        REQUIRE_FALSE( renderer.attachPluginSchema( "org.test.hostile", schema,
                                                    std::make_unique<FakeDelegate>(), error ) );
        REQUIRE_FALSE( error.isEmpty() );
        REQUIRE_FALSE( renderer.hasPluginUi( "org.test.hostile" ) );
        REQUIRE( sink.settingsPage == nullptr );
    }

    SECTION( "unbounded group nesting is refused, not recursed" )
    {
        // Pre-fix: buildControls recursed once per group level with no bound
        // (~30 B/level inside the frame cap -> stack exhaustion).
        Json::Value control( Json::objectValue );
        control["id"] = "leaf";
        control["type"] = "label";
        control["label"] = "Leaf";
        for ( int depth = 0; depth < 64; ++depth )
        {
            Json::Value group( Json::objectValue );
            group["id"] = "g" + std::to_string( depth );
            group["type"] = "group";
            Json::Value children( Json::arrayValue );
            children.append( control );
            group["controls"] = children;
            control = group;
        }
        Json::Value page( Json::objectValue );
        page["id"] = "page.deep";
        page["title"] = "Deep";
        Json::Value controls( Json::arrayValue );
        controls.append( control );
        page["controls"] = controls;
        Json::Value pages( Json::arrayValue );
        pages.append( page );
        Json::Value schema( Json::objectValue );
        schema["version"] = 1;
        schema["settingsPages"] = pages;

        QString error;
        REQUIRE_FALSE( renderer.attachPluginSchema( "org.test.deep", schema,
                                                    std::make_unique<FakeDelegate>(), error ) );
        REQUIRE_FALSE( error.isEmpty() );
        REQUIRE_FALSE( renderer.hasPluginUi( "org.test.deep" ) );
    }

    renderer.setShellSink( nullptr );
}

TEST_CASE( "wrong-typed state values are ignored, never thrown (issue #1039)",
           "[uischemahost]" )
{
    AppFixture fixture;
    auto &renderer = *PluginUiSchemaRenderer::instance();
    TestShellSink sink;
    renderer.setShellSink( &sink );

    // Valid schema plus a combo, so the hostile state has real targets.
    Json::Value schema = buildSchema();
    Json::Value combo( Json::objectValue );
    combo["id"] = "mode";
    combo["type"] = "combo";
    combo["label"] = "Mode";
    Json::Value option( Json::objectValue );
    option["value"] = "a";
    option["label"] = "A";
    Json::Value options( Json::arrayValue );
    options.append( option );
    combo["options"] = options;
    schema["settingsPages"][0]["controls"].append( combo );

    auto delegate = std::make_unique<HostileStateDelegate>();
    HostileStateDelegate *delegatePtr = delegate.get();
    QString error;
    REQUIRE( renderer.attachPluginSchema( "org.test.hostile-state", schema, std::move( delegate ),
                                          error ) );

    auto *page = sink.settingsPage;
    REQUIRE( page != nullptr );
    auto *button = page->findChild<QPushButton *>();
    REQUIRE( button != nullptr );
    button->click();
    for ( int i = 0; i < 100 && delegatePtr->delivered.load() < 1; ++i )
    {
        QApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
    REQUIRE( delegatePtr->delivered.load() >= 1 );
    // Pre-fix the queued applyState lambda called asString() on the array and
    // terminated the process here; the guards must simply ignore the value.
    for ( int i = 0; i < 10; ++i )
    {
        QApplication::processEvents();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    auto *comboWidget = page->findChild<QComboBox *>();
    REQUIRE( comboWidget != nullptr );
    REQUIRE( comboWidget->currentIndex() == 0 );

    renderer.releasePluginUi( "org.test.hostile-state" );
    renderer.setShellSink( nullptr );
}

TEST_CASE( "repeated attach/re-attach/release is idempotent (dock & menu ownership)",
           "[uischemahost][p12]" )
{
    AppFixture fixture;
    auto &renderer = *PluginUiSchemaRenderer::instance();
    TestShellSink sink;
    renderer.setShellSink( &sink );

    // Re-attach replaces the previous rendering without leaking the old
    // widgets or stacking menu actions (hot-reload path, WP5 contract).
    QString error;
    REQUIRE( renderer.attachPluginSchema( "org.test.idempotent", buildSchema(),
                                          std::make_unique<FakeDelegate>(), error ) );
    // QPointer (not a raw pointer): it nulls itself when the widget dies, so
    // "the old page was released" is an OBSERVABLE fact instead of a pointer
    // comparison that a heap-reused address would make lie.
    QPointer<QWidget> firstPage( sink.settingsPage );
    REQUIRE( !firstPage.isNull() );
    REQUIRE( sink.actions.size() == 1 );

    REQUIRE( renderer.attachPluginSchema( "org.test.idempotent", buildSchema(),
                                          std::make_unique<FakeDelegate>(), error ) );
    REQUIRE( firstPage.isNull() );                    // old page released
    REQUIRE( sink.settingsPage != nullptr );          // new page attached
    REQUIRE( sink.actions.size() == 1 );              // not duplicated
    REQUIRE( renderer.hasPluginUi( "org.test.idempotent" ) );

    // Repeated release is a no-op, not a double delete.
    renderer.releasePluginUi( "org.test.idempotent" );
    renderer.releasePluginUi( "org.test.idempotent" );
    renderer.releasePluginUi( "org.test.never-attached" );
    REQUIRE_FALSE( renderer.hasPluginUi( "org.test.idempotent" ) );
    REQUIRE( sink.settingsPage == nullptr );
    REQUIRE( sink.actions.isEmpty() );

    // A fresh schema for the SAME id after release attaches normally
    // (reload after unload).
    REQUIRE( renderer.attachPluginSchema( "org.test.idempotent", buildSchema(),
                                          std::make_unique<FakeDelegate>(), error ) );
    REQUIRE( renderer.hasPluginUi( "org.test.idempotent" ) );
    REQUIRE( sink.actions.size() == 1 );
    renderer.releasePluginUi( "org.test.idempotent" );

    // While a record exists, a second plugin's UI stays independent.
    REQUIRE( renderer.attachPluginSchema( "org.test.a", buildSchema(),
                                          std::make_unique<FakeDelegate>(), error ) );
    REQUIRE( renderer.attachPluginSchema( "org.test.b", buildSchema(),
                                          std::make_unique<FakeDelegate>(), error ) );
    renderer.releasePluginUi( "org.test.a" );
    REQUIRE( renderer.hasPluginUi( "org.test.b" ) );
    REQUIRE_FALSE( renderer.hasPluginUi( "org.test.a" ) );
    renderer.releasePluginUi( "org.test.b" );

    renderer.setShellSink( nullptr );
}
