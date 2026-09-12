/***************************************************************************
 * workbench_state.cpp — Workbench 9.0 M1 state model implementation
 ***************************************************************************/
#include "workbench_state.h"

#include "selection_context.h"

#include <qgsmapcanvas.h>
#include <qgsmaptool.h>

#include <QTimer>

namespace sicnu::app
{

namespace
{

/// Stable id for a map tool: class name, with the pan default normalized so
/// idle-state consumers read "pan" regardless of which pan instance ran last.
QString toolModeId( const QgsMapTool *tool )
{
  if ( !tool )
    return QStringLiteral( "pan" );
  const QString className = QString::fromLatin1( tool->metaObject()->className() );
  if ( className == QLatin1String( "QgsMapToolPan" ) )
    return QStringLiteral( "pan" );
  return className;
}

} // namespace

// ── WorkbenchRules ──────────────────────────────────────────────────────────

int WorkbenchRules::canvasStackPage( ProjectPhase phase )
{
  // Page 0 = welcome/empty state, page 1 = map canvas (main_window.cpp).
  return phase == ProjectPhase::Populated ? 1 : 0;
}

bool WorkbenchRules::importIsPrimaryAction( const WorkbenchFacts &facts )
{
  return facts.phase == ProjectPhase::Empty && !facts.taskInFlight;
}

QString WorkbenchRules::phaseSummary( const WorkbenchFacts &facts )
{
  if ( facts.phase == ProjectPhase::Empty )
    return QObject::tr( "工作区为空 — 导入数据开始" );
  return QObject::tr( "工作区包含 %1 个图层" ).arg( facts.layerCount );
}

// ── WorkbenchStateModel ─────────────────────────────────────────────────────

WorkbenchStateModel::WorkbenchStateModel( QgsMapCanvas *canvas, QObject *parent )
    : QObject( parent )
    , m_canvas( canvas )
{
  connectCanvas( canvas );
}

void WorkbenchStateModel::connectCanvas( QgsMapCanvas *canvas )
{
  if ( !canvas )
    return;
  // The canvas is the single authority for staged layers (the shell draws
  // from the same project the canvas renders) and the active tool.
  connect( canvas, &QgsMapCanvas::mapToolSet, this, [this]( QgsMapTool *tool ) {
    const QString id = toolModeId( tool );
    if ( m_facts.toolMode != id )
    {
      m_facts.toolMode = id;
      emit toolModeChanged( id );
    }
    refresh();
  } );
  connect( canvas, &QgsMapCanvas::layersChanged, this, &WorkbenchStateModel::refresh );
}

void WorkbenchStateModel::attachSelectionContext( SelectionContext *context )
{
  if ( !context )
    return;
  m_selectionContext = context;
  // SelectionContext already coalesces canvas/project/tree churn into one
  // signal — piggyback on it instead of duplicating those connections.
  connect( context, &SelectionContext::changed, this, &WorkbenchStateModel::refresh );
}

void WorkbenchStateModel::setInFlightTaskPredicate( std::function<bool()> predicate )
{
  m_inFlightPredicate = std::move( predicate );
  refresh();
}

void WorkbenchStateModel::refresh()
{
  if ( m_refreshScheduled )
    return;
  // Coalesce burst updates (project read emits dozens of layer signals) into
  // one factsChanged per event-loop turn.
  m_refreshScheduled = true;
  QTimer::singleShot( 0, this, [this] {
    m_refreshScheduled = false;

    WorkbenchFacts next;
    if ( m_canvas )
    {
      // Read current truth from the canvas — cheap getters, no probing.
      next.layerCount = m_canvas->layers().size();
      next.toolMode = toolModeId( m_canvas->mapTool() );
    }
    next.phase = next.layerCount > 0 ? ProjectPhase::Populated : ProjectPhase::Empty;
    next.taskInFlight = m_inFlightPredicate ? m_inFlightPredicate() : false;
    if ( m_selectionContext )
    {
      const SelectionContextSnapshot snap = m_selectionContext->snapshot();
      next.hasBrokenLayer = snap.hasBroken;
    }

    if ( next != m_facts )
    {
      const ProjectPhase oldPhase = m_facts.phase;
      const QString oldTool = m_facts.toolMode;
      m_facts = next;
      emit factsChanged( m_facts );
      if ( oldPhase != m_facts.phase )
        emit phaseChanged( m_facts.phase );
      if ( oldTool != m_facts.toolMode )
        emit toolModeChanged( m_facts.toolMode );
    }
  } );
}

} // namespace sicnu::app
