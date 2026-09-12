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

  QGroupBox *paramGroup = setupParamGroup(
    mainLayout, tr( "Orthorectification Parameters" ) );
  paramGroup->setToolTip(
    tr( "Terrain-corrects the image using RPC/GCPs and an optional DEM. The input raster must carry "
        tr("RPC metadata or GCPs.") ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  m_targetCrsEdit = new CrsSelector( paramGroup );
  // Keep the inner edit's stable object name so tests and UI lookups by name
  // keep working; the browse button delegates to the QGIS projection dialog.
  m_targetCrsEdit->lineEdit()->setObjectName( QStringLiteral( "orthoTargetCrsEdit" ) );
  m_targetCrsEdit->setCrsString( QStringLiteral( "EPSG:4326" ) );
  SicnuDialogHelp::tip( m_targetCrsEdit, tr(
    tr("Target CRS (e.g. EPSG:4326, EPSG:32650). Left empty, the CRS carried by the RPC / GCPs is used.") ) );
  form->addRow( tr( "Target CRS" ), m_targetCrsEdit );

  auto *demRow = new QHBoxLayout;
  m_demEdit = new QLineEdit( paramGroup );
  m_demEdit->setObjectName( QStringLiteral( "orthoDemEdit" ) );
  m_demEdit->setPlaceholderText( tr( "DEM raster (optional, for terrain correction)" ) );
  SicnuDialogHelp::tip( m_demEdit, tr( "Path of the elevation raster for terrain correction (DEM/DSM)" ) );
  m_demBrowseButton = new QPushButton( tr( "Browse..." ), paramGroup );
  SicnuUi::markSecondary( m_demBrowseButton );
  SicnuDialogHelp::tip( m_demBrowseButton, tr( "Browse and choose the DEM elevation raster file" ) );
  connect( m_demBrowseButton, &QPushButton::clicked, this,
           &OrthorectificationDialog::onBrowseDem );
  demRow->addWidget( m_demEdit, 1 );
  demRow->addWidget( m_demBrowseButton );
  form->addRow( tr( "DEM raster" ), demRow );

  m_resamplingCombo = new QComboBox( paramGroup );
  m_resamplingCombo->setObjectName( QStringLiteral( "orthoResamplingCombo" ) );
  m_resamplingCombo->addItem( tr( "Bilinear (default)" ), QStringLiteral( "bilinear" ) );
  m_resamplingCombo->addItem( tr( "Nearest Neighbour" ), QStringLiteral( "nearest" ) );
  m_resamplingCombo->addItem( tr( "Cubic Convolution" ), QStringLiteral( "cubic" ) );
  m_resamplingCombo->addItem( tr( "Cubic Spline" ), QStringLiteral( "cubicspline" ) );
  m_resamplingCombo->addItem( tr( "Lanczos" ), QStringLiteral( "lanczos" ) );
  SicnuDialogHelp::tip( m_resamplingCombo, tr( "Raster resampling interpolation: bilinear or cubic convolution for continuous imagery; nearest neighbour for classification / discrete rasters" ) );
  form->addRow( tr( "Resampling Method" ), m_resamplingCombo );

  m_resolutionSpin = new QDoubleSpinBox( paramGroup );
  m_resolutionSpin->setObjectName( QStringLiteral( "orthoResolutionSpin" ) );
  m_resolutionSpin->setRange( 0.0, 1e9 );
  m_resolutionSpin->setDecimals( 6 );
  m_resolutionSpin->setValue( 0.0 );
  m_resolutionSpin->setSpecialValueText( tr( "Automatic" ) );
  SicnuDialogHelp::tip( m_resolutionSpin, tr( "Output pixel size (in target CRS units); 0 = automatic." ) );
  form->addRow( tr( "Output Resolution" ), m_resolutionSpin );

  m_heightSpin = new QDoubleSpinBox( paramGroup );
  m_heightSpin->setObjectName( QStringLiteral( "orthoHeightSpin" ) );
  m_heightSpin->setRange( -10000.0, 100000.0 );
  m_heightSpin->setDecimals( 2 );
  m_heightSpin->setValue( 0.0 );
  m_heightSpin->setSpecialValueText( tr( "None" ) );
  SicnuDialogHelp::tip( m_heightSpin, tr( "Constant elevation (m) when no DEM is available." ) );
  form->addRow( tr( "Constant Elevation" ), m_heightSpin );

  m_nodataCheck = new QCheckBox( tr( "Specify NoData value" ), paramGroup );
  m_nodataCheck->setObjectName( QStringLiteral( "orthoNodataCheck" ) );
  m_nodataCheck->setChecked( false );
  SicnuDialogHelp::tip( m_nodataCheck, tr( "Specify a custom NoData value for the output orthophoto" ) );
  m_nodataSpin = new QDoubleSpinBox( paramGroup );
  m_nodataSpin->setObjectName( QStringLiteral( "orthoNodataSpin" ) );
  m_nodataSpin->setRange( -1e9, 1e9 );
  m_nodataSpin->setDecimals( 6 );
  m_nodataSpin->setValue( 0.0 );
  m_nodataSpin->setEnabled( false );
  SicnuDialogHelp::tip( m_nodataSpin, tr( "Fill pixel value for invalid / uncovered areas of the output orthophoto" ) );
  connect( m_nodataCheck, &QCheckBox::toggled, m_nodataSpin, &QDoubleSpinBox::setEnabled );
  auto *nodataRow = new QHBoxLayout;
  nodataRow->addWidget( m_nodataCheck );
  nodataRow->addWidget( m_nodataSpin, 1 );
  form->addRow( tr( "NoData Settings" ), nodataRow );

  m_modelStatusLabel = SicnuUi::makeHintLabel( paramGroup, QString() );
  m_modelStatusLabel->setWordWrap( true );
  form->addRow( QString(), m_modelStatusLabel );

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
    m_modelStatusLabel->setText( tr( "The input carries RPC metadata; RPC orthorectification will be enabled." ) );
  else if ( model == 2 )
    m_modelStatusLabel->setText( tr( "The input carries GCPs; correction will be GCP-based." ) );
  else
    m_modelStatusLabel->setText(
      tr( "The input has neither RPC metadata nor GCPs; gdal:orthorectification will refuse to run." ) );
}

void OrthorectificationDialog::onBrowseDem()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Select DEM Raster" ), m_demEdit->text(),
    tr( "Raster Files (*.tif *.tiff *.img);;All Files (*)" ) );
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
    handleFailed( tr( "Select a valid raster layer first." ) );
    return;
  }
  runOperatorTask( QStringLiteral( "gdal:orthorectification" ), buildParams() );
}
