#include "layer_manager.h"

#include <utility>

#include <qgsmapcanvas.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapoverviewcanvas.h>
#include <qgsproject.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

// Layer properties dialogs
#include <raster/qgsrasterlayerproperties.h>
#include <vector/qgsvectorlayerproperties.h>

#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QDebug>
#include <QStringList>

#include "data/data_manager.h"
#include "data/source_descriptor.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LayerManager::LayerManager( QgsMapCanvas *canvas,
                            QgsLayerTreeView *treeView,
                            QgsMapOverviewCanvas *overviewCanvas,
                            sicnu::data::DataManager *dataManager,
                            sicnu::display::QgisDisplayManager *displayManager,
                            sicnu::display::DisplayViewId mainViewId,
                            QWidget *parentWidget )
    : QObject( parentWidget )
    , m_mapCanvas( canvas )
    , m_layerTreeView( treeView )
    , m_overviewCanvas( overviewCanvas )
    , m_dataManager( dataManager )
    , m_displayManager( displayManager )
    , m_mainViewId( mainViewId )
    , m_parentWidget( parentWidget )
{
    if ( m_mapCanvas && m_overviewCanvas )
    {
        connect( m_mapCanvas, &QgsMapCanvas::layersChanged, this, [this] {
            if ( m_mapCanvas && m_overviewCanvas )
                m_overviewCanvas->setLayers( m_mapCanvas->layers() );
        } );
    }
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

}

// ---------------------------------------------------------------------------
// Layer loading (programmatic)
// ---------------------------------------------------------------------------

sicnu::data::Result<sicnu::display::DisplayLayerId>
LayerManager::loadLayer( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.canonicalSource = filePath;
    const auto loaded = loadSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Load Layer" ), loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
LayerManager::loadRasterLayer( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = filePath;
    const auto loaded = loadSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Load Raster Layer" ),
                           loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
LayerManager::loadVectorLayer( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "ogr" );
    source.canonicalSource = filePath;
    const auto loaded = loadSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Load Vector Layer" ),
                           loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
LayerManager::loadSource( sicnu::data::SourceDescriptor source )
{
    using sicnu::data::Diagnostic;
    using sicnu::data::DiagnosticSeverity;
    using sicnu::data::Result;
    using sicnu::display::DisplayLayerId;

    if ( !m_mapCanvas || !m_dataManager || !m_displayManager
         || m_mainViewId.isNull() )
    {
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "layer.context_unavailable" ),
                        QObject::tr( "The project Data Context is unavailable" ),
                        DiagnosticSeverity::Error } );
    }

    const bool hadVisibleLayers = !m_mapCanvas->layers().isEmpty();
    const sicnu::data::RegisterResult registered =
        m_dataManager->registerSource(
            sicnu::data::RegisterRequest{ std::move( source ) } );
    if ( registered.assetId.isNull() )
    {
        if ( !registered.diagnostics.isEmpty() )
            return Result<DisplayLayerId>::failure( registered.diagnostics );
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "layer.registration_failed" ),
                        QObject::tr( "The source could not be registered" ),
                        DiagnosticSeverity::Error } );
    }

    const Result<DisplayLayerId> displayed =
        m_displayManager->addLayer( m_mainViewId, registered.assetId );
    if ( !displayed )
        return displayed;

    QgsMapLayer *layer = m_displayManager->mapLayer( displayed.value() );
    const std::optional<sicnu::data::AssetSnapshot> asset =
        m_dataManager->asset( registered.assetId );
    if ( !layer || !asset )
    {
        ( void ) m_displayManager->removeLayer( displayed.value() );
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "layer.display_adapter_missing" ),
                        QObject::tr( "The QGIS display adapter was not created" ),
                        DiagnosticSeverity::Error } );
    }

    const QString groupName =
        asset->kind() == sicnu::data::AssetKind::Raster
            ? QObject::tr( "Raster Layers" )
            : QObject::tr( "Vector Layers" );
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    if ( QgsLayerTreeLayer *node = root->findLayer( layer->id() ) )
    {
        if ( QgsLayerTreeGroup *parent =
                 qobject_cast<QgsLayerTreeGroup *>( node->parent() ) )
            parent->removeChildNode( node );
    }
    findOrCreateGroup( groupName )->addLayer( layer );

    refreshCanvasLayers();
    if ( !hadVisibleLayers )
        m_mapCanvas->setExtent( layer->extent() );

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
    {
        if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
        {
            win->statusBar()->showMessage(
                QObject::tr( "Loaded: %1 (%2x%3, %4 bands)" )
                    .arg( layer->name() )
                    .arg( raster->width() )
                    .arg( raster->height() )
                    .arg( raster->bandCount() ),
                3000 );
        }
        else if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
        {
            win->statusBar()->showMessage(
                QObject::tr( "Loaded: %1 (%2 features)" )
                    .arg( layer->name() )
                    .arg( vector->featureCount() ),
                3000 );
        }
    }

    return Result<DisplayLayerId>::success( displayed.value(),
                                            registered.diagnostics );
}

void LayerManager::reportDiagnostics(
    const QString &title,
    const QVector<sicnu::data::Diagnostic> &diagnostics )
{
    QStringList details;
    details.reserve( diagnostics.size() );
    for ( const sicnu::data::Diagnostic &diagnostic : diagnostics )
    {
        details.append( QStringLiteral( "[%1] %2" )
                            .arg( diagnostic.code, diagnostic.message ) );
    }
    if ( details.isEmpty() )
        details.append( QObject::tr( "The operation failed without details" ) );

    if ( m_parentWidget )
        QMessageBox::warning( m_parentWidget, title, details.join( '\n' ) );
    else
        qWarning().noquote() << title << details.join( '\n' );
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
        const std::optional<sicnu::display::DisplayLayerId> displayLayerId =
            sicnu::display::DisplayLayerId::fromString(
                layer->customProperty(
                         QStringLiteral( "sicnu/displayLayerId" ) )
                    .toString() );
        if ( displayLayerId && m_displayManager )
        {
            const sicnu::data::Result<void> removed =
                m_displayManager->removeLayer( *displayLayerId );
            if ( !removed )
                reportDiagnostics( QObject::tr( "Remove Layer" ),
                                   removed.diagnostics() );
        }
        else
        {
            // Standard/external QGIS layers are still presentation-only here.
            QgsProject::instance()->removeMapLayer( layer->id() );
        }
    }
    refreshCanvasLayers();

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
        win->statusBar()->showMessage( QObject::tr( "Layer removed" ), 2000 );
}

void LayerManager::refreshCanvasLayers()
{
    if ( !m_mapCanvas )
        return;

    // The Display Manager owns the main tree/canvas bridge. This explicit
    // synchronization retains compatibility for callers that request a refresh.
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QList<QgsMapLayer *> layers = root->checkedLayers();
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
