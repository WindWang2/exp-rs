/***************************************************************************
 * workbench_host.h — Professional Workbench 5.0 unified workspace model
 *
 * Additive contract over the existing surfaces. A "workbench" is one
 * professional workspace the user can enter: the map, the layout studio,
 * the classification lab, the georeferencer shells, the OBIA window, the
 * pipeline editor. The contract gives the shell a single place to ask:
 *   - which workspace is active (drives command context + inspector),
 *   - what it is operating on (selection context),
 *   - whether it is dirty (close semantics),
 *   - how to save/restore its state.
 *
 * Deliberately NOT a window manager: interactive sessions keep their own
 * dedicated canvases/windows. Adapters either embed a center widget
 * (embedded bench) or drive an existing top-level window (external bench).
 * No compute paths move; TaskCenter/JobEngine/DisplayManager stay untouched.
 ***************************************************************************/
#pragma once

#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantMap>

class QWidget;

namespace sicnu::app
{

struct SelectionContextSnapshot;

/// Capability flags a workbench reports so the shell knows which chrome
/// applies (e.g. layer/toolbar state belongs to map-capable benches only).
enum class WorkbenchFeature
{
    MapCanvas = 1 << 0,      ///< hosts the shared main map canvas
    LayerTree = 1 << 1,      ///< uses the shared layer tree / display seam
    ExternalWindow = 1 << 2, ///< primary widget is a top-level session window
    ModalInteraction = 1 << 3, ///< owns modal-ish interaction states (GCP, ROI)
};
Q_DECLARE_FLAGS( WorkbenchFeatures, WorkbenchFeature )
Q_DECLARE_OPERATORS_FOR_FLAGS( sicnu::app::WorkbenchFeatures )

/**
 * One professional workspace surface. Implementations are small adapters:
 * they translate activate/deactivate into whatever the existing surface
 * already does (show/raise, set current tool, pause sync) and answer the
 * shell's lifecycle questions.
 */
class IWorkbench
{
  public:
    virtual ~IWorkbench() = default;

    /// Stable id used by settings, command context and tests (e.g. "map").
    virtual QString id() const = 0;
    /// User-facing title (中文).
    virtual QString title() const = 0;
    virtual QIcon icon() const = 0;
    /// Center widget when the bench is embedded; null for external windows.
    virtual QWidget *primaryWidget() = 0;
    /// Enter this workspace (may lazily construct the surface).
    virtual void activate() = 0;
    /// Leave this workspace without destroying state.
    virtual void deactivate() {}
    virtual bool isActive() const = 0;
    /// Unsaved interaction state (samples, GCPs, edits). Default: clean.
    virtual bool isDirty() const { return false; }
    /// True while a TaskCenter-tracked compute runs inside the bench
    /// (#813 unified lifecycle). Default: none.
    virtual bool hasInFlightCompute() const { return false; }
    /// Cancel the bench's in-flight compute through its TaskCenter seam.
    /// Returns false when nothing is running. Never blocks. Default: none.
    virtual bool requestCancel() { return false; }
    /// Ask the bench to close (confirming its own dirty state). Default: yes.
    virtual bool requestClose() { return true; }
    virtual QVariantMap saveState() const { return QVariantMap(); }
    virtual void restoreState( const QVariantMap &state ) { Q_UNUSED( state ); }
    /// The bench's projection into the shared selection context (may be empty).
    virtual void augmentSelectionContext( SelectionContextSnapshot &snapshot ) const
    {
        Q_UNUSED( snapshot );
    }
    /// Command-context tag used by CommandRegistry availability predicates.
    virtual QString commandContext() const { return id(); }
    virtual WorkbenchFeatures features() const = 0;
};

/**
 * Shell-owned registry + switcher. Owns the bench list, the active bench and
 * the persisted "last active" id. The host never inspects bench internals —
 * everything flows through IWorkbench.
 */
class WorkbenchHost : public QObject
{
    Q_OBJECT
  public:
    explicit WorkbenchHost( QObject *parent = nullptr );

    /// Registers a bench. The host does NOT take ownership (benches are owned
    /// by their adapter parent — typically the main window). Duplicate ids
    /// are rejected (returns false) — a contract test guards this.
    bool registerWorkbench( IWorkbench *bench );

    QList<IWorkbench *> workbenches() const { return m_benches; }
    IWorkbench *workbench( const QString &id ) const;
    IWorkbench *activeWorkbench() const;
    QString activeWorkbenchId() const;
    bool isActive( const QString &id ) const { return m_activeId == id; }

    /// All registered ids, registration order (stable for menus/palette).
    QStringList workbenchIds() const;

  public slots:
    /// Switches to @p id: deactivates the previous bench, activates the new
    /// one, broadcasts. Unknown ids are ignored (no-op, returns false).
    bool activate( const QString &id );

  signals:
    void workbenchRegistered( const QString &id );
    /// Emitted after the switch completed (prevId → newId).
    void activeWorkbenchChanged( const QString &activeId, const QString &previousId );

  private:
    QList<IWorkbench *> m_benches;
    QString m_activeId;
};

/// Registry-side helpers shared by adapters.
namespace workbench_detail
{
/// Default persistence bucket for bench state under QSettings.
inline QString settingsKey( const QString &benchId ) { return QStringLiteral( "workbench/%1/state" ).arg( benchId ); }
} // namespace workbench_detail

} // namespace sicnu::app
