/***************************************************************************
 * rs_session_map_workspace.cpp
 ***************************************************************************/
#include "rs_session_map_workspace.h"

#include "qgslayertree.h"
#include "qgslayertreegroup.h"
#include "qgslayertreelayer.h"
#include "qgslayertreemapcanvasbridge.h"
#include "qgslayertreemodel.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayer.h"
#include "qgsmaplayerstore.h"
#include "qgsrectangle.h"

RsSessionMapWorkspace::RsSessionMapWorkspace( QgsMapCanvas *canvas, QObject *parent )
  : QObject( parent )
  , m_canvas( canvas )
{
  m_layerStore = new QgsMapLayerStore( this );

  // Root node is not owned by QgsLayerTreeModel — parent to this for lifetime.
  m_layerTree = new QgsLayerTree();
  m_layerTree->setObjectName( QStringLiteral( "rsSessionLayerTree" ) );
  m_layerTree->setParent( this );
  m_layerTreeModel = new QgsLayerTreeModel( m_layerTree, this );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegend, true );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegendAsTree, true );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeReorder, true );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeRename, true );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeChangeVisibility, true );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowLegendChangeState, true );

  if ( m_canvas )
  {
    m_layerTreeBridge = new QgsLayerTreeMapCanvasBridge( m_layerTree, m_canvas, this );
    // Session-local stack: do not zoom/set QgsProject CRS from the first layer
    // (classify seeds an empty memory sample layer before a real source opens).
    m_layerTreeBridge->setAutoSetupOnFirstLayer( false );
  }
}

RsSessionMapWorkspace::~RsSessionMapWorkspace() = default;

void RsSessionMapWorkspace::syncCanvasLayers()
{
  if ( m_layerTreeBridge )
  {
    // Synchronous membership update (bridge otherwise queues via deferredSetCanvasLayers).
    m_layerTreeBridge->setCanvasLayers();
    return;
  }
  if ( m_canvas )
    m_canvas->refresh();
}

void RsSessionMapWorkspace::addLayer( QgsMapLayer *layer, bool insertOnTop )
{
  if ( !layer || !m_layerTree )
    return;

  // Always repair store membership first (covers tree-present/store-missing).
  if ( m_layerStore && !m_layerStore->mapLayer( layer->id() ) )
    m_layerStore->addMapLayer( layer );

  // Avoid duplicate nodes for the same layer
  if ( m_layerTree->findLayer( layer->id() ) )
  {
    syncCanvasLayers();
    return;
  }

  if ( insertOnTop )
    m_layerTree->insertLayer( 0, layer );
  else
    m_layerTree->addLayer( layer );

  syncCanvasLayers();
}

void RsSessionMapWorkspace::removeLayer( QgsMapLayer *layer )
{
  if ( !layer || !m_layerTree )
    return;

  QgsLayerTreeLayer *node = m_layerTree->findLayer( layer->id() );
  if ( node )
  {
    if ( auto *group = qobject_cast<QgsLayerTreeGroup *>( node->parent() ) )
      group->removeChildNode( node );
  }

  // Detach from store without deleting (R2: caller owns the layer afterward;
  // re-add after remove must work for z-order shuffle).
  if ( m_layerStore && m_layerStore->mapLayer( layer->id() ) )
    m_layerStore->takeMapLayer( layer );

  syncCanvasLayers();
}

void RsSessionMapWorkspace::setExtent( const QgsRectangle &extent )
{
  if ( !m_canvas )
    return;
  m_canvas->setExtent( extent );
  m_canvas->refresh();
}

void RsSessionMapWorkspace::zoomToLayer( QgsMapLayer *layer )
{
  if ( !m_canvas || !layer || !layer->isValid() )
    return;
  const QgsRectangle ext = layer->extent();
  if ( ext.isEmpty() )
    return;
  m_canvas->setExtent( ext );
  m_canvas->refresh();
}

void RsSessionMapWorkspace::setCurrentLayer( QgsMapLayer *layer )
{
  if ( !m_canvas )
    return;
  m_canvas->setCurrentLayer( layer );
}
