// src/app/dialogs/change_detection_dialog.cpp
#include "change_detection_dialog.h"
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "widgets/raster_layer_combo.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QMessageBox>

#include <qgsproject.h>

ChangeDetectionDialog::ChangeDetectionDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( tr( "Change Detection" ) );
  setMinimumWidth( 480 );
  setupUi();
}

void ChangeDetectionDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Two-Date Input Data" ) );
  inputGroup->setToolTip(
    tr( "The two epochs must be precisely co-registered and radiometrically normalized." ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );

  m_beforeLayerCombo = new RasterLayerCombo( inputGroup );
  m_beforeLayerCombo->setObjectName( QStringLiteral( "cdBeforeCombo" ) );
  m_afterLayerCombo = new RasterLayerCombo( inputGroup );
  m_afterLayerCombo->setObjectName( QStringLiteral( "cdAfterCombo" ) );
  m_beforeBandCombo = new QComboBox( inputGroup );
  m_afterBandCombo = new QComboBox( inputGroup );
  SicnuDialogHelp::tip( m_beforeLayerCombo, tr( "Raster image of the earlier (pre-change) epoch." ) );
  SicnuDialogHelp::tip( m_afterLayerCombo, tr( "Raster image of the later (post-change) epoch." ) );
  SicnuDialogHelp::tip( m_beforeBandCombo, tr( "Band of the earlier image used in the comparison." ) );
  SicnuDialogHelp::tip( m_afterBandCombo, tr( "Band of the later image used in the comparison." ) );
  form->addRow( tr( "Earlier Image" ), m_beforeLayerCombo );
  form->addRow( tr( "Earlier Band" ), m_beforeBandCombo );
  form->addRow( tr( "Later Image" ), m_afterLayerCombo );
  form->addRow( tr( "Later Band" ), m_afterBandCombo );

  // Dual-view interpretation aid (DoD: synchronized viewports / swipe where
  // they improve interpretation): open the comparison dialog prefilled with
  // the selected before/after rasters.
  auto *compareButton = new QPushButton( tr( "Dual-View Comparison..." ), inputGroup );
  compareButton->setObjectName( QStringLiteral( "changeCompareButton" ) );
  SicnuUi::markSecondary( compareButton );
  SicnuDialogHelp::tip( compareButton, tr(
    tr("Opens the side-by-side comparison view (divider / swipe + blink) to visually inspect registration and change.") ) );
  connect( compareButton, &QPushButton::clicked,
           this, &ChangeDetectionDialog::openComparisonPreview );
  form->addRow( QString(), compareButton );

  QGroupBox *methodGroup = setupParamGroup(
    mainLayout, tr( "Detection Method and Mask Options" ) );
  methodGroup->setToolTip(
    tr( "Supports differencing, normalized differencing, ratioing, CVA change vector analysis and MAD multivariate change detection." ) );
  auto *methodForm = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( methodGroup->layout() )->addLayout( methodForm );

  m_methodCombo = new QComboBox( methodGroup );
  m_methodCombo->setObjectName( QStringLiteral( "cdMethodCombo" ) );
  m_methodCombo->addItem( tr( "Difference" ), QStringLiteral( "difference" ) );
  m_methodCombo->addItem( tr( "Normalized Difference" ), QStringLiteral( "normalized_difference" ) );
  m_methodCombo->addItem( tr( "Ratio" ), QStringLiteral( "ratio" ) );
  m_methodCombo->addItem( tr( "Change Vector Analysis (CVA)" ), QStringLiteral( "cva" ) );
  m_methodCombo->addItem( tr( "Multivariate Alteration Detection (MAD)" ), QStringLiteral( "mad" ) );
  m_methodCombo->addItem( tr( "Change Mask (manual threshold)" ), QStringLiteral( "change_mask" ) );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    tr("• Difference: later − earlier\n• Normalized difference: (later − earlier)/(later + earlier)\n• Ratio: later / earlier\n")
    tr("• CVA: multiband change vector magnitude (all bands)\n• MAD: multivariate alteration detection (canonical correlation analysis)\n• Mask: |difference| ≥ threshold") ) );
  methodForm->addRow( tr( "Change Algorithm" ), m_methodCombo );

  m_makeMaskCheck = new QCheckBox( tr( "Also output a binary change mask" ), methodGroup );
  m_makeMaskCheck->setObjectName( QStringLiteral( "cdMakeMaskCheck" ) );
  SicnuDialogHelp::tip( m_makeMaskCheck, tr(
    tr("Besides the method raster, also outputs a 0/1 change mask (with threshold strategy, morphological cleanup and a minimum mapping unit).") ) );
  methodForm->addRow( QString(), m_makeMaskCheck );

  // Mask parameter section: threshold strategy + cleanup + minimum mapping unit.
  m_maskParamFrame = new QFrame( methodGroup );
  auto *maskForm = new QFormLayout( m_maskParamFrame );
  maskForm->setContentsMargins( 0, 0, 0, 0 );
  maskForm->setHorizontalSpacing( 10 );
  maskForm->setVerticalSpacing( 8 );

  m_thresholdMethodCombo = new QComboBox( m_maskParamFrame );
  m_thresholdMethodCombo->setObjectName( QStringLiteral( "cdThresholdMethodCombo" ) );
  m_thresholdMethodCombo->addItem( tr( "Manual Threshold" ), QStringLiteral( "manual" ) );
  m_thresholdMethodCombo->addItem( tr( "Otsu Threshold" ), QStringLiteral( "otsu" ) );
  m_thresholdMethodCombo->addItem( tr( "Percentile Threshold" ), QStringLiteral( "percentile" ) );
  m_thresholdMethodCombo->addItem( tr( "Statistical Threshold (mean + kσ)" ), QStringLiteral( "statistical" ) );
  SicnuDialogHelp::tip( m_thresholdMethodCombo, tr( "Threshold extraction strategy for the binary change mask." ) );
  maskForm->addRow( tr( "Threshold Strategy" ), m_thresholdMethodCombo );

  m_thresholdLabel = new QLabel( tr( "Threshold" ), m_maskParamFrame );
  m_thresholdSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_thresholdSpin->setObjectName( QStringLiteral( "cdThresholdSpin" ) );
  m_thresholdSpin->setRange( 0.0, 10000.0 );
  m_thresholdSpin->setDecimals( 2 );
  m_thresholdSpin->setValue( 10.0 );
  SicnuDialogHelp::tip( m_thresholdSpin, tr( "Specify an absolute change threshold manually." ) );
  maskForm->addRow( m_thresholdLabel, m_thresholdSpin );

  m_percentileSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_percentileSpin->setObjectName( QStringLiteral( "cdPercentileSpin" ) );
  m_percentileSpin->setRange( 0.0, 100.0 );
  m_percentileSpin->setDecimals( 1 );
  m_percentileSpin->setValue( 90.0 );
  SicnuDialogHelp::tip( m_percentileSpin, tr( "Extract change areas by change-magnitude percentile (0–100)." ) );
  maskForm->addRow( tr( "Percentile Value (%)" ), m_percentileSpin );

  m_statisticalKSpin = new QDoubleSpinBox( m_maskParamFrame );
  m_statisticalKSpin->setObjectName( QStringLiteral( "cdStatisticalKSpin" ) );
  m_statisticalKSpin->setRange( 0.0, 10.0 );
  m_statisticalKSpin->setDecimals( 2 );
  m_statisticalKSpin->setValue( 2.0 );
  SicnuDialogHelp::tip( m_statisticalKSpin, tr( "Statistical threshold = change mean + k × std dev." ) );
  maskForm->addRow( tr( "k (std-dev multiplier)" ), m_statisticalKSpin );

  m_cleanupCombo = new QComboBox( m_maskParamFrame );
  m_cleanupCombo->setObjectName( QStringLiteral( "cdCleanupCombo" ) );
  m_cleanupCombo->addItem( tr( "No Operation" ), QStringLiteral( "none" ) );
  m_cleanupCombo->addItem( tr( "Morphological Erosion" ), QStringLiteral( "erode" ) );
  m_cleanupCombo->addItem( tr( "Morphological Dilation" ), QStringLiteral( "dilate" ) );
  m_cleanupCombo->addItem( tr( "Opening (remove isolated patches)" ), QStringLiteral( "open" ) );
  m_cleanupCombo->addItem( tr( "Closing (fill holes)" ), QStringLiteral( "close" ) );
  SicnuDialogHelp::tip( m_cleanupCombo, tr( "Morphological post-processing for the binary change mask." ) );
  maskForm->addRow( tr( "Morphological Cleanup" ), m_cleanupCombo );

  m_cleanupIterSpin = new QSpinBox( m_maskParamFrame );
  m_cleanupIterSpin->setObjectName( QStringLiteral( "cdCleanupIterSpin" ) );
  m_cleanupIterSpin->setRange( 1, 20 );
  m_cleanupIterSpin->setValue( 1 );
  SicnuDialogHelp::tip( m_cleanupIterSpin, tr( "Morphological operation iterations" ) );
  maskForm->addRow( tr( "Iterations" ), m_cleanupIterSpin );

  m_minAreaSpin = new QSpinBox( m_maskParamFrame );
  m_minAreaSpin->setObjectName( QStringLiteral( "cdMinAreaSpin" ) );
  m_minAreaSpin->setRange( 0, 100000000 );
  m_minAreaSpin->setValue( 0 );
  SicnuDialogHelp::tip( m_minAreaSpin, tr( "Minimum mapping unit (pixels): removes small connected patches below this area; 0 = off." ) );
  maskForm->addRow( tr( "Minimum Mapping Unit (pixels)" ), m_minAreaSpin );

  methodForm->addRow( m_maskParamFrame );

  setupOutputRow( mainLayout );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "Ready" ) );
  mainLayout->addWidget( m_statusLabel );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_beforeLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::updateBandSelectors );
  connect( m_afterLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::updateBandSelectors );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::onMethodChanged );
  connect( m_makeMaskCheck, &QCheckBox::toggled,
           this, &ChangeDetectionDialog::onMakeMaskToggled );
  connect( m_thresholdMethodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ChangeDetectionDialog::onThresholdMethodChanged );

  updateMaskParamVisibility();
  populateLayers();
}

void ChangeDetectionDialog::populateLayers()
{
  m_beforeLayerCombo->populate();
  m_afterLayerCombo->populate();
  updateBandSelectors();
}

void ChangeDetectionDialog::updateBandSelectors()
{
  auto fillBands = []( QComboBox *layerCombo, QComboBox *bandCombo ) {
    bandCombo->clear();
    const QString id = layerCombo->currentData().toString();
    auto *rl = qobject_cast<QgsRasterLayer *>( QgsProject::instance()->mapLayer( id ) );
    if ( !rl )
      return;
    for ( int i = 1; i <= rl->bandCount(); ++i )
      bandCombo->addItem( tr( "Band %1" ).arg( i ), i );
  };
  fillBands( m_beforeLayerCombo, m_beforeBandCombo );
  fillBands( m_afterLayerCombo, m_afterBandCombo );
}

void ChangeDetectionDialog::onMethodChanged( int index )
{
  Q_UNUSED( index );
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::onMakeMaskToggled()
{
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::onThresholdMethodChanged( int index )
{
  Q_UNUSED( index );
  updateMaskParamVisibility();
}

void ChangeDetectionDialog::updateMaskParamVisibility()
{
  if ( !m_maskParamFrame || !m_thresholdMethodCombo )
    return;

  const QString method = m_methodCombo->currentData().toString();
  const bool maskRequested = m_makeMaskCheck->isChecked()
                             || method == QStringLiteral( "change_mask" );
  m_maskParamFrame->setVisible( maskRequested );
  if ( !maskRequested )
    return;

  const QString strategy = m_thresholdMethodCombo->currentData().toString();
  const bool manual = ( strategy == QStringLiteral( "manual" ) );
  const bool percentile = ( strategy == QStringLiteral( "percentile" ) );
  const bool statistical = ( strategy == QStringLiteral( "statistical" ) );
  m_thresholdLabel->setVisible( manual );
  m_thresholdSpin->setVisible( manual );
  m_percentileSpin->setVisible( percentile );
  m_statisticalKSpin->setVisible( statistical );

  // The legacy change_mask method only supports the manual threshold; its
  // backend path also ignores cleanup and the MMU, so those controls are
  // disabled (not just the strategy combo).
  const bool legacy = ( method == QStringLiteral( "change_mask" ) );
  m_thresholdMethodCombo->setEnabled( !legacy );
  m_cleanupCombo->setEnabled( !legacy );
  m_cleanupIterSpin->setEnabled( !legacy );
  m_minAreaSpin->setEnabled( !legacy );
  if ( legacy && strategy != QStringLiteral( "manual" ) )
    m_thresholdMethodCombo->setCurrentIndex(
      m_thresholdMethodCombo->findData( QStringLiteral( "manual" ) ) );
}

void ChangeDetectionDialog::openComparisonPreview()
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the earlier and later epoch images first." ) );
    return;
  }

  ComparisonDialog dialog( this );
  dialog.setLeftLayer( before );
  dialog.setRightLayer( after );
  dialog.exec();
}

bool ChangeDetectionDialog::validateInputs()
{
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Specify the output file." ) );
    return false;
  }
  if ( m_beforeLayerCombo->currentData().toString().isEmpty()
       || m_afterLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the earlier and later images." ) );
    return false;
  }
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The earlier or later image is invalid." ) );
    return false;
  }
  const QString gridMessage = rasterGridCompatibilityMessage(
    before->source(), after->source() );
  if ( !gridMessage.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(),
                          tr( "The pixel grids of the two images are incompatible; per-pixel comparison is not possible:\n%1" )
                            .arg( gridMessage ) );
    return false;
  }
  return true;
}

Json::Value ChangeDetectionDialog::buildParams() const
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );

  Json::Value params( Json::objectValue );
  params["before"] = before ? before->source().toStdString() : std::string();
  params["after"] = after ? after->source().toStdString() : std::string();
  params["beforeBand"] = m_beforeBandCombo->currentData().toInt();
  params["afterBand"] = m_afterBandCombo->currentData().toInt();
  params["method"] = m_methodCombo->currentData().toString().toStdString();
  params["output"] = outputPath().toStdString();

  // Mask parameters surface when the user requests a mask output (the legacy
  // change_mask method always writes one; for the other methods the checkbox
  // opts in).
  const bool maskRequested = m_makeMaskCheck->isChecked()
                             || m_methodCombo->currentData().toString()
                                  == QStringLiteral( "change_mask" );
  if ( maskRequested )
  {
    params["makeMask"] = true;
    params["threshold"] = m_thresholdSpin->value();
    const QString strategy = m_thresholdMethodCombo->currentData().toString();
    if ( strategy != QStringLiteral( "manual" ) )
      params["thresholdMethod"] = strategy.toStdString();
    if ( strategy == QStringLiteral( "percentile" ) )
      params["percentile"] = m_percentileSpin->value();
    if ( strategy == QStringLiteral( "statistical" ) )
      params["statisticalK"] = m_statisticalKSpin->value();
    if ( m_minAreaSpin->value() > 0 )
      params["minAreaPixels"] = m_minAreaSpin->value();
    const QString cleanup = m_cleanupCombo->currentData().toString();
    if ( cleanup != QStringLiteral( "none" ) )
    {
      params["cleanup"] = cleanup.toStdString();
      params["cleanupIterations"] = m_cleanupIterSpin->value();
    }
  }

  return params;
}

void ChangeDetectionDialog::onRun()
{
  if ( !validateInputs() )
    return;

  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The earlier image is invalid." ) );
    return;
  }
  if ( !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The later image is invalid." ) );
    return;
  }

  setRasterLayer( before );
  const Json::Value params = buildParams();
  m_statusLabel->setText( tr( "Running..." ) );
  runOperatorTask( QStringLiteral( "rs:change_detection" ), params,
                   [this]( const Json::Value &result ) {
                     if ( !m_statusLabel )
                       return;
                     if ( result.isMember( "mean" ) )
                     {
                       QString text = tr( "Change mean %1, std dev %2" )
                                        .arg( result["mean"].asDouble(), 0, 'f', 4 )
                                        .arg( result["stddev"].asDouble(), 0, 'f', 4 );
                       if ( result.isMember( "changedPercent" ) )
                         text += tr( "; changed pixels %1 / %2 (%3%)" )
                                   .arg( result["changedPixels"].asUInt64() )
                                   .arg( result["totalPixels"].asUInt64() )
                                   .arg( result["changedPercent"].asDouble(), 0, 'f', 2 );
                       m_statusLabel->setText( text );
                     }
                   } );
}
