/***************************************************************************
 * command_defs.cpp — capability → definition wiring (batch 1)
 ***************************************************************************/
#include "command_defs.h"

#include "command_registry.h"
#include "main_window.h"
#include "workbench_host.h"

#include "dialogs/extract_band_dialog.h"

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include <functional>

using sicnu::app::CommandDefinition;
using sicnu::app::SelectionContextSnapshot;
namespace ContextRules = sicnu::app::ContextRules;

namespace
{

CommandDefinition base( const char *id, const QString &title, const QString &description,
                        const QString &icon, const QString &category )
{
    CommandDefinition def;
    def.id = QString::fromUtf8( id );
    def.title = title;
    def.description = description;
    def.iconName = icon;
    def.category = category;
    return def;
}

/// Standard edit-includes for brevity below.
#define RS_CMD( var, id, title, desc, icon, category ) \
    CommandDefinition var = base( id, title, desc, icon, category )

} // namespace

void registerShellCommands( sicnu::app::CommandRegistry *registry, QgisDesktopWindow *window )
{
    if ( !registry || !window )
        return;

    // ── 工程 Project ──────────────────────────────────────────────────
    {
        RS_CMD( d, "project.new", QObject::tr( "新建工程" ),
                QObject::tr( "创建空白工程，清除当前图层与视图状态。" ),
                "new_project", QObject::tr( "工程" ) );
        d.shortcut = QKeySequence::New;
        d.handler = [window] { window->newProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.open", QObject::tr( "打开工程..." ),
                QObject::tr( "打开已保存的工程文件。" ),
                "o_en", QObject::tr( "工程" ) );
        d.shortcut = QKeySequence::Open;
        d.handler = [window] { window->openProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.save", QObject::tr( "保存工程" ),
                QObject::tr( "保存当前工程到已有路径。" ),
                "s_ve", QObject::tr( "工程" ) );
        d.shortcut = QKeySequence::Save;
        d.handler = [window] { window->saveProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.saveAs", QObject::tr( "工程另存为..." ),
                QObject::tr( "将工程另存为新文件。" ),
                "ex_ort", QObject::tr( "工程" ) );
        d.handler = [window] { window->saveProjectAs(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.importLayer", QObject::tr( "导入图层..." ),
                QObject::tr( "导入栅格或矢量图层到工程。" ),
                "i_ort", QObject::tr( "工程" ) );
        d.handler = [window] { window->importLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.stacBrowse", QObject::tr( "浏览 STAC 目录..." ),
                QObject::tr( "浏览 STAC 目录检索遥感数据。" ),
                "cloud_sync", QObject::tr( "工程" ) );
        d.handler = [window] { window->browseStacCatalog(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.newLayout", QObject::tr( "新建布局..." ),
                QObject::tr( "创建打印布局 / 出图。" ),
                "print_l_yout", QObject::tr( "工程" ) );
        // The layout bench opens the same designer — keep the workbench id
        // truthful when the surface goes through the command (review L #8).
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "layout" ) );
            else
                window->newLayout();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.exit", QObject::tr( "退出" ),
                QObject::tr( "退出应用程序。" ), QString(), QObject::tr( "工程" ) );
        d.shortcut = QKeySequence::Quit;
        d.destructive = true;
        d.handler = [window] { window->close(); };
        registry->registerCommand( d );
    }

    // ── 图层 Layer ────────────────────────────────────────────────────
    {
        RS_CMD( d, "layer.addRaster", QObject::tr( "添加栅格图层..." ),
                QObject::tr( "从文件添加栅格图层。" ), "r_ster", QObject::tr( "图层" ) );
        d.handler = [window] { window->addRasterLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.addVector", QObject::tr( "添加矢量图层..." ),
                QObject::tr( "从文件添加矢量图层。" ), "vector", QObject::tr( "图层" ) );
        d.handler = [window] { window->addVectorLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.properties", QObject::tr( "图层属性..." ),
                QObject::tr( "打开当前图层属性。" ), "met_d_t_", QObject::tr( "图层" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+I" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->layerProperties(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.remove", QObject::tr( "移除图层" ),
                QObject::tr( "从工程中移除当前图层。" ), "er_se", QObject::tr( "图层" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+Delete" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.destructive = true;
        d.handler = [window] { window->removeLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.zoomTo", QObject::tr( "缩放到图层" ),
                QObject::tr( "缩放到当前图层范围。" ), "l_yer_m_n_ger", QObject::tr( "图层" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+L" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->zoomToLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.toggleEditing", QObject::tr( "切换编辑" ),
                QObject::tr( "开启/关闭当前矢量图层编辑。" ),
                "mActionToggleEditing", QObject::tr( "矢量编辑" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+E" ) );
        d.checkable = true;
        d.availability = ContextRules::editingAvailable;
        d.checkedState = ContextRules::editingActive;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->toggleEditing(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.saveEdits", QObject::tr( "保存编辑" ),
                QObject::tr( "保存矢量编辑。" ), "mActionSaveEdits", QObject::tr( "矢量编辑" ) );
        d.availability = ContextRules::editingActive;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->saveEdits(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.newVector", QObject::tr( "新建 Shapefile 图层..." ),
                QObject::tr( "创建新的 Shapefile 矢量图层。" ),
                "new_fe_ture_cl_ss", QObject::tr( "图层" ) );
        d.handler = [window] { window->newVectorLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.attributeTable", QObject::tr( "打开属性表" ),
                QObject::tr( "查看/编辑当前矢量图层属性表。" ),
                "t_ble", QObject::tr( "图层" ) );
        d.availability = ContextRules::vectorSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->openAttributeTable(); };
        registry->registerCommand( d );
    }

    // ── 视图 / 地图工具 View ─────────────────────────────────────────
    {
        RS_CMD( d, "map.zoomIn", QObject::tr( "放大" ), QObject::tr( "放大地图视图。" ),
                "zoo_in", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence::ZoomIn;
        d.handler = [window] { window->zoomIn(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.zoomOut", QObject::tr( "缩小" ), QObject::tr( "缩小地图视图。" ),
                "zoo_out", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence::ZoomOut;
        d.handler = [window] { window->zoomOut(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.zoomFull", QObject::tr( "全图" ), QObject::tr( "缩放到所有图层范围。" ),
                "full_extent", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+F" ) );
        d.handler = [window] { window->zoomFullExtent(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.pan", QObject::tr( "平移" ), QObject::tr( "平移地图。" ),
                "p_n", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "H" ) );
        d.handler = [window] { window->panMap(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.identify", QObject::tr( "识别" ),
                QObject::tr( "点击地图查询要素/像元属性。" ), "identify", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+I" ) );
        d.handler = [window] { window->identifyFeatures(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.measureDistance", QObject::tr( "测距" ), QObject::tr( "量测距离。" ),
                "me_sure_dist", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+D" ) );
        d.handler = [window] { window->measureDistance(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.measureArea", QObject::tr( "测面" ), QObject::tr( "量测面积。" ),
                "me_sure_are_", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+A" ) );
        d.handler = [window] { window->measureArea(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.refresh", QObject::tr( "刷新" ), QObject::tr( "刷新地图渲染。" ),
                "refresh_view", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "F5" ) );
        d.handler = [window] { window->refreshMap(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.compareLayers", QObject::tr( "图层对比..." ),
                QObject::tr( "左右并排对比两个图层。" ), "overl_y", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+C" ) );
        d.handler = [window] { window->openComparisonDialog(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.swipe", QObject::tr( "卷帘对比" ),
                QObject::tr( "在地图上拖动分割线对比上下图层。" ), "s_lit", QObject::tr( "地图" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+S" ) );
        d.handler = [window] { window->toggleSwipeTool(); };
        registry->registerCommand( d );
    }

    // ── 工作区 Workbenches ───────────────────────────────────────────
    {
        RS_CMD( d, "workbench.classify", QObject::tr( "分类工作区..." ),
                QObject::tr( "打开监督/非监督分类交互工作区。" ),
                "su_ervised", QObject::tr( "工作区" ) );
        // Route through WorkbenchHost::activate (review L #8): the opener
        // alone left m_activeId on the previous bench, desyncing the switcher
        // and the selection context's workbench projection.
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "classify" ) );
            else
                window->openClassificationWindow();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.georefI2I", QObject::tr( "影像对影像配准 (I2I)..." ),
                QObject::tr( "双画布 SRC|REF 同名点配准，支持 SIFT。不含 RPC。" ),
                "coregistr_tion", QObject::tr( "工作区" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "georef-i2i" ) );
            else
                window->openGeorefImageToImage();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.georefI2M", QObject::tr( "影像对地图配准 (I2M)..." ),
                QObject::tr( "源影像 + 主工程地图取点；支持 RPC Physical。" ),
                "geocorrection", QObject::tr( "工作区" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "georef-i2m" ) );
            else
                window->openGeorefImageToMap();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.obia", QObject::tr( "对象级分类 (OBIA)..." ),
                QObject::tr( "分割 + 对象特征 + 面向对象分类。" ),
                "seg_ent_tion", QObject::tr( "工作区" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "obia" ) );
            else
                window->openObiaWindow();
        };
        registry->registerCommand( d );
    }

    // ── 处理 Processing（对话框开放器批次） ──────────────────────────
    auto rasterTool = [&]( const char *id, const QString &title, const QString &desc,
                           const QString &icon, void ( QgisDesktopWindow::*slot )() ) {
        CommandDefinition d = base( id, title, desc, icon, QObject::tr( "处理" ) );
        d.availability = ContextRules::rasterSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window, slot] { ( window->*slot )(); };
        registry->registerCommand( d );
    };
    rasterTool( "rs.bandMath", QObject::tr( "波段数学..." ),
                QObject::tr( "表达式驱动的多波段运算。" ), "b_nd_m_th",
                &QgisDesktopWindow::openBandMathDialog );
    rasterTool( "rs.spectralIndex", QObject::tr( "光谱指数..." ),
                QObject::tr( "NDVI / NDWI / NDBI 等常用指数计算。" ), "s_ectr_l_profile",
                &QgisDesktopWindow::openSpectralIndexDialog );
    rasterTool( "rs.contrastStretch", QObject::tr( "对比度拉伸..." ),
                QObject::tr( "线性 / 百分比裁剪 / 直方图均衡输出。" ), "enh_nce",
                &QgisDesktopWindow::openContrastStretchDialog );
    rasterTool( "rs.spatialFilter", QObject::tr( "空间滤波..." ),
                QObject::tr( "均值 / 高斯 / 中值 / 拉普拉斯卷积。" ), "r_ster_c_lc",
                &QgisDesktopWindow::openSpatialFilterDialog );
    rasterTool( "rs.pca", QObject::tr( "主成分分析..." ),
                QObject::tr( "多波段 PCA 变换与逆变换。" ), "pca", &QgisDesktopWindow::openPcaDialog );
    rasterTool( "rs.bandRatio", QObject::tr( "波段比值..." ),
                QObject::tr( "两波段比值 / 归一化比值输出。" ), "b_nd_m_th_pro",
                &QgisDesktopWindow::openBandRatioDialog );
    rasterTool( "rs.mosaic", QObject::tr( "镶嵌..." ),
                QObject::tr( "多景栅格镶嵌为连续影像。" ), "mos_ic",
                &QgisDesktopWindow::openMosaicDialog );
    rasterTool( "rs.changeDetection", QObject::tr( "变化检测..." ),
                QObject::tr( "双时相差异 / 比值 / CVA 检测。" ), "ch_nge_detect",
                &QgisDesktopWindow::openChangeDetectionDialog );
    rasterTool( "rs.atmospheric", QObject::tr( "大气校正..." ),
                QObject::tr( "6S / DOS 反射率产品。" ), "at_os_corr",
                &QgisDesktopWindow::openAtmosphericCorrectionDialog );
    rasterTool( "rs.qaMask", QObject::tr( "QA 掩膜生成..." ),
                QObject::tr( " Landsat/Sentinel QA 波段解码为掩膜。" ), "cloud_m_sk",
                &QgisDesktopWindow::openQaMaskDialog );
    rasterTool( "rs.applyMask", QObject::tr( "应用掩膜..." ),
                QObject::tr( "以掩膜裁剪/置 NoData。" ), "fill_nod_t_",
                &QgisDesktopWindow::openApplyMaskDialog );
    rasterTool( "rs.radiometric", QObject::tr( "辐射定标..." ),
                QObject::tr( "DN → 辐亮度 / 反射率。" ), "r_dio__c_lib",
                &QgisDesktopWindow::openRadiometricCalibrationDialog );
    rasterTool( "rs.ortho", QObject::tr( "正射纠正..." ),
                QObject::tr( "RPC / GCP 几何纠正到地图坐标。" ), "geocorrection",
                &QgisDesktopWindow::openOrthorectificationDialog );
    rasterTool( "rs.terrain", QObject::tr( "地形分析..." ),
                QObject::tr( "坡度 / 坡向 / 山影等 DEM 产品。" ), "hillsh_de",
                &QgisDesktopWindow::openTerrainDialog );
    rasterTool( "rs.fusion", QObject::tr( "影像融合..." ),
                QObject::tr( "全色锐化 (Brovey / IHS / Gram-Schmidt)。" ), "p_nsh_r_en",
                &QgisDesktopWindow::openFusionDialog );
    rasterTool( "rs.temporal", QObject::tr( "时间序列分析..." ),
                QObject::tr( "时序 NDVI / 物候曲线分析。" ), "ti_e_series",
                &QgisDesktopWindow::openTemporalAnalysisDialog );

    // SAR 感知：斑点滤波要求 raster + SAR 双条件。
    {
        RS_CMD( d, "rs.speckle", QObject::tr( "斑点滤波 (SAR)..." ),
                QObject::tr( "Lee / Frost / Kuan / Gamma-MAP。需要 SAR 栅格。" ),
                "sar_process", QObject::tr( "处理" ) );
        d.availability = []( const SelectionContextSnapshot &s ) {
            return ContextRules::rasterSelected( s ) && ContextRules::sarSelected( s );
        };
        d.explain = []( const SelectionContextSnapshot &s ) {
            if ( !ContextRules::rasterSelected( s ) )
                return QObject::tr( "需要选中栅格图层" );
            return QObject::tr( "需要选中 SAR 数据" );
        };
        d.handler = [window] { window->openSpeckleFilterDialog(); };
        registry->registerCommand( d );
    }

    // 提取波段：菜单内联 lambda 能力收编为命令（行为一致：模态对话框）。
    {
        RS_CMD( d, "rs.extractBands", QObject::tr( "提取波段..." ),
                QObject::tr( "从多波段栅格提取单一波段保存。" ),
                "extr_ct_b_nd", QObject::tr( "处理" ) );
        d.availability = ContextRules::rasterSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] {
            ExtractBandDialog dlg( window );
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( window->mapCanvas()->currentLayer() ) )
                dlg.setRasterLayer( rl );
            dlg.exec();
        };
        registry->registerCommand( d );
    }
}
