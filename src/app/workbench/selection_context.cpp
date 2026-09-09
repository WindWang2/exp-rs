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

QString unavailabilityReason( const SelectionContextSnapshot &s, const QString &commandId )
{
    // Milestone E: deterministic reasons over prerequisiteFacts — an empty
    // return means "available", a non-empty return is the user-facing
    // explanation the palette / tooltips show. Every prefix family that
    // DECLARES an availability predicate must have a case here.
    if ( commandId.startsWith( QStringLiteral( "layer.edit." ) ) )
    {
        if ( !vectorSelected( s ) )
            return QObject::tr( "需要选中矢量图层" );
        if ( !editingAvailable( s ) )
            return QObject::tr( "当前图层不可编辑" );
    }
    else if ( commandId.startsWith( QStringLiteral( "layer." ) ) && !layerSelected( s ) )
    {
        return QObject::tr( "需要选中图层" );
    }
    else if ( ( commandId.startsWith( QStringLiteral( "raster." ) )
                || commandId.startsWith( QStringLiteral( "rs." ) ) )
              && !rasterSelected( s ) )
    {
        return QObject::tr( "需要选中栅格图层" );
    }
    else if ( commandId.startsWith( QStringLiteral( "sar." ) ) && !sarSelected( s ) )
    {
        return QObject::tr( "需要选中 SAR 数据" );
    }
    else if ( commandId.startsWith( QStringLiteral( "result." ) ) && !resultSelected( s ) )
    {
        return QObject::tr( "需要选中治理结果" );
    }
    else if ( commandId.startsWith( QStringLiteral( "asset." ) ) && !assetSelected( s ) )
    {
        return QObject::tr( "需要选中数据资产" );
    }
    return QString();
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
    // #778: the project announces layer removal BEFORE the QgsMapLayer object
    // is destroyed — purge it from every projection right away. Batch removals
    // (removeMapLayers / clear) emit this per-layer signal for each layer.
    connect( QgsProject::instance(), qOverload<QgsMapLayer *>( &QgsProject::layerWillBeRemoved ),
             this, [this]( QgsMapLayer *layer ) { handleLayerWillBeRemoved( layer ); } );
    connect( QgsProject::instance(), qOverload<const QString &>( &QgsProject::layerWillBeRemoved ),
             this, [this]( const QString & ) {
                 // id-based announcement: the cached snapshot may borrow the
                 // doomed layer even if the object signal was not observed.
                 m_cacheValid = false;
             } );
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
    connect( tree->selectionModel(), &QItemSelectionModel::selectionChanged,
             this, &SelectionContext::scheduleRefresh );
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

void SelectionContext::setSarPredicate( SarPredicate predicate )
{
    m_sarPredicate = std::move( predicate );
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
    if ( !m_debounce->isActive() )
        m_debounce->start();
}

SelectionContextSnapshot SelectionContext::computeSnapshot() const
{
    SelectionContextSnapshot snap;

    // #778: layers that announced removal are invisible to projections even
    // while they are still briefly reachable from the canvas / tree.
    const auto dying = [this]( QgsMapLayer *layer ) {
        if ( !layer )
            return true;
        for ( const DyingLayer &d : m_dyingLayers )
        {
            if ( d.guard && d.guard.data() == layer )
                return true; // doomed and still alive
            if ( !d.guard && d.raw == layer )
                return true; // destroyed — the pointer must never resurface
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
    return snap;
}

} // namespace sicnu::app
