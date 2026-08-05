// src/app/workflow/preset_catalog_widget.cpp
#include "preset_catalog_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QGroupBox>

namespace sicnu::workflow::gui {

PresetCatalogWidget::PresetCatalogWidget( QWidget *parent )
  : QWidget( parent )
{
  auto *layout = new QVBoxLayout( this );
  layout->setContentsMargins( 8, 8, 8, 8 );
  layout->setSpacing( 6 );

  auto *header = new QLabel( tr( "预设流程模板 / Presets" ), this );
  header->setStyleSheet( QStringLiteral( "font-weight: bold; font-size: 13px; color: #38bdf8;" ) );
  layout->addWidget( header );

  mListWidget = new QListWidget( this );
  mListWidget->setSelectionMode( QAbstractItemView::SingleSelection );
  mListWidget->setStyleSheet( QStringLiteral(
    "QListWidget { background-color: #1e293b; color: #f8fafc; border: 1px solid #334155; border-radius: 4px; }"
    "QListWidget::item { padding: 8px; border-bottom: 1px solid #334155; }"
    "QListWidget::item:selected { background-color: #0284c7; color: #ffffff; }"
  ) );
  layout->addWidget( mListWidget, 1 );

  mDescLabel = new QLabel( tr( "请选择左侧流程模板查看说明" ), this );
  mDescLabel->setWordWrap( true );
  mDescLabel->setStyleSheet( QStringLiteral( "color: #94a3b8; font-size: 11px; padding: 4px; background: #0f172a; border-radius: 4px;" ) );
  mDescLabel->setMinimumHeight( 60 );
  layout->addWidget( mDescLabel );

  mLoadBtn = new QPushButton( tr( "加载模板到画布 / Load Template" ), this );
  mLoadBtn->setEnabled( false );
  mLoadBtn->setStyleSheet( QStringLiteral(
    "QPushButton { background-color: #0284c7; color: white; border: none; padding: 8px; border-radius: 4px; font-weight: bold; }"
    "QPushButton:hover { background-color: #0369a1; }"
    "QPushButton:disabled { background-color: #475569; color: #94a3b8; }"
  ) );
  layout->addWidget( mLoadBtn );

  connect( mListWidget, &QListWidget::itemDoubleClicked, this, &PresetCatalogWidget::onItemDoubleClicked );
  connect( mListWidget, &QListWidget::itemSelectionChanged, this, &PresetCatalogWidget::onItemSelectionChanged );
  connect( mLoadBtn, &QPushButton::clicked, this, &PresetCatalogWidget::onLoadButtonClicked );

  populatePresets();
}

std::vector<PresetItemInfo> PresetCatalogWidget::builtinPresets()
{
  std::vector<PresetItemInfo> presets;

  // 1. Landsat NDVI & Change Detection
  {
    PresetItemInfo p1;
    p1.id = "preset_landsat_ndvi_change";
    p1.title = tr( "Landsat 植被指数与变化检测" );
    p1.category = tr( "遥感变化检测" );
    p1.description = tr( "包含 Landsat 数据导入、植被指数 (NDVI) 计算以及前后时相变化检测流。" );

    WorkflowDefinition wf;
    wf.id = "landsat_ndvi_change";
    wf.title = "Landsat NDVI & Change Detection";

    StepDef s1;
    s1.id = "landsat_import_t1";
    s1.title = "T1 影像导入";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "t1_raster";
    s1.uiMeta = { 80.0, 100.0 };

    StepDef s2;
    s2.id = "landsat_import_t2";
    s2.title = "T2 影像导入";
    s2.operatorId = "gdal:import";
    s2.artifactOnSuccess = "t2_raster";
    s2.uiMeta = { 80.0, 300.0 };

    StepDef s3;
    s3.id = "ndvi_calc";
    s3.title = "植被指数 (NDVI)";
    s3.operatorId = "rs:spectral_index";
    s3.artifactOnSuccess = "ndvi_raster";
    s3.uiMeta = { 360.0, 100.0 };
    s3.uiMeta.portAddToMap["ndvi_raster"] = true;

    StepConnection c1;
    c1.fromStepId = "landsat_import_t1";
    c1.fromPort = "t1_raster";
    c1.toPort = "input";
    s3.inputs.push_back( c1 );

    StepDef s4;
    s4.id = "change_detection";
    s4.title = "影像变化检测";
    s4.operatorId = "rs:change_detection";
    s4.artifactOnSuccess = "change_mask";
    s4.uiMeta = { 640.0, 200.0 };
    s4.uiMeta.portAddToMap["change_mask"] = true;

    StepConnection c2;
    c2.fromStepId = "ndvi_calc";
    c2.fromPort = "ndvi_raster";
    c2.toPort = "before";
    s4.inputs.push_back( c2 );

    StepConnection c3;
    c3.fromStepId = "landsat_import_t2";
    c3.fromPort = "t2_raster";
    c3.toPort = "after";
    s4.inputs.push_back( c3 );

    wf.steps = { s1, s2, s3, s4 };
    p1.definition = wf;
    presets.push_back( p1 );
  }

  // 2. DEM Terrain & Slope Analysis
  {
    PresetItemInfo p2;
    p2.id = "preset_dem_terrain_slope";
    p2.title = tr( "DEM 高程与坡度分析" );
    p2.category = tr( "地形分析" );
    p2.description = tr( "包含高程 DEM 导入、坡度 (Slope) 计算以及山体阴影 (Hillshade) 地形渲染。" );

    WorkflowDefinition wf;
    wf.id = "dem_terrain_slope";
    wf.title = "DEM Terrain & Slope Analysis";

    StepDef s1;
    s1.id = "dem_import";
    s1.title = "DEM 数据导入";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "dem_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "slope_calc";
    s2.title = "坡度计算 (Slope)";
    s2.operatorId = "gdal:slope";
    s2.artifactOnSuccess = "slope_raster";
    s2.uiMeta = { 400.0, 80.0 };
    s2.uiMeta.portAddToMap["slope_raster"] = true;

    StepConnection c1;
    c1.fromStepId = "dem_import";
    c1.fromPort = "dem_raster";
    c1.toPort = "input";
    s2.inputs.push_back( c1 );

    StepDef s3;
    s3.id = "hillshade_render";
    s3.title = "山体阴影 (Hillshade)";
    s3.operatorId = "gdal:hillshade";
    s3.artifactOnSuccess = "hillshade_raster";
    s3.uiMeta = { 400.0, 260.0 };
    s3.uiMeta.portAddToMap["hillshade_raster"] = true;

    StepConnection c2;
    c2.fromStepId = "dem_import";
    c2.fromPort = "dem_raster";
    c2.toPort = "input";
    s3.inputs.push_back( c2 );

    wf.steps = { s1, s2, s3 };
    p2.definition = wf;
    presets.push_back( p2 );
  }

  // 3. OBIA Image Segmentation & Classification
  {
    PresetItemInfo p3;
    p3.id = "preset_obia_seg_classify";
    p3.title = tr( "OBIA 面向对象分割与分类" );
    p3.category = tr( "智能分类" );
    p3.description = tr( "包含高分辨率影像导入、MeanShift 面向对象分割以及随机森林 (Random Forest) 对象分类。" );

    WorkflowDefinition wf;
    wf.id = "obia_seg_classify";
    wf.title = "OBIA Segmentation & Classification";
    wf.workspaceKind = "obia";

    StepDef s1;
    s1.id = "image_import";
    s1.title = "高分影像导入";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "image_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "obia_segment";
    s2.title = "MeanShift 图像分割";
    s2.operatorId = "rs:obia_segment";
    s2.artifactOnSuccess = "segmented_vector";
    s2.uiMeta = { 400.0, 150.0 };
    s2.uiMeta.portAddToMap["segmented_vector"] = true;
    s2.params["input"] = "$image_import.image_raster";

    StepConnection c1;
    c1.fromStepId = "image_import";
    c1.fromPort = "image_raster";
    c1.toPort = "input";
    s2.inputs.push_back( c1 );

    StepDef s3;
    s3.id = "obia_classify";
    s3.title = "随机森林分类";
    s3.operatorId = "rs:obia_classify";
    s3.artifactOnSuccess = "classified_result";
    s3.uiMeta = { 700.0, 150.0 };
    s3.uiMeta.portAddToMap["classified_result"] = true;
    s3.params["input"] = "$obia_segment.segmented_vector";

    StepConnection c2;
    c2.fromStepId = "obia_segment";
    c2.fromPort = "segmented_vector";
    c2.toPort = "input";
    s3.inputs.push_back( c2 );

    wf.steps = { s1, s2, s3 };
    p3.definition = wf;
    presets.push_back( p3 );
  }

  // 4. Water Index Extraction (NDWI)
  {
    PresetItemInfo p4;
    p4.id = "preset_ndwi_water_extraction";
    p4.title = tr( "水体指数 (NDWI) 提取" );
    p4.category = tr( "波段运算" );
    p4.description = tr( "包含绿光与近红外波段水体归一化差值指数计算。" );

    WorkflowDefinition wf;
    wf.id = "ndwi_water_extraction";
    wf.title = "NDWI Water Extraction";

    StepDef s1;
    s1.id = "landsat_import";
    s1.title = "Landsat 数据导入";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "image_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "ndwi_calc";
    s2.title = "NDWI 水体指数";
    s2.operatorId = "rs:ndwi";
    s2.artifactOnSuccess = "ndwi_raster";
    s2.uiMeta = { 420.0, 150.0 };
    s2.uiMeta.portAddToMap["ndwi_raster"] = true;

    StepConnection c1;
    c1.fromStepId = "landsat_import";
    c1.fromPort = "image_raster";
    c1.toPort = "input";
    s2.inputs.push_back( c1 );

    wf.steps = { s1, s2 };
    p4.definition = wf;
    presets.push_back( p4 );
  }

  // 5. Classification with Noise Removal & Class Merge
  {
    PresetItemInfo p5;
    p5.id = "preset_classification_postprocess_merge";
    p5.title = tr( "遥感分类、降噪过滤与类别合并" );
    p5.category = tr( "遥感图像分类" );
    p5.description = tr( "包含监督/非监督分类、3x3 众数滤波降噪以及类别重编码合并全流程。" );

    WorkflowDefinition wf;
    wf.id = "classification_postprocess_merge";
    wf.title = "Classification Post-Processing & Class Merge";

    StepDef s1;
    s1.id = "classify_step";
    s1.title = "遥感图像分类";
    s1.operatorId = "rs:obia_classify";
    s1.artifactOnSuccess = "class_map";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "majority_filter";
    s2.title = "3x3 众数滤波降噪";
    s2.operatorId = "rs:majority_filter";
    s2.artifactOnSuccess = "filter_map";
    s2.uiMeta = { 400.0, 150.0 };

    StepConnection c1;
    c1.fromStepId = "classify_step";
    c1.fromPort = "class_map";
    c1.toPort = "input";
    s2.inputs.push_back( c1 );

    StepDef s3;
    s3.id = "recode_step";
    s3.title = "类别合并重编码";
    s3.operatorId = "rs:recode";
    s3.artifactOnSuccess = "final_class_map";
    s3.uiMeta = { 700.0, 150.0 };
    s3.uiMeta.portAddToMap["final_class_map"] = true;

    StepConnection c2;
    c2.fromStepId = "majority_filter";
    c2.fromPort = "filter_map";
    c2.toPort = "input";
    s3.inputs.push_back( c2 );

    wf.steps = { s1, s2, s3 };
    p5.definition = wf;
    presets.push_back( p5 );
  }

  return presets;
}

void PresetCatalogWidget::populatePresets()
{
  mPresets = builtinPresets();
  mListWidget->clear();

  for ( const auto &preset : mPresets )
  {
    auto *item = new QListWidgetItem( QStringLiteral( "📌 %1 (%2)" ).arg( preset.title, preset.category ), mListWidget );
    item->setData( Qt::UserRole, preset.id );
  }
}

void PresetCatalogWidget::onItemSelectionChanged()
{
  auto *item = mListWidget->currentItem();
  if ( !item )
  {
    mDescLabel->setText( tr( "请选择左侧流程模板查看说明" ) );
    mLoadBtn->setEnabled( false );
    return;
  }

  QString presetId = item->data( Qt::UserRole ).toString();
  for ( const auto &preset : mPresets )
  {
    if ( preset.id == presetId )
    {
      mDescLabel->setText( QStringLiteral( "<b>[%1]</b><br/>%2" ).arg( preset.title, preset.description ) );
      mLoadBtn->setEnabled( true );
      break;
    }
  }
}

void PresetCatalogWidget::onItemDoubleClicked( QListWidgetItem *item )
{
  if ( !item )
    return;

  QString presetId = item->data( Qt::UserRole ).toString();
  for ( const auto &preset : mPresets )
  {
    if ( preset.id == presetId )
    {
      emit presetSelected( preset.definition );
      break;
    }
  }
}

void PresetCatalogWidget::onLoadButtonClicked()
{
  onItemDoubleClicked( mListWidget->currentItem() );
}

} // namespace sicnu::workflow::gui
