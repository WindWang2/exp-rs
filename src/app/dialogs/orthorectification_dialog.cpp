// src/app/dialogs/orthorectification_dialog.cpp
#include "orthorectification_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/crs_selector.h"

#include <raster/qgsrasterlayer.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <gdal.h>
#include <cpl_string.h>

#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

/// True when the raster carries RPC metadata; false when it carries GCPs.
/// Returns -1 for neither (unsupported by gdal:orthorectification).
int rpcOrGcp( const QString &rasterPath )
{
  ensureGdalInit();
  GDALDatasetH ds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return -1;
  const int rpc = CSLCount( GDALGetMetadata( ds, "RPC" ) ) > 0 ? 1 : 0;
  const int gcp = GDALGetGCPCount( ds ) > 0 ? 2 : 0;
  GDALClose( ds );
  return rpc != 0 ? rpc : ( gcp != 0 ? gcp : -1 );
}

} // namespace

OrthorectificationDialog::OrthorectificationDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void OrthorectificationDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  refreshModelStatus();
}

void OrthorectificationDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "正射参数" ),
    tr( "基于 RPC/GCP 与可选 DEM 对影像做地形纠正。输入栅格必须携带 "
        "RPC 元数据或 GCP。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );

  m_targetCrsEdit = new CrsSelector( sec );
  // Keep the inner edit's stable object name so tests and UI lookups by name
  // keep working; the browse button delegates to the QGIS projection dialog.
  m_targetCrsEdit->lineEdit()->setObjectName( QStringLiteral( "orthoTargetCrsEdit" ) );
  m_targetCrsEdit->setCrsString( QStringLiteral( "EPSG:4326" ) );
  SicnuDialogHelp::tip( m_targetCrsEdit, tr(
    "目标 CRS（如 EPSG:4326、EPSG:32650）。留空使用 RPC/GCP 自带 CRS。" ) );
  form->addRow( tr( "目标 CRS" ), m_targetCrsEdit );

  auto *demRow = new QHBoxLayout;
  m_demEdit = new QLineEdit( sec );
  m_demEdit->setObjectName( QStringLiteral( "orthoDemEdit" ) );
  m_demEdit->setPlaceholderText( tr( "DEM 栅格（可选，用于地形纠正）" ) );
  SicnuDialogHelp::tip( m_demEdit, tr( "指定用于地形校正的高程栅格路径（DEM/DSM）" ) );
  m_demBrowseButton = new QPushButton( tr( "浏览…" ), sec );
  SicnuDialogHelp::tip( m_demBrowseButton, tr( "浏览并选择 DEM 高程栅格文件" ) );
  connect( m_demBrowseButton, &QPushButton::clicked, this,
           &OrthorectificationDialog::onBrowseDem );
  demRow->addWidget( m_demEdit, 1 );
  demRow->addWidget( m_demBrowseButton );
  form->addRow( tr( "DEM" ), demRow );

  m_resamplingCombo = new QComboBox( sec );
  m_resamplingCombo->setObjectName( QStringLiteral( "orthoResamplingCombo" ) );
  m_resamplingCombo->addItem( tr( "双线性（默认）" ), QStringLiteral( "bilinear" ) );
  m_resamplingCombo->addItem( tr( "最邻近" ), QStringLiteral( "nearest" ) );
  m_resamplingCombo->addItem( tr( "三次卷积" ), QStringLiteral( "cubic" ) );
  m_resamplingCombo->addItem( tr( "三次样条" ), QStringLiteral( "cubicspline" ) );
  m_resamplingCombo->addItem( tr( "Lanczos" ), QStringLiteral( "lanczos" ) );
  SicnuDialogHelp::tip( m_resamplingCombo, tr( "栅格重采样插值算法：连续影像建议双线性或三次卷积，分类/离散栅格建议最邻近" ) );
  form->addRow( tr( "重采样" ), m_resamplingCombo );

  m_resolutionSpin = new QDoubleSpinBox( sec );
  m_resolutionSpin->setObjectName( QStringLiteral( "orthoResolutionSpin" ) );
  m_resolutionSpin->setRange( 0.0, 1e9 );
  m_resolutionSpin->setDecimals( 6 );
  m_resolutionSpin->setValue( 0.0 );
  m_resolutionSpin->setSpecialValueText( tr( "自动" ) );
  SicnuDialogHelp::tip( m_resolutionSpin, tr( "输出像元尺寸（目标 CRS 单位）；0 = 自动。" ) );
  form->addRow( tr( "输出分辨率" ), m_resolutionSpin );

  m_heightSpin = new QDoubleSpinBox( sec );
  m_heightSpin->setObjectName( QStringLiteral( "orthoHeightSpin" ) );
  m_heightSpin->setRange( -10000.0, 100000.0 );
  m_heightSpin->setDecimals( 2 );
  m_heightSpin->setValue( 0.0 );
  m_heightSpin->setSpecialValueText( tr( "不使用" ) );
  SicnuDialogHelp::tip( m_heightSpin, tr( "无 DEM 时的恒定高程（米）。" ) );
  form->addRow( tr( "恒定高程" ), m_heightSpin );

  m_nodataCheck = new QCheckBox( tr( "设置输出 NoData" ), sec );
  m_nodataCheck->setObjectName( QStringLiteral( "orthoNodataCheck" ) );
  m_nodataCheck->setChecked( false );
  SicnuDialogHelp::tip( m_nodataCheck, tr( "是否为输出正射影像指定自定义的无效像元值 (NoData)" ) );
  m_nodataSpin = new QDoubleSpinBox( sec );
  m_nodataSpin->setObjectName( QStringLiteral( "orthoNodataSpin" ) );
  m_nodataSpin->setRange( -1e9, 1e9 );
  m_nodataSpin->setDecimals( 6 );
  m_nodataSpin->setValue( 0.0 );
  m_nodataSpin->setEnabled( false );
  SicnuDialogHelp::tip( m_nodataSpin, tr( "输出正射影像中无效/未覆盖区域的填充像元值" ) );
  connect( m_nodataCheck, &QCheckBox::toggled, m_nodataSpin, &QDoubleSpinBox::setEnabled );
  auto *nodataRow = new QHBoxLayout;
  nodataRow->addWidget( m_nodataCheck );
  nodataRow->addWidget( m_nodataSpin, 1 );
  form->addRow( tr( "NoData" ), nodataRow );

  m_modelStatusLabel = new QLabel( sec );
  m_modelStatusLabel->setWordWrap( true );
  m_modelStatusLabel->setStyleSheet( QStringLiteral( "color: #666;" ) );
  form->addRow( QString(), m_modelStatusLabel );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );
}

void OrthorectificationDialog::refreshModelStatus()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    m_modelStatusLabel->clear();
    return;
  }
  const int model = rpcOrGcp( m_rasterLayer->source() );
  if ( model == 1 )
    m_modelStatusLabel->setText( tr( "输入含 RPC 元数据，将启用 RPC 正射。" ) );
  else if ( model == 2 )
    m_modelStatusLabel->setText( tr( "输入含 GCP，将基于 GCP 校正。" ) );
  else
    m_modelStatusLabel->setText(
      tr( "输入无 RPC 元数据且无 GCP；gdal:orthorectification 将拒绝执行。" ) );
}

void OrthorectificationDialog::onBrowseDem()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "选择 DEM 栅格" ), m_demEdit->text(),
    tr( "栅格文件 (*.tif *.tiff *.img);;所有文件 (*)" ) );
  if ( path.isEmpty() )
    return;
  m_demEdit->setText( path );
}

Json::Value OrthorectificationDialog::buildParams() const
{
  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer ? m_rasterLayer->source().toStdString() : std::string();
  params["output"] = outputPath().toStdString();
  params["resampling"] = m_resamplingCombo->currentData().toString().toStdString();

  const QString dstCrs = m_targetCrsEdit->crsString();
  if ( !dstCrs.isEmpty() )
    params["dstCrs"] = dstCrs.toStdString();

  const QString dem = m_demEdit->text().trimmed();
  if ( !dem.isEmpty() )
    params["dem"] = dem.toStdString();

  if ( m_resolutionSpin->value() > 0.0 )
    params["targetResolution"] = m_resolutionSpin->value();

  if ( m_heightSpin->value() != 0.0 )
    params["height"] = m_heightSpin->value();

  if ( m_nodataCheck->isChecked() )
    params["nodata"] = m_nodataSpin->value();

  return params;
}

void OrthorectificationDialog::onRun()
{
  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    handleFailed( tr( "请先选择一个有效的栅格图层。" ) );
    return;
  }
  runOperatorTask( QStringLiteral( "gdal:orthorectification" ), buildParams() );
}
