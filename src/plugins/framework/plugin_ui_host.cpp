/***************************************************************************
 * src/plugins/framework/plugin_ui_host.cpp
 ***************************************************************************/
#include "plugin_ui_host.h"

#include <algorithm>

namespace sicnu::plugins {

PluginUiHost *PluginUiHost::instance()
{
    static PluginUiHost host;
    return &host;
}

PluginUiHost::~PluginUiHost()
{
    for ( UiContributionRecord &record : mRecords )
    {
        // Widgets created by plugins are owned here unless the shell took
        // them (via takeDockWidget); delete what is still ours.
        delete record.dockWidget;
        delete record.settingsPage;
    }
}

UiContributionRecord &PluginUiHost::recordFor( const QString &pluginId )
{
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId == pluginId )
            return record;
    }
    UiContributionRecord record;
    record.pluginId = pluginId;
    mRecords.push_back( std::move( record ) );
    return mRecords.back();
}

void PluginUiHost::addDockWidget( const QString &pluginId, const QString &title, QWidget *widget )
{
    if ( !widget || title.isEmpty() )
        return;
    UiContributionRecord &record = recordFor( pluginId );
    record.dockTitle = title;
    record.dockWidget = widget;
    emit contributionChanged();
}

void PluginUiHost::addMenuActions( const QString &pluginId, const QList<QAction *> &actions )
{
    if ( actions.isEmpty() )
        return;
    UiContributionRecord &record = recordFor( pluginId );
    record.menuActions.append( actions );
    emit contributionChanged();
}

void PluginUiHost::registerSettingsPage( const QString &pluginId, const QString &title,
                                         QWidget *page )
{
    if ( !page || title.isEmpty() )
        return;
    UiContributionRecord &record = recordFor( pluginId );
    record.settingsPageTitle = title;
    record.settingsPage = page;
    emit contributionChanged();
}

void PluginUiHost::showMessage( const QString &level, const QString &message )
{
    if ( mMessageSink )
        mMessageSink( level, message );
}

void PluginUiHost::collectFromPlugin( const QString &pluginId,
                                      exprs::UiContributionV1 *contribution )
{
    if ( !contribution )
        return;
    releasePluginUi( pluginId );
    QWidget *dock = contribution->createDockWidget( nullptr );
    if ( dock )
        addDockWidget( pluginId, dock->windowTitle().isEmpty() ? pluginId : dock->windowTitle(),
                       dock );
    addMenuActions( pluginId, contribution->createMenuActions( nullptr ) );
    QWidget *page = contribution->createSettingsPage( nullptr );
    if ( page )
        registerSettingsPage( pluginId, page->windowTitle().isEmpty() ? pluginId
                                                                      : page->windowTitle(),
                              page );
}

QWidget *PluginUiHost::takeDockWidget( const QString &pluginId )
{
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId == pluginId )
        {
            QWidget *widget = record.dockWidget;
            record.dockWidget = nullptr;
            return widget;
        }
    }
    return nullptr;
}

QWidget *PluginUiHost::takeSettingsPage( const QString &pluginId )
{
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId == pluginId )
        {
            QWidget *widget = record.settingsPage;
            record.settingsPage = nullptr;
            return widget;
        }
    }
    return nullptr;
}

QList<QAction *> PluginUiHost::takeMenuActions( const QString &pluginId )
{
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId == pluginId )
        {
            const QList<QAction *> actions = record.menuActions;
            record.menuActions.clear();
            return actions;
        }
    }
    return {};
}

void PluginUiHost::releasePluginUi( const QString &pluginId )
{
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId != pluginId )
            continue;
        delete record.dockWidget;
        delete record.settingsPage;
        record.dockWidget = nullptr;
        record.settingsPage = nullptr;
        record.dockTitle.clear();
        record.settingsPageTitle.clear();
        record.menuActions.clear();
    }
    emit contributionChanged();
}

} // namespace sicnu::plugins
