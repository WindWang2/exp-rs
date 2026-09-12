/***************************************************************************
 * src/app/plugin_shell_ui.cpp
 ***************************************************************************/
#include "plugin_shell_ui.h"

#include "main_window.h"

#include "dialogs/preferences_dialog.h"
#include "qgsdockwidget.h"

#include <QMenu>
#include <QTabWidget>

namespace {

/// Removes @p page from any tab widget hosting it (an open Preferences
/// dialog), returning true when a live tab was found. Safe on a page that
/// was already consumed and destroyed by a closed dialog (QPointer null).
bool detachPageFromTabs( QWidget *page )
{
    if ( !page )
        return false;
    QWidget *stacked = page->parentWidget();
    if ( !stacked )
        return false;
    auto *tabs = qobject_cast<QTabWidget *>( stacked->parentWidget() );
    if ( !tabs )
        return false;
    const int index = tabs->indexOf( page );
    if ( index < 0 )
        return false;
    tabs->removeTab( index );
    return true;
}

} // namespace

ExprsPluginShellUi::ExprsPluginShellUi( QgisDesktopWindow *window, QMenu *pluginMenu )
    : QObject( window )
    , mWindow( window )
    , mPluginMenu( pluginMenu )
{
}

void ExprsPluginShellUi::attachDock( const QString &pluginId, const QString &title,
                                     QWidget *widget )
{
    if ( !mWindow || !widget || mDocks.contains( pluginId ) )
        return;
    auto *dock = new QgsDockWidget( title.isEmpty() ? pluginId : title, mWindow );
    dock->setObjectName( QStringLiteral( "exprs_plugin_%1" )
                             .arg( pluginId.toLower().replace( " ", "_" ) ) );
    dock->setWidget( widget );
    mWindow->addDockWidget( Qt::RightDockWidgetArea, dock );
    mWindow->windowMenu()->addAction( dock->toggleViewAction() );
    mDocks[pluginId] = dock;
}

void ExprsPluginShellUi::attachMenuActions( const QString &pluginId,
                                            const QList<QAction *> &actions )
{
    if ( !mPluginMenu )
        return;
    mPluginMenu->addActions( actions );
    mMenuActions[pluginId] = std::vector<QAction *>( actions.begin(), actions.end() );
}

void ExprsPluginShellUi::attachSettingsPage( const QString &pluginId, const QString &title,
                                             QWidget *page )
{
    if ( !page )
        return;
    PreferencesDialog::registerExternalPage( title.isEmpty() ? pluginId : title, page );
    mSettingsPages[pluginId] = page;
    mSettingsPageTitles[pluginId] = title.isEmpty() ? pluginId : title;
}

QList<QAction *> ExprsPluginShellUi::menuActionsFor( const QString &pluginId ) const
{
    QList<QAction *> actions;
    if ( auto it = mMenuActions.constFind( pluginId ); it != mMenuActions.constEnd() )
    {
        for ( QAction *action : it.value() )
            if ( action )
                actions << action;
    }
    return actions;
}

void ExprsPluginShellUi::releaseUi( const QString &pluginId )
{
    // Contract: every plugin-created object is detached AND deleted here,
    // while the plugin binary is still mapped (destructors/vtables valid).

    // 1. Dock: delete the wrapper (it owns the plugin content widget).
    if ( QgsDockWidget *dock = mDocks.take( pluginId ) )
        delete dock;

    // 2. Menu actions: remove from the menu, then delete the actions.
    const auto actions = mMenuActions.take( pluginId );
    for ( QAction *action : actions )
    {
        if ( !action )
            continue;
        if ( mPluginMenu )
            mPluginMenu->removeAction( action );
        delete action;
    }

    // 3. Settings page: whichever state it is in, the widget must not
    // outlive the plugin library.
    releaseSettingsPage( pluginId );

    // 4. Registry commands projecting this plugin's contributions go with
    // them (review A3) — no dead palette entries across unload.
    if ( mCommandReleaseHook )
        mCommandReleaseHook( pluginId );
}

void ExprsPluginShellUi::releaseSettingsPage( const QString &pluginId )
{
    const QPointer<QWidget> page = mSettingsPages.take( pluginId );
    const QString title = mSettingsPageTitles.take( pluginId );
    if ( !page )
        return; // consumed and destroyed by a closed preferences dialog
    // Drop our cached registration (no-op when a dialog consumed it). A
    // foreign page under the same title belongs to another plugin — leave
    // it alone (title collision, P2 review finding).
    QWidget *cached = PreferencesDialog::unregisterExternalPage( title );
    if ( cached == page.data() )
    {
        delete page.data();
        return;
    }
    // Consumed by an (possibly open) preferences dialog: detach from its
    // tab widget so the dialog keeps working, then delete the page.
    detachPageFromTabs( page.data() );
    delete page.data();
}
