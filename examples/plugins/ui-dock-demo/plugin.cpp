// examples/plugins/ui-dock-demo/plugin.cpp
//
// UI plugin: contributes one dock widget + a settings page through the
// versioned UiContributionV1 surface. The plugin never touches the main
// window; the host pulls and places the widgets.
//
// NOTE: UI plugins are build-locked to the host (Qt6::Widgets + matching Qt
// minor version). This example additionally links Qt6::Widgets itself; it
// still needs no exp-rs internal headers.
#include "exprs/plugin_interface.h"
#include "exprs/plugin_ui.h"

#include <QAction>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace {

class DemoDockUi : public exprs::UiContributionV1
{
public:
    QWidget *createDockWidget( QWidget *parent ) override
    {
        auto *widget = new QWidget( parent );
        widget->setObjectName( "demoUiDockWidget" );
        auto *layout = new QVBoxLayout( widget );
        auto *label = new QLabel( QStringLiteral( "Demo UI plugin dock" ), widget );
        layout->addWidget( label );
        auto *log = new QTextEdit( widget );
        log->setReadOnly( true );
        log->setPlaceholderText( QStringLiteral( "Plugin log output" ) );
        layout->addWidget( log );
        widget->setWindowTitle( QStringLiteral( "Demo Plugin" ) );
        return widget;
    }

    QWidget *createSettingsPage( QWidget *parent ) override
    {
        auto *page = new QWidget( parent );
        auto *layout = new QVBoxLayout( page );
        layout->addWidget( new QLabel( QStringLiteral( "Demo plugin settings (none yet)" ), page ) );
        page->setWindowTitle( QStringLiteral( "Demo Plugin" ) );
        return page;
    }
};

class UiDockDemoPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.example.ui-dock-demo"; }

    bool initialize( exprs::HostServicesV1 &services ) override
    {
        services.log( "info", "ui-dock-demo initialized" );
        return true;
    }

    void registerContributions( exprs::ContributionContextV1 & ) override
    {
        // UI contributions come through EXPRS_createUiContributionV1 below;
        // nothing extra to register here.
    }

    void shutdown() override {}
};

} // namespace

EXPRS_EXPORT_PLUGIN( UiDockDemoPlugin )
EXPRS_EXPORT_UI_CONTRIBUTION( DemoDockUi )
