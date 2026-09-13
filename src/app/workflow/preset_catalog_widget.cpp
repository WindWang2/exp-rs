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

  auto *header = new QLabel( tr( "Preset Pipeline Templates" ), this );
  header->setObjectName( QStringLiteral( "rsPresetHeader" ) );
  layout->addWidget( header );

  mSearchEdit = new QLineEdit( this );
  mSearchEdit->setPlaceholderText( tr( "Search pipeline templates..." ) );
  mSearchEdit->setClearButtonEnabled( true );
  layout->addWidget( mSearchEdit );

  mListWidget = new QListWidget( this );
  mListWidget->setObjectName( QStringLiteral( "rsPresetList" ) );
  mListWidget->setSelectionMode( QAbstractItemView::SingleSelection );
  layout->addWidget( mListWidget, 1 );

  mDescLabel = new QLabel( tr( "Select a pipeline template above to see its description" ), this );
  mDescLabel->setObjectName( QStringLiteral( "rsPresetDesc" ) );
  mDescLabel->setWordWrap( true );
  mDescLabel->setMinimumHeight( 60 );
  layout->addWidget( mDescLabel );

  mLoadBtn = new QPushButton( tr( "Load Template onto Canvas" ), this );
  mLoadBtn->setObjectName( QStringLiteral( "rsPresetLoadBtn" ) );
  mLoadBtn->setEnabled( false );
  mLoadBtn->setProperty( "primary", true );
  layout->addWidget( mLoadBtn );

  connect( mSearchEdit, &QLineEdit::textChanged, this, &PresetCatalogWidget::onSearchTextChanged );
  connect( mListWidget, &QListWidget::itemDoubleClicked, this, &PresetCatalogWidget::onItemDoubleClicked );
  connect( mListWidget, &QListWidget::itemSelectionChanged, this, &PresetCatalogWidget::onItemSelectionChanged );
  connect( mLoadBtn, &QPushButton::clicked, this, &PresetCatalogWidget::onLoadButtonClicked );

  mPresets = builtinPresets();
  populatePresets();
}

std::vector<PresetItemInfo> PresetCatalogWidget::builtinPresets()
{
  std::vector<PresetItemInfo> presets;

  // 1. Landsat NDVI & Change Detection
  {
    PresetItemInfo p1;
    p1.id = "preset_landsat_ndvi_change";
    p1.title = tr( "Landsat Vegetation Indices and Change Detection" );
    p1.category = tr( "Remote-Sensing Change Detection" );
    p1.description = tr( "Covers Landsat import, NDVI computation and a two-date change detection pipeline." );

    WorkflowDefinition wf;
    wf.id = "landsat_ndvi_change";
    wf.title = "Landsat NDVI & Change Detection";

    StepDef s1;
    s1.id = "landsat_import_t1";
    s1.title = "T1 Image Import";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "t1_raster";
    s1.uiMeta = { 80.0, 100.0 };

    StepDef s2;
    s2.id = "landsat_import_t2";
    s2.title = "T2 Image Import";
    s2.operatorId = "gdal:import";
    s2.artifactOnSuccess = "t2_raster";
    s2.uiMeta = { 80.0, 300.0 };

    StepDef s3;
    s3.id = "ndvi_calc";
    s3.title = "Vegetation Index (NDVI)";
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
    s4.title = "Image Change Detection";
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
    p2.title = tr( "DEM Elevation and Slope Analysis" );
    p2.category = tr( "Terrain Analysis" );
    p2.description = tr( "Covers DEM import, slope computation and hillshade terrain rendering." );

    WorkflowDefinition wf;
    wf.id = "dem_terrain_slope";
    wf.title = "DEM Terrain & Slope Analysis";

    StepDef s1;
    s1.id = "dem_import";
    s1.title = "DEM Data Import";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "dem_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "slope_calc";
    s2.title = "Slope Computation";
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
    s3.title = "Hillshade";
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
    p3.title = tr( "OBIA Object-Based Segmentation and Classification" );
    p3.category = tr( "Smart Classification" );
    p3.description = tr( "Covers high-resolution import, MeanShift object-based segmentation and Random Forest object classification." );

    WorkflowDefinition wf;
    wf.id = "obia_seg_classify";
    wf.title = "OBIA Segmentation & Classification";
    wf.workspaceKind = "obia";

    StepDef s1;
    s1.id = "image_import";
    s1.title = "High-Resolution Image Import";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "image_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "obia_segment";
    s2.title = "MeanShift Image Segmentation";
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
    s3.title = "Random Forest Classification";
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
    p4.title = tr( "Water Index (NDWI) Extraction" );
    p4.category = tr( "Band Math" );
    p4.description = tr( "Covers the normalized-difference water index from the green and NIR bands." );

    WorkflowDefinition wf;
    wf.id = "ndwi_water_extraction";
    wf.title = "NDWI Water Extraction";

    StepDef s1;
    s1.id = "landsat_import";
    s1.title = "Landsat Data Import";
    s1.operatorId = "gdal:import";
    s1.artifactOnSuccess = "image_raster";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "ndwi_calc";
    s2.title = "NDWI Water Index";
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
    p5.title = tr( "Remote-sensing classification, denoising filters and class merging" );
    p5.category = tr( "Remote-Sensing Image Classification" );
    p5.description = tr( "Covers supervised/unsupervised classification, 3x3 majority-filter denoising and class recoding/merging end to end." );

    WorkflowDefinition wf;
    wf.id = "classification_postprocess_merge";
    wf.title = "Classification Post-Processing & Class Merge";

    StepDef s1;
    s1.id = "classify_step";
    s1.title = "Remote-Sensing Image Classification";
    s1.operatorId = "rs:obia_classify";
    s1.artifactOnSuccess = "class_map";
    s1.uiMeta = { 100.0, 150.0 };

    StepDef s2;
    s2.id = "majority_filter";
    s2.title = "3x3 majority filter denoising";
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
    s3.title = "Class Merging and Recoding";
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

void PresetCatalogWidget::populatePresets( const QString &filter )
{
  mListWidget->clear();

  QString cleanFilter = filter.trimmed();
  for ( const auto &preset : mPresets )
  {
    if ( !cleanFilter.isEmpty() )
    {
      bool match = preset.title.contains( cleanFilter, Qt::CaseInsensitive ) ||
                   preset.category.contains( cleanFilter, Qt::CaseInsensitive ) ||
                   preset.description.contains( cleanFilter, Qt::CaseInsensitive );
      if ( !match )
        continue;
    }

    auto *item = new QListWidgetItem( QStringLiteral( "%1 (%2)" ).arg( preset.title, preset.category ), mListWidget );
    item->setData( Qt::UserRole, preset.id );
  }

  if ( mListWidget->count() == 0 )
  {
    mDescLabel->setText( tr( "No matching preset pipeline template" ) );
    mLoadBtn->setEnabled( false );
  }
}

int PresetCatalogWidget::visiblePresetCount() const
{
  return mListWidget ? mListWidget->count() : 0;
}

void PresetCatalogWidget::onSearchTextChanged( const QString &text )
{
  populatePresets( text );
}

void PresetCatalogWidget::onItemSelectionChanged()
{
  auto *item = mListWidget->currentItem();
  if ( !item )
  {
    mDescLabel->setText( tr( "Select a pipeline template above to see its description" ) );
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
