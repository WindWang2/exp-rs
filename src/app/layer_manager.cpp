#include "layer_manager.h"

#include <qgsmapcanvas.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgslayertreemapcanvasbridge.h>
#include <qgsmapoverviewcanvas.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

// Layer properties dialogs
#include <raster/qgsrasterlayerproperties.h>
#include <vector/qgsvectorlayerproperties.h>

#include <QFileInfo>
#include <QFileDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>

#include "app_paths.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LayerManager::LayerManager( QgsMapCanvas *canvas,
                            QgsLayerTreeView *treeView,
                            QgsMapOverviewCanvas *overviewCanvas,
                            QWidget *parentWidget )
    : QObject( parentWidget )
    , m_mapCanvas( canvas )
    , m_layerTreeView( treeView )
    , m_overviewCanvas( overviewCanvas )
    , m_parentWidget( parentWidget )
{
}

LayerManager::~LayerManager() = default;

// ---------------------------------------------------------------------------
// Layer tree initialization
// ---------------------------------------------------------------------------

void LayerManager::initLayerTree()
{
    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    // Create layer tree model with QGIS-compatible flags
    m_layerTreeModel = new QgsLayerTreeModel( root, this );

    // Display flags (matching QGIS defaults)
    m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegend );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegendAsTree );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::UseEmbeddedWidgets );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::UseTextFormatting );

    // Behavioral flags (matching QGIS defaults)
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeReorder );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeRename );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeChangeVisibility );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowLegendChangeState );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::ActionHierarchical );

    m_layerTreeView->setLayerTreeModel( m_layerTreeModel );
    m_layerTreeView->setModel( m_layerTreeModel );

    // Expand all nodes by default (QGIS behavior)
    m_layerTreeView->expandAll();

    // Bridge: automatic layer tree -> canvas synchronization
    m_layerTreeBridge = new QgsLayerTreeMapCanvasBridge( root, m_mapCanvas, this );
    connect( m_layerTreeBridge, &QgsLayerTreeMapCanvasBridge::canvasLayersChanged,
             this, [this]() {
                 if ( m_overviewCanvas )
                     m_overviewCanvas->setLayers( m_mapCanvas->layers() );
             } );
}

// ---------------------------------------------------------------------------
// Layer loading (programmatic)
// ---------------------------------------------------------------------------

void LayerManager::loadRasterLayer( const QString &filePath )
{
    QFileInfo fi( filePath );
    QString name = fi.fileName();

    QgsRasterLayer *layer = new QgsRasterLayer( filePath, name, "gdal" );

    if ( !layer->isValid() )
    {
        QMessageBox::warning( m_parentWidget, QObject::tr( "Load Layer" ),
                              QObject::tr( "Failed to load raster layer:\n%1\n\nError: %2" )
                                  .arg( filePath, layer->error().message() ) );
        delete layer;
        return;
    }

    QgsProject::instance()->addMapLayer( layer, /*addToLegend=*/false );

    QgsLayerTreeGroup *group = findOrCreateGroup( "Raster Layers" );
    group->addLayer( layer );

    // Only zoom to new layer if canvas has no other visible layers
    if ( m_mapCanvas->layers().isEmpty() )
        m_mapCanvas->setExtent( layer->extent() );
    refreshCanvasLayers();

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
        win->statusBar()->showMessage( QObject::tr( "Loaded: %1 (%2x%3, %4 bands)" )
                                           .arg( name )
                                           .arg( layer->width() )
                                           .arg( layer->height() )
                                           .arg( layer->bandCount() ),
                                       3000 );
}

void LayerManager::loadVectorLayer( const QString &filePath )
{
    QFileInfo fi( filePath );
    QString name = fi.fileName();

    QgsVectorLayer *layer = new QgsVectorLayer( filePath, name, "ogr" );

    if ( !layer->isValid() )
    {
        QMessageBox::warning( m_parentWidget, QObject::tr( "Load Layer" ),
                              QObject::tr( "Failed to load vector layer:\n%1\n\nError: %2" )
                                  .arg( filePath, layer->error().message() ) );
        delete layer;
        return;
    }

    QgsProject::instance()->addMapLayer( layer, /*addToLegend=*/false );

    QgsLayerTreeGroup *group = findOrCreateGroup( "Vector Layers" );
    group->addLayer( layer );

    // Only zoom to new layer if canvas has no other visible layers
    if ( m_mapCanvas->layers().isEmpty() )
        m_mapCanvas->setExtent( layer->extent() );
    refreshCanvasLayers();

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
        win->statusBar()->showMessage( QObject::tr( "Loaded: %1 (%2 features)" )
                                           .arg( name )
                                           .arg( layer->featureCount() ),
                                       3000 );
}

// ---------------------------------------------------------------------------
// Layer operations
// ---------------------------------------------------------------------------

void LayerManager::showLayerProperties( QgsMapLayer *layer )
{
    if ( !layer )
        return;

    if ( layer->type() == Qgis::LayerType::Raster )
    {
        QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer *>( layer );
        if ( rasterLayer )
        {
            QgsRasterLayerProperties dialog( rasterLayer, m_mapCanvas, m_parentWidget );
            dialog.exec();
            m_mapCanvas->refresh();
        }
    }
    else if ( layer->type() == Qgis::LayerType::Vector )
    {
        QgsVectorLayer *vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
        if ( vectorLayer )
        {
            QgsVectorLayerProperties dialog( m_mapCanvas, nullptr, vectorLayer, m_parentWidget );
            dialog.exec();
            m_mapCanvas->refresh();
        }
    }
}

void LayerManager::removeSelectedLayers()
{
    QList<QgsMapLayer *> selected = selectedLayers();
    if ( selected.isEmpty() )
    {
        if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
            win->statusBar()->showMessage( QObject::tr( "No layer selected" ), 2000 );
        return;
    }

    for ( QgsMapLayer *layer : selected )
    {
        QgsProject::instance()->removeMapLayer( layer->id() );
    }
    refreshCanvasLayers();

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
        win->statusBar()->showMessage( QObject::tr( "Layer removed" ), 2000 );
}

void LayerManager::refreshCanvasLayers()
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QList<QgsMapLayer *> layers = root->layerOrder();
    m_mapCanvas->setLayers( layers );

    // Keep overview canvas in sync with the main canvas layers
    if ( m_overviewCanvas )
    {
        m_overviewCanvas->setLayers( layers );
    }
}

// ---------------------------------------------------------------------------
// Layer queries
// ---------------------------------------------------------------------------

QgsMapLayer *LayerManager::activeLayer()
{
    // Priority: canvas current layer -> tree selection
    if ( m_mapCanvas && m_mapCanvas->currentLayer() )
        return m_mapCanvas->currentLayer();
    QList<QgsMapLayer *> layers = selectedLayers();
    return layers.isEmpty() ? nullptr : layers.first();
}

QList<QgsMapLayer *> LayerManager::selectedLayers()
{
    QList<QgsMapLayer *> result;
    QModelIndexList selected = m_layerTreeView->selectionModel()->selectedIndexes();
    for ( const QModelIndex &idx : selected )
    {
        QgsLayerTreeNode *node = m_layerTreeView->index2node( idx );
        if ( node && node->nodeType() == QgsLayerTreeNode::NodeLayer )
        {
            QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>( node );
            if ( layerNode->layer() )
            {
                result.append( layerNode->layer() );
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

QgsLayerTreeGroup *LayerManager::findOrCreateGroup( const QString &name )
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QgsLayerTreeGroup *group = root->findGroup( name );
    if ( !group )
    {
        group = root->addGroup( name );
    }
    return group;
}
