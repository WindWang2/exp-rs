#include "active_view_host.h"

#include <utility>

#include <qgsmapcanvas.h>
#include <qgsmessagebar.h>
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

ActiveViewHost::ActiveViewHost( QgsMapCanvas *canvas,
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
    // The display manager owns the active view id; align it with the main view
    // so both sides start from the same state.
    if ( m_displayManager )
        m_displayManager->setActiveViewId( mainViewId );

    if ( m_mapCanvas && m_overviewCanvas )
    {
        connect( m_mapCanvas, &QgsMapCanvas::layersChanged, this, [this] {
            if ( m_mapCanvas && m_overviewCanvas )
                m_overviewCanvas->setLayers( m_mapCanvas->layers() );
        } );
    }
}

ActiveViewHost::~ActiveViewHost() = default;

void ActiveViewHost::pushMessageBarAlert( const QString &title, const QString &text, Qgis::MessageLevel level )
{
    if ( m_messageBar )
    {
        m_messageBar->pushMessage( title, text, level );
    }
}



bool ActiveViewHost::setActiveViewId( sicnu::display::DisplayViewId viewId )
{
    if ( viewId.isNull() || !m_displayManager )
        return false;
    if ( !m_displayManager->view( viewId ).has_value() )
        return false;
    m_displayManager->setActiveViewId( viewId );
    return true;
}

void ActiveViewHost::setExtent( const QgsRectangle &extent )
{
  if ( m_mapCanvas )
  {
    m_mapCanvas->setExtent( extent );
    m_mapCanvas->refresh();
  }
}

void ActiveViewHost::setCenter( const QgsPointXY &center )
{
  if ( m_mapCanvas )
  {
    m_mapCanvas->setCenter( center );
    m_mapCanvas->refresh();
  }
}

void ActiveViewHost::setScale( double scale )
{
  if ( m_mapCanvas )
  {
    m_mapCanvas->zoomScale( scale );
  }
}

void ActiveViewHost::zoomToFullExtent()
{
  if ( m_mapCanvas )
  {
    m_mapCanvas->zoomToFullExtent();
  }
}

void ActiveViewHost::refreshCanvas()
{
  if ( m_mapCanvas )
  {
    m_mapCanvas->refresh();
  }
}

// ---------------------------------------------------------------------------
// Layer tree initialization
// ---------------------------------------------------------------------------

void ActiveViewHost::initLayerTree()
{
    QgsProject *project = QgsProject::instance();
    QgsLayerTree *root = project->layerTreeRoot();

    m_layerTreeModel = new QgsLayerTreeModel( root, this );

    m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegend );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegendAsTree );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::UseEmbeddedWidgets );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::UseTextFormatting );

    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeReorder );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeRename );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowNodeChangeVisibility );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::AllowLegendChangeState );
    m_layerTreeModel->setFlag( QgsLayerTreeModel::ActionHierarchical );

    m_layerTreeView->setLayerTreeModel( m_layerTreeModel );
    m_layerTreeView->setModel( m_layerTreeModel );
    m_layerTreeView->expandAll();
}

// ---------------------------------------------------------------------------
// Open path / display asset
// ---------------------------------------------------------------------------

sicnu::data::Result<sicnu::display::DisplayLayerId>
ActiveViewHost::openPath( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.canonicalSource = filePath;
    const auto loaded = openSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Open Path" ), loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
ActiveViewHost::openRasterPath( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = filePath;
    const auto loaded = openSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Open Raster" ), loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
ActiveViewHost::openVectorPath( const QString &filePath )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "ogr" );
    source.canonicalSource = filePath;
    const auto loaded = openSource( std::move( source ) );
    if ( !loaded )
        reportDiagnostics( QObject::tr( "Open Vector" ), loaded.diagnostics() );
    return loaded;
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
ActiveViewHost::displayAsset( sicnu::data::AssetId assetId )
{
    using sicnu::data::Diagnostic;
    using sicnu::data::DiagnosticSeverity;
    using sicnu::data::Result;
    using sicnu::display::DisplayLayerId;

    if ( !m_mapCanvas || !m_dataManager || !m_displayManager
         || activeViewId().isNull() || assetId.isNull() )
    {
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "view.context_unavailable" ),
                        QObject::tr( "The active Display View context is unavailable" ),
                        DiagnosticSeverity::Error } );
    }

    if ( !m_dataManager->asset( assetId ).has_value() )
    {
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "view.asset_missing" ),
                        QObject::tr( "Data Asset is not in the project catalog" ),
                        DiagnosticSeverity::Error } );
    }

    const bool hadVisibleLayers = !m_mapCanvas->layers().isEmpty();
    const Result<DisplayLayerId> displayed =
        m_displayManager->addLayer( activeViewId(), assetId );
    if ( !displayed )
        return displayed;

    QgsMapLayer *layer = m_displayManager->mapLayer( displayed.value() );
    const std::optional<sicnu::data::AssetSnapshot> asset =
        m_dataManager->asset( assetId );
    if ( !layer || !asset )
    {
        ( void ) m_displayManager->removeLayer( displayed.value() );
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "view.display_adapter_missing" ),
                        QObject::tr( "The QGIS display adapter was not created" ),
                        DiagnosticSeverity::Error } );
    }

    placeInTreeGroup( layer, asset->kind() );
    refreshCanvasLayers();
    if ( !hadVisibleLayers )
        m_mapCanvas->setExtent( layer->extent() );

    return Result<DisplayLayerId>::success( displayed.value() );
}

sicnu::data::Result<sicnu::display::DisplayLayerId>
ActiveViewHost::openSource( sicnu::data::SourceDescriptor source )
{
    using sicnu::data::Diagnostic;
    using sicnu::data::DiagnosticSeverity;
    using sicnu::data::Result;
    using sicnu::display::DisplayLayerId;

    if ( !m_mapCanvas || !m_dataManager || !m_displayManager
         || activeViewId().isNull() )
    {
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "view.context_unavailable" ),
                        QObject::tr( "The active Display View context is unavailable" ),
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
            Diagnostic{ QStringLiteral( "view.registration_failed" ),
                        QObject::tr( "The source could not be registered" ),
                        DiagnosticSeverity::Error } );
    }

    const Result<DisplayLayerId> displayed =
        m_displayManager->addLayer( activeViewId(), registered.assetId );
    if ( !displayed )
        return displayed;

    QgsMapLayer *layer = m_displayManager->mapLayer( displayed.value() );
    const std::optional<sicnu::data::AssetSnapshot> asset =
        m_dataManager->asset( registered.assetId );
    if ( !layer || !asset )
    {
        ( void ) m_displayManager->removeLayer( displayed.value() );
        return Result<DisplayLayerId>::failure(
            Diagnostic{ QStringLiteral( "view.display_adapter_missing" ),
                        QObject::tr( "The QGIS display adapter was not created" ),
                        DiagnosticSeverity::Error } );
    }

    placeInTreeGroup( layer, asset->kind() );
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

void ActiveViewHost::placeInTreeGroup( QgsMapLayer *layer, sicnu::data::AssetKind kind )
{
    if ( !layer )
        return;
    // Tree grouping applies to the main QGIS project tree (main view). Secondary
    // views own independent trees via DisplayManager; host only groups main.
    if ( activeViewId() != m_mainViewId )
        return;

    const QString groupName =
        kind == sicnu::data::AssetKind::Raster
            ? QObject::tr( "Raster Layers" )
            : QObject::tr( "Vector Layers" );
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    // Insert new node FIRST, then remove old — see QgsLayerTreeRegistryBridge note.
    QgsLayerTreeLayer *oldNode = root->findLayer( layer->id() );
    findOrCreateGroup( groupName )->addLayer( layer );
    if ( oldNode )
    {
        if ( QgsLayerTreeGroup *parent =
                 qobject_cast<QgsLayerTreeGroup *>( oldNode->parent() ) )
            parent->removeChildNode( oldNode );
    }
}

void ActiveViewHost::reportDiagnostics(
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
// Display-layer operations
// ---------------------------------------------------------------------------

void ActiveViewHost::showLayerProperties( QgsMapLayer *layer )
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

void ActiveViewHost::removeSelectedDisplayLayers()
{
    QList<QgsMapLayer *> selected = selectedLayers();
    if ( selected.isEmpty() )
    {
        if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
            win->statusBar()->showMessage( QObject::tr( "No layer selected" ), 2000 );
        return;
    }

    // Safety-first: confirm before removing (legacy QGIS layers are removed from project).
    QStringList names;
    for ( QgsMapLayer *layer : selected )
    {
        if ( layer )
            names.append( layer->name() );
    }
    QString detail = QObject::tr( "从显示移除选中的 %1 个图层？\n（数据资产保留在工程中，仅移除显示；"
                                  "外部 QGIS 图层将从工程移除。）" ).arg( selected.size() );
    if ( names.size() <= 5 )
        detail += QStringLiteral( "\n\n" ) + names.join( QStringLiteral( "\n" ) );
    else
        detail += QStringLiteral( "\n\n" ) + names.mid( 0, 5 ).join( QStringLiteral( "\n" ) )
                  + QObject::tr( "\n…及其余 %1 个" ).arg( names.size() - 5 );
    const auto choice = QMessageBox::question(
        m_parentWidget, QObject::tr( "移除图层" ), detail,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( choice != QMessageBox::Yes )
        return;

    for ( QgsMapLayer *layer : selected )
    {
        const std::optional<sicnu::display::DisplayLayerId> displayLayerId =
            sicnu::display::DisplayLayerId::fromMapLayer( layer );
        if ( displayLayerId && m_displayManager )
        {
            const sicnu::data::Result<void> removed =
                m_displayManager->removeLayer( *displayLayerId );
            if ( !removed )
                reportDiagnostics( QObject::tr( "Remove Display Layer" ),
                                   removed.diagnostics() );
        }
        else
        {
            // Legacy/external QGIS layers: presentation-only removal.
            QgsProject::instance()->removeMapLayer( layer->id() );
        }
    }
    refreshCanvasLayers();

    if ( auto *win = qobject_cast<QMainWindow *>( m_parentWidget ) )
        win->statusBar()->showMessage( QObject::tr( "Removed from view (data kept)" ), 2000 );
}

void ActiveViewHost::zoomToLayer( QgsMapLayer *layer )
{
    if ( !m_mapCanvas )
        return;

    QgsMapLayer *target = layer ? layer : activeLayer();
    if ( !target )
        return;

    m_mapCanvas->setExtent( target->extent() );
    m_mapCanvas->refresh();
}

void ActiveViewHost::zoomToNativeResolution( QgsMapLayer *layer )
{
    if ( !m_mapCanvas )
        return;

    QgsMapLayer *target = layer ? layer : activeLayer();
    if ( !target )
        return;

    QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>( target );
    if ( !rl )
    {
        zoomToLayer( target );
        return;
    }

    double xRes = rl->rasterUnitsPerPixelX();
    double yRes = rl->rasterUnitsPerPixelY();
    QgsRectangle ext = rl->extent();
    double cx = ( ext.xMinimum() + ext.xMaximum() ) / 2.0;
    double cy = ( ext.yMinimum() + ext.yMaximum() ) / 2.0;
    double w = m_mapCanvas->width() * xRes;
    double h = m_mapCanvas->height() * yRes;
    m_mapCanvas->setExtent( QgsRectangle( cx - w / 2.0, cy - h / 2.0, cx + w / 2.0, cy + h / 2.0 ) );
    m_mapCanvas->refresh();
}

void ActiveViewHost::refreshCanvasLayers()
{
    if ( !m_mapCanvas )
        return;

    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QList<QgsMapLayer *> layers = root->checkedLayers();
    m_mapCanvas->setLayers( layers );

    if ( m_overviewCanvas )
        m_overviewCanvas->setLayers( layers );
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------



QgsMapLayer *ActiveViewHost::activeLayer() const
{
    if ( m_mapCanvas && m_mapCanvas->currentLayer() )
        return m_mapCanvas->currentLayer();
    const QList<QgsMapLayer *> layers = selectedLayers();
    return layers.isEmpty() ? nullptr : layers.first();
}

QList<QgsMapLayer *> ActiveViewHost::selectedLayers() const
{
    QList<QgsMapLayer *> result;
    if ( !m_layerTreeView || !m_layerTreeView->selectionModel() )
        return result;
    QModelIndexList selected = m_layerTreeView->selectionModel()->selectedIndexes();
    for ( const QModelIndex &idx : selected )
    {
        QgsLayerTreeNode *node = m_layerTreeView->index2node( idx );
        if ( node && node->nodeType() == QgsLayerTreeNode::NodeLayer )
        {
            QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>( node );
            if ( layerNode->layer() )
                result.append( layerNode->layer() );
        }
    }
    return result;
}

QgsLayerTreeGroup *ActiveViewHost::findOrCreateGroup( const QString &name )
{
    QgsLayerTree *root = QgsProject::instance()->layerTreeRoot();
    QgsLayerTreeGroup *group = root->findGroup( name );
    if ( !group )
        group = root->addGroup( name );
    return group;
}
