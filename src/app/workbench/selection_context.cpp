/***************************************************************************
 * selection_context.cpp — aggregation + pure availability rules
 ***************************************************************************/
#include "selection_context.h"

#include "workbench_host.h"

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayertemporalproperties.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsproject.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>

#include <QMetaType>
#include <QSet>
#include <QTimer>

namespace sicnu::app
{

namespace
{

/// #778 helper: drop dead entries from the guard list; true when any entry
/// died (the cached snapshot then references a destroyed object).
bool pruneDeadGuards( QList<QPointer<QgsMapLayer>> &guards )
{
    bool removed = false;
    for ( int i = guards.size() - 1; i >= 0; --i )
    {
        if ( !guards.at( i ) )
        {
            guards.removeAt( i );
            removed = true;
        }
    }
    return removed;
}

} // namespace

namespace
{

/// Conservative SAR product-token heuristic (Sentinel-1, ALOS/PALSAR,
/// TerraSAR, COSMO-SkyMed, RADARSAT, Gaofen-3). Overridable via
/// SelectionContext::setSarPredicate; documented as a heuristic, never as
/// an authoritative modality flag.
QStringList sarProductTokens()
{
    return {
        QStringLiteral( "s1a_" ), QStringLiteral( "s1b_" ), QStringLiteral( "s1a-" ),
        QStringLiteral( "s1b-" ), QStringLiteral( "sentinel-1" ), QStringLiteral( "sentinel1" ),
        QStringLiteral( "palsar" ), QStringLiteral( "alos" ), QStringLiteral( "terrasar" ),
        QStringLiteral( "cosmo-skymed" ), QStringLiteral( "csk" ), QStringLiteral( "radarsat" ),
        QStringLiteral( "gaofen-3" ), QStringLiteral( "gf-3" ), QStringLiteral( "gf3" ),
    };
}

bool sourceMentionsSar( const QgsMapLayer *layer )
{
    if ( !layer )
        return false;
    const QString haystack = QStringLiteral( "%1 %2" )
                                 .arg( layer->name(),
                                       layer->source() )
                                 .toLower();
    const QStringList tokens = sarProductTokens();
    for ( const QString &token : tokens )
    {
        if ( haystack.contains( token ) )
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Snapshot helpers
// ---------------------------------------------------------------------------

QgsVectorLayer *SelectionContextSnapshot::firstVectorLayer() const
{
    if ( QgsVectorLayer *v = qobject_cast<QgsVectorLayer *>( activeLayer ) )
        return v;
    for ( QgsMapLayer *layer : selectedLayers )
    {
        if ( QgsVectorLayer *v = qobject_cast<QgsVectorLayer *>( layer ) )
            return v;
    }
    return nullptr;
}

QgsRasterLayer *SelectionContextSnapshot::firstRasterLayer() const
{
    if ( QgsRasterLayer *r = qobject_cast<QgsRasterLayer *>( activeLayer ) )
        return r;
    for ( QgsMapLayer *layer : selectedLayers )
    {
        if ( QgsRasterLayer *r = qobject_cast<QgsRasterLayer *>( layer ) )
            return r;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Pure rules
// ---------------------------------------------------------------------------

namespace ContextRules
{

ContextFacts prerequisiteFacts( const SelectionContextSnapshot &s )
{
    ContextFacts facts;
    facts.workbenchId = s.workbenchId;
    facts.hasLayerSelection = layerSelected( s );
    facts.hasRaster = rasterSelected( s );
    facts.hasVector = vectorSelected( s );
    facts.hasSar = sarSelected( s );
    facts.editable = editingAvailable( s );
    facts.editing = editingActive( s );
    facts.hasGovernanceResult = resultSelected( s );
    facts.hasGovernanceAsset = assetSelected( s );
    facts.hasBrokenLayer = s.hasBroken;
    facts.hasInFlightTask = s.hasInFlightTask;
    facts.hasExperiment = experimentSelected( s );
    facts.hasDataset = datasetSelected( s );
    facts.hasModel = modelSelected( s );
    facts.hasWorkflowRun = workflowRunSelected( s );
    return facts;
}

bool rasterSelected( const SelectionContextSnapshot &s )
{
    if ( s.hasRaster )
        return true;
    return s.firstRasterLayer() != nullptr;
}

bool vectorSelected( const SelectionContextSnapshot &s )
{
    if ( s.hasVector )
        return true;
    return s.firstVectorLayer() != nullptr;
}

bool editingActive( const SelectionContextSnapshot &s )
{
    const QgsVectorLayer *v = s.firstVectorLayer();
    return v && v->isEditable() && s.activeEditable;
}

bool editingAvailable( const SelectionContextSnapshot &s )
{
    QgsVectorLayer *v = s.firstVectorLayer();
    return v && v->isValid() && !v->readOnly();
}

bool sarSelected( const SelectionContextSnapshot &s ) { return s.hasSar; }

bool layerSelected( const SelectionContextSnapshot &s )
{
    return s.activeLayer != nullptr || !s.selectedLayers.isEmpty();
}

bool resultSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedResultIds.isEmpty();
}

bool assetSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedAssetIds.isEmpty();
}

bool experimentSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedExperimentIds.isEmpty();
}

bool datasetSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedDatasetIds.isEmpty();
}

bool modelSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedModelIds.isEmpty();
}

bool workflowRunSelected( const SelectionContextSnapshot &s )
{
    return !s.selectedWorkflowRunIds.isEmpty();
}

bool missionTaskSelected( const SelectionContextSnapshot &s )
{
    return s.hasMissionTaskSelection && !s.selectedMissionTaskId.isEmpty();
}

bool missionTaskRetryable( const SelectionContextSnapshot &s )
{
    return missionTaskSelected( s )
           && missionTaskStatusIsRetryable( s.selectedMissionTaskStatus );
}

bool missionTaskResumable( const SelectionContextSnapshot &s )
{
    // Stale work must be re-bound and re-verified; a canceled task resumes.
    return missionTaskSelected( s )
           && ( s.selectedMissionTaskStatus == MissionTaskStatus::Stale
                || s.selectedMissionTaskStatus == MissionTaskStatus::Canceled );
}

QStringList selectedLayerIds( const SelectionContextSnapshot &s )
{
    QStringList ids;
    if ( s.activeLayer )
        ids.append( s.activeLayer->id() );
    for ( QgsMapLayer *layer : s.selectedLayers )
    {
        if ( layer && !ids.contains( layer->id() ) )
            ids.append( layer->id() );
    }
    return ids;
}

QVector<RequirementFact> requirementFacts( const SelectionContextSnapshot &s, const QString &commandId )
{
    // Single derivation of availability requirements. The dispatch mirrors
    // exactly the command families that DECLARE an availability predicate
    // (command_defs.cpp); a command id with no case here has no predicate,
    // so empty facts honestly mean "always available".
    struct Spec
    {
        const char *code;
        const char *label;
        bool ( *predicate )( const SelectionContextSnapshot & );
    };
    QVector<Spec> specs;
    const auto vectorRequirements = [&] {
        specs.append( { "vector.selected", QT_TR_NOOP( "A vector layer must be selected" ), &vectorSelected } );
    };
    const auto rasterRequirements = [&] {
        specs.append( { "raster.selected", QT_TR_NOOP( "A raster layer must be selected" ), &rasterSelected } );
    };

    if ( commandId == QLatin1String( "layer.toggleEditing" ) )
    {
        vectorRequirements();
        specs.append( { "vector.editable", QT_TR_NOOP( "The current layer is not editable" ), &editingAvailable } );
    }
    else if ( commandId == QLatin1String( "layer.saveEdits" ) )
    {
        vectorRequirements();
        specs.append( { "editing.active", QT_TR_NOOP( "Start an editing session first" ), &editingActive } );
    }
    else if ( commandId == QLatin1String( "layer.attributeTable" ) )
    {
        vectorRequirements();
    }
    else if ( commandId.startsWith( QLatin1String( "layer.edit." ) ) )
    {
        // Reserved edit-command family (no registrations yet).
        vectorRequirements();
        specs.append( { "vector.editable", QT_TR_NOOP( "The current layer is not editable" ), &editingAvailable } );
    }
    else if ( commandId.startsWith( QLatin1String( "layer." ) ) )
    {
        specs.append( { "layer.selected", QT_TR_NOOP( "A layer must be selected" ), &layerSelected } );
    }
    else if ( commandId == QLatin1String( "rs.speckle" ) )
    {
        // SAR speckle filtering is the one command with a compound predicate
        // (raster AND SAR); spell out both rows so the disabled state always
        // names the missing condition.
        rasterRequirements();
        specs.append( { "sar.selected", QT_TR_NOOP( "SAR data must be selected" ), &sarSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "raster." ) )
              || commandId.startsWith( QLatin1String( "rs." ) ) )
    {
        rasterRequirements();
    }
    else if ( commandId.startsWith( QLatin1String( "sar." ) ) )
    {
        specs.append( { "sar.selected", QT_TR_NOOP( "SAR data must be selected" ), &sarSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "result." ) ) )
    {
        specs.append( { "result.selected", QT_TR_NOOP( "Governance results must be selected" ), &resultSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "asset." ) ) )
    {
        specs.append( { "asset.selected", QT_TR_NOOP( "Data assets must be selected" ), &assetSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "experiment." ) ) )
    {
        specs.append( { "experiment.selected", QT_TR_NOOP( "An experiment run must be selected" ), &experimentSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "dataset." ) ) )
    {
        specs.append( { "dataset.selected", QT_TR_NOOP( "A dataset must be selected" ), &datasetSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "model." ) ) )
    {
        specs.append( { "model.selected", QT_TR_NOOP( "A model must be selected" ), &modelSelected } );
    }
    else if ( commandId.startsWith( QLatin1String( "workflowrun." ) ) )
    {
        specs.append( { "workflowrun.selected", QT_TR_NOOP( "A workflow run must be selected" ), &workflowRunSelected } );
    }
    else if ( commandId == QLatin1String( "mission.task.retry" ) )
    {
        specs.append( { "mission.task.selected", QT_TR_NOOP( "Select a mission task first" ), &missionTaskSelected } );
        specs.append( { "mission.task.retryable", QT_TR_NOOP( "The selected mission task cannot be retried" ), &missionTaskRetryable } );
    }
    else if ( commandId == QLatin1String( "mission.task.resume" ) )
    {
        specs.append( { "mission.task.selected", QT_TR_NOOP( "Select a mission task first" ), &missionTaskSelected } );
        specs.append( { "mission.task.resumable", QT_TR_NOOP( "The selected mission task has nothing to resume" ), &missionTaskResumable } );
    }
    else if ( commandId.startsWith( QLatin1String( "mission." ) ) )
    {
        // mission.timeline.show is always available — no requirements.
    }

    QVector<RequirementFact> facts;
    facts.reserve( specs.size() );
    for ( const Spec &spec : specs )
        facts.append( { QString::fromUtf8( spec.code ), QObject::tr( spec.label ),
                        spec.predicate( s ) } );
    return facts;
}

QString unavailabilityReason( const SelectionContextSnapshot &s, const QString &commandId )
{
    // Milestone E: derived from requirementFacts — an empty return means
    // "available", a non-empty return is the first unsatisfied requirement's
    // label. Kept as the text projection so palette / tooltips and the
    // structured facts channel always agree (review L #1).
    const QVector<RequirementFact> facts = requirementFacts( s, commandId );
    for ( const RequirementFact &fact : facts ) {
        if ( !fact.satisfied )
            return fact.label;
    }
    return QString();
}

NextAction suggestedNextAction( const SelectionContextSnapshot &s )
{
    // Deterministic priority: the most time-sensitive state wins. Only
    // registered registry commands are suggested (a suggestion that cannot
    // execute would be noise).
    NextAction action;

    if ( editingActive( s ) )
    {
        action.commandId = QStringLiteral( "layer.saveEdits" );
        action.text = QObject::tr( "An editing session is active — save or discard the edits" );
        return action;
    }
    if ( rasterSelected( s ) )
    {
        action.commandId = QStringLiteral( "rs.spectralIndex" );
        action.text = QObject::tr( "Raster selected — processing tools such as spectral indices can run" );
        return action;
    }
    if ( vectorSelected( s ) && editingAvailable( s ) )
    {
        action.commandId = QStringLiteral( "layer.toggleEditing" );
        action.text = QObject::tr( "Vector layer selected — editing can start" );
        return action;
    }
    if ( s.hasTemporal )
    {
        action.commandId = QStringLiteral( "workbench.temporal" );
        action.text = QObject::tr( "Epoch data detected — the Temporal Workbench is available" );
        return action;
    }
    if ( s.hasInFlightTask )
    {
        action.commandId = QStringLiteral( "workbench.processingHistory" );
        action.text = QObject::tr( "A task is running — check progress and artifacts in the processing history" );
        return action;
    }
    if ( s.layerCount == 0 && !s.hasGovernanceSelection() )
    {
        action.commandId = QStringLiteral( "project.importLayer" );
        action.text = QObject::tr( "Workspace is empty — import or open data to start" );
        return action;
    }
    return action;
}

} // namespace ContextRules

// ---------------------------------------------------------------------------
// SelectionContext
// ---------------------------------------------------------------------------

SelectionContext::SelectionContext( QObject *parent )
    : QObject( parent )
{
    qRegisterMetaType<SelectionContextSnapshot>( "sicnu::app::SelectionContextSnapshot" );
    m_debounce = new QTimer( this );
    m_debounce->setSingleShot( true );
    m_debounce->setInterval( 150 );
    connect( m_debounce, &QTimer::timeout, this, &SelectionContext::refreshNow );
    if ( QgsProject::instance() )
    {
        // #778: the project announces layer removal BEFORE the QgsMapLayer object
        // is destroyed — purge it from every projection right away. Batch removals
        // (removeMapLayers / clear) emit this per-layer signal for each layer.
        connect( QgsProject::instance(), qOverload<QgsMapLayer *>( &QgsProject::layerWillBeRemoved ),
                 this, [this]( QgsMapLayer *layer ) { handleLayerWillBeRemoved( layer ); } );
        connect( QgsProject::instance(), qOverload<const QList<QgsMapLayer *> &>( &QgsProject::layersWillBeRemoved ),
                 this, [this]( const QList<QgsMapLayer *> &layers ) {
                     for ( QgsMapLayer *layer : layers )
                         handleLayerWillBeRemoved( layer );
                 } );
        connect( QgsProject::instance(), qOverload<const QString &>( &QgsProject::layerWillBeRemoved ),
                 this, [this]( const QString & ) {
                     // id-based announcement: the cached snapshot may borrow the
                     // doomed layer even if the object signal was not observed.
                     m_cacheValid = false;
                 } );
    }
}

void SelectionContext::handleLayerWillBeRemoved( QgsMapLayer *layer )
{
    if ( !layer )
        return;
    DyingLayer tombstone;
    tombstone.guard = QPointer<QgsMapLayer>( layer );
    tombstone.raw = layer;
    m_dyingLayers.append( tombstone );
    m_cacheValid = false;
    if ( m_cached.activeLayer == layer )
        m_cached.activeLayer = nullptr;
    m_cached.selectedLayers.removeAll( layer );
    if ( m_canvas && m_canvas->currentLayer() == layer )
    {
        m_canvas->setCurrentLayer( nullptr );
    }
    // Re-broadcast immediately (not on the debounce): consumers re-query and
    // never observe the doomed pointer across the removal.
    refreshNow();
}

void SelectionContext::attachCanvas( QgsMapCanvas *canvas )
{
    if ( !canvas || canvas == m_canvas )
        return;
    m_canvas = canvas;
    connect( canvas, &QgsMapCanvas::currentLayerChanged, this, &SelectionContext::scheduleRefresh );
    connect( canvas, &QgsMapCanvas::layersChanged, this, &SelectionContext::scheduleRefresh );
}

void SelectionContext::attachLayerTree( QgsLayerTreeView *tree )
{
    if ( !tree || tree == m_layerTree )
        return;
    m_layerTree = tree;
    // A view without a QTreeView model has no selection model yet (headless
    // fixtures that skipped setModel()) — guard instead of connecting null.
    if ( QItemSelectionModel *selectionModel = tree->selectionModel() )
    {
        connect( selectionModel, &QItemSelectionModel::selectionChanged,
                 this, &SelectionContext::scheduleRefresh );
    }
}

void SelectionContext::attachWorkbenchHost( WorkbenchHost *host )
{
    if ( !host || host == m_workbenchHost )
        return;
    m_workbenchHost = host;
    connect( host, &WorkbenchHost::activeWorkbenchChanged,
             this, &SelectionContext::scheduleRefresh );
}

void SelectionContext::notifyAssetSelection( const QStringList &assetIds )
{
    if ( m_selectedAssetIds == assetIds )
        return;
    m_selectedAssetIds = assetIds;
    scheduleRefresh();
}

void SelectionContext::notifyGovernanceSelection( const QStringList &entityIds )
{
    if ( m_selectedResultIds == entityIds )
        return;
    m_selectedResultIds = entityIds;
    scheduleRefresh();
}

void SelectionContext::notifyDatasetSelection( const QStringList &datasetIds )
{
    if ( m_selectedDatasetIds == datasetIds )
        return;
    m_selectedDatasetIds = datasetIds;
    scheduleRefresh();
}

void SelectionContext::notifyExperimentSelection( const QStringList &runIds )
{
    if ( m_selectedExperimentIds == runIds )
        return;
    m_selectedExperimentIds = runIds;
    scheduleRefresh();
}

void SelectionContext::notifyModelSelection( const QStringList &modelNames )
{
    if ( m_selectedModelIds == modelNames )
        return;
    m_selectedModelIds = modelNames;
    scheduleRefresh();
}

void SelectionContext::notifyWorkflowSelection( const QStringList &runIds )
{
    if ( m_selectedWorkflowRunIds == runIds )
        return;
    m_selectedWorkflowRunIds = runIds;
    scheduleRefresh();
}

void SelectionContext::notifyMissionTaskSelection( const QString &taskId, MissionTaskStatus status )
{
    const QString trimmed = taskId.trimmed();
    if ( trimmed.isEmpty() )
    {
        if ( m_missionTaskId.isEmpty() )
            return;
        m_missionTaskId.clear();
    }
    else if ( m_missionTaskId == trimmed && m_missionTaskStatus == status )
    {
        return; // idempotent re-announce
    }
    else
    {
        m_missionTaskId = trimmed;
    }
    m_missionTaskStatus = status;
    scheduleRefresh();
}

void SelectionContext::setSarPredicate( SarPredicate predicate )
{
    m_sarPredicate = std::move( predicate );
    scheduleRefresh();
}

void SelectionContext::notifyPipelineNodeSelection( const QString &nodeId )
{
    const QString trimmed = nodeId.trimmed();
    if ( m_selectedPipelineNodeId == trimmed )
        return; // idempotent re-announce (also covers clear-on-clear)
    m_selectedPipelineNodeId = trimmed;
    scheduleRefresh();
}

void SelectionContext::setInFlightTaskPredicate( InFlightPredicate predicate )
{
    m_inFlightPredicate = std::move( predicate );
    scheduleRefresh();
}

SelectionContextSnapshot SelectionContext::snapshot() const
{
    // #778: a cached layer may have been destroyed without a removal signal
    // (tests, third-party code) — the guard mirror detects the corpse.
    if ( m_cacheValid && pruneDeadGuards( m_cachedLayerGuard ) )
        m_cacheValid = false;
    if ( !m_cacheValid )
    {
        m_cached = computeSnapshot();
        m_cachedLayerGuard.clear();
        if ( m_cached.activeLayer )
            m_cachedLayerGuard.append( QPointer<QgsMapLayer>( m_cached.activeLayer ) );
        for ( QgsMapLayer *layer : m_cached.selectedLayers )
        {
            if ( layer )
                m_cachedLayerGuard.append( QPointer<QgsMapLayer>( layer ) );
        }
        m_cacheValid = true;
    }
    return m_cached;
}

void SelectionContext::refreshNow()
{
    if ( m_debounce->isActive() )
        m_debounce->stop();
    // Retire tombstones: keep them while the doomed layer is alive, and while
    // the canvas could still report the (destroyed) pointer as its current
    // layer; drop the rest so the blacklist never grows unbounded and never
    // filters an unrelated layer that reused the address.
    const QgsMapLayer *canvasCurrentRaw = m_canvas ? m_canvas->currentLayer() : nullptr;
    for ( int i = m_dyingLayers.size() - 1; i >= 0; --i )
    {
        const DyingLayer &d = m_dyingLayers.at( i );
        if ( d.guard )
            continue; // doomed but alive — still must be filtered
        if ( canvasCurrentRaw && canvasCurrentRaw == d.raw )
            continue; // canvas can still produce the dangling pointer
        m_dyingLayers.removeAt( i );
    }
    m_cacheValid = false;
    emit changed( snapshot() );
}

void SelectionContext::scheduleRefresh()
{
    m_cacheValid = false;
    if ( !m_debounce->isActive() )
        m_debounce->start();
}

SelectionContextSnapshot SelectionContext::computeSnapshot() const
{
    SelectionContextSnapshot snap;

    // #849: the dying-layer probe below tests bare pointer membership in the
    // project's layer map — it must never dereference the possibly-freed
    // pointer. Materialize that pointer set once per pass; the previous
    // per-probe mapLayers().values().contains() copied the whole layer map
    // for every candidate layer.
    QSet<const void *> liveLayerPointers;
    if ( QgsProject::instance() )
    {
        const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
        liveLayerPointers.reserve( layers.size() );
        for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
            liveLayerPointers.insert( it.value() );
    }

    // #778: layers that announced removal are invisible to projections even
    // while they are still briefly reachable from the canvas / tree.
    const auto dying = [this, &liveLayerPointers]( QgsMapLayer *layer ) {
        if ( !layer )
            return true;
        for ( const DyingLayer &d : m_dyingLayers )
        {
            if ( d.guard && d.guard.data() == layer )
                return true; // doomed and still alive
            if ( !d.guard && d.raw == layer )
            {
                if ( liveLayerPointers.contains( layer ) )
                    continue; // valid new layer reusing the address
                return true; // destroyed — the pointer must never resurface
            }
        }
        return false;
    };

    if ( m_workbenchHost )
    {
        snap.workbenchId = m_workbenchHost->activeWorkbenchId();
        if ( IWorkbench *bench = m_workbenchHost->activeWorkbench() )
            bench->augmentSelectionContext( snap );
    }

    if ( m_canvas )
    {
        QgsMapLayer *current = m_canvas->currentLayer();
        snap.activeLayer = dying( current ) ? nullptr : current;
        snap.layerCount = m_canvas->layerCount();
    }

    if ( m_layerTree )
    {
        const QList<QgsMapLayer *> selected = m_layerTree->selectedLayers();
        for ( QgsMapLayer *layer : selected )
        {
            if ( !layer || dying( layer ) )
                continue;
            snap.selectedLayers.append( layer );
            if ( !layer->isValid() )
            {
                snap.hasBroken = true;
                continue;
            }
            switch ( layer->type() )
            {
                case Qgis::LayerType::Raster:
                {
                    snap.hasRaster = true;
                    if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
                    {
                        if ( raster->temporalProperties()->isActive() )
                            snap.hasTemporal = true;
                    }
                    break;
                }
                case Qgis::LayerType::Vector:
                {
                    snap.hasVector = true;
                    if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
                    {
                        if ( vector->isEditable() && layer == snap.activeLayer )
                        {
                            snap.activeEditable = true;
                            snap.activeModified = vector->isModified();
                        }
                    }
                    break;
                }
                case Qgis::LayerType::VectorTile:
                    break;
                default:
                    break;
            }
            if ( layer->dataProvider() && !layer->dataProvider()->isValid() )
                snap.hasBroken = true;
        }
    }

    // SAR predicate: override wins, else the conservative product-token probe.
    if ( !snap.hasSar )
    {
        for ( QgsMapLayer *layer : snap.selectedLayers )
        {
            const bool sar = m_sarPredicate ? m_sarPredicate( layer ) : sourceMentionsSar( layer );
            if ( sar )
            {
                snap.hasSar = true;
                break;
            }
        }
    }

    snap.selectedAssetIds = m_selectedAssetIds;
    snap.selectedResultIds = m_selectedResultIds;
    snap.selectedDatasetIds = m_selectedDatasetIds;
    snap.selectedExperimentIds = m_selectedExperimentIds;
    snap.selectedModelIds = m_selectedModelIds;
    snap.selectedWorkflowRunIds = m_selectedWorkflowRunIds;
    // Workbench 8.0: injected in-flight fact (shell binds TaskCenter).
    snap.hasInFlightTask = m_inFlightPredicate && m_inFlightPredicate();
    // Mission Runtime 13.0: the mission panel's task selection.
    snap.selectedMissionTaskId = m_missionTaskId;
    snap.selectedMissionTaskStatus = m_missionTaskStatus;
    snap.hasMissionTaskSelection = !m_missionTaskId.isEmpty();
    // RS14-15: the IR 2.0 canvas's selected node.
    snap.selectedPipelineNodeId = m_selectedPipelineNodeId;
    return snap;
}

} // namespace sicnu::app
