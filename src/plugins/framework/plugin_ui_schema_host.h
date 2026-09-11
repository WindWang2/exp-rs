/***************************************************************************
 * src/plugins/framework/plugin_ui_schema_host.h
 *
 * Host-side renderer for DECLARATIVE out-of-process UI contributions
 * (plugin-platform 8.0, protocol 1.1 ui.describe / ui.invoke).
 *
 * Ownership model (the reason this class exists):
 *   - the plugin describes UI as bounded JSON (exprs/plugin_ui_schema.h);
 *   - THIS host builds every widget/action as its OWN Qt objects and
 *     attaches them through the same reverse-ownership shell sink the
 *     in-process PluginUiHost uses (src/plugins/framework/plugin_ui_host.h);
 *   - user interaction becomes a bounded event JSON delivered to the
 *     plugin through a UiInvokeDelegate (the production delegate wraps
 *     PluginHostProcessRuntime::invokeUi); plugin state updates are applied
 *     back onto the host-owned widgets;
 *   - releasePluginUi deletes everything before the plugin binary can be
 *     unloaded — no cross-process widget pointer ever exists.
 *
 * Thread discipline: widgets live on the GUI thread; events are delivered
 * on a single serialized worker thread with a bounded queue (overflow drops
 * the oldest and counts it), and responses hop back through queued signal
 * connections. A wedged plugin can therefore delay one event, never the UI.
 ***************************************************************************/
#pragma once

#include "plugin_ui_host.h"

#include <json/json.h>

#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QWidget;
class QAction;

namespace sicnu::plugins {

/// Delivers one rendered event to the owning plugin and returns its bounded
/// response. The production delegate wraps
/// PluginHostProcessRuntime::invokeUi and is created by the SHELL when it
/// attaches a schema (shell integration is the workbench track's seam);
/// tests fake it. Implementations must be thread-safe (called from the
/// renderer's delivery thread).
class UiInvokeDelegate
{
public:
    virtual ~UiInvokeDelegate() = default;
    virtual Json::Value invoke( const Json::Value &event ) = 0;
};

class PluginUiSchemaRenderer : public QObject
{
    Q_OBJECT

public:
    static PluginUiSchemaRenderer *instance();
    ~PluginUiSchemaRenderer() override;

    /// Installs the shell sink (same contract as PluginUiHost::setShellSink).
    void setShellSink( UiShellSink *sink ) { mShellSink = sink; }
    UiShellSink *shellSink() const { return mShellSink; }

    /// Renders an already-VALIDATED schema for @p pluginId and attaches the
    /// results through the shell sink. Takes ownership of @p delegate.
    /// Re-attachment replaces the previous rendering (reload path).
    /// Returns false (with @p error) when there is nothing attachable or no
    /// sink is installed.
    bool attachPluginSchema( const QString &pluginId, const Json::Value &schema,
                             std::unique_ptr<UiInvokeDelegate> delegate, QString &error );

    /// Detaches (through the shell sink) and forgets everything rendered
    /// for @p pluginId. Called on unload BEFORE the worker is torn down.
    void releasePluginUi( const QString &pluginId );

    /// True while @p pluginId has rendered contributions.
    bool hasPluginUi( const QString &pluginId ) const;

    /// Diagnostics: events dropped by the bounded queue.
    long long droppedEvents() const { return mDroppedEvents; }

signals:
    void contributionChanged();
    /// Emitted (GUI thread) after the plugin answered an event with state.
    void eventApplied( const QString &pluginId, const QString &contributionId );

private:
    PluginUiSchemaRenderer();

    struct ControlBinding
    {
        QString contributionId;
        QString controlId;
        QWidget *widget = nullptr;
        std::function<Json::Value()> readValue;      ///< current value as JSON
        std::function<void( const Json::Value & )> applyValue;
    };

    struct RenderedRecord
    {
        QString pluginId;
        Json::Value schema;
        /// Shared ownership: delivery thread and queued GUI lambdas copy
        /// this under the event mutex; release clears the field, and any
        /// copy taken before that still finishes its bounded invoke but
        /// finds no widgets worth touching afterwards.
        std::shared_ptr<UiInvokeDelegate> delegate;
        struct Surface
        {
            QString contributionId;
            QWidget *widget = nullptr;              ///< dock content / settings page
            QString title;
        };
        QVector<Surface> surfaces;
        QList<QAction *> menuActions;
        std::vector<ControlBinding> bindings;
    };

    void enqueueEvent( const QString &pluginId, const Json::Value &event );
    void deliveryLoop();
    void applyState( RenderedRecord &record, const Json::Value &state );
    void buildControls( QWidget *parent, const Json::Value &controls, const QString &contributionId,
                        const QString &pluginId, RenderedRecord &record );

    UiShellSink *mShellSink = nullptr;
    std::vector<std::shared_ptr<RenderedRecord>> mRecords;

    // Serialized delivery thread (bounded queue; responses queue back).
    std::thread mDelivery;
    mutable std::mutex mEventMutex;
    std::condition_variable mEventCv;
    struct PendingEvent
    {
        std::shared_ptr<RenderedRecord> record;
        Json::Value event;
    };
    std::deque<PendingEvent> mQueue;
    size_t mQueueCap = 64;
    std::atomic<bool> mStopping{ false };
    std::atomic<long long> mDroppedEvents{ 0 };
};

} // namespace sicnu::plugins
