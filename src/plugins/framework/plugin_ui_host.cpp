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
        // Widgets created by plugins are owned here unless the shell sink
        // took them (attached records are the shell's to retire); delete
        // what is still ours.
        if ( !record.attached )
        {
            delete record.dockWidget;
            delete record.settingsPage;
        }
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

void PluginUiHost::attachCollectedUi( const QString &pluginId,
                                      exprs::UiContributionV1 *contribution )
{
    collectFromPlugin( pluginId, contribution );
    attachPluginUi( pluginId );
}

void PluginUiHost::attachPluginUi( const QString &pluginId )
{
    if ( !mShellSink )
    {
        // Honest no-op: without the shell the contributions can never
        // appear (nor be released) — surface it instead of silently eating
        // them (P3 review finding).
        showMessage( QStringLiteral( "warning" ),
                     QStringLiteral( "plugin UI contributions for %1 cannot attach: "
                                     "no shell UI sink installed" )
                         .arg( pluginId ) );
        return;
    }
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId != pluginId || record.attached )
            continue;
        if ( record.dockWidget )
            mShellSink->attachDock( record.pluginId, record.dockTitle, record.dockWidget );
        if ( !record.menuActions.isEmpty() )
            mShellSink->attachMenuActions( record.pluginId, record.menuActions );
        if ( record.settingsPage )
            mShellSink->attachSettingsPage( record.pluginId, record.settingsPageTitle,
                                            record.settingsPage );
        record.attached = true;
    }
    emit contributionChanged();
}

void PluginUiHost::releasePluginUi( const QString &pluginId )
{
    bool changed = false;
    for ( UiContributionRecord &record : mRecords )
    {
        if ( record.pluginId != pluginId )
            continue;
        // Attached objects go back through the shell sink, which detaches
        // and deletes them (menu, dock, preferences page). This runs before
        // the plugin binary is unmapped, so plugin-side destructors and
        // vtables are still valid.
        if ( record.attached && mShellSink )
            mShellSink->releaseUi( pluginId );
        else
        {
            // Never attached: the host still owns the widgets.
            delete record.dockWidget;
            delete record.settingsPage;
        }
        record.dockWidget = nullptr;
        record.settingsPage = nullptr;
        record.dockTitle.clear();
        record.settingsPageTitle.clear();
        record.menuActions.clear();
        record.attached = false;
        changed = true;
    }
    if ( changed )
        emit contributionChanged();
}

} // namespace sicnu::plugins
