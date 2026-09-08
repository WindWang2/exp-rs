/***************************************************************************
 * selection_context.cpp — aggregation + pure availability rules
 ***************************************************************************/
#include "selection_context.h"

#include "workbench_host.h"

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayertemporalproperties.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>

#include <QMetaType>
#include <QTimer>

namespace sicnu::app
{

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
    else if ( commandId.startsWith( QStringLiteral( "raster." ) ) && !rasterSelected( s ) )
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
    if ( !m_cacheValid )
    {
        m_cached = computeSnapshot();
        m_cacheValid = true;
    }
    return m_cached;
}

void SelectionContext::refreshNow()
{
    if ( m_debounce->isActive() )
        m_debounce->stop();
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

    if ( m_workbenchHost )
    {
        snap.workbenchId = m_workbenchHost->activeWorkbenchId();
        if ( IWorkbench *bench = m_workbenchHost->activeWorkbench() )
            bench->augmentSelectionContext( snap );
    }

    if ( m_canvas )
    {
        snap.activeLayer = m_canvas->currentLayer();
        snap.layerCount = m_canvas->layerCount();
    }

    if ( m_layerTree )
    {
        const QList<QgsMapLayer *> selected = m_layerTree->selectedLayers();
        snap.selectedLayers = selected;
        for ( QgsMapLayer *layer : selected )
        {
            if ( !layer || !layer->isValid() )
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
