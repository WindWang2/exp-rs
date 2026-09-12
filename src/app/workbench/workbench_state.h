/***************************************************************************
 * workbench_state.h — Workbench 9.0 M1: explicit workbench state model
 *
 * Before M1 the shell's coarse state (empty vs populated workspace, active
 * map tool, in-flight task) was re-derived ad hoc at every consumer: five
 * call sites each computed `hasLayers` by hand, tool state lived only in
 * the canvas, and the task predicate was re-evaluated in place. This model
 * is the single aggregation point those consumers project from.
 *
 * It is a *projection aggregator*, not a second authority: every fact keeps
 * exactly one upstream source (QgsMapCanvas for layers/tool, the injected
 * TaskCenter predicate for in-flight work, SelectionContext for broken
 * layers). The model only merges, exposes and broadcasts them.
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QgsMapCanvas;

namespace sicnu::app
{

class SelectionContext;
struct SelectionContextSnapshot;

/// Coarse workspace phase — the canvas welcome/empty-state projection and
/// "workspace is usable" rules derive from this, never from raw layer probes.
enum class ProjectPhase
{
  Empty,     ///< no layers staged (welcome canvas, empty-state CTAs apply)
  Populated, ///< at least one layer is staged
};

/// Orthogonal shell facts, broadcast together on any change.
struct WorkbenchFacts
{
  ProjectPhase phase = ProjectPhase::Empty;
  /// Object name of the active map tool, or "pan" for the idle default.
  QString toolMode = QStringLiteral( "pan" );
  bool taskInFlight = false;   ///< TaskCenter has queued/running work
  bool hasBrokenLayer = false; ///< a staged layer failed to resolve
  int layerCount = 0;

  bool operator==( const WorkbenchFacts & ) const = default;
};

/// Pure helpers over the facts — unit-testable without widgets (M1 contract:
/// enable/disable projections derive here, not at call sites).
namespace WorkbenchRules
{
/// Map the phase onto the canvas stack page the shell shows.
int canvasStackPage( ProjectPhase phase );
/// True when import affordances are the *primary* suggested action.
bool importIsPrimaryAction( const WorkbenchFacts &facts );
/// Human summary for status surfaces (title tooltips, palette footer).
QString phaseSummary( const WorkbenchFacts &facts );
} // namespace WorkbenchRules

class WorkbenchStateModel : public QObject
{
  Q_OBJECT
  public:
    explicit WorkbenchStateModel( QgsMapCanvas *canvas, QObject *parent = nullptr );

    /// Selection/broken-layer facts come from the existing context hub.
    void attachSelectionContext( SelectionContext *context );
    /// Inject the TaskCenter in-flight predicate (same seam as
    /// SelectionContext::setInFlightTaskPredicate — the model stays free of
    /// scheduler includes).
    void setInFlightTaskPredicate( std::function<bool()> predicate );

    WorkbenchFacts facts() const { return m_facts; }
    ProjectPhase phase() const { return m_facts.phase; }

  signals:
    /// Coalesced: at most one emission per event-loop turn.
    void factsChanged( const sicnu::app::WorkbenchFacts &facts );
    void phaseChanged( sicnu::app::ProjectPhase phase );
    void toolModeChanged( const QString &toolMode );

  public slots:
    /// Recompute from every source and broadcast if anything changed.
    void refresh();

  private:
    void connectCanvas( QgsMapCanvas *canvas );

    QgsMapCanvas *m_canvas = nullptr;
    SelectionContext *m_selectionContext = nullptr;
    std::function<bool()> m_inFlightPredicate;
    WorkbenchFacts m_facts;
    bool m_refreshScheduled = false;
};

} // namespace sicnu::app
