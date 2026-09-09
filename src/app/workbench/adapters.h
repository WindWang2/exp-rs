/***************************************************************************
 * adapters.h — concrete IWorkbench adapters (Workbench 5.0)
 *
 * MapWorkbench: the embedded map center (canvas stack) — always present.
 * ExternalWindowWorkbench: adapter over a lazily opened top-level session
 *   window (classification lab, georeferencer shells, OBIA). The opener is
 *   the existing "open X window" slot — no session logic moves; the shell
 *   only gains a uniform lifecycle handle (activate/dirty/close/context).
 ***************************************************************************/
#pragma once

#include "workbench_host.h"

#include <QWidget>

#include <functional>

namespace sicnu::app
{

/// Embedded map center bench.
class MapWorkbench : public QObject, public IWorkbench
{
    Q_OBJECT
  public:
    explicit MapWorkbench( QWidget *canvasStack, QObject *parent = nullptr );

    QString id() const override { return QStringLiteral( "map" ); }
    QString title() const override { return tr( "地图工作区" ); }
    QIcon icon() const override { return QIcon( QStringLiteral( ":/icons/r_ster" ) ); }
    QWidget *primaryWidget() override { return m_canvasStack.data(); }
    void activate() override;
    bool isActive() const override { return m_active; }
    WorkbenchFeatures features() const override
    {
        return WorkbenchFeature::MapCanvas | WorkbenchFeature::LayerTree;
    }

  private:
    QPointer<QWidget> m_canvasStack;
    bool m_active = false;
};

/**
 * External session-window bench. @p opener must ensure the window exists,
 * is visible and raised (the existing lazy-open slots already do exactly
 * that); @p windowGetter returns the top-level widget so the adapter can
 * track lifetime (null getter = no lifetime tracking, headless-friendly).
 */
class ExternalWindowWorkbench : public QObject, public IWorkbench
{
    Q_OBJECT
  public:
    using Opener = std::function<void()>;
    using WindowGetter = std::function<QWidget *()>;
    using DirtyFn = std::function<bool()>;

    ExternalWindowWorkbench( const QString &id, const QString &title, const QString &iconAlias,
                             Opener opener, QObject *parent = nullptr );
    ExternalWindowWorkbench( const QString &id, const QString &title, const QString &iconAlias,
                             Opener opener, WindowGetter windowGetter, DirtyFn dirtyFn = nullptr,
                             std::function<bool()> closeFn = nullptr, QObject *parent = nullptr );

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }
    QIcon icon() const override { return QIcon( m_iconAlias ); }
    QWidget *primaryWidget() override { return m_windowGetter ? m_windowGetter() : nullptr; }
    void activate() override;
    void deactivate() override { m_active = false; }
    bool isActive() const override
    {
        return m_active && ( !m_windowGetter || m_windowGetter() != nullptr );
    }
    bool isDirty() const override { return m_dirtyFn ? m_dirtyFn() : false; }
    bool requestClose() override;
    WorkbenchFeatures features() const override { return m_features; }
    void setFeatures( WorkbenchFeatures f ) { m_features = f; }
    void setDirtyFn( DirtyFn fn ) { m_dirtyFn = std::move( fn ); }
    void setWindowGetter( WindowGetter fn ) { m_windowGetter = std::move( fn ); }
    /// Install a close hook (e.g. window->close()); default clears active.
    void setCloseFn( std::function<bool()> fn ) { m_closeFn = std::move( fn ); }

  private:
    QString m_id;
    QString m_title;
    QString m_iconAlias;
    Opener m_opener;
    WindowGetter m_windowGetter;
    DirtyFn m_dirtyFn;
    std::function<bool()> m_closeFn;
    WorkbenchFeatures m_features = WorkbenchFeature::ExternalWindow | WorkbenchFeature::ModalInteraction;
    bool m_active = false;
};

} // namespace sicnu::app
