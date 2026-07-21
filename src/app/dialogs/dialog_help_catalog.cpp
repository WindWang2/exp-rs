// dialog_help_catalog.cpp
#include "dialog_help_catalog.h"

#include <QAction>
#include <QDialog>
#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QWidget>

namespace
{

struct Entry
{
    const char *summary; // short one-line
    const char *body;    // multi-sentence plain (wrapped as <p>)
};

const QHash<QString, Entry> &catalog()
{
    static const QHash<QString, Entry> k = {
        // --- Raster processing dialogs (toolName) ---
        { QStringLiteral( "spectral_index" ),
          { "计算 NDVI/NDWI 等光谱指数",
            "选择输入栅格与指数类型，输出单波段指数影像。适用于植被、水体等专题提取。" } },
        { QStringLiteral( "terrain" ),
          { "DEM 地形分析",
            "从 DEM 生成坡度、坡向、山体阴影等。可设置像元大小、太阳方位角与高度角。" } },
        { QStringLiteral( "extract_band" ),
          { "提取栅格波段",
            "从多波段栅格中抽取指定波段保存为新文件，便于单波段分析或组合。" } },
        { QStringLiteral( "mosaic" ),
          { "多景影像镶嵌",
            "将多幅栅格镶嵌为连续影像。注意投影一致与重叠区处理策略。" } },
        { QStringLiteral( "atmospheric_correction" ),
          { "大气校正",
            "将 DN 转为辐射/反射率或做 DOS 类暗目标扣除，改善光学影像定量应用。" } },
        { QStringLiteral( "contrast_stretch" ),
          { "对比度拉伸",
            "线性、百分比截断、标准差或直方图均衡，改善显示与后续分类效果。" } },
        { QStringLiteral( "fusion" ),
          { "影像融合/全色锐化",
            "将高分辨率全色与多光谱融合，提高空间分辨率同时尽量保持光谱。" } },
        { QStringLiteral( "change_detection" ),
          { "变化检测",
            "比较前后时相影像，输出差值/归一化差值等变化图层，需几何对齐。" } },
        { QStringLiteral( "speckle_filter" ),
          { "斑点滤波",
            "抑制 SAR 斑点噪声（Lee/Frost 等）。滤波窗口越大越平滑，细节损失越多。" } },
        { QStringLiteral( "band_ratio" ),
          { "波段比值 / IHS",
            "波段比值突出地物差异；IHS 可将 RGB 与强度分量组合做变换分析。" } },
        { QStringLiteral( "pca" ),
          { "主成分分析 (PCA)",
            "对多波段做正交变换，压缩冗余、突出方差主成分，常用于去相关与特征降维。" } },
        { QStringLiteral( "spatial_filter" ),
          { "空间滤波",
            "均值/中值/锐化等卷积滤波，用于平滑噪声或增强边缘。" } },
        { QStringLiteral( "image_enhancement" ),
          { "影像增强",
            "综合亮度、对比度、直方图与显示拉伸，改善目视解译效果。" } },
        { QStringLiteral( "band_math" ),
          { "波段运算",
            "用表达式计算多波段结果，如 (b1-b2)/(b1+b2)。波段序号从 1 开始。" } },

        // --- Standalone dialogs ---
        { QStringLiteral( "batch_processing" ),
          { "批量处理",
            "选择算法与多个输入文件，统一输出目录批量运行，适合重复流水线。" } },
        { QStringLiteral( "preferences" ),
          { "首选项",
            "配置 GDAL/OTB 路径、界面与工程默认项。修改后部分选项需重启生效。" } },
        { QStringLiteral( "stac_browser" ),
          { "STAC 数据浏览",
            "浏览 STAC 目录、检索时空范围与资产，下载或加载遥感数据产品。" } },
        { QStringLiteral( "comparison" ),
          { "图层对比",
            "左右对照显示两个图层，便于目视比较配准、变化或分类结果。" } },
        { QStringLiteral( "crs_preset" ),
          { "CRS 预设",
            "按名称/EPSG 搜索并选择常用坐标系预设。" } },
        { QStringLiteral( "processing_algorithm" ),
          { "处理算法对话框",
            "设置算法参数后运行。GDAL/OTB 工具底部可预览 CLI 命令；结果可选加载到图层。" } },
        { QStringLiteral( "sift_match" ),
          { "SIFT 自动匹配",
            "在双影像间提取 SIFT 特征并筛选内点，可批量生成 GCP（需 OpenCV）。" } },
        { QStringLiteral( "map_coords" ),
          { "GCP 目标坐标",
            "输入或从地图拾取控制点目标坐标，并与源像素位置配对。" } },
        { QStringLiteral( "georef_i2i" ),
          { "Image 2 Image 配准",
            "左侧源影像 (Warp) 与右侧参考影像 (Base) 双画布点选 GCP。"
            "可从文件或主工程图层打开影像；两侧都打开后 Add/Move/Delete GCP 才可用。"
            "支持 SIFT 自动匹配。右侧「校正参数」控制变换方法与输出；运行后任务列表跟踪 warp。"
            "同景多波段配准时请关闭 Sync zoom，并对比 GCP 表中的行列号。" } },
        { QStringLiteral( "georef_i2m" ),
          { "Image 2 Map 配准",
            "上方源影像 (Warp) 对下方主工程地图 (Base) 取 GCP。"
            "源可从文件或工程图层打开；Map 侧同步主窗口可见图层。"
            "变换方法含 RPC Physical（需源含 RPC，可选 DEM）。运行后任务列表跟踪结果。" } },
        { QStringLiteral( "georef_params" ),
          { "几何校正参数（右侧面板）",
            "【坐标变换】根据 GCP 拟合源→目标几何模型。"
            "Linear/Helmert：全局相似或线性，最少约 2 点；"
            "多项式 1/2/3：阶数越高越灵活，需更多点（约 3/6/10），易过拟合；"
            "TPS：局部弹性变形；Projective：透视；RPC：仅 I2M。"
            "最少点数/实际点数/DOF：DOF=实际−最少，>0 才能用残差评估精度。"
            "【重采样】写出结果时的插值：Nearest 保类别，Bilinear/Cubic 适合连续影像。"
            "输出像元大小 auto=自动；背景值为空洞填充。"
            "【RMS 误差分布】散点与 X/Y/Total/最大残差（源像元）。同名点应对齐良好时 RMS 宜小。"
            "【坐标系】目标 CRS 用于拟合与输出 GeoTIFF；I2I 常跟参考影像 CRS。"
            "【输出】结果路径，必填后才能运行。"
            "【DEM】仅 RPC 显示，可选高程模型与偏移。" } },
        { QStringLiteral( "georef_gcp_table" ),
          { "GCP 控制点表",
            "每行一对源/参考控制点。列：地图坐标 (map)、像元列/行、残差 ΔX/ΔY/RMS。"
            "右键可定位、启用/禁用、编辑、删除。Delete 键删除选中行。"
            "同景配准时「列源/行源」应接近「列参/行参」。" } },
        { QStringLiteral( "georef_tasks" ),
          { "校正任务列表",
            "点「运行」后 warp 任务出现在此：进度、取消、完成后双击或菜单加载结果到工程。" } },
        { QStringLiteral( "classification" ),
          { "监督分类",
            "采集 ROI 训练样本 → 选择算法与波段 → 训练并分类。"
            "可设 NoData/忽略值处理边缘；快速预览只分类当前视口；"
            "交叉验证评估模型；精度评价用混淆矩阵与 Kappa。" } },
        { QStringLiteral( "classify_setup" ),
          { "分类设置栏",
            "算法：NormalBayes / SVM / K-Means（其余为占位）。"
            "波段：逗号分隔如 1,2,3。"
            "训练比例：分层抽样中用于训练的比例，其余作测试精度。"
            "输出：分类结果 GeoTIFF。"
            "使用源 NoData / 忽略值：边缘与背景不参与分类。"
            "匹配：任一波段忽略则整像素忽略（默认），或全部波段才忽略。" } },
        { QStringLiteral( "obia" ),
          { "面向对象分类 (OBIA)",
            "先分割生成对象，再基于对象光谱/形状特征分类，适合高分辨率场景。" } },
        { QStringLiteral( "accuracy" ),
          { "精度评价",
            "混淆矩阵、总体精度 OA、Kappa、生产者/用户精度，用于验证分类结果。" } },
        { QStringLiteral( "main_window" ),
          { "主窗口",
            "图层树管理数据；地图画布浏览；处理工具箱运行 GDAL/OTB；"
            "栅格菜单打开配准、分类、OBIA 等专题窗口。" } },
    };
    return k;
}

QString wrapBody( const QString &title, const QString &summary, const QString &body )
{
    QString bodyHtml = body.toHtmlEscaped();
    bodyHtml.replace( QLatin1String( "\n" ), QLatin1String( "<br/>" ) );
    // Light markup: 【section】 as bold line starts
    bodyHtml.replace( QStringLiteral( "【" ), QStringLiteral( "<br/><b>【" ) );
    bodyHtml.replace( QStringLiteral( "】" ), QStringLiteral( "】</b> " ) );
    return QObject::tr(
             "<h3>%1</h3>"
             "<p><b>%2</b></p>"
             "<p>%3</p>"
             "<hr/>"
             "<p>提示：将鼠标悬停在任意控件上可查看该项说明；"
             "菜单 Help → 这是什么？(Shift+F1) 后点击控件；"
             "工具箱中的 GDAL/OTB 算法另有独立帮助与命令预览。</p>" )
      .arg( title.toHtmlEscaped(), summary.toHtmlEscaped(), bodyHtml );
}

} // namespace

void SicnuDialogHelp::tip( QWidget *w, const QString &text )
{
    if ( !w || text.isEmpty() )
        return;
    w->setToolTip( text );
    w->setStatusTip( text );
    w->setWhatsThis( text );
}

void SicnuDialogHelp::tip( QAction *a, const QString &text )
{
    if ( !a || text.isEmpty() )
        return;
    a->setToolTip( text );
    a->setStatusTip( text );
    a->setWhatsThis( text );
}

QString SicnuDialogHelp::shortForTool( const QString &toolId, const QString &titleFallback )
{
    const auto it = catalog().constFind( toolId );
    if ( it != catalog().constEnd() )
        return QObject::tr( it->summary );
    if ( !titleFallback.isEmpty() )
        return titleFallback;
    return toolId;
}

QString SicnuDialogHelp::htmlForTool( const QString &toolId, const QString &titleFallback )
{
    const auto it = catalog().constFind( toolId );
    const QString title = titleFallback.isEmpty() ? toolId : titleFallback;
    if ( it != catalog().constEnd() )
    {
        return wrapBody( title, QObject::tr( it->summary ), QObject::tr( it->body ) );
    }
    return wrapBody(
      title,
      QObject::tr( "功能说明" ),
      QObject::tr( "请按对话框中的标签填写输入、参数与输出路径，然后运行。"
                   "悬停控件可查看更多提示。" ) );
}

void SicnuDialogHelp::applyDialogChrome( QDialog *dlg, const QString &toolId )
{
    if ( !dlg )
        return;
    const QString title = dlg->windowTitle().isEmpty() ? toolId : dlg->windowTitle();
    const QString summary = shortForTool( toolId, title );
    dlg->setToolTip( summary );
    dlg->setWhatsThis( htmlForTool( toolId, title ) );
    dlg->setStatusTip( summary );
}

void SicnuDialogHelp::showHelpBox( QWidget *parent, const QString &title, const QString &html )
{
    QMessageBox box( parent );
    box.setWindowTitle( title.isEmpty() ? QObject::tr( "帮助" ) : title );
    box.setTextFormat( Qt::RichText );
    box.setIcon( QMessageBox::Information );
    box.setText( html );
    box.setStandardButtons( QMessageBox::Ok );
    box.exec();
}

void SicnuDialogHelp::showToolHelp( QWidget *parent, const QString &toolId, const QString &title )
{
    showHelpBox( parent, title.isEmpty() ? QObject::tr( "帮助" ) : title,
                 htmlForTool( toolId, title ) );
}
