// tests/test_plugin_ui_schema_host.cpp — declarative UI schema renderer
// (plugin-platform 8.0): schema -> host-owned widgets, events through a
// fake delegate, state applied back, release detaches and deletes.
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/plugin_ui_schema_host.h"

#include <QApplication>
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
        if ( event.get( "eventType", "" ).asString() == "clicked" )
        {
            Json::Value state( Json::objectValue );
            state["name"] = "clicked-response";
            response["state"] = state;
        }
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
