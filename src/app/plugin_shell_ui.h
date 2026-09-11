/***************************************************************************
 * src/app/plugin_shell_ui.h
 *
 * Shell-side implementation of the exprs plugin UI reverse-ownership
 * contract (sicnu::plugins::UiShellSink, issue #747). The main window owns
 * one instance; PluginUiHost drives attach (startup, enable) and release
 * (disable/unload/shutdown) through it, so plugin-created widgets, menu
 * actions and preferences pages are always detached AND deleted while the
 * plugin binary is still mapped.
 ***************************************************************************/
#pragma once

#include "plugins/framework/plugin_ui_host.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

class QgisDesktopWindow;
class QMenu;
class QgsDockWidget;

class ExprsPluginShellUi : public QObject, public sicnu::plugins::UiShellSink
{
    Q_OBJECT

public:
    /// @param window the main window (dock parent; also resolved from menus)
    /// @param pluginMenu the 插件 menu receiving plugin menu actions
    ExprsPluginShellUi( QgisDesktopWindow *window, QMenu *pluginMenu );

    // -- UiShellSink -----------------------------------------------------------
    void attachDock( const QString &pluginId, const QString &title, QWidget *widget ) override;
    void attachMenuActions( const QString &pluginId, const QList<QAction *> &actions ) override;
    void attachSettingsPage( const QString &pluginId, const QString &title,
                             QWidget *page ) override;
    void releaseUi( const QString &pluginId ) override;

    /// True when the plugin currently has a shell-attached dock.
    bool hasDock( const QString &pluginId ) const { return mDocks.contains( pluginId ); }

    /// Workbench 9.0 M8: rendered menu actions of @p pluginId (registry
    /// command projections trigger these; empty after releasePluginUi).
    QList<QAction *> menuActionsFor( const QString &pluginId ) const;

private:
    void releaseSettingsPage( const QString &pluginId );

    QgisDesktopWindow *mWindow = nullptr;
    QMenu *mPluginMenu = nullptr;
    QMap<QString, QgsDockWidget *> mDocks;
    QMap<QString, std::vector<QAction *>> mMenuActions;
    QMap<QString, QPointer<QWidget>> mSettingsPages;
    QMap<QString, QString> mSettingsPageTitles;
};
