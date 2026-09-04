/***************************************************************************
 * src/plugins/framework/plugin_ui_host.h
 *
 * Qt-side host for UI plugin contributions (Phases Q/R). The application
 * shell constructs one, installs it as the process-wide exprs::UiHostV1
 * BEFORE plugins load, and consumes the collected contributions after the
 * PluginRegistry loads plugins. Plugins never see the main window.
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

    // -- shell consumption ----------------------------------------------------
    /// After plugins load: asks every loaded plugin that implements
    /// exprs::UiContributionV1 to build its contributions.
    void collectFromPlugin( const QString &pluginId, exprs::UiContributionV1 *contribution );

    const std::vector<UiContributionRecord> &records() const { return mRecords; }

    /// The shell takes ownership when it attaches the widget (dock added to
    /// the main window). Returns nullptr when the plugin contributed none.
    QWidget *takeDockWidget( const QString &pluginId );
    QWidget *takeSettingsPage( const QString &pluginId );
    QList<QAction *> takeMenuActions( const QString &pluginId );

    /// Detaches (not deletes) all plugin widgets/actions; called before
    /// plugin unload so the shell never touches deleted widgets. Widgets the
    /// shell took over are the shell's to retire; widgets still owned here
    /// are destroyed.
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
};

} // namespace sicnu::plugins
