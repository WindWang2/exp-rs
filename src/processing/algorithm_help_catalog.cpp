#include "algorithm_help_catalog.h"
#include <QHash>
#include <QObject>

namespace {
struct Entry { const char *oneLine; const char *detail; };
const QHash<QString, Entry> &catalog()
{
  static const QHash<QString, Entry> k = {
    { QStringLiteral("gdal2xyz"), { "栅格转 XYZ", "将栅格像元导出为 X Y Z 文本表，便于点云/统计处理。" } },
    { QStringLiteral("gdal_calc"), { "栅格计算器", "对多输入栅格按表达式计算（gdal_calc.py），如 NDVI 或波段运算。" } },
    { QStringLiteral("gdal_contour"), { "等值线", "从 DEM 或连续栅格生成等值线矢量。" } },
    { QStringLiteral("gdal_edit"), { "编辑栅格元数据", "修改 GeoTIFF 等栅格的投影、NoData、统计等元数据而不重写像元。" } },
    { QStringLiteral("gdal_fillnodata"), { "填充 NoData", "用边缘插值填充 NoData 空洞，适合 DEM 与连续表面。" } },
    { QStringLiteral("gdal_grid"), { "点插值成栅格", "将点矢量插值为规则栅格（反距离、近邻等算法）。" } },
    { QStringLiteral("gdal_merge"), { "镶嵌合并", "将多景栅格镶嵌为单一影像，可控制重叠像元取值策略。" } },
    { QStringLiteral("gdal_polygonize"), { "栅格矢量化", "将同类像元区域转为多边形矢量。" } },
    { QStringLiteral("gdal_proximity"), { "距离栅格", "计算到目标像元的距离栅格（邻近分析）。" } },
    { QStringLiteral("gdal_rasterize"), { "矢量栅格化", "将矢量要素烧录为栅格像元值。" } },
    { QStringLiteral("gdal_retile"), { "栅格分块", "将大幅栅格切成规则瓦片并可选生成金字塔，便于分发与显示。" } },
    { QStringLiteral("gdal_sieve"), { "筛除碎斑", "去除面积过小的栅格斑块，常用于分类后处理。" } },
    { QStringLiteral("gdal_translate"), { "栅格格式转换", "使用 gdal_translate 转换栅格格式、数据类型、波段子集或地理变换参数。" } },
    { QStringLiteral("gdaladdo"), { "构建金字塔", "为栅格生成 overview 金字塔，加快缩放显示。" } },
    { QStringLiteral("gdalbuildvrt"), { "构建虚拟栅格 VRT", "生成不复制数据的虚拟镶嵌/波段组合文件，适合大数据快速预览。" } },
    { QStringLiteral("gdaldem"), { "地形分析", "从 DEM 计算坡度、坡向、山体阴影、粗糙度等地形产品。" } },
    { QStringLiteral("gdalinfo"), { "栅格元信息", "打印栅格驱动、尺寸、投影、地理变换与统计摘要，用于检查数据。" } },
    { QStringLiteral("gdalmanage"), { "栅格数据管理", "复制/重命名/删除栅格数据集等管理操作。" } },
    { QStringLiteral("gdaltindex"), { "栅格索引", "为多景栅格生成范围索引矢量（tile index）。" } },
    { QStringLiteral("gdaltransform"), { "坐标点变换", "在不同坐标系之间转换点坐标（命令行 gdaltransform）。" } },
    { QStringLiteral("gdalwarp"), { "栅格重投影/裁剪", "使用 gdalwarp 对栅格进行坐标变换、重采样与裁剪。可指定目标 CRS、分辨率与范围。" } },
    { QStringLiteral("native_assign_projection"), { "指定投影", "为无投影图层指定 CRS。" } },
    { QStringLiteral("native_centroids"), { "质心", "计算要素质心。" } },
    { QStringLiteral("native_clip"), { "裁剪(原生)", "矢量裁剪。" } },
    { QStringLiteral("native_clip_raster"), { "栅格裁剪", "按范围/掩膜裁剪栅格。" } },
    { QStringLiteral("native_convex_hull"), { "凸包", "计算凸包多边形。" } },
    { QStringLiteral("native_difference"), { "差集(原生)", "矢量差集。" } },
    { QStringLiteral("native_extract_by_attribute"), { "按属性提取", "按属性表达式提取。" } },
    { QStringLiteral("native_hillshade"), { "山体阴影", "从 DEM 生成山体阴影。" } },
    { QStringLiteral("native_intersection"), { "相交(原生)", "矢量相交。" } },
    { QStringLiteral("native_raster_statistics"), { "栅格统计(原生)", "波段统计。" } },
    { QStringLiteral("native_reproject_layer"), { "图层重投影", "重投影矢量图层。" } },
    { QStringLiteral("native_simplify"), { "简化", "简化几何顶点。" } },
    { QStringLiteral("native_union"), { "联合(原生)", "矢量联合。" } },
    { QStringLiteral("ogr2ogr"), { "矢量格式转换", "转换矢量格式、重投影、属性筛选与裁剪（ogr2ogr）。" } },
    { QStringLiteral("ogrinfo"), { "矢量信息", "查看矢量图层、字段、要素数量与空间范围。" } },
    { QStringLiteral("ogrtindex"), { "矢量瓦片索引", "为矢量图层集合生成索引。" } },
    { QStringLiteral("otb_band_math"), { "波段数学", "对单影像多波段按表达式计算新栅格（OTB BandMath）。" } },
    { QStringLiteral("otb_band_math_x"), { "多影像波段数学", "对多输入影像按表达式联合计算（BandMathX）。" } },
    { QStringLiteral("otb_binary_morphological"), { "二值形态学", "腐蚀/膨胀/开闭等二值形态学运算，用于掩膜清理。" } },
    { QStringLiteral("otb_bundle_to_perfect_sensor"), { "全色锐化准备", "将多光谱与全色捆绑到完美传感器模型，供融合使用。" } },
    { QStringLiteral("otb_compute_images_statistics"), { "影像统计", "计算多波段均值/方差等，常用于分类训练前标准化。" } },
    { QStringLiteral("otb_concatenate_images"), { "波段堆叠", "将多幅影像在波段维拼接成多波段立方体。" } },
    { QStringLiteral("otb_convert"), { "格式/类型转换", "转换影像数据类型或格式。" } },
    { QStringLiteral("otb_dynamic_convert"), { "动态范围转换", "按百分位等策略拉伸并转换像素动态范围。" } },
    { QStringLiteral("otb_extract_roi"), { "提取感兴趣区", "按矩形或矢量裁剪 ROI 子区。" } },
    { QStringLiteral("otb_feature_extraction"), { "特征提取", "从影像提取纹理/光谱等特征层。" } },
    { QStringLiteral("otb_gray_level_cooccurrence_matrix"), { "灰度共生矩阵", "计算 GLCM 纹理特征。" } },
    { QStringLiteral("otb_gray_scale_morphological"), { "灰度形态学", "对灰度影像做形态学运算。" } },
    { QStringLiteral("otb_haralick_texture"), { "Haralick 纹理", "提取 Haralick 系列纹理指标。" } },
    { QStringLiteral("otb_image_classifier"), { "影像分类(推理)", "使用已训练模型对影像做监督分类预测。" } },
    { QStringLiteral("otb_kmeans_classification"), { "K-Means 分类", "无监督 K 均值聚类分类。" } },
    { QStringLiteral("otb_local_statistic_extraction"), { "局部统计", "滑动窗口局部均值/方差等统计特征。" } },
    { QStringLiteral("otb_lsms"), { "LSMS 分割", "大尺度均值漂移分割流水线（OBIA 常用）。" } },
    { QStringLiteral("otb_mean_shift_smoothing"), { "均值漂移平滑", "边缘保持的平滑滤波，常作分割前处理。" } },
    { QStringLiteral("otb_multi_resolution_pyramid"), { "多分辨率金字塔", "生成多尺度影像金字塔。" } },
    { QStringLiteral("otb_multivariate_alteration_detector"), { "MAD 变化检测", "多元变化检测（MAD），比较两时相影像。" } },
    { QStringLiteral("otb_ortho_rectification"), { "正射校正", "基于传感器模型与 DEM 的正射纠正。" } },
    { QStringLiteral("otb_pixel_info"), { "像元信息", "查询指定像元的波段值与位置信息。" } },
    { QStringLiteral("otb_radiometric_indices"), { "辐射指数", "批量计算 NDVI 等光谱/辐射指数。" } },
    { QStringLiteral("otb_read_image_info"), { "读取影像信息", "输出影像尺寸、投影、波段数等元数据。" } },
    { QStringLiteral("otb_rescale"), { "重缩放", "线性/统计重缩放像素值到指定范围。" } },
    { QStringLiteral("otb_segmentation"), { "影像分割", "区域生长/均值漂移等分割，输出对象标签图。" } },
    { QStringLiteral("otb_stereo_rectification"), { "立体校正", "生成立体相对核线重采样网格。" } },
    { QStringLiteral("otb_superimpose"), { "影像套合", "将一景影像重采样到另一景的网格与投影。" } },
    { QStringLiteral("otb_svm_classification"), { "SVM 训练分类", "训练支持向量机影像分类器。" } },
    { QStringLiteral("otb_train_vector_classifier"), { "矢量样本训练分类器", "基于矢量样本训练分类模型。" } },
    { QStringLiteral("pct2rgb"), { "调色板转 RGB", "将伪彩色（调色板）栅格转为真彩色 RGB。" } },
    { QStringLiteral("raster_calculator"), { "栅格计算器", "表达式计算多栅格。" } },
    { QStringLiteral("raster_clip"), { "栅格裁剪", "裁剪栅格到范围。" } },
    { QStringLiteral("raster_merge_bands"), { "合并波段", "将多单波段合成为多波段。" } },
    { QStringLiteral("raster_statistics"), { "栅格统计", "计算栅格波段统计量。" } },
    { QStringLiteral("rgb2pct"), { "RGB 转调色板", "将 RGB 栅格量化为调色板索引栅格。" } },
    { QStringLiteral("rs_atmospheric_correction"), { "大气校正", "对光学遥感影像做大气校正，输出反射率或纠正结果。" } },
    { QStringLiteral("rs_band_math"), { "波段运算", "自定义波段表达式计算，支持多波段算术。" } },
    { QStringLiteral("rs_spectral_index"), { "光谱指数", "计算 NDVI、NDWI 等常用光谱指数。" } },
    { QStringLiteral("vector_attribute_query"), { "属性查询", "按表达式筛选属性。" } },
    { QStringLiteral("vector_buffer"), { "缓冲区", "对矢量要素生成缓冲多边形。" } },
    { QStringLiteral("vector_clip"), { "矢量裁剪", "用裁剪多边形裁剪输入矢量。" } },
    { QStringLiteral("vector_difference"), { "擦除/差集", "从输入中擦除叠加区。" } },
    { QStringLiteral("vector_dissolve"), { "融合", "按属性融合相邻要素。" } },
    { QStringLiteral("vector_distance_matrix"), { "距离矩阵", "点对点距离矩阵。" } },
    { QStringLiteral("vector_extract_by_location"), { "按位置提取", "按空间关系提取要素。" } },
    { QStringLiteral("vector_field_calculator"), { "字段计算器", "新建或更新属性字段。" } },
    { QStringLiteral("vector_fix_geometries"), { "修复几何", "修复无效几何。" } },
    { QStringLiteral("vector_intersection"), { "相交", "计算几何相交部分。" } },
    { QStringLiteral("vector_merge"), { "合并图层", "合并多个矢量图层。" } },
    { QStringLiteral("vector_multipart_to_singlepart"), { "多部件转单部件", "拆分多部件要素。" } },
    { QStringLiteral("vector_nearest_neighbor"), { "最近邻分析", "计算到最近要素的距离。" } },
    { QStringLiteral("vector_reproject"), { "矢量重投影", "转换矢量图层坐标系。" } },
    { QStringLiteral("vector_select_by_location"), { "按位置选择", "按空间关系选择要素。" } },
    { QStringLiteral("vector_smooth_geometry"), { "平滑几何", "平滑线/面几何。" } },
    { QStringLiteral("vector_spatial_query"), { "空间查询", "基于空间谓词查询要素。" } },
    { QStringLiteral("vector_symmetrical_difference"), { "对称差", "对称差集分析。" } },
    { QStringLiteral("vector_union"), { "联合", "多边形联合叠加。" } },
  };
  return k;
}
} // namespace

QString SicnuAlgorithmHelp::shortDescription( const QString &algorithmName, const QString &displayName )
{
  const auto it = catalog().constFind( algorithmName );
  if ( it != catalog().constEnd() )
    return QObject::tr( it->oneLine );
  if ( !displayName.isEmpty() )
    return displayName;
  return algorithmName;
}

QString SicnuAlgorithmHelp::shortHelpString( const QString &algorithmName, const QString &displayName,
                                            const QString &cliName, const QStringList &tags )
{
  const auto it = catalog().constFind( algorithmName );
  QString body;
  if ( it != catalog().constEnd() )
  {
    body = QObject::tr( it->detail );
  }
  else
  {
    body = QObject::tr( "Processing tool: %1" ).arg( displayName.isEmpty() ? algorithmName : displayName );
  }
  QString html = QObject::tr( "<p><b>%1</b></p><p>%2</p>" )
                   .arg( displayName.isEmpty() ? algorithmName : displayName, body );
  if ( !cliName.isEmpty() )
    html += QObject::tr( "<p>CLI / 应用: <code>%1</code></p>" ).arg( cliName );
  if ( !tags.isEmpty() )
    html += QObject::tr( "<p>标签: %1</p>" ).arg( tags.join( QStringLiteral( ", " ) ) );
  html += QObject::tr( "<p>在工具箱中悬停可查看简介；打开对话框后右侧/帮助区显示完整说明。" );
  return html;
}
