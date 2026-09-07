/***************************************************************************
 * src/plugins/framework/plugin_ui_host.h
 *
 * Qt-side host for UI plugin contributions (Phases Q/R). The application
 * shell constructs one, installs it as the process-wide exprs::UiHostV1
 * BEFORE plugins load, and consumes the collected contributions after the
 * PluginRegistry loads plugins. Plugins never see the main window.
 *
 * Reverse ownership (issue #747): the shell installs a UiShellSink; plugin
 * widgets/actions/pages are attached THROUGH it and released THROUGH it, so
 * PluginUiHost can order "detach + delete plugin UI" before the plugin
 * binary is unloaded. The host never loses track of an attached widget.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_ui.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <vector>

namespace sicnu::plugins {

struct UiContributionRecord
{
    QString pluginId;
    QString dockTitle;          ///< empty when ui.dock == false
    QWidget *dockWidget = nullptr;
    QList<QAction *> menuActions;
    QString settingsPageTitle;  ///< empty when ui.settings_page == false
    QWidget *settingsPage = nullptr;
    /// True once the shell sink took the widgets/actions (attached state).
    bool attached = false;
};

/// The shell side of the reverse-ownership contract. Implemented by the
/// application shell; releaseUi() must detach AND delete everything the
/// shell attached for @p pluginId (deletion is safe here: it happens while
/// the plugin binary is still mapped).
class UiShellSink
{
public:
    virtual ~UiShellSink() = default;

    virtual void attachDock( const QString &pluginId, const QString &title, QWidget *widget ) = 0;
    virtual void attachMenuActions( const QString &pluginId, const QList<QAction *> &actions ) = 0;
    virtual void attachSettingsPage( const QString &pluginId, const QString &title,
                                     QWidget *page ) = 0;
    /// Detaches and deletes every UI object the shell attached for the
    /// plugin (dock + content, menu actions, settings page).
    virtual void releaseUi( const QString &pluginId ) = 0;
};

class PluginUiHost : public QObject
{
    Q_OBJECT

public:
    static PluginUiHost *instance();

    ~PluginUiHost() override;

    // -- contribution sink API (host pulls from plugins through
    // collectFromPlugin; these methods record what the shell should attach)
    void addDockWidget( const QString &pluginId, const QString &title, QWidget *widget );
    void addMenuActions( const QString &pluginId, const QList<QAction *> &actions );
    void registerSettingsPage( const QString &pluginId, const QString &title, QWidget *page );
    void showMessage( const QString &level, const QString &message );

    // -- contribution collection ----------------------------------------------
    /// Asks a loaded plugin that implements exprs::UiContributionV1 to build
    /// its contributions (records only; attachPluginUi hands them to the
    /// shell). Re-collection releases the previous contributions first.
    void collectFromPlugin( const QString &pluginId, exprs::UiContributionV1 *contribution );

    // -- shell consumption (reverse ownership, issue #747) --------------------
    /// Installs the shell sink. Must be set before attachPluginUi/releaseUi.
    void setShellSink( UiShellSink *sink ) { mShellSink = sink; }
    UiShellSink *shellSink() const { return mShellSink; }

    /// Hands a plugin's collected contributions to the shell sink. Records
    /// stay in the host (attached state) so a later releasePluginUi can
    /// drive the reverse path. Warns (message sink) when no sink is
    /// installed — contributions would silently never appear.
    void attachPluginUi( const QString &pluginId );

    /// One-call re-attach seam shared by the startup shell and the plugin
    /// manager dialog: collect (or re-collect) from a loaded plugin's UI
    /// contribution, then attach through the shell sink.
    void attachCollectedUi( const QString &pluginId, exprs::UiContributionV1 *contribution );

    const std::vector<UiContributionRecord> &records() const { return mRecords; }

    /// Detaches (via the shell sink) and deletes all plugin widgets/actions
    /// for @p pluginId. Called on unload — BEFORE the plugin binary leaves
    /// the address space. No plugin-created widget/action survives it.
    void releasePluginUi( const QString &pluginId );

    void setMessageSink( std::function<void( const QString &, const QString & )> sink )
    {
        mMessageSink = std::move( sink );
    }

signals:
    void contributionChanged();

private:
    PluginUiHost() = default;

    UiContributionRecord &recordFor( const QString &pluginId );

    std::vector<UiContributionRecord> mRecords;
    std::function<void( const QString &, const QString & )> mMessageSink;
    UiShellSink *mShellSink = nullptr;
};

} // namespace sicnu::plugins
