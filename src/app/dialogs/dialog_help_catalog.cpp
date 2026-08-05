// dialog_help_catalog.cpp — Shared parameter-level help for all app tools.
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
        // ========== Raster processing dialogs (toolName) ==========
        { QStringLiteral( "spectral_index" ),
          { "光谱指数：NDVI / EVI / SAVI / NDWI / NDBI / MNDWI",
            "【指数 Index】\n"
            "• NDVI = (NIR−Red)/(NIR+Red)：植被长势，−1～1\n"
            "• EVI：增强植被指数，需 NIR/Red/Blue，抑制大气与土壤\n"
            "• SAVI：土壤调节植被指数，稀疏植被更好\n"
            "• NDWI = (Green−NIR)/(Green+NIR)：水体/湿润\n"
            "• NDBI = (SWIR−NIR)/(SWIR+NIR)：建成区\n"
            "• MNDWI = (Green−SWIR)/(Green+SWIR)：改进水体指数\n"
            "【波段】按传感器映射 NIR/Red/Green/Blue/SWIR（波段号从 1 起）。"
            "默认按 Landsat/Sentinel 常见顺序预填，请按实际数据核对。\n"
            "【输出】单波段浮点 GeoTIFF。运行前必须填写路径。" } },
        { QStringLiteral( "terrain" ),
          { "DEM 地形分析：坡度 / 坡向 / 山体阴影等",
            "【DEM 图层】高程栅格，单位应与 CRS 一致（米制投影更可靠）。\n"
            "【分析类型】\n"
            "• Slope：坡度（度）\n"
            "• Aspect：坡向（度，北为 0 顺时针）\n"
            "• Hillshade：山体阴影（需太阳方位/高度）\n"
            "• Roughness / TRI / TPI：粗糙度与地形位置指数\n"
            "【像元大小 Cell Size】地面分辨率（地图单位），选图层后常自动估算。\n"
            "【太阳方位/高度】仅 Hillshade：方位 0–360°（北为 0），高度 0–90°。\n"
            "【输出】单波段结果 GeoTIFF。" } },
        { QStringLiteral( "extract_band" ),
          { "从多波段栅格提取单一波段",
            "【栅格图层】工程中波段数>1 的栅格。\n"
            "【波段】要单独保存的波段号/名称。\n"
            "【输出】单波段 GeoTIFF，便于单波段分析或与其它数据组合。" } },
        { QStringLiteral( "mosaic" ),
          { "多景栅格镶嵌为连续影像",
            "【输入列表】至少 2 个栅格文件；投影宜一致，重叠区由引擎按默认策略合并。\n"
            "【添加/移除】管理参与镶嵌的文件。\n"
            "【输出】镶嵌后的 GeoTIFF。大图注意磁盘与内存。" } },
        { QStringLiteral( "atmospheric_correction" ),
          { "大气校正 / DN 转辐射",
            "【方法】\n"
            "• DN to Radiance：L = gain×DN + bias，需传感器增益/偏置\n"
            "• DOS1：暗目标减法，估算路径辐射\n"
            "• DOS2：在 DOS1 上考虑透过率，需气团(Airmass)\n"
            "• QUAC：基于图像统计的全波段快速大气校正，输出近似地表反射率[0,1]，无需外部参数\n"
            "【波段】处理的波段号（QUAC 自动处理全部波段，此项忽略）。\n"
            "【Gain / Bias】辐射定标系数（元数据或产品手册，QUAC 忽略）。\n"
            "【Airmass】仅 DOS2：气团，通常≥1。\n"
            "【输出】校正后栅格。" } },
        { QStringLiteral( "contrast_stretch" ),
          { "对比度拉伸，改善显示与后续分析",
            "【方法】\n"
            "• Linear：按最小–最大线性拉伸到输出范围\n"
            "• Percentage Clip：两端各裁剪 Clip% 后拉伸，抑制极端值\n"
            "• Std Dev：以均值±K×标准差拉伸\n"
            "• Histogram Equalization：直方图均衡，增强全局对比\n"
            "【Clip %】仅百分比裁剪，常用 1–2%。\n"
            "【Std Dev K】仅标准差法，常用 2。\n"
            "【输出】拉伸后多波段 GeoTIFF（按输入波段逐一处理）。" } },
        { QStringLiteral( "fusion" ),
          { "全色锐化 / 影像融合",
            "【全色 Panchromatic】高空间分辨率单波段。\n"
            "【多光谱 Multispectral】低分辨率多波段。两者需大致同范围、已配准。\n"
            "【方法】\n"
            "• Linear Weighted：加权融合，可调 Pan Weight 与分波段权重\n"
            "• Brovey：比值融合，快速\n"
            "• IHS：需指定 RGB 波段\n"
            "• PCA：主成分替换\n"
            "• OTB BundleToPerfectSensor / GDAL Pansharpen：外部工具链\n"
            "【Pan Weight】线性法中全色占比 0–1。\n"
            "【RGB 波段】仅 IHS：多光谱中的红/绿/蓝波段号。\n"
            "【输出】锐化后的多光谱 GeoTIFF。" } },
        { QStringLiteral( "change_detection" ),
          { "双时相变化检测",
            "【前期/后期影像】须几何对齐（同投影、同分辨率更佳），可先做配准。\n"
            "【波段】各时相参与计算的波段（常用同名波段或同一指数）。\n"
            "【方法】\n"
            "• Difference：后−前\n"
            "• Normalized Difference：(后−前)/(后+前)\n"
            "• Change Mask：差值超过阈值的二值变化掩膜\n"
            "【Threshold】仅 Change Mask：变化阈值（与 DN 量纲一致）。\n"
            "【输出】差值或掩膜 GeoTIFF。" } },
        { QStringLiteral( "speckle_filter" ),
          { "SAR 斑点滤波",
            "【滤波器】Lee / Frost / Kuan / Gamma-MAP，均抑制相干斑、保边缘能力不同。\n"
            "【窗口 Window】3×3 / 5×5 / 7×7，越大越平滑、细节越少。\n"
            "【Noise Variance】Lee/Kuan/Gamma-MAP 噪声方差估计，依传感器调整。\n"
            "【Damping】仅 Frost：阻尼因子，越大平滑越强。\n"
            "【输出】滤波后栅格（保留波段数）。" } },
        { QStringLiteral( "band_ratio" ),
          { "波段比值或 IHS 变换",
            "【模式】\n"
            "• Band Ratio：分子/分母，突出特定地物光谱差异\n"
            "• IHS Transform：将 RGB 转到强度-色调-饱和度空间\n"
            "【Numerator / Denominator】比值法的分子、分母波段。\n"
            "【R/G/B】IHS 的输入三波段。\n"
            "【输出】比值单波段或 IHS 多波段结果。" } },
        { QStringLiteral( "pca" ),
          { "主成分分析 (PCA)",
            "【Components】输出主成分个数，≤输入波段数。\n"
            "前几个主成分通常包含大部分方差，用于去相关、降维与目视增强。\n"
            "【输出】多波段主成分 GeoTIFF（波段=PC1, PC2…）。" } },
        { QStringLiteral( "spatial_filter" ),
          { "空间卷积滤波",
            "【滤波器】\n"
            "• Mean / Gaussian / Median：平滑噪声（中值保边缘更好）\n"
            "• Sobel / Laplacian：边缘增强\n"
            "【Kernel Size】卷积核 3×3 或 5×5。\n"
            "【输出】滤波后栅格。" } },
        { QStringLiteral( "image_enhancement" ),
          { "影像增强综合面板",
            "【Method】在同一对话框切换：对比度拉伸 / 空间滤波 / 波段比值·IHS / SAR 斑点滤波。\n"
            "各子页参数与对应独立菜单工具一致；详见悬停说明。\n"
            "【输出】增强结果 GeoTIFF。" } },
        { QStringLiteral( "band_math" ),
          { "波段运算表达式",
            "【Expression】算术表达式，波段用 b1,b2…（从 1 起）。\n"
            "示例：(b1-b2)/(b1+b2) 为 NDVI 类运算；b1*0.0001 缩放。\n"
            "支持 + − * / 与括号。\n"
            "【输出】单波段计算结果。" } },

        // ========== Standalone dialogs ==========
        { QStringLiteral( "batch_processing" ),
          { "批量处理：同一算法处理多文件",
            "【Algorithm】从处理注册表选择算法（GDAL/OTB/内置等）。\n"
            "【Input Files】Add/Remove 管理待处理文件列表。\n"
            "【Output Directory】所有结果写入该目录（按输入名派生文件名）。\n"
            "【Run Batch】顺序执行；进度条与状态行反馈。\n"
            "适合重复流水线；复杂参数请先在工具箱单文件验证。" } },
        { QStringLiteral( "preferences" ),
          { "首选项：主题、CRS、日志与外部工具路径",
            "【Theme】浅色/深色界面主题。\n"
            "【Default CRS】新建工程默认坐标系。\n"
            "【Log to file / Log File】是否写日志文件及路径。\n"
            "【GDAL Path / OTB Path】外部可执行文件目录，供 CLI 包装算法使用。\n"
            "部分选项保存后需重启才完全生效。" } },
        { QStringLiteral( "stac_browser" ),
          { "STAC 目录检索与资产加载",
            "【Endpoint】STAC API 根 URL，如 Element84 Earth Search。\n"
            "【Collection】数据集 ID，如 sentinel-2-l2a。\n"
            "【Datetime】时间过滤（ISO 区间或时刻，视目录支持）。\n"
            "【BBox】min_lon,min_lat,max_lon,max_lat。\n"
            "【Search】检索要素；表格显示 ID/集合/时间/资产数。\n"
            "【Load Selected Asset】将选中项资产加载到工程（需网络与权限）。" } },
        { QStringLiteral( "comparison" ),
          { "左右图层目视对比",
            "【Left / Right Layer】工程中的栅格图层。\n"
            "【Load】加载到对比视图，便于检查配准、变化或分类差异。\n"
            "与主窗口「卷帘 Swipe」互补：本工具为并排对照。" } },
        { QStringLiteral( "crs_preset" ),
          { "常用坐标系预设",
            "【搜索】按名称或 EPSG 过滤。\n"
            "【树形列表】分组浏览预设。\n"
            "【详情】选中项的 EPSG、名称与 WKT 摘要。\n"
            "双击或确定将 CRS 应用到工程/调用方。" } },
        { QStringLiteral( "processing_algorithm" ),
          { "处理算法对话框（工具箱）",
            "【参数表】每个参数标签悬停可见说明；必填项运行前校验。\n"
            "【Advanced】高级参数默认折叠。\n"
            "【Load result layers】完成后自动加入图层树。\n"
            "【调用命令】GDAL/OTB/通用 CLI 实时预览，可复制到终端。\n"
            "帮助页含算法 shortHelp；与菜单入口的 RS 专用对话框互补。" } },
        { QStringLiteral( "sift_match" ),
          { "SIFT 自动匹配生成 GCP",
            "【Contrast】特征对比度阈值，越大点越少越稳。\n"
            "【Max Matches】保留匹配对数上限。\n"
            "【Min Inlier】RANSAC 内点比例下限。\n"
            "【RANSAC 阈值】像素容差。\n"
            "【Max Image Side】匹配前缩放到的最大边长（加速）。\n"
            "结果导入 GCP 表后仍应目视检查离群点。" } },
        { QStringLiteral( "map_coords" ),
          { "GCP 目标坐标输入",
            "输入或从地图拾取控制点目标坐标，与源像素位置配对。"
            "I2I 双画布模式通常直接在两侧点选，较少使用本表单。" } },

        // ========== Georeferencer ==========
        { QStringLiteral( "georef_i2i" ),
          { "Image 2 Image 配准",
            "【画布】左=源 (Warp)，右=参考 (Base)；两侧打开后才可 Add/Move/Delete GCP。\n"
            "【打开】文件或主工程图层。\n"
            "【Sync zoom】默认关闭；不同 CRS 或同景检查行列时勿强行同步。\n"
            "【SIFT】自动匹配 GCP（OpenCV）。\n"
            "【校正参数】右侧面板：变换/重采样/RMS/CRS/输出（点「参数说明」）。\n"
            "【GCP 表】行列号、残差；右键定位/启停/删除。\n"
            "【任务】运行后跟踪 warp 进度与加载结果。" } },
        { QStringLiteral( "georef_i2m" ),
          { "Image 2 Map 配准",
            "【源】文件或工程图层；【Base】主工程地图可见图层。\n"
            "【变换】含 RPC Physical（需 RPC 元数据，可选 DEM）。\n"
            "其余与 I2I 类似：GCP 表、校正参数、任务列表。" } },
        { QStringLiteral( "georef_params" ),
          { "几何校正参数（右侧面板）",
            "【坐标变换】Linear/Helmert≥2 点；多项式 1/2/3 约 3/6/10 点；TPS/Projective/RPC。\n"
            "最少点数/实际点数/DOF：DOF=实际−最少，>0 才能用残差评估；DOF=0 残差无统计意义。\n"
            "【重采样】Nearest 保类别；Bilinear/Cubic 连续影像；像元大小 auto；背景值填充空洞。\n"
            "【RMS】源像元单位；散点与 X/Y/Total/最大残差。\n"
            "【坐标系】目标 CRS 决定输出与拟合；I2I 常跟参考。\n"
            "【输出】必填路径后才能运行。【DEM】仅 RPC。" } },
        { QStringLiteral( "georef_gcp_table" ),
          { "GCP 控制点表",
            "列：地图坐标、两侧像元列/行、残差 ΔX/ΔY/RMS、启用状态。\n"
            "右键：定位、启用/禁用、编辑、删除。Delete 删除选中。\n"
            "同景配准「列源/行源」应接近「列参/行参」。" } },
        { QStringLiteral( "georef_tasks" ),
          { "校正任务列表",
            "运行后显示进度；可取消；完成后加载结果到工程。" } },

        // ========== Classification / OBIA ==========
        { QStringLiteral( "classification" ),
          { "像元级监督分类",
            "【流程】加载影像 → 定义类别 → 采集 ROI → 设置算法/波段/忽略值 → 训练分类 → 精度评价。\n"
            "【ROI 工具】点/矩形/多边形/自由手/魔棒采集训练样本。\n"
            "【设置栏】算法、波段、训练比例、NoData/忽略值、输出路径；预览/交叉验证/应用。\n"
            "【精度】OA、Kappa、混淆矩阵、制图/用户精度。" } },
        { QStringLiteral( "classify_setup" ),
          { "分类设置栏参数",
            "【算法】NormalBayes / SVM / K-Means（RF/马氏/UNet 占位）。\n"
            "【波段】逗号分隔如 1,2,3。\n"
            "【训练比例】分层抽样训练占比，其余 holdout 测精度。\n"
            "【输出】分类结果 GeoTIFF。\n"
            "【使用源 NoData】元数据 NoData 不参与。\n"
            "【忽略值】额外 DN 列表（如 0 边缘）。\n"
            "【匹配】任一波段忽略则整像素忽略（默认），或全部波段才忽略。\n"
            "【预览】仅当前视口。【交叉验证】K 折评估。【应用】全图分类。" } },
        { QStringLiteral( "obia" ),
          { "面向对象分类 (OBIA)",
            "【Load Raster】加载待分割影像。\n"
            "【Segments 核】平滑核大小 3–21，越大对象越粗。\n"
            "【Bins】量化级数（内置分割回退），2–128。\n"
            "【Min region】最小对象像元数，抑制碎斑。\n"
            "【Segment】运行分割。【Classifier】NormalBayes/SVM/KMeans。\n"
            "【Classify】对象级分类。【Export】导出结果。\n"
            "【Classes 表】类别 ID/名称/颜色。适合高分影像。" } },
        { QStringLiteral( "obia_class_table" ),
          { "OBIA 类别表",
            "【列】ID / 名称 / 颜色。\n"
            "【ID】对应分类栅格像元值，从 1 起，不可直接编辑。\n"
            "【右键】编辑名称、更改颜色、插入/删除类别。\n"
            "【Assign】把当前类别赋给画布上选中的对象。" } },
        { QStringLiteral( "obia_segment_table" ),
          { "OBIA 对象列表",
            "【列】ID / 像元数 / 类别。\n"
            "【右键】在画布定位对象、赋为当前类别、复制 ID。\n"
            "层级切换时按当前层重新填充；需先完成分割。" } },
        { QStringLiteral( "obia_segment_info" ),
          { "OBIA 对象信息",
            "展示画布上选中对象的形状、光谱与层级统计（只读 HTML）。\n"
            "使用「选择对象」地图工具在画布点选对象即可刷新。" } },
        { QStringLiteral( "obia_task_list" ),
          { "任务中心（OBIA 任务列表）",
            "【列】标题 / 状态 / 进度 / 加载勾选。\n"
            "【右键】查看详情与日志、停止、暂停/恢复、重试、加载输出到主图、复制信息。\n"
            "【加载勾选】任务成功后自动把输出加载到主程序。\n"
            "【状态色】蓝=运行中、绿=完成、红=失败、灰=排队/已取消。" } },
        { QStringLiteral( "obia_data_manager" ),
          { "数据管理",
            "【树】工程数据资产与集合；左侧色条表示状态（绿=可用，红=不可用）。\n"
            "【右键】添加到显示、提升为工程持久、卸载、查看属性、复制源路径。\n"
            "【双击】等同于「添加到显示」。下方为元信息检视器。" } },
        { QStringLiteral( "accuracy" ),
          { "分类精度评价",
            "【OA】总体精度。【Kappa】一致性系数。\n"
            "【混淆矩阵】行=真实，列=预测。\n"
            "【制图精度】某类真实被正确分出的比例（召回）。\n"
            "【用户精度】某类预测中正确的比例（精确率）。\n"
            "【F1】制图与用户精度调和平均。【导出 CSV】保存报告。" } },
        { QStringLiteral( "post_process" ),
          { "分类后处理",
            "【矢量化】把分类栅格转为矢量面。\n"
            "【过滤】按最小面积过滤碎斑。\n"
            "【合并】小图斑合并到相邻主类。\n"
            "【输出】导出处理后栅格/矢量。" } },
        { QStringLiteral( "classifier_load" ),
          { "加载已训练分类器",
            "从磁盘加载先前保存的分类器模型文件。\n"
            "加载后可直接对新影像应用分类，无需重新训练。" } },
        { QStringLiteral( "merge_classes" ),
          { "类别合并",
            "把多个细类合并为一个粗类，更新类别表与已分类结果。\n"
            "选择源类别与目标类别后执行合并。" } },
        { QStringLiteral( "template_match" ),
          { "几何校正 - 模板匹配",
            "【模板】在源影像框选参考模板区域。\n"
            "【搜索】在待校正影像上自动匹配同名点，生成 GCP。\n"
            "【相关阈值】控制匹配可信度；过低易误配，过高漏点。" } },
        { QStringLiteral( "landsat_import" ),
          { "导入 Landsat 产品",
            "【场景目录】含 *_MTL.txt 的解压目录。\n"
            "【探测】解析 MTL，列出子项（网格组）与波段。\n"
            "【预览树】勾选需导入的波段；默认多光谱波段。\n"
            "【导入】按选择合成并加载到工程。" } },
        { QStringLiteral( "digitize_tools" ),
          { "矢量数字化编辑工具",
            "【选择】矩形框选要素。【添加要素】绘制新要素。\n"
            "【节点】编辑顶点。【移动/旋转】整体变换。\n"
            "【重塑】修改边界。【分割】切分要素。【偏移】偏移线。\n"
            "【简化】抽稀顶点。【反转】反转线方向。\n"
            "【添加环/填充环】处理面内空洞。【删除部件】移除多部件之一。" } },

        // ========== Main / misc ==========
        { QStringLiteral( "main_window" ),
          { "主窗口 SICNU GEO RS",
            "【菜单布局】\n"
            "• 工程：打开/保存、导入、STAC、布局与报告\n"
            "• 编辑：要素编辑；数字化工具在「编辑→数字化」子菜单\n"
            "• 视图：缩放、平移、识别、量测、对比/卷帘\n"
            "• 图层：添加/移除栅格与矢量、工程 CRS\n"
            "• 栅格：预处理（含影像配准）、影像增强、波段与变换\n"
            "• 分析：光谱指数、变化检测、融合、地形、分类（专题）\n"
            "• 矢量：几何处理、叠加、空间选择、属性与投影\n"
            "• 处理：工具箱 / 历史 / 批量\n"
            "• 设置 / 窗口 / 帮助\n"
            "悬停菜单项与对话框控件查看参数说明；Shift+F1 为「这是什么」。" } },
        { QStringLiteral( "swipe" ),
          { "卷帘对比工具",
            "在地图上拖动分割线对比上下图层，检查配准或变化。" } },
        { QStringLiteral( "layout" ),
          { "打印布局 / 出图",
            "设计图框、图例、比例尺与导出地图产品。" } },
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
                   "悬停控件可查看更多提示；点「帮助」查看完整说明（若有）。" ) );
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
