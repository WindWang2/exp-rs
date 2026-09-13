// main_window_menus.cpp — Menu bar, toolbars, and status bar setup
// Extracted from main_window.cpp for maintainability
#include "main_window.h"
#include "workbench/command_registry.h"

#include "app/help/help_system_controller.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/extract_band_dialog.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QLabel>
#include <QSlider>
#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QStyle>
#include <QSignalBlocker>
#include <QTimer>
#include <QWhatsThis>

#include <qgsmaplayer.h>
#include <qgsmapcanvas.h>

#include <qgsrasterlayer.h>


namespace {
void tip( QAction *a, const QString &t )
{
  if ( !a )
    return;
  a->setToolTip( t );
  a->setStatusTip( t );
  a->setWhatsThis( t );
}

/// Load app icon from :/icons/<name>
QIcon ic( const char *name )
{
  return QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( name ) );
}

/// Qt standard icon (undo/redo/clipboard when no custom asset)
QIcon stdIc( QStyle::StandardPixmap sp )
{
  if ( qApp && qApp->style() )
    return qApp->style()->standardIcon( sp );
  return {};
}

void setMenuIcon( QMenu *m, const QIcon &icon )
{
  if ( m && m->menuAction() )
    m->menuAction()->setIcon( icon );
}
} // namespace

QMenuBar *QgisDesktopWindow::appMenuBar()
{
    // Never call QMainWindow::menuBar() after product chrome is installed:
    // menuBar() creates a real menubar and Qt deletes setMenuWidget() chrome.
    if ( !m_hiddenMenuBar )
    {
        m_hiddenMenuBar = new QMenuBar( this );
        m_hiddenMenuBar->setObjectName( QStringLiteral( "rsHiddenMenuBar" ) );
        m_hiddenMenuBar->setNativeMenuBar( false );
        m_hiddenMenuBar->hide();
        m_hiddenMenuBar->setMaximumHeight( 0 );
    }
    return m_hiddenMenuBar;
}

void QgisDesktopWindow::setupMenu()
{
    // Brand logo (left corner) — app icon + short product name
    QWidget *brandWidget = new QWidget(this);
    brandWidget->setObjectName("rsMenuBarBrand");
    QHBoxLayout *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(8, 0, 0, 0);
    brandLayout->setSpacing(6);
    QLabel *logo = new QLabel;
    logo->setObjectName("rsBrandLogo");
    {
        const QIcon ic(QStringLiteral(":/icons/app_icon"));
        if (!ic.isNull())
            logo->setPixmap(ic.pixmap(22, 22));
        else
            logo->setText(QStringLiteral("RS"));
    }
    QLabel *name = new QLabel("RS Studio");
    name->setObjectName("rsBrandName");
    brandLayout->addWidget(logo);
    brandLayout->addWidget(name);
    appMenuBar()->setCornerWidget(brandWidget, Qt::TopLeftCorner);

    // Version label (right corner)
    QLabel *versionLabel = new QLabel("v0.9.2-dev");
    versionLabel->setObjectName("rsBrandVersion");
    appMenuBar()->setCornerWidget(versionLabel, Qt::TopRightCorner);

    // Helper: enable tooltips on every top-level / submenu we create.
    auto makeMenu = []( QMenu *m ) -> QMenu * {
        if ( m )
            m->setToolTipsVisible( true );
        return m;
    };

    // Workbench 9.0 M2: registry-backed menu items. CommandRegistry is the
    // single authority for command ids and shortcut ownership — a menu item
    // whose command exists in the registry MUST be the registry projection,
    // never a parallel QAction re-claiming the same binding (the duplicate
    // owner made the registry's canonical-shortcut record a lie and any
    // second installShortcut an ambiguous-shortcut trap).
    auto addCmd = [this]( QMenu *menu, const char *commandId ) -> QAction * {
        QAction *act = m_commandRegistry->action( QString::fromLatin1( commandId ),
                                                  /*installShortcut=*/true );
        if ( act )
            menu->addAction( act );
        return act;
    };

    // ------------------------------------------------------------------
    // 工程 Project — file I/O, data import, layout, quit
    // ------------------------------------------------------------------
    QMenu *projectMenu = makeMenu( appMenuBar()->addMenu( tr( "&Project" ) ) );
    tip( addCmd( projectMenu, "project.new" ),
         tr( "Create an empty project, clearing current layers and view state." ) );
    tip( addCmd( projectMenu, "project.open" ),
         tr( "Opens a saved project file." ) );
    tip( addCmd( projectMenu, "project.save" ),
         tr( "Saves the current project to its existing path." ) );
    tip( addCmd( projectMenu, "project.saveAs" ),
         tr( "Saves the project as a new file." ) );
    projectMenu->addSeparator();
    tip( addCmd( projectMenu, "project.importLayer" ),
         tr( "Import raster or vector layers into the project." ) );
    tip( projectMenu->addAction( ic( "i_ort" ), tr( "Import Product..." ),
                                 this, [this]() { openProductImportDialog( QStringLiteral( "auto" ) ); } ),
         tr( "Import Landsat / Sentinel-2 / MODIS scenes as products: preview bands / grid groups and import as data collections." ) );
    // Per-family entries for the two modern optical product lines.
    tip( projectMenu->addAction( ic( "i_ort" ), tr( "Import Landsat Product..." ),
                                 this, [this]() { openProductImportDialog( QStringLiteral( "landsat" ) ); } ),
         tr( "Import a Landsat scene as a product (directory containing *_MTL.txt)." ) );
    tip( projectMenu->addAction( ic( "i_ort" ), tr( "Import Sentinel-2 Product..." ),
                                 this, [this]() { openProductImportDialog( QStringLiteral( "sentinel2" ) ); } ),
         tr( "Import a Sentinel-2 SAFE product (a .SAFE directory containing MTD_MSI*.xml)." ) );
    tip( addCmd( projectMenu, "project.stacBrowse" ),
         tr( "Browses STAC catalogs to find remote-sensing data." ) );
    projectMenu->addSeparator();
    tip( addCmd( projectMenu, "project.newLayout" ),
         tr( "Create a print layout / map product." ) );
    tip( projectMenu->addAction( ic( "re_ort" ), tr( "Export Experiment Report..." ),
                                 this, &QgisDesktopWindow::exportLabReport ),
         tr( "Export course / lab reports." ) );
    projectMenu->addSeparator();
    tip( addCmd( projectMenu, "project.exit" ),
         tr( "Quits the application." ) );

    // ------------------------------------------------------------------
    // 编辑 Edit — feature edit + 数字化 as submenu (no longer top-level)
    // ------------------------------------------------------------------
    QMenu *editMenu = makeMenu( appMenuBar()->addMenu( tr( "&Edit" ) ) );
    m_toggleEditingAction = addCmd( editMenu, "layer.toggleEditing" );
        tip( m_toggleEditingAction, tr( "Toggles editing of the current vector layer." ) );
    m_saveEditsAction = editMenu->addAction(
      ic( "mActionSaveEdits" ), tr( "Save Edits" ),
      this, &QgisDesktopWindow::saveEdits );
    m_saveEditsAction->setEnabled( false );
    tip( m_saveEditsAction, tr( "Saves vector edits." ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( stdIc( QStyle::SP_ArrowBack ), tr( "Undo" ),
                              QKeySequence::Undo, this, &QgisDesktopWindow::undo ),
         tr( "Undoes the last edit." ) );
    tip( editMenu->addAction( stdIc( QStyle::SP_ArrowForward ), tr( "Redo" ),
                              QKeySequence::Redo, this, &QgisDesktopWindow::redo ),
         tr( "Redoes the undone edit." ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "cut_fill" ), tr( "Cut Features" ),
                              QKeySequence::Cut, this, &QgisDesktopWindow::cutFeatures ),
         tr( "Cuts the selected features." ) );
    tip( editMenu->addAction( ic( "l_yer_st_ck" ), tr( "Copy Features" ),
                              QKeySequence::Copy, this, &QgisDesktopWindow::copyFeatures ),
         tr( "Copies the selected features." ) );
    tip( editMenu->addAction( ic( "i_ort" ), tr( "Paste Features" ),
                              QKeySequence::Paste, this, &QgisDesktopWindow::pasteFeatures ),
         tr( "Pastes features." ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "select" ), tr( "Select All" ),
                              QKeySequence( "Ctrl+A" ), this, &QgisDesktopWindow::selectAll ),
         tr( "Selects all features of the current layer." ) );
    tip( editMenu->addAction( ic( "mActionSelectRectangle" ), tr( "Select Features" ),
                             this, &QgisDesktopWindow::selectFeatures ),
         tr( "Selects features with a rectangle." ) );
    tip( editMenu->addAction( ic( "mActionDeleteSelectedFeatures" ), tr( "Delete Selected" ),
                             QKeySequence::Delete, this, &QgisDesktopWindow::deleteSelectedFeatures ),
         tr( "Deletes the selected features." ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "t_ble" ), tr( "Open Attribute Table..." ),
                              this, &QgisDesktopWindow::openAttributeTable ),
         tr( "Opens the attribute table." ) );

    // Digitize tools nested under Edit (grouped)
    QMenu *digitizeMenu = makeMenu( editMenu->addMenu( tr( "Digitizing" ) ) );
    setMenuIcon( digitizeMenu, ic( "mActionCapturePoint" ) );
    tip( digitizeMenu->addAction( ic( "mActionCapturePoint" ), tr( "Add Feature" ),
                                  QKeySequence( "Ctrl+." ), this, &QgisDesktopWindow::addFeature ),
         tr( "Digitize to add a new feature." ) );
    tip( digitizeMenu->addAction( ic( "mActionVertexTool" ), tr( "Node Tool" ),
                                  QKeySequence( "Ctrl+Shift+V" ), this, &QgisDesktopWindow::vertexTool ),
         tr( "Edit nodes." ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionMoveFeature" ), tr( "Move Features" ),
                                  this, &QgisDesktopWindow::moveFeature ),
         tr( "Moves the selected features." ) );
    tip( digitizeMenu->addAction( ic( "mActionRotateFeature" ), tr( "Rotate Features" ),
                                  this, &QgisDesktopWindow::rotateFeature ),
         tr( "Rotates the selected features." ) );
    tip( digitizeMenu->addAction( ic( "mActionScaleFeature" ), tr( "Scale Features" ),
                                  this, &QgisDesktopWindow::scaleFeature ),
         tr( "Scales the selected features." ) );
    tip( digitizeMenu->addAction( ic( "mActionOffsetCurve" ), tr( "Offset Line" ),
                                  this, &QgisDesktopWindow::offsetCurve ),
         tr( "Offsets line features." ) );
    tip( digitizeMenu->addAction( ic( "mActionReverseLine" ), tr( "Reverse Line Direction" ),
                                  this, &QgisDesktopWindow::reverseLine ),
         tr( "Reverses the direction of line features." ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionReshape" ), tr( "Reshape Geometry" ),
                                  this, &QgisDesktopWindow::reshapeGeometry ),
         tr( "Reshapes feature geometry." ) );
    tip( digitizeMenu->addAction( ic( "mActionSplitFeatures" ), tr( "Split Features" ),
                                  this, &QgisDesktopWindow::splitFeatures ),
         tr( "Splits features." ) );
    tip( digitizeMenu->addAction( ic( "s_lit" ), tr( "Split Part" ),
                                  this, &QgisDesktopWindow::splitParts ),
         tr( "Split multipart geometry." ) );
    tip( digitizeMenu->addAction( ic( "mActionSimplify" ), tr( "Simplify" ),
                                  this, &QgisDesktopWindow::simplifyFeature ),
         tr( "Simplifies geometry." ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionAddRing" ), tr( "Add Ring" ),
                                  this, &QgisDesktopWindow::addRing ),
         tr( "Add an interior ring." ) );
    tip( digitizeMenu->addAction( ic( "mActionAddPart" ), tr( "Add Part" ),
                                  this, &QgisDesktopWindow::addPart ),
         tr( "Adds a part." ) );
    tip( digitizeMenu->addAction( ic( "mActionFillRing" ), tr( "Fill Ring" ),
                                  this, &QgisDesktopWindow::fillRing ),
         tr( "Filling the ring creates a new feature." ) );
    tip( digitizeMenu->addAction( ic( "mActionDeletePart" ), tr( "Delete Part" ),
                                  this, &QgisDesktopWindow::deletePart ),
         tr( "Deletes the part." ) );
    tip( digitizeMenu->addAction( ic( "mActionDeleteRing" ), tr( "Delete Ring" ),
                                  this, &QgisDesktopWindow::deleteRing ),
         tr( "Delete an interior ring." ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionTrimExtendFeature" ), tr( "Trim/Extend" ),
                                  this, &QgisDesktopWindow::trimExtendFeature ),
         tr( "Trim or extend features." ) );
    tip( digitizeMenu->addAction( ic( "mActionChamferFillet" ), tr( "Chamfer/Fillet" ),
                                  this, &QgisDesktopWindow::chamferFillet ),
         tr( "Chamfer or fillet." ) );
    tip( digitizeMenu->addAction( ic( "mActionFeatureArray" ), tr( "Feature Array" ),
                                  this, &QgisDesktopWindow::featureArray ),
         tr( "Duplicates features in an array." ) );

    // ------------------------------------------------------------------
    // 视图 View — navigation, measure, compare
    // ------------------------------------------------------------------
    QMenu *viewMenu = makeMenu( appMenuBar()->addMenu( tr( "&View" ) ) );
    tip( addCmd( viewMenu, "map.zoomIn" ),
         tr( "Zooms the map view in." ) );
    tip( addCmd( viewMenu, "map.zoomOut" ),
         tr( "Zooms the map view out." ) );
    tip( addCmd( viewMenu, "map.zoomFull" ),
         tr( "Zooms to the extent of all layers." ) );
    tip( addCmd( viewMenu, "layer.zoomTo" ),
         tr( "Zooms to the current layer's extent." ) );
    viewMenu->addSeparator();
    tip( addCmd( viewMenu, "map.pan" ),
         tr( "Pans the map." ) );
    tip( addCmd( viewMenu, "map.identify" ),
         tr( "Click the map to query feature / pixel attributes." ) );
    viewMenu->addSeparator();
    tip( addCmd( viewMenu, "map.measureDistance" ),
         tr( "Measures distance." ) );
    tip( addCmd( viewMenu, "map.measureArea" ),
         tr( "Measures area." ) );
    viewMenu->addSeparator();
    tip( addCmd( viewMenu, "map.compareLayers" ),
         tr( "Compare two layers side by side." ) );
    tip( addCmd( viewMenu, "map.swipe" ),
         tr( "Drag the divider on the map to compare the layers above and below." ) );
    viewMenu->addSeparator();
    // Multi-view shell (Wave D): secondary Display View beside main canvas.
    m_secondaryViewAction = viewMenu->addAction( ic( "dis_l_y" ), tr( "Second View" ) );
    m_secondaryViewAction->setCheckable( true );
    m_secondaryViewAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+2" ) ) );
    tip( m_secondaryViewAction,
         tr( "Opens/closes the second display view (independent layer stack and rendering; can be made the active view)." ) );
    connect( m_secondaryViewAction, &QAction::toggled,
             this, &QgisDesktopWindow::toggleSecondaryMapView );
    tip( viewMenu->addAction( tr( "Activate Main View" ),
                              this, &QgisDesktopWindow::activateMainMapView ),
         tr( "Routes open / show operations to the main map." ) );
    tip( viewMenu->addAction( tr( "Activate Second View" ),
                              this, &QgisDesktopWindow::activateSecondaryMapView ),
         tr( "Routes open / show operations to the second view (must be open)." ) );
    tip( viewMenu->addAction( tr( "Sync main view layers to the second view" ),
                              this, &QgisDesktopWindow::syncMainLayersToSecondaryView ),
         tr( "Clones the main view display layers into the second view (independent renderer)." ) );
    m_dualViewportSyncAction = viewMenu->addAction( tr( "Linked Viewports" ) );
    m_dualViewportSyncAction->setCheckable( true );
    m_dualViewportSyncAction->setChecked( true );
    m_dualViewportSyncAction->setShortcut( QKeySequence( QStringLiteral( "Ctrl+Shift+L" ) ) );
    tip( m_dualViewportSyncAction,
         tr( "When enabled, both viewports pan/zoom in pixel-level sync (layers can still differ per viewport in swipe comparison)." ) );
    connect( m_dualViewportSyncAction, &QAction::toggled,
             this, &QgisDesktopWindow::toggleDualViewportSync );
    viewMenu->addSeparator();
    tip( addCmd( viewMenu, "map.refresh" ),
         tr( "Refreshes the map rendering." ) );

    // ------------------------------------------------------------------
    // 图层 Layer — add/manage layers only
    // ------------------------------------------------------------------
    QMenu *layerMenu = makeMenu( appMenuBar()->addMenu( tr( "&Layer" ) ) );
    tip( layerMenu->addAction( ic( "r_ster" ), tr( "Add Raster Layer..." ),
                               this, &QgisDesktopWindow::addRasterLayer ),
         tr( "Add a raster layer from a file." ) );
    tip( layerMenu->addAction( ic( "vector" ), tr( "Add Vector Layer..." ),
                               this, &QgisDesktopWindow::addVectorLayer ),
         tr( "Add a vector layer from a file." ) );
    layerMenu->addSeparator();
    tip( layerMenu->addAction( ic( "new_fe_ture_cl_ss" ), tr( "New Shapefile Layer..." ),
                               this, &QgisDesktopWindow::newVectorLayer ),
         tr( "Create a new Shapefile vector layer." ) );
    layerMenu->addSeparator();
    tip( addCmd( layerMenu, "layer.properties" ),
         tr( "Opens the current layer properties." ) );
    tip( addCmd( layerMenu, "layer.remove" ),
         tr( "Remove the current layer from the project." ) );
    layerMenu->addSeparator();
    tip( layerMenu->addAction( ic( "define_crs" ), tr( "Set Project CRS..." ),
                               this, &QgisDesktopWindow::setProjectCrs ),
         tr( "Sets the project CRS." ) );

    // ------------------------------------------------------------------
    // 栅格 Raster — 预处理 + 增强 + 波段（数据准备）
    // ------------------------------------------------------------------
    QMenu *rasterMenu = makeMenu( appMenuBar()->addMenu( tr( "&Raster" ) ) );

    // 预处理（几何/波段准备：配准、镶嵌、波段）
    // 产品级预处理 (辐射定标 / QA 掩膜 / 应用掩膜 / 大气校正 / 正射纠正) lives
    // once, under 遥感 > 产品与预处理 (ADR 0099 task-centric surface) — no
    // duplicated menu actions for the same capability.
    QMenu *preprocessMenu = makeMenu( rasterMenu->addMenu( tr( "Preprocessing" ) ) );
    setMenuIcon( preprocessMenu, ic( "geocorrection" ) );

    // 影像配准 — 预处理第一步常用
    QMenu *regMenu = makeMenu( preprocessMenu->addMenu( tr( "Image Registration" ) ) );
    regMenu->setObjectName( QStringLiteral( "mImageRegistrationMenu" ) );
    setMenuIcon( regMenu, ic( "geocorrection" ) );
    tip( regMenu->addAction( ic( "coregistr_tion" ),
                             tr( "Image to Image (I2I)..." ),
                             this, &QgisDesktopWindow::openGeorefImageToImage ),
         tr( "Two-canvas SRC|REF ground-point registration with SIFT support. No RPC." ) );
    tip( regMenu->addAction( ic( "geocorrection" ),
                             tr( "Image to Map (I2M)..." ),
                             this, &QgisDesktopWindow::openGeorefImageToMap ),
         tr( "Source image + main-project map picking; RPC Physical supported." ) );
    preprocessMenu->addSeparator();

    tip( preprocessMenu->addAction( ic( "mos_ic" ), tr( "Mosaic..." ),
                                    this, &QgisDesktopWindow::openMosaicDialog ),
         tr( "Mosaic multiple rasters into a continuous image." ) );
    tip( preprocessMenu->addAction( ic( "extr_ct_b_nd" ), tr( "Extract Band..." ), this, [this]() {
          ExtractBandDialog dlg( this );
          if ( m_mapCanvas && m_mapCanvas->currentLayer() )
          {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( m_mapCanvas->currentLayer() ) )
              dlg.setRasterLayer( rl );
          }
          dlg.exec();
        } ),
         tr( "Extract and save a single band from a multiband raster." ) );
    tip( preprocessMenu->addAction( ic( "b_nd_co_bo" ), tr( "Band Composition..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:raster_merge_bands" ) );
        } ),
         tr( "Multiband composition / merge." ) );

    // 影像增强
    QMenu *enhanceMenu = makeMenu( rasterMenu->addMenu( tr( "Image Enhancement" ) ) );
    setMenuIcon( enhanceMenu, ic( "enh_nce" ) );
    tip( enhanceMenu->addAction( ic( "enh_nce" ), tr( "Combined Enhancement Panel..." ),
                                 this, &QgisDesktopWindow::openImageEnhancementPanel ),
         tr( "Combined panel: contrast stretch, spatial filtering, band ratio / IHS, SAR speckle filtering." ) );
    enhanceMenu->addSeparator();
    tip( enhanceMenu->addAction( ic( "histogr_eq" ), tr( "Contrast Stretch..." ),
                                 this, &QgisDesktopWindow::openContrastStretchDialog ),
         tr( "Linear / percent clip / std dev / histogram equalization." ) );
    tip( enhanceMenu->addAction( ic( "s_ooth_line" ), tr( "Spatial Filtering..." ),
                                 this, &QgisDesktopWindow::openSpatialFilterDialog ),
         tr( "Mean / Gaussian / Median / Sobel / Laplacian." ) );
    tip( enhanceMenu->addAction( ic( "sar_process" ), tr( "Speckle Filtering (SAR)..." ),
                                 this, &QgisDesktopWindow::openSpeckleFilterDialog ),
         tr( "SAR speckle filtering: Lee / Frost / Kuan / Gamma-MAP." ) );

    // 波段运算与变换
    QMenu *bandMenu = makeMenu( rasterMenu->addMenu( tr( "Bands and Transform" ) ) );
    setMenuIcon( bandMenu, ic( "b_nd_m_th" ) );
    tip( bandMenu->addAction( ic( "b_nd_m_th" ), tr( "Band Math..." ),
                              this, &QgisDesktopWindow::openBandMathDialog ),
         tr( "Expression math, e.g. (b1-b2)/(b1+b2)." ) );
    tip( bandMenu->addAction( ic( "color_r_" ), tr( "Band Ratio / IHS..." ),
                              this, &QgisDesktopWindow::openBandRatioDialog ),
         tr( "Band ratio or IHS transform." ) );
    tip( bandMenu->addAction( ic( "pca" ), tr( "Principal Component Analysis (PCA)..." ),
                              this, &QgisDesktopWindow::openPcaDialog ),
         tr( "PCA: dimensionality reduction and decorrelation." ) );

    // ------------------------------------------------------------------
    // 分析 Analysis — 保留 0099 遥感菜单之外的高价值分析入口
    // （光谱指数 / 光谱分析 / 变化检测 / 融合 / 地形 once under 遥感 > 分析；
    //   这里仅保留其独有条目，避免同一能力重复的菜单动作。）
    // ------------------------------------------------------------------
    QMenu *analysisMenu = makeMenu( appMenuBar()->addMenu( tr( "&Analysis" ) ) );

    tip( analysisMenu->addAction( ic( "veget_tion_index" ), tr( "Time Series Analysis..." ),
                                      this, &QgisDesktopWindow::openTemporalAnalysisDialog ),
         tr( "Multitemporal statistics / compositing / index time series / trends / anomalies / point and ROI series (with scientific prechecks)." ) );

    analysisMenu->addSeparator();
    QMenu *classifyMenu = makeMenu( analysisMenu->addMenu( tr( "Classification" ) ) );
    setMenuIcon( classifyMenu, ic( "su_ervised" ) );
#ifdef SICNU_HAS_CLASSIFY
    tip( classifyMenu->addAction( ic( "su_ervised" ), tr( "Supervised Classification (pixel level)..." ),
                                  this, &QgisDesktopWindow::openClassificationWindow ),
         tr( "Pixel-level supervised classification: ROIs, algorithms, accuracy assessment." ) );
#ifdef SICNU_HAS_OBIA
    tip( classifyMenu->addAction( ic( "seg_ent_tion" ), tr( "Object-Based Classification (OBIA)..." ),
                                  this, &QgisDesktopWindow::openObiaWindow ),
         tr( "Segmentation + object-level classification." ) );
#else
    auto *obiaAct = classifyMenu->addAction( ic( "seg_ent_tion" ),
                                             tr( "Object-Based Classification (OBIA) — not enabled" ) );
    obiaAct->setEnabled( false );
#endif
#else
    auto *disabledAct = classifyMenu->addAction( ic( "su_ervised" ),
                                                 tr( "Classification (OpenCV ml unavailable)" ) );
    disabledAct->setEnabled( false );
#endif

    // ------------------------------------------------------------------
    // 遥感 Remote Sensing — 任务导向入口（C5 / ADR 0099）：不暴露 provider
    // 名称，按领域工作流分组，是产品级预处理与光谱/变化/融合/地形能力的
    // 唯一菜单入口（栅格/分析菜单仅保留其独有条目）。
    // ------------------------------------------------------------------
    QMenu *rsMenu = makeMenu( appMenuBar()->addMenu( tr( "&Remote Sensing" ) ) );

    QMenu *rsProduct = makeMenu( rsMenu->addMenu( tr( "Products and Preprocessing" ) ) );
    setMenuIcon( rsProduct, ic( "extr_ct_b_nd" ) );
    tip( rsProduct->addAction( ic( "i_ort" ), tr( "Import Remote-Sensing Product..." ), this,
                               [this]() { openProductImportDialog( QStringLiteral( "auto" ) ); } ),
         tr( "Product-aware import for Sentinel-2 / Landsat / MODIS with band roles and metadata parsed automatically." ) );
    tip( rsProduct->addAction( ic( "at_os_corr" ), tr( "Radiometric Calibration..." ),
                               this, &QgisDesktopWindow::openRadiometricCalibrationDialog ),
         tr( "DN → radiance / TOA reflectance / brightness temperature (sensor metadata auto-detected)." ) );
    tip( rsProduct->addAction( ic( "qa_mask" ), tr( "QA Mask (cloud/shadow/snow)..." ),
                               this, &QgisDesktopWindow::openQaMaskDialog ),
         tr( "Generate cloud/shadow/snow masks from Landsat QA_PIXEL / Sentinel-2 SCL." ) );
    tip( rsProduct->addAction( ic( "qa_mask" ), tr( "Apply Mask..." ),
                               this, &QgisDesktopWindow::openApplyMaskDialog ),
         tr( "Applies the mask to the product: obscured pixels become NoData, yielding an analysis-ready image." ) );
    tip( rsProduct->addAction( ic( "at_os_corr" ), tr( "Atmospheric Correction..." ),
                               this, &QgisDesktopWindow::openAtmosphericCorrectionDialog ),
         tr( "DOS1 / DOS2 / QUAC with parameters auto-filled from metadata." ) );
    tip( rsProduct->addAction( ic( "geocorrection" ), tr( "Orthorectification (RPC/GCP)..." ),
                               this, &QgisDesktopWindow::openOrthorectificationDialog ),
         tr( "Terrain correction based on RPC/GCPs and an optional DEM." ) );

    QMenu *rsAnalysis = makeMenu( rsMenu->addMenu( tr( "Analysis" ) ) );
    setMenuIcon( rsAnalysis, ic( "veget_tion_index" ) );
    tip( rsAnalysis->addAction( ic( "veget_tion_index" ), tr( "Spectral Indices..." ),
                                this, &QgisDesktopWindow::openSpectralIndexDialog ),
         tr( "NDVI / EVI / SAVI / NDWI / NDBI / MNDWI (bands chosen automatically by semantic role)." ) );
    QMenu *rsSpectral = makeMenu( rsAnalysis->addMenu( tr( "Spectral Analysis" ) ) );
    tip( rsSpectral->addAction( ic( "su_ervised" ), tr( "Spectral Library Matching..." ),
                                this, &QgisDesktopWindow::openSpectralLibraryDialog ),
         tr( "Match pixel/ROI spectra against the spectral library (SAM + SID)." ) );
    tip( rsSpectral->addAction( ic( "sel_tool" ), tr( "ROI Mean Spectrum..." ),
                                this, &QgisDesktopWindow::activateRoiSpectrumTool ),
         tr( "Polygon ROI mean spectrum → spectral profile panel." ) );
    tip( rsAnalysis->addAction( ic( "ch_nge_detect" ), tr( "Change Detection..." ),
                                this, &QgisDesktopWindow::openChangeDetectionDialog ),
         tr( "Differencing / normalized difference / change mask / post-classification comparison." ) );
    tip( rsAnalysis->addAction( ic( "accur_cy" ), tr( "Post-Classification Comparison..." ),
                                this, &QgisDesktopWindow::openPostClassificationDialog ),
         tr( "Two-date classification comparison: per-class transition matrix, gains/losses and a change-type map." ) );
    tip( rsAnalysis->addAction( ic( "p_nsh_r_en" ), tr( "Image Fusion..." ),
                                this, &QgisDesktopWindow::openFusionDialog ),
         tr( "Pansharpening: Linear / Brovey / IHS / PCA or OTB/GDAL." ) );
    tip( rsAnalysis->addAction( ic( "dem" ), tr( "Terrain Analysis..." ),
                                this, &QgisDesktopWindow::openTerrainDialog ),
         tr( "DEM: slope / aspect / hillshade / roughness, etc." ) );

    rsMenu->addSeparator();
    tip( rsMenu->addAction( ic( "workflow" ), tr( "Preprocessing Workflow (DAG)..." ), this,
                           [this]() { openWorkflowTool( QStringLiteral( "lab.preprocess.optical" ) ); } ),
         tr( "A reusable analysis-ready pipeline: calibration → QA mask → atmospheric correction → apply mask → NDVI." ) );

    // ------------------------------------------------------------------
    // 矢量 Vector — 按功能分组
    // ------------------------------------------------------------------
    QMenu *vectorMenu = makeMenu( appMenuBar()->addMenu( tr( "&Vector" ) ) );

    QMenu *vecGeo = makeMenu( vectorMenu->addMenu( tr( "Geometry Processing" ) ) );
    setMenuIcon( vecGeo, ic( "buffer" ) );
    tip( vecGeo->addAction( ic( "buffer" ), tr( "Buffer..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_buffer" ) );
        } ),
         tr( "Vector buffer analysis." ) );
    tip( vecGeo->addAction( ic( "dissolve" ), tr( "Fusion..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_dissolve" ) );
        } ),
         tr( "Dissolves features by attribute." ) );
    tip( vecGeo->addAction( ic( "merge" ), tr( "Merge..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_merge" ) );
        } ),
         tr( "Merge multiple vector layers." ) );
    tip( vecGeo->addAction( ic( "cli_" ), tr( "Clip..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_clip" ) );
        } ),
         tr( "Clips vectors by a boundary." ) );

    QMenu *vecOverlay = makeMenu( vectorMenu->addMenu( tr( "Overlay Analysis" ) ) );
    setMenuIcon( vecOverlay, ic( "overl_y" ) );
    tip( vecOverlay->addAction( ic( "er_se" ), tr( "Erase..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_difference" ) );
        } ),
         tr( "Vector erase / difference." ) );
    tip( vecOverlay->addAction( ic( "overl_y" ), tr( "Intersect..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_intersection" ) );
        } ),
         tr( "Vector intersection." ) );
    tip( vecOverlay->addAction( ic( "merge" ), tr( "Union..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_union" ) );
        } ),
         tr( "Vector union." ) );

    QMenu *vecSelect = makeMenu( vectorMenu->addMenu( tr( "Spatial Selection" ) ) );
    setMenuIcon( vecSelect, ic( "select_by_loc" ) );
    tip( vecSelect->addAction( ic( "select_by_loc" ), tr( "Select by Location..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_select_by_location" ) );
        } ),
         tr( "Select features by spatial relation." ) );
    tip( vecSelect->addAction( ic( "extr_ct_by_m_sk" ), tr( "Extract by Location..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_extract_by_location" ) );
        } ),
         tr( "Extract features into a new layer by spatial relation." ) );

    QMenu *vecAttr = makeMenu( vectorMenu->addMenu( tr( "Properties and Projection" ) ) );
    setMenuIcon( vecAttr, ic( "field_c_lc" ) );
    tip( vecAttr->addAction( ic( "re_roject" ), tr( "Reproject..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_reproject" ) );
        } ),
         tr( "Vector reprojection." ) );
    tip( vecAttr->addAction( ic( "field_c_lc" ), tr( "Field Calculator..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_field_calculator" ) );
        } ),
         tr( "Field calculator." ) );
    tip( vecAttr->addAction( ic( "closest_f_cility" ), tr( "Nearest Neighbour..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_nearest_neighbor" ) );
        } ),
         tr( "Nearest-neighbour analysis." ) );
    tip( vecAttr->addAction( ic( "st_tistics" ), tr( "Distance Matrix..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_distance_matrix" ) );
        } ),
         tr( "Distance matrix." ) );

    // ------------------------------------------------------------------
    // 处理 Processing — toolbox / history / batch only
    // ------------------------------------------------------------------
    QMenu *processingMenu = makeMenu( appMenuBar()->addMenu( tr( "&Processing" ) ) );
    tip( processingMenu->addAction( ic( "toolbox" ), tr( "Toolbox" ),
                                    this, &QgisDesktopWindow::showProcessingToolbox ),
         tr( "Opens the Processing Toolbox: GDAL / OTB / built-in algorithms." ) );
    tip( processingMenu->addAction( ic( "log_viewer" ), tr( "History" ),
                                    this, &QgisDesktopWindow::showProcessingHistory ),
         tr( "View the history of processing algorithms that have run." ) );
    processingMenu->addSeparator();
    tip( processingMenu->addAction( ic( "b_tch" ), tr( "Batch Processing..." ),
                                    this, &QgisDesktopWindow::openBatchProcessingDialog ),
         tr( "Run one algorithm over multiple input files in a batch." ) );

    // ------------------------------------------------------------------
    // 设置 Settings
    // ------------------------------------------------------------------
    QMenu *settingsMenu = makeMenu( appMenuBar()->addMenu( tr( "&Settings" ) ) );
    tip( settingsMenu->addAction( ic( "settings" ), tr( "Options..." ),
                                  this, &QgisDesktopWindow::options ),
         tr( "Theme, default CRS, logging, GDAL/OTB paths." ) );
    settingsMenu->addSeparator();
    tip( settingsMenu->addAction( ic( "define_crs" ), tr( "CRS Presets..." ),
                                  this, &QgisDesktopWindow::openCrsPresetDialog ),
         tr( "Browse and choose a common CRS preset." ) );

    // Window Menu (dock toggle actions added in setupDockWidgets)
    m_windowMenu = makeMenu( appMenuBar()->addMenu( tr( "&Window" ) ) );
    setMenuIcon( m_windowMenu, ic( "p_nel_l_yout" ) );

    // ------------------------------------------------------------------
    // 帮助 Help
    // ------------------------------------------------------------------
    QMenu *helpMenu = makeMenu( appMenuBar()->addMenu( tr( "&Help" ) ) );
    tip( helpMenu->addAction( ic( "hel_" ), tr( "Help Center (F1)" ),
                              this, []() {
                                  sicnu::app::HelpSystemController::instance().openHelpCenter();
                              } ),
         tr( "Opens the Help Center: search help topics, operator descriptions and error diagnostics." ) );
    // No F1 binding here: bare F1 is context help (Help Center) owned by
    // HelpSystemController; this entry stays reachable from the menu.
    tip( helpMenu->addAction( ic( "hel_" ), tr( "Help Content" ),
                              this, &QgisDesktopWindow::helpContents ),
         tr( "Opens the help document." ) );
    tip( helpMenu->addAction( tr( "What's This?" ), this, []() {
             QWhatsThis::enterWhatsThisMode();
         } ),
         tr( "Enter 'What's This?' mode and click any widget for its explanation." ) );
    helpMenu->addSeparator();
    tip( helpMenu->addAction( ic( "s_tellite" ), tr( "Load Sample Data" ),
                              this, &QgisDesktopWindow::loadSampleData ),
         tr( "Load the built-in sample datasets." ) );
    tip( helpMenu->addAction( ic( "workflow" ), tr( "Guided Workflow" ),
                              this, &QgisDesktopWindow::showGuidedWorkflows ),
         tr( "Step-by-step guided experiment workflow." ) );
    helpMenu->addSeparator();
    tip( helpMenu->addAction( ic( "met_d_t_" ), tr( "Check Version" ),
                              this, &QgisDesktopWindow::checkVersion ),
         tr( "Shows current version information." ) );
    tip( helpMenu->addAction( ic( "app_icon" ), tr( "About" ),
                              this, &QgisDesktopWindow::about ),
         tr( "About this software." ) );
}

void QgisDesktopWindow::setupToolbars()
{
    // Optional classic toolbars sit under the Ribbon (max 2 rows). Toggle via
    // ribbon right-click → 工具栏. Default: 导航与显示 on, 数字化 off.

    auto polishBar = []( QToolBar *tb ) {
        if ( !tb )
            return;
        // Not a QMainWindow toolbar area child — lives in the top chrome strip
        // under the ribbon so it cannot paint above the ribbon dock.
        tb->setMovable( false );
        tb->setFloatable( false );
        tb->setIconSize( QSize( 20, 20 ) );
        tb->setToolButtonStyle( Qt::ToolButtonIconOnly );
        tb->setFixedHeight( 32 );
        tb->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    };

    // Row 1 (when shown): map navigation + identify / measure / display.
    // Construct with Qt::Widget flags so the bar can live inside the chrome
    // strip (not as a Qt::Tool floating window under QMainWindow).
    m_mapToolsToolBar = new QToolBar( tr( "Navigation and Display" ), this );
    m_mapToolsToolBar->setWindowFlags( Qt::Widget );
    auto *mapToolsToolBar = m_mapToolsToolBar;
    mapToolsToolBar->setObjectName( QStringLiteral( "mapToolsToolBar" ) );
    mapToolsToolBar->setWindowTitle( tr( "Navigation and Display" ) );
    polishBar( mapToolsToolBar );
    tip( mapToolsToolBar->addAction( ic( "p_n" ), tr( "Pan" ),
                                     this, &QgisDesktopWindow::panMap ),
         tr( "Pan (Space)" ) );
    tip( mapToolsToolBar->addAction( ic( "zoo_in" ), tr( "Zoom In" ),
                                     this, &QgisDesktopWindow::zoomIn ),
         tr( "Zoom In" ) );
    tip( mapToolsToolBar->addAction( ic( "zoo_out" ), tr( "Zoom Out" ),
                                     this, &QgisDesktopWindow::zoomOut ),
         tr( "Zoom Out" ) );
    tip( mapToolsToolBar->addAction( ic( "full_extent" ), tr( "Full Extent" ),
                                     this, &QgisDesktopWindow::zoomFullExtent ),
         tr( "Full Extent (Ctrl+Shift+F)" ) );
    tip( mapToolsToolBar->addAction( ic( "refresh_view" ), tr( "Refresh" ),
                                     this, &QgisDesktopWindow::refreshMap ),
         tr( "Refresh Map" ) );
    mapToolsToolBar->addSeparator();
    tip( mapToolsToolBar->addAction( ic( "identify" ), tr( "Identify" ),
                                     this, &QgisDesktopWindow::identifyFeatures ),
         tr( "Identify (Ctrl+Shift+I)" ) );
    tip( mapToolsToolBar->addAction( ic( "me_sure_dist" ), tr( "Measure Distance" ),
                                     this, &QgisDesktopWindow::measureDistance ),
         tr( "Measure Distance (Ctrl+Shift+D)" ) );
    tip( mapToolsToolBar->addAction( ic( "me_sure_are_" ), tr( "Measure Area" ),
                                     this, &QgisDesktopWindow::measureArea ),
         tr( "Measure Area (Ctrl+Shift+A)" ) );
    mapToolsToolBar->addSeparator();
    tip( mapToolsToolBar->addAction( ic( "enh_nce" ), tr( "Display Stretch" ),
                                     this, &QgisDesktopWindow::openDisplayStretchPanel ),
         tr( "Display contrast stretch (changes rendering only; no file is exported)" ) );
    tip( mapToolsToolBar->addAction( ic( "dis_l_y" ), tr( "Layer Properties" ),
                                     this, &QgisDesktopWindow::layerProperties ),
         tr( "Open Current Layer Properties" ) );
    // Visible by default under the ribbon (hosted later in rsToolbarStrip).
    mapToolsToolBar->show();
    if ( mapToolsToolBar->toggleViewAction() )
        mapToolsToolBar->toggleViewAction()->setChecked( true );

    // Row 2 (when shown): digitizing / vector edit
    m_digitizeToolBar = new QToolBar( tr( "Digitizing" ), this );
    m_digitizeToolBar->setWindowFlags( Qt::Widget );
    auto *digitizeToolBar = m_digitizeToolBar;
    digitizeToolBar->setObjectName( QStringLiteral( "digitizeToolBar" ) );
    digitizeToolBar->setWindowTitle( tr( "Digitizing" ) );
    digitizeToolBar->setWhatsThis( SicnuDialogHelp::htmlForTool(
        QStringLiteral( "digitize_tools" ), tr( "Digitizing Edit Tools" ) ) );
    polishBar( digitizeToolBar );

    if ( m_toggleEditingAction )
    {
        // M2: the registry projection already carries the canonical
        // Ctrl+E binding — do not hand-write the shortcut into the tooltip.
        digitizeToolBar->addAction( m_toggleEditingAction );
    }
    if ( m_saveEditsAction )
    {
        m_saveEditsAction->setToolTip( tr( "Save Edits" ) );
        digitizeToolBar->addAction( m_saveEditsAction );
    }
    digitizeToolBar->addSeparator();

    // Editing tool actions (also used when toolbar is hidden — window-owned).
    auto makeEditAct = [this]( const char *icon, const QString &text, const QString &tipText,
                               void ( QgisDesktopWindow::*slot )() ) -> QAction * {
        auto *a = new QAction( QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( icon ) ), text, this );
        tip( a, tipText ); // toolTip + statusTip + whatsThis
        a->setEnabled( false );
        connect( a, &QAction::triggered, this, slot );
        return a;
    };
    m_editingToolActions = {
        makeEditAct( "mActionSelectRectangle", tr( "Select" ), tr( "Select Features (rectangle)" ),
                     &QgisDesktopWindow::selectFeatures ),
        makeEditAct( "mActionCapturePoint", tr( "Add Feature" ), tr( "Draw New Feature" ),
                     &QgisDesktopWindow::addFeature ),
        makeEditAct( "mActionVertexTool", tr( "Node" ), tr( "Node tool: drag vertices to edit geometry" ),
                     &QgisDesktopWindow::vertexTool ),
        makeEditAct( "mActionMoveFeature", tr( "Move" ), tr( "Move Features" ),
                     &QgisDesktopWindow::moveFeature ),
        makeEditAct( "mActionRotateFeature", tr( "Rotate" ), tr( "Rotate Features" ),
                     &QgisDesktopWindow::rotateFeature ),
        makeEditAct( "mActionReshape", tr( "Reshape" ), tr( "Reshape Geometry: modify feature boundaries" ),
                     &QgisDesktopWindow::reshapeGeometry ),
        makeEditAct( "mActionSplitFeatures", tr( "Segmentation" ), tr( "Split Features" ),
                     &QgisDesktopWindow::splitFeatures ),
        makeEditAct( "mActionOffsetCurve", tr( "Offset" ), tr( "Offset Line (parallel line)" ),
                     &QgisDesktopWindow::offsetCurve ),
        makeEditAct( "mActionSimplify", tr( "Simplify" ), tr( "Simplify Geometry: thin out vertices" ),
                     &QgisDesktopWindow::simplifyFeature ),
        makeEditAct( "mActionReverseLine", tr( "Flip" ), tr( "Reverse Line Direction" ),
                     &QgisDesktopWindow::reverseLine ),
        makeEditAct( "mActionAddRing", tr( "Add Ring" ), tr( "Add Ring (hole inside a polygon)" ),
                     &QgisDesktopWindow::addRing ),
        makeEditAct( "mActionFillRing", tr( "Fill Ring" ), tr( "Fill Ring (draw a new polygon inside a hole)" ),
                     &QgisDesktopWindow::fillRing ),
        makeEditAct( "mActionDeletePart", tr( "Delete Part" ), tr( "Delete Selected Part" ),
                     &QgisDesktopWindow::deletePart ),
    };
    for ( QAction *a : m_editingToolActions )
        digitizeToolBar->addAction( a );
    digitizeToolBar->hide();
    if ( digitizeToolBar->toggleViewAction() )
        digitizeToolBar->toggleViewAction()->setChecked( false );

    // Keep strip geometry in sync when the user toggles bars (ribbon context menu).
    // Pass the toggled value via rsWantVisible so layout does not race isChecked().
    // Skip while layoutToolbarsUnderRibbon is running (avoids show→toggled→layout loop).
    auto wireToggle = [this]( QToolBar *tb ) {
        if ( !tb || !tb->toggleViewAction() )
            return;
        connect( tb->toggleViewAction(), &QAction::toggled, this,
                 [this, tb]( bool on ) {
                     if ( !tb || m_layoutingToolbarsUnderRibbon )
                         return;
                     tb->setProperty( "rsWantVisible", on );
                     // Defer until QToolBar finishes its own show/hide.
                     QTimer::singleShot( 0, this, [this]() {
                         if ( !m_layoutingToolbarsUnderRibbon )
                             layoutToolbarsUnderRibbon();
                     } );
                 } );
    };
    wireToggle( m_mapToolsToolBar );
    wireToggle( m_digitizeToolBar );

    // CRS picker lives on the status bar; create here for wiring.
    m_crsSelector = new QgsProjectionSelectionWidget( this );
    m_crsSelector->setOptionVisible( QgsProjectionSelectionWidget::ProjectCrs, true );
    connect( m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
             this, &QgisDesktopWindow::onCrsChanged );
}
void QgisDesktopWindow::setupStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName( "rsStatusBar" );
    bar->setFixedHeight( 28 );

    // Ready / task summary (left)
    m_readyLabel = new QLabel( tr( "Ready" ), bar );
    m_readyLabel->setObjectName( QStringLiteral( "rsReadyLabel" ) );
    bar->addWidget( m_readyLabel );

    // Coordinates
    m_coordinatesLabel = new QLabel( QStringLiteral( "0.00, 0.00" ), bar );
    m_coordinatesLabel->setObjectName( QStringLiteral( "rsCoordLabel" ) );
    bar->addPermanentWidget( m_coordinatesLabel );

    // Scale (single source — not on band rail)
    m_scaleLabel = new QLabel( tr( "Scale —" ), bar );
    m_scaleLabel->setObjectName( QStringLiteral( "rsScaleLabel" ) );
    bar->addPermanentWidget( m_scaleLabel );

    // CRS picker
    if ( m_crsSelector )
    {
        m_crsSelector->setParent( bar );
        m_crsSelector->setMaximumWidth( 200 );
        m_crsSelector->setMaximumHeight( 22 );
        bar->addPermanentWidget( m_crsSelector );
    }
    m_crsLabel = new QLabel( QStringLiteral( "EPSG:3857" ), bar );
    m_crsLabel->setObjectName( QStringLiteral( "rsCrsLabel" ) );
    m_crsLabel->hide();

    // Active layer name
    m_layerStatusLabel = new QLabel( tr( "No Layers" ), bar );
    m_layerStatusLabel->setObjectName( QStringLiteral( "rsLayerStatusLabel" ) );
    m_layerStatusLabel->setMinimumWidth( 80 );
    m_layerStatusLabel->setMaximumWidth( 180 );
    m_layerStatusLabel->setToolTip( tr( "Current Active Layer" ) );
    bar->addPermanentWidget( m_layerStatusLabel );

    // Opacity (single control — not on band rail)
    auto *opacityTitle = new QLabel( tr( "Opacity" ), bar );
    opacityTitle->setObjectName( QStringLiteral( "rsStatusMetaLabel" ) );
    bar->addPermanentWidget( opacityTitle );
    m_statusOpacitySlider = new QSlider( Qt::Horizontal, bar );
    m_statusOpacitySlider->setObjectName( QStringLiteral( "rsStatusOpacitySlider" ) );
    m_statusOpacitySlider->setRange( 0, 100 );
    m_statusOpacitySlider->setValue( 100 );
    m_statusOpacitySlider->setFixedWidth( 88 );
    m_statusOpacitySlider->setFixedHeight( 16 );
    m_statusOpacitySlider->setToolTip( tr( "Current layer opacity" ) );
    bar->addPermanentWidget( m_statusOpacitySlider );
    m_statusOpacityValue = new QLabel( QStringLiteral( "100%" ), bar );
    m_statusOpacityValue->setObjectName( QStringLiteral( "rsStatusMetaLabel" ) );
    m_statusOpacityValue->setMinimumWidth( 36 );
    bar->addPermanentWidget( m_statusOpacityValue );
    connect( m_statusOpacitySlider, &QSlider::valueChanged, this, [this]( int v ) {
        if ( m_statusOpacityValue )
            m_statusOpacityValue->setText( QStringLiteral( "%1%" ).arg( v ) );
        QgsMapLayer *layer = m_mapCanvas ? m_mapCanvas->currentLayer() : nullptr;
        if ( !layer && m_mapCanvas )
        {
            const auto layers = m_mapCanvas->layers();
            if ( !layers.isEmpty() )
                layer = layers.first();
        }
        if ( layer )
        {
            layer->setOpacity( v / 100.0 );
            layer->triggerRepaint();
        }
    } );

    m_renderTimeLabel = new QLabel( QString(), bar );
    m_renderTimeLabel->setObjectName( QStringLiteral( "rsRenderLabel" ) );
    bar->addPermanentWidget( m_renderTimeLabel );

    m_cacheLabel = new QLabel( tr( "Cache: 0 MB" ), bar );
    m_cacheLabel->setObjectName( QStringLiteral( "rsCacheLabel" ) );
    bar->addPermanentWidget( m_cacheLabel );
}
