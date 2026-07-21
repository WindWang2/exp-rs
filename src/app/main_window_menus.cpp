// main_window_menus.cpp — Menu bar, toolbars, and status bar setup
// Extracted from main_window.cpp for maintainability
#include "main_window.h"

#include "dialogs/extract_band_dialog.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QLabel>
#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QStyle>

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
    menuBar()->setCornerWidget(brandWidget, Qt::TopLeftCorner);

    // Version label (right corner)
    QLabel *versionLabel = new QLabel("v0.9.2-dev");
    versionLabel->setObjectName("rsBrandVersion");
    menuBar()->setCornerWidget(versionLabel, Qt::TopRightCorner);

    // Helper: enable tooltips on every top-level / submenu we create.
    auto makeMenu = []( QMenu *m ) -> QMenu * {
        if ( m )
            m->setToolTipsVisible( true );
        return m;
    };

    // ------------------------------------------------------------------
    // 工程 Project — file I/O, data import, layout, quit
    // ------------------------------------------------------------------
    QMenu *projectMenu = makeMenu( menuBar()->addMenu( tr( "工程(&P)" ) ) );
    tip( projectMenu->addAction( ic( "new_project" ), tr( "新建工程" ),
                                 this, &QgisDesktopWindow::newProject, QKeySequence::New ),
         tr( "创建空白工程，清除当前图层与视图状态。" ) );
    tip( projectMenu->addAction( ic( "o_en" ), tr( "打开工程..." ),
                                 this, &QgisDesktopWindow::openProject, QKeySequence::Open ),
         tr( "打开已保存的工程文件。" ) );
    tip( projectMenu->addAction( ic( "s_ve" ), tr( "保存工程" ),
                                 this, &QgisDesktopWindow::saveProject, QKeySequence::Save ),
         tr( "保存当前工程到已有路径。" ) );
    tip( projectMenu->addAction( ic( "ex_ort" ), tr( "工程另存为..." ),
                                 this, &QgisDesktopWindow::saveProjectAs ),
         tr( "将工程另存为新文件。" ) );
    projectMenu->addSeparator();
    tip( projectMenu->addAction( ic( "i_ort" ), tr( "导入图层..." ),
                                 this, &QgisDesktopWindow::importLayer ),
         tr( "导入栅格或矢量图层到工程。" ) );
    tip( projectMenu->addAction( ic( "cloud_sync" ), tr( "浏览 STAC 目录..." ),
                                 this, &QgisDesktopWindow::browseStacCatalog ),
         tr( "浏览 STAC 目录检索遥感数据。" ) );
    projectMenu->addSeparator();
    tip( projectMenu->addAction( ic( "print_l_yout" ), tr( "新建布局..." ),
                                 this, &QgisDesktopWindow::newLayout ),
         tr( "创建打印布局 / 出图。" ) );
    tip( projectMenu->addAction( ic( "re_ort" ), tr( "导出实验报告..." ),
                                 this, &QgisDesktopWindow::exportLabReport ),
         tr( "导出课程/实验报告。" ) );
    projectMenu->addSeparator();
    tip( projectMenu->addAction( stdIc( QStyle::SP_DialogCloseButton ), tr( "退出" ),
                                 this, &QMainWindow::close, QKeySequence::Quit ),
         tr( "退出应用程序。" ) );

    // ------------------------------------------------------------------
    // 编辑 Edit — feature edit + 数字化 as submenu (no longer top-level)
    // ------------------------------------------------------------------
    QMenu *editMenu = makeMenu( menuBar()->addMenu( tr( "编辑(&E)" ) ) );
    m_toggleEditingAction = editMenu->addAction(
      ic( "mActionToggleEditing" ), tr( "切换编辑" ),
      this, &QgisDesktopWindow::toggleEditing );
    m_toggleEditingAction->setCheckable( true );
    m_toggleEditingAction->setShortcut( QKeySequence( "Ctrl+E" ) );
    tip( m_toggleEditingAction, tr( "开启/关闭当前矢量图层编辑。" ) );
    m_saveEditsAction = editMenu->addAction(
      ic( "mActionSaveEdits" ), tr( "保存编辑" ),
      this, &QgisDesktopWindow::saveEdits );
    m_saveEditsAction->setEnabled( false );
    tip( m_saveEditsAction, tr( "保存矢量编辑。" ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( stdIc( QStyle::SP_ArrowBack ), tr( "撤销" ),
                              this, &QgisDesktopWindow::undo, QKeySequence::Undo ),
         tr( "撤销上一步编辑。" ) );
    tip( editMenu->addAction( stdIc( QStyle::SP_ArrowForward ), tr( "重做" ),
                              this, &QgisDesktopWindow::redo, QKeySequence::Redo ),
         tr( "重做已撤销的编辑。" ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "cut_fill" ), tr( "剪切要素" ),
                              this, &QgisDesktopWindow::cutFeatures, QKeySequence::Cut ),
         tr( "剪切选中要素。" ) );
    tip( editMenu->addAction( ic( "l_yer_st_ck" ), tr( "复制要素" ),
                              this, &QgisDesktopWindow::copyFeatures, QKeySequence::Copy ),
         tr( "复制选中要素。" ) );
    tip( editMenu->addAction( ic( "i_ort" ), tr( "粘贴要素" ),
                              this, &QgisDesktopWindow::pasteFeatures, QKeySequence::Paste ),
         tr( "粘贴要素。" ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "select" ), tr( "全选" ),
                              this, &QgisDesktopWindow::selectAll, QKeySequence( "Ctrl+A" ) ),
         tr( "选择当前图层全部要素。" ) );
    tip( editMenu->addAction( ic( "mActionSelectRectangle" ), tr( "选择要素" ),
                             this, &QgisDesktopWindow::selectFeatures ),
         tr( "矩形选择要素。" ) );
    tip( editMenu->addAction( ic( "mActionDeleteSelectedFeatures" ), tr( "删除选中" ),
                             this, &QgisDesktopWindow::deleteSelectedFeatures, QKeySequence::Delete ),
         tr( "删除选中要素。" ) );
    editMenu->addSeparator();
    tip( editMenu->addAction( ic( "t_ble" ), tr( "打开属性表..." ),
                              this, &QgisDesktopWindow::openAttributeTable ),
         tr( "打开属性表。" ) );

    // Digitize tools nested under Edit (grouped)
    QMenu *digitizeMenu = makeMenu( editMenu->addMenu( tr( "数字化" ) ) );
    setMenuIcon( digitizeMenu, ic( "mActionCapturePoint" ) );
    tip( digitizeMenu->addAction( ic( "mActionCapturePoint" ), tr( "添加要素" ),
                                  this, &QgisDesktopWindow::addFeature, QKeySequence( "Ctrl+." ) ),
         tr( "数字化添加新要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionVertexTool" ), tr( "节点工具" ),
                                  this, &QgisDesktopWindow::vertexTool, QKeySequence( "Ctrl+V" ) ),
         tr( "编辑节点。" ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionMoveFeature" ), tr( "移动要素" ),
                                  this, &QgisDesktopWindow::moveFeature ),
         tr( "移动选中要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionRotateFeature" ), tr( "旋转要素" ),
                                  this, &QgisDesktopWindow::rotateFeature ),
         tr( "旋转选中要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionScaleFeature" ), tr( "缩放要素" ),
                                  this, &QgisDesktopWindow::scaleFeature ),
         tr( "缩放选中要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionOffsetCurve" ), tr( "偏移线" ),
                                  this, &QgisDesktopWindow::offsetCurve ),
         tr( "线要素偏移。" ) );
    tip( digitizeMenu->addAction( ic( "mActionReverseLine" ), tr( "反转线方向" ),
                                  this, &QgisDesktopWindow::reverseLine ),
         tr( "反转线要素方向。" ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionReshape" ), tr( "重塑几何" ),
                                  this, &QgisDesktopWindow::reshapeGeometry ),
         tr( "重塑要素几何。" ) );
    tip( digitizeMenu->addAction( ic( "mActionSplitFeatures" ), tr( "分割要素" ),
                                  this, &QgisDesktopWindow::splitFeatures ),
         tr( "分割要素。" ) );
    tip( digitizeMenu->addAction( ic( "s_lit" ), tr( "分割部件" ),
                                  this, &QgisDesktopWindow::splitParts ),
         tr( "分割多部件几何。" ) );
    tip( digitizeMenu->addAction( ic( "mActionSimplify" ), tr( "简化" ),
                                  this, &QgisDesktopWindow::simplifyFeature ),
         tr( "简化几何。" ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionAddRing" ), tr( "添加环" ),
                                  this, &QgisDesktopWindow::addRing ),
         tr( "添加内环。" ) );
    tip( digitizeMenu->addAction( ic( "mActionAddPart" ), tr( "添加部件" ),
                                  this, &QgisDesktopWindow::addPart ),
         tr( "添加多部件。" ) );
    tip( digitizeMenu->addAction( ic( "mActionFillRing" ), tr( "填充环" ),
                                  this, &QgisDesktopWindow::fillRing ),
         tr( "填充环生成新要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionDeletePart" ), tr( "删除部件" ),
                                  this, &QgisDesktopWindow::deletePart ),
         tr( "删除部件。" ) );
    tip( digitizeMenu->addAction( ic( "mActionDeleteRing" ), tr( "删除环" ),
                                  this, &QgisDesktopWindow::deleteRing ),
         tr( "删除内环。" ) );
    digitizeMenu->addSeparator();
    tip( digitizeMenu->addAction( ic( "mActionTrimExtendFeature" ), tr( "修剪/延伸" ),
                                  this, &QgisDesktopWindow::trimExtendFeature ),
         tr( "修剪或延伸要素。" ) );
    tip( digitizeMenu->addAction( ic( "mActionChamferFillet" ), tr( "倒角/圆角" ),
                                  this, &QgisDesktopWindow::chamferFillet ),
         tr( "倒角或圆角。" ) );
    tip( digitizeMenu->addAction( ic( "mActionFeatureArray" ), tr( "要素阵列" ),
                                  this, &QgisDesktopWindow::featureArray ),
         tr( "按阵列复制要素。" ) );

    // ------------------------------------------------------------------
    // 视图 View — navigation, measure, compare
    // ------------------------------------------------------------------
    QMenu *viewMenu = makeMenu( menuBar()->addMenu( tr( "视图(&V)" ) ) );
    tip( viewMenu->addAction( ic( "zoo_in" ), tr( "放大" ),
                              this, &QgisDesktopWindow::zoomIn, QKeySequence::ZoomIn ),
         tr( "放大地图视图。" ) );
    tip( viewMenu->addAction( ic( "zoo_out" ), tr( "缩小" ),
                              this, &QgisDesktopWindow::zoomOut, QKeySequence::ZoomOut ),
         tr( "缩小地图视图。" ) );
    tip( viewMenu->addAction( ic( "full_extent" ), tr( "全图" ),
                              this, &QgisDesktopWindow::zoomFullExtent, QKeySequence( "Ctrl+Shift+F" ) ),
         tr( "缩放到所有图层范围。" ) );
    tip( viewMenu->addAction( ic( "l_yer_m_n_ger" ), tr( "缩放到图层" ),
                              this, &QgisDesktopWindow::zoomToLayer, QKeySequence( "Ctrl+L" ) ),
         tr( "缩放到当前图层范围。" ) );
    viewMenu->addSeparator();
    tip( viewMenu->addAction( ic( "p_n" ), tr( "平移" ),
                              this, &QgisDesktopWindow::panMap, QKeySequence( "Space" ) ),
         tr( "平移地图。" ) );
    tip( viewMenu->addAction( ic( "identify" ), tr( "识别" ),
                              this, &QgisDesktopWindow::identifyFeatures, QKeySequence( "Ctrl+Shift+I" ) ),
         tr( "点击地图查询要素/像元属性。" ) );
    viewMenu->addSeparator();
    tip( viewMenu->addAction( ic( "me_sure_dist" ), tr( "测距" ),
                              this, &QgisDesktopWindow::measureDistance, QKeySequence( "Ctrl+Shift+D" ) ),
         tr( "量测距离。" ) );
    tip( viewMenu->addAction( ic( "me_sure_are_" ), tr( "测面" ),
                              this, &QgisDesktopWindow::measureArea, QKeySequence( "Ctrl+Shift+A" ) ),
         tr( "量测面积。" ) );
    viewMenu->addSeparator();
    tip( viewMenu->addAction( ic( "overl_y" ), tr( "图层对比..." ),
                              this, &QgisDesktopWindow::openComparisonDialog, QKeySequence( "Ctrl+Shift+C" ) ),
         tr( "左右并排对比两个图层。" ) );
    tip( viewMenu->addAction( ic( "s_lit" ), tr( "卷帘对比" ),
                              this, &QgisDesktopWindow::toggleSwipeTool, QKeySequence( "Ctrl+Shift+S" ) ),
         tr( "在地图上拖动分割线对比上下图层。" ) );
    viewMenu->addSeparator();
    tip( viewMenu->addAction( ic( "refresh_view" ), tr( "刷新" ),
                              this, &QgisDesktopWindow::refreshMap, QKeySequence( "F5" ) ),
         tr( "刷新地图渲染。" ) );

    // ------------------------------------------------------------------
    // 图层 Layer — add/manage layers only
    // ------------------------------------------------------------------
    QMenu *layerMenu = makeMenu( menuBar()->addMenu( tr( "图层(&L)" ) ) );
    tip( layerMenu->addAction( ic( "r_ster" ), tr( "添加栅格图层..." ),
                               this, &QgisDesktopWindow::addRasterLayer ),
         tr( "从文件添加栅格图层。" ) );
    tip( layerMenu->addAction( ic( "vector" ), tr( "添加矢量图层..." ),
                               this, &QgisDesktopWindow::addVectorLayer ),
         tr( "从文件添加矢量图层。" ) );
    layerMenu->addSeparator();
    tip( layerMenu->addAction( ic( "new_fe_ture_cl_ss" ), tr( "新建 Shapefile 图层..." ),
                               this, &QgisDesktopWindow::newVectorLayer ),
         tr( "创建新的 Shapefile 矢量图层。" ) );
    layerMenu->addSeparator();
    tip( layerMenu->addAction( ic( "met_d_t_" ), tr( "图层属性..." ),
                               this, &QgisDesktopWindow::layerProperties, QKeySequence( "Ctrl+I" ) ),
         tr( "打开当前图层属性。" ) );
    tip( layerMenu->addAction( ic( "er_se" ), tr( "移除图层" ),
                               this, &QgisDesktopWindow::removeLayer, QKeySequence( "Ctrl+Shift+Delete" ) ),
         tr( "从工程中移除当前图层。" ) );
    layerMenu->addSeparator();
    tip( layerMenu->addAction( ic( "define_crs" ), tr( "设置工程 CRS..." ),
                               this, &QgisDesktopWindow::setProjectCrs ),
         tr( "设置工程坐标系。" ) );

    // ------------------------------------------------------------------
    // 栅格 Raster — 预处理 + 增强 + 波段（数据准备）
    // ------------------------------------------------------------------
    QMenu *rasterMenu = makeMenu( menuBar()->addMenu( tr( "栅格(&R)" ) ) );

    // 预处理
    QMenu *preprocessMenu = makeMenu( rasterMenu->addMenu( tr( "预处理" ) ) );
    setMenuIcon( preprocessMenu, ic( "at_os_corr" ) );
    tip( preprocessMenu->addAction( ic( "at_os_corr" ), tr( "大气校正..." ),
                                    this, &QgisDesktopWindow::openAtmosphericCorrectionDialog ),
         tr( "大气校正：DN→辐射、DOS1/DOS2。" ) );
    tip( preprocessMenu->addAction( ic( "mos_ic" ), tr( "镶嵌..." ),
                                    this, &QgisDesktopWindow::openMosaicDialog ),
         tr( "多景栅格镶嵌为连续影像。" ) );
    tip( preprocessMenu->addAction( ic( "extr_ct_b_nd" ), tr( "提取波段..." ), this, [this]() {
          ExtractBandDialog dlg( this );
          if ( m_mapCanvas && m_mapCanvas->currentLayer() )
          {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( m_mapCanvas->currentLayer() ) )
              dlg.setRasterLayer( rl );
          }
          dlg.exec();
        } ),
         tr( "从多波段栅格提取单一波段保存。" ) );
    tip( preprocessMenu->addAction( ic( "b_nd_co_bo" ), tr( "波段合成..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:raster_merge_bands" ) );
        } ),
         tr( "多波段合成/合并。" ) );

    // 影像增强
    QMenu *enhanceMenu = makeMenu( rasterMenu->addMenu( tr( "影像增强" ) ) );
    setMenuIcon( enhanceMenu, ic( "enh_nce" ) );
    tip( enhanceMenu->addAction( ic( "enh_nce" ), tr( "增强综合面板..." ),
                                 this, &QgisDesktopWindow::openImageEnhancementPanel ),
         tr( "综合面板：对比度拉伸、空间滤波、波段比值/IHS、SAR 斑点滤波。" ) );
    enhanceMenu->addSeparator();
    tip( enhanceMenu->addAction( ic( "histogr_eq" ), tr( "对比度拉伸..." ),
                                 this, &QgisDesktopWindow::openContrastStretchDialog ),
         tr( "线性 / 百分比裁剪 / 标准差 / 直方图均衡。" ) );
    tip( enhanceMenu->addAction( ic( "s_ooth_line" ), tr( "空间滤波..." ),
                                 this, &QgisDesktopWindow::openSpatialFilterDialog ),
         tr( "均值 / 高斯 / 中值 / Sobel / Laplacian。" ) );
    tip( enhanceMenu->addAction( ic( "sar_process" ), tr( "斑点滤波 (SAR)..." ),
                                 this, &QgisDesktopWindow::openSpeckleFilterDialog ),
         tr( "SAR 斑点滤波：Lee / Frost / Kuan / Gamma-MAP。" ) );

    // 波段运算与变换
    QMenu *bandMenu = makeMenu( rasterMenu->addMenu( tr( "波段与变换" ) ) );
    setMenuIcon( bandMenu, ic( "b_nd_m_th" ) );
    tip( bandMenu->addAction( ic( "b_nd_m_th" ), tr( "波段运算..." ),
                              this, &QgisDesktopWindow::openBandMathDialog ),
         tr( "表达式运算，如 (b1-b2)/(b1+b2)。" ) );
    tip( bandMenu->addAction( ic( "color_r_" ), tr( "波段比值 / IHS..." ),
                              this, &QgisDesktopWindow::openBandRatioDialog ),
         tr( "波段比值或 IHS 变换。" ) );
    tip( bandMenu->addAction( ic( "pca" ), tr( "主成分分析 (PCA)..." ),
                              this, &QgisDesktopWindow::openPcaDialog ),
         tr( "主成分分析：降维与去相关。" ) );

    // ------------------------------------------------------------------
    // 分析 Analysis — 配准、指数、变化、分类、地形、融合（专题）
    // ------------------------------------------------------------------
    QMenu *analysisMenu = makeMenu( menuBar()->addMenu( tr( "分析(&A)" ) ) );

    QMenu *regMenu = makeMenu( analysisMenu->addMenu( tr( "影像配准" ) ) );
    regMenu->setObjectName( QStringLiteral( "mImageRegistrationMenu" ) );
    setMenuIcon( regMenu, ic( "geocorrection" ) );
    tip( regMenu->addAction( ic( "coregistr_tion" ),
                             tr( "影像对影像 (I2I)..." ),
                             this, &QgisDesktopWindow::openGeorefImageToImage ),
         tr( "双画布 SRC|REF 同名点配准，支持 SIFT。不含 RPC。" ) );
    tip( regMenu->addAction( ic( "geocorrection" ),
                             tr( "影像对地图 (I2M)..." ),
                             this, &QgisDesktopWindow::openGeorefImageToMap ),
         tr( "源影像 + 主工程地图取点；支持 RPC Physical。" ) );

    tip( analysisMenu->addAction( ic( "veget_tion_index" ), tr( "光谱指数..." ),
                                  this, &QgisDesktopWindow::openSpectralIndexDialog ),
         tr( "NDVI / EVI / SAVI / NDWI / NDBI / MNDWI。" ) );
    tip( analysisMenu->addAction( ic( "ch_nge_detect" ), tr( "变化检测..." ),
                                  this, &QgisDesktopWindow::openChangeDetectionDialog ),
         tr( "双时相：差值 / 归一化差值 / 变化掩膜。" ) );
    tip( analysisMenu->addAction( ic( "p_nsh_r_en" ), tr( "影像融合..." ),
                                  this, &QgisDesktopWindow::openFusionDialog ),
         tr( "全色锐化：Linear / Brovey / IHS / PCA 或 OTB/GDAL。" ) );
    tip( analysisMenu->addAction( ic( "dem" ), tr( "地形分析..." ),
                                  this, &QgisDesktopWindow::openTerrainDialog ),
         tr( "DEM：坡度 / 坡向 / 山体阴影 / 粗糙度等。" ) );

    analysisMenu->addSeparator();
    QMenu *classifyMenu = makeMenu( analysisMenu->addMenu( tr( "分类" ) ) );
    setMenuIcon( classifyMenu, ic( "su_ervised" ) );
#ifdef SICNU_HAS_CLASSIFY
    tip( classifyMenu->addAction( ic( "su_ervised" ), tr( "监督分类（像元级）..." ),
                                  this, &QgisDesktopWindow::openClassificationWindow ),
         tr( "像元级监督分类：ROI、算法、精度评价。" ) );
#ifdef SICNU_HAS_OBIA
    tip( classifyMenu->addAction( ic( "seg_ent_tion" ), tr( "面向对象分类 (OBIA)..." ),
                                  this, &QgisDesktopWindow::openObiaWindow ),
         tr( "分割 + 对象级分类。" ) );
#else
    auto *obiaAct = classifyMenu->addAction( ic( "seg_ent_tion" ),
                                             tr( "面向对象分类 (OBIA) — 未启用" ) );
    obiaAct->setEnabled( false );
#endif
#else
    auto *disabledAct = classifyMenu->addAction( ic( "su_ervised" ),
                                                 tr( "分类（OpenCV ml 不可用）" ) );
    disabledAct->setEnabled( false );
#endif

    // ------------------------------------------------------------------
    // 矢量 Vector — 按功能分组
    // ------------------------------------------------------------------
    QMenu *vectorMenu = makeMenu( menuBar()->addMenu( tr( "矢量(&T)" ) ) );

    QMenu *vecGeo = makeMenu( vectorMenu->addMenu( tr( "几何处理" ) ) );
    setMenuIcon( vecGeo, ic( "buffer" ) );
    tip( vecGeo->addAction( ic( "buffer" ), tr( "缓冲区..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_buffer" ) );
        } ),
         tr( "矢量缓冲区分析。" ) );
    tip( vecGeo->addAction( ic( "dissolve" ), tr( "融合..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_dissolve" ) );
        } ),
         tr( "按属性融合要素。" ) );
    tip( vecGeo->addAction( ic( "merge" ), tr( "合并..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_merge" ) );
        } ),
         tr( "合并多个矢量图层。" ) );
    tip( vecGeo->addAction( ic( "cli_" ), tr( "裁剪..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_clip" ) );
        } ),
         tr( "按边界裁剪矢量。" ) );

    QMenu *vecOverlay = makeMenu( vectorMenu->addMenu( tr( "叠加分析" ) ) );
    setMenuIcon( vecOverlay, ic( "overl_y" ) );
    tip( vecOverlay->addAction( ic( "er_se" ), tr( "擦除..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_difference" ) );
        } ),
         tr( "矢量擦除 / 差集。" ) );
    tip( vecOverlay->addAction( ic( "overl_y" ), tr( "相交..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_intersection" ) );
        } ),
         tr( "矢量相交。" ) );
    tip( vecOverlay->addAction( ic( "merge" ), tr( "联合..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:native_union" ) );
        } ),
         tr( "矢量联合。" ) );

    QMenu *vecSelect = makeMenu( vectorMenu->addMenu( tr( "空间选择" ) ) );
    setMenuIcon( vecSelect, ic( "select_by_loc" ) );
    tip( vecSelect->addAction( ic( "select_by_loc" ), tr( "按位置选择..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_select_by_location" ) );
        } ),
         tr( "按空间关系选择要素。" ) );
    tip( vecSelect->addAction( ic( "extr_ct_by_m_sk" ), tr( "按位置提取..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_extract_by_location" ) );
        } ),
         tr( "按空间关系提取要素到新图层。" ) );

    QMenu *vecAttr = makeMenu( vectorMenu->addMenu( tr( "属性与投影" ) ) );
    setMenuIcon( vecAttr, ic( "field_c_lc" ) );
    tip( vecAttr->addAction( ic( "re_roject" ), tr( "重投影..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_reproject" ) );
        } ),
         tr( "矢量重投影。" ) );
    tip( vecAttr->addAction( ic( "field_c_lc" ), tr( "字段计算器..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_field_calculator" ) );
        } ),
         tr( "字段计算器。" ) );
    tip( vecAttr->addAction( ic( "closest_f_cility" ), tr( "最近邻..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_nearest_neighbor" ) );
        } ),
         tr( "最近邻分析。" ) );
    tip( vecAttr->addAction( ic( "st_tistics" ), tr( "距离矩阵..." ), this, [this]() {
          openProcessingAlgorithm( QStringLiteral( "qgis_algorithms:vector_distance_matrix" ) );
        } ),
         tr( "距离矩阵。" ) );

    // ------------------------------------------------------------------
    // 处理 Processing — toolbox / history / batch only
    // ------------------------------------------------------------------
    QMenu *processingMenu = makeMenu( menuBar()->addMenu( tr( "处理(&O)" ) ) );
    tip( processingMenu->addAction( ic( "toolbox" ), tr( "工具箱" ),
                                    this, &QgisDesktopWindow::showProcessingToolbox ),
         tr( "打开处理工具箱：GDAL / OTB / 内置算法。" ) );
    tip( processingMenu->addAction( ic( "log_viewer" ), tr( "历史记录" ),
                                    this, &QgisDesktopWindow::showProcessingHistory ),
         tr( "查看已运行处理算法的历史。" ) );
    processingMenu->addSeparator();
    tip( processingMenu->addAction( ic( "b_tch" ), tr( "批量处理..." ),
                                    this, &QgisDesktopWindow::openBatchProcessingDialog ),
         tr( "同一算法批量处理多个输入文件。" ) );

    // ------------------------------------------------------------------
    // 设置 Settings
    // ------------------------------------------------------------------
    QMenu *settingsMenu = makeMenu( menuBar()->addMenu( tr( "设置(&S)" ) ) );
    tip( settingsMenu->addAction( ic( "settings" ), tr( "选项..." ),
                                  this, &QgisDesktopWindow::options ),
         tr( "主题、默认 CRS、日志、GDAL/OTB 路径。" ) );
    settingsMenu->addSeparator();
    tip( settingsMenu->addAction( ic( "define_crs" ), tr( "CRS 预设..." ),
                                  this, &QgisDesktopWindow::openCrsPresetDialog ),
         tr( "浏览并选择常用坐标系预设。" ) );

    // Window Menu (dock toggle actions added in setupDockWidgets)
    m_windowMenu = makeMenu( menuBar()->addMenu( tr( "窗口(&W)" ) ) );
    setMenuIcon( m_windowMenu, ic( "p_nel_l_yout" ) );

    // ------------------------------------------------------------------
    // 帮助 Help
    // ------------------------------------------------------------------
    QMenu *helpMenu = makeMenu( menuBar()->addMenu( tr( "帮助(&H)" ) ) );
    tip( helpMenu->addAction( ic( "hel_" ), tr( "帮助内容" ),
                              this, &QgisDesktopWindow::helpContents, QKeySequence::HelpContents ),
         tr( "打开帮助文档。" ) );
    helpMenu->addSeparator();
    tip( helpMenu->addAction( ic( "s_tellite" ), tr( "加载示例数据" ),
                              this, &QgisDesktopWindow::loadSampleData ),
         tr( "加载内置示例数据集。" ) );
    tip( helpMenu->addAction( ic( "workflow" ), tr( "引导工作流" ),
                              this, &QgisDesktopWindow::showGuidedWorkflows ),
         tr( "分步引导式实验流程。" ) );
    helpMenu->addSeparator();
    tip( helpMenu->addAction( ic( "met_d_t_" ), tr( "检查版本" ),
                              this, &QgisDesktopWindow::checkVersion ),
         tr( "显示当前版本信息。" ) );
    tip( helpMenu->addAction( ic( "app_icon" ), tr( "关于" ),
                              this, &QgisDesktopWindow::about ),
         tr( "关于本软件。" ) );
}

void QgisDesktopWindow::setupToolbars()
{
    // Unified toolbar chrome
    auto polishBar = []( QToolBar *bar ) {
      if ( !bar )
        return;
      bar->setIconSize( QSize( 22, 22 ) );
      bar->setToolButtonStyle( Qt::ToolButtonIconOnly );
      bar->setMovable( true );
      bar->setFloatable( false );
    };

    // File Toolbar
    QToolBar *fileToolBar = addToolBar( tr( "文件" ) );
    fileToolBar->setObjectName( "fileToolBar" );
    polishBar( fileToolBar );

    tip( fileToolBar->addAction( ic( "new_project" ), tr( "新建工程" ),
                                 this, &QgisDesktopWindow::newProject ),
         tr( "新建工程 (Ctrl+N)" ) );
    tip( fileToolBar->addAction( ic( "o_en" ), tr( "打开工程" ),
                                 this, &QgisDesktopWindow::openProject ),
         tr( "打开工程 (Ctrl+O)" ) );
    tip( fileToolBar->addAction( ic( "s_ve" ), tr( "保存工程" ),
                                 this, &QgisDesktopWindow::saveProject ),
         tr( "保存工程 (Ctrl+S)" ) );
    fileToolBar->addSeparator();
    tip( fileToolBar->addAction( ic( "r_ster" ), tr( "添加栅格" ),
                                 this, &QgisDesktopWindow::addRasterLayer ),
         tr( "添加栅格图层" ) );
    tip( fileToolBar->addAction( ic( "vector" ), tr( "添加矢量" ),
                                 this, &QgisDesktopWindow::addVectorLayer ),
         tr( "添加矢量图层" ) );

    // Map Tools Toolbar
    QToolBar *mapToolsToolBar = addToolBar( tr( "地图工具" ) );
    mapToolsToolBar->setObjectName( "mapToolsToolBar" );
    polishBar( mapToolsToolBar );

    tip( mapToolsToolBar->addAction( ic( "p_n" ), tr( "平移" ),
                                     this, &QgisDesktopWindow::panMap ),
         tr( "平移 (Space)" ) );
    tip( mapToolsToolBar->addAction( ic( "zoo_in" ), tr( "放大" ),
                                     this, &QgisDesktopWindow::zoomIn ),
         tr( "放大" ) );
    tip( mapToolsToolBar->addAction( ic( "zoo_out" ), tr( "缩小" ),
                                     this, &QgisDesktopWindow::zoomOut ),
         tr( "缩小" ) );
    tip( mapToolsToolBar->addAction( ic( "full_extent" ), tr( "全图" ),
                                     this, &QgisDesktopWindow::zoomFullExtent ),
         tr( "全图 (Ctrl+Shift+F)" ) );
    mapToolsToolBar->addSeparator();
    tip( mapToolsToolBar->addAction( ic( "identify" ), tr( "识别" ),
                                     this, &QgisDesktopWindow::identifyFeatures ),
         tr( "识别 (Ctrl+Shift+I)" ) );
    mapToolsToolBar->addSeparator();
    tip( mapToolsToolBar->addAction( ic( "me_sure_dist" ), tr( "测距" ),
                                     this, &QgisDesktopWindow::measureDistance ),
         tr( "测距 (Ctrl+Shift+D)" ) );
    tip( mapToolsToolBar->addAction( ic( "me_sure_are_" ), tr( "测面" ),
                                     this, &QgisDesktopWindow::measureArea ),
         tr( "测面 (Ctrl+Shift+A)" ) );

    // CRS Selector in toolbar
    m_crsSelector = new QgsProjectionSelectionWidget(mapToolsToolBar);
    m_crsSelector->setOptionVisible(QgsProjectionSelectionWidget::ProjectCrs, true);
    connect(m_crsSelector, &QgsProjectionSelectionWidget::crsChanged,
            this, &QgisDesktopWindow::onCrsChanged);
    mapToolsToolBar->addSeparator();
    mapToolsToolBar->addWidget(m_crsSelector);

    // Remote Sensing Toolbar — 分析快捷入口（与「分析」菜单对应）
    QToolBar *rsToolBar = addToolBar( tr( "遥感分析" ) );
    rsToolBar->setObjectName( "rsToolBar" );
    polishBar( rsToolBar );

    tip( rsToolBar->addAction( QIcon( ":/icons/veget_tion_index" ), tr( "光谱指数" ),
                               this, &QgisDesktopWindow::openSpectralIndexDialog ),
         tr( "光谱指数：NDVI / EVI 等。" ) );
    tip( rsToolBar->addAction( QIcon( ":/icons/at_os_corr" ), tr( "大气校正" ),
                               this, &QgisDesktopWindow::openAtmosphericCorrectionDialog ),
         tr( "大气校正：DOS1 / DOS2。" ) );
    tip( rsToolBar->addAction( QIcon( ":/icons/mos_ic" ), tr( "镶嵌" ),
                               this, &QgisDesktopWindow::openMosaicDialog ),
         tr( "多景镶嵌。" ) );
    tip( rsToolBar->addAction( QIcon( ":/icons/b_nd_m_th" ), tr( "波段运算" ),
                               this, &QgisDesktopWindow::openBandMathDialog ),
         tr( "波段运算表达式。" ) );
    rsToolBar->addSeparator();
    tip( rsToolBar->addAction( QIcon( ":/icons/r_ster_calc" ), tr( "配准 I2I" ),
                               this, &QgisDesktopWindow::openGeorefImageToImage ),
         tr( "影像对影像配准。" ) );
    tip( rsToolBar->addAction( QIcon( ":/icons/su_ervised" ), tr( "监督分类" ),
                               this, &QgisDesktopWindow::openClassificationWindow ),
         tr( "像元级监督分类。" ) );

    // Digitizing Toolbar
    QToolBar *digitizeToolBar = addToolBar( tr( "数字化" ) );
    digitizeToolBar->setObjectName( "digitizeToolBar" );
    polishBar( digitizeToolBar );

    m_toggleEditingAction->setToolTip( tr( "切换编辑 (Ctrl+E)" ) );
    digitizeToolBar->addAction( m_toggleEditingAction );
    m_saveEditsAction->setToolTip( tr( "保存编辑" ) );
    digitizeToolBar->addAction( m_saveEditsAction );
    digitizeToolBar->addSeparator();
    auto *actSelect = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionSelectRectangle" ), tr( "选择" ),
      this, &QgisDesktopWindow::selectFeatures );
    actSelect->setToolTip( tr( "选择要素" ) );
    auto *actAddFeature = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionCapturePoint" ), tr( "添加要素" ),
      this, &QgisDesktopWindow::addFeature );
    actAddFeature->setToolTip( tr( "添加要素" ) );
    auto *actVertex = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionVertexTool" ), tr( "节点" ),
      this, &QgisDesktopWindow::vertexTool );
    actVertex->setToolTip( tr( "节点工具" ) );
    digitizeToolBar->addSeparator();
    auto *actMove = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionMoveFeature" ), tr( "移动" ),
      this, &QgisDesktopWindow::moveFeature );
    actMove->setToolTip( tr( "移动要素" ) );
    auto *actRotate = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionRotateFeature" ), tr( "旋转" ),
      this, &QgisDesktopWindow::rotateFeature );
    actRotate->setToolTip( tr( "旋转要素" ) );
    auto *actReshape = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionReshape" ), tr( "重塑" ),
      this, &QgisDesktopWindow::reshapeGeometry );
    actReshape->setToolTip( tr( "重塑几何" ) );
    auto *actSplit = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionSplitFeatures" ), tr( "分割" ),
      this, &QgisDesktopWindow::splitFeatures );
    actSplit->setToolTip( tr( "分割要素" ) );
    digitizeToolBar->addSeparator();
    auto *actOffset = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionOffsetCurve" ), tr( "偏移" ),
      this, &QgisDesktopWindow::offsetCurve );
    actOffset->setToolTip( tr( "偏移线" ) );
    auto *actSimplify = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionSimplify" ), tr( "简化" ),
      this, &QgisDesktopWindow::simplifyFeature );
    actSimplify->setToolTip( tr( "简化" ) );
    auto *actReverse = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionReverseLine" ), tr( "反转" ),
      this, &QgisDesktopWindow::reverseLine );
    actReverse->setToolTip( tr( "反转线方向" ) );
    auto *actAddRing = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionAddRing" ), tr( "添加环" ),
      this, &QgisDesktopWindow::addRing );
    actAddRing->setToolTip( tr( "添加环" ) );
    auto *actFillRing = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionFillRing" ), tr( "填充环" ),
      this, &QgisDesktopWindow::fillRing );
    actFillRing->setToolTip( tr( "填充环" ) );
    auto *actDelPart = digitizeToolBar->addAction(
      QIcon( ":/icons/mActionDeletePart" ), tr( "删除部件" ),
      this, &QgisDesktopWindow::deletePart );
    actDelPart->setToolTip( tr( "删除部件" ) );

    // Editing tool actions — enabled only when a vector layer is in editing mode
    m_editingToolActions = { actSelect, actAddFeature, actVertex, actMove, actRotate,
                             actReshape, actSplit, actOffset, actSimplify, actReverse,
                             actAddRing, actFillRing, actDelPart };
    for ( QAction *a : m_editingToolActions )
      a->setEnabled( false );

    // Fill empty tooltips with cleaned action text
    for ( QAction *a : findChildren<QAction *>() )
    {
      if ( !a || a->isSeparator() )
        continue;
      if ( a->toolTip().isEmpty() )
      {
        const QString t = a->text().remove( QLatin1Char( '&' ) ).trimmed();
        if ( !t.isEmpty() )
        {
          a->setToolTip( t );
          a->setStatusTip( t );
        }
      }
    }
}
void QgisDesktopWindow::setupStatusBar()
{
    QStatusBar *bar = statusBar();
    bar->setObjectName("rsStatusBar");
    bar->setFixedHeight(22);

    // Ready status (left side)
    m_readyLabel = new QLabel("Ready", bar);
    m_readyLabel->setObjectName("rsReadyLabel");
    bar->addWidget(m_readyLabel);

    // Coordinates display
    m_coordinatesLabel = new QLabel("0.000000, 0.000000", bar);
    m_coordinatesLabel->setObjectName("rsCoordLabel");
    bar->addPermanentWidget(m_coordinatesLabel);

    // Scale display
    m_scaleLabel = new QLabel("Scale: 1:1,000", bar);
    m_scaleLabel->setObjectName("rsScaleLabel");
    bar->addPermanentWidget(m_scaleLabel);

    // CRS display
    m_crsLabel = new QLabel("EPSG:3857", bar);
    m_crsLabel->setObjectName("rsCrsLabel");
    bar->addPermanentWidget(m_crsLabel);

    // Render time display
    m_renderTimeLabel = new QLabel("", bar);
    m_renderTimeLabel->setObjectName("rsRenderLabel");
    bar->addPermanentWidget(m_renderTimeLabel);

    // Cache usage
    m_cacheLabel = new QLabel("Cache: 0 MB", bar);
    m_cacheLabel->setObjectName("rsCacheLabel");
    bar->addPermanentWidget(m_cacheLabel);
}
