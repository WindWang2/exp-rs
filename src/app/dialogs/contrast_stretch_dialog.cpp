// src/app/dialogs/contrast_stretch_dialog.cpp
#include "contrast_stretch_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "app/widgets/histogram_stretch_widget.h"
#include "widgets/raster_layer_combo.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

ContrastStretchDialog::ContrastStretchDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void ContrastStretchDialog::setRasterLayer( QgsRasterLayer *layer )
{
  RasterProcessingDialogBase::setRasterLayer( layer );
  if ( m_layerCombo && layer )
  {
    const int idx = m_layerCombo->findData( layer->id() );
    if ( idx >= 0 && m_layerCombo->currentIndex() != idx )
    {
      m_layerCombo->blockSignals( true );
      m_layerCombo->setCurrentIndex( idx );
      m_layerCombo->blockSignals( false );
    }
  }
  if ( m_stretchWidget && layer )
  {
    m_stretchWidget->setRasterLayer( layer );
  }
}

void ContrastStretchDialog::onLayerChanged( int /*index*/ )
{
  if ( m_layerCombo )
  {
    auto *layer = m_layerCombo->currentRasterLayer();
    if ( layer && layer != m_rasterLayer )
      setRasterLayer( layer );
  }
}

void ContrastStretchDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // Input Layer Group
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Input Data" ) );
  auto *inputForm = SicnuUi::makeFormLayout();
  inputForm->setContentsMargins( 0, 0, 0, 0 );

  m_layerCombo = new RasterLayerCombo( inputGroup );
  m_layerCombo->setObjectName( QStringLiteral( "contrastStretchInputLayerCombo" ) );
  SicnuDialogHelp::tip( m_layerCombo, tr( "Select the raster layer for contrast stretching." ) );
  m_layerCombo->populate();
  connect( m_layerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onLayerChanged );
  inputForm->addRow( tr( "Input Raster" ), m_layerCombo );
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputForm );

  // Embedded Interactive Photoshop Levels & Histogram Panel
  m_stretchWidget = new HistogramStretchWidget( this );
  mainLayout->addWidget( m_stretchWidget, 1 );

  // Preset Parameters Group
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Preset Algorithms and Export" ) );
  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_methodCombo = new QComboBox( paramGroup );
  m_methodCombo->addItems( { tr( "Custom Photoshop Levels" ), tr( "Linear Stretch (Min-Max)" ), tr( "Percent Clip Stretch" ),
                             tr( "Std-Dev Stretch" ), tr( "Histogram Equalization" ) } );
  SicnuDialogHelp::tip( m_methodCombo, tr(
    "Stretch method:\n"
    "• Photoshop Levels: interactively adjust shadows, highlights and the gamma midtone\n"
    "• Linear: min–max\n"
    "• Percent clip: clip both tails, then stretch\n"
    "• Std dev: mean±K×std dev\n"
    "• Histogram equalization: enhances global contrast")  );
  connect( m_methodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &ContrastStretchDialog::onMethodChanged );
  form->addRow( tr( "Preset Method" ), m_methodCombo );

  m_clipLabel = new QLabel( tr( "Clip Ratio" ), paramGroup );
  m_clipSpin = new QDoubleSpinBox( paramGroup );
  m_clipSpin->setRange( 0.1, 50.0 );
  m_clipSpin->setValue( 2.0 );
  m_clipSpin->setSingleStep( 0.5 );
  m_clipSpin->setDecimals( 1 );
  m_clipSpin->setSuffix( QStringLiteral( " %" ) );
  SicnuDialogHelp::tip( m_clipSpin, tr( "Discard this fraction of pixels at both tails before stretching; 1–2% is typical." ) );
  form->addRow( m_clipLabel, m_clipSpin );

  m_stddevLabel = new QLabel( tr( "Std-Dev Multiplier K" ), paramGroup );
  m_stddevSpin = new QDoubleSpinBox( paramGroup );
  m_stddevSpin->setRange( 0.1, 10.0 );
  m_stddevSpin->setValue( 2.0 );
  m_stddevSpin->setSingleStep( 0.5 );
  m_stddevSpin->setDecimals( 1 );
  SicnuDialogHelp::tip( m_stddevSpin, tr( "Stretches to mean±K·σ; 2 is typical." ) );
  form->addRow( m_stddevLabel, m_stddevSpin );

  qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );

  onMethodChanged( 0 );

  if ( m_rasterLayer )
    m_layerCombo->selectLayer( m_rasterLayer->id() );
  else if ( m_layerCombo->count() > 0 )
    setRasterLayer( m_layerCombo->currentRasterLayer() );
}

void ContrastStretchDialog::onMethodChanged( int index )
{
  m_clipLabel->setVisible( index == 2 );
  m_clipSpin->setVisible( index == 2 );
  m_stddevLabel->setVisible( index == 3 );
  m_stddevSpin->setVisible( index == 3 );
}

void ContrastStretchDialog::onRun()
{
  if ( !m_rasterLayer )
    return;

  int methodIndex = m_methodCombo->currentIndex();
  double clipValue = m_clipSpin->value();
  double stddevValue = m_stddevSpin->value();

  // Thin client: the streaming stretch kernel runs as the rs:contrast_stretch
  // operator through the Task Center — same execution path as CLI/MCP.
  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  switch ( methodIndex )
  {
    case 0:
    {
      QVector<QPointF> piecewisePoints = m_stretchWidget ? m_stretchWidget->piecewisePoints()
                                                         : QVector<QPointF>();
      if ( piecewisePoints.size() < 2 )
      {
        QMessageBox::warning( this, dialogTitle(),
                              tr( "Custom levels need at least two control points; use a preset method instead." ) );
        return;
      }
      params["method"] = "piecewise";
      params["piecewisePoints"] = Json::Value( Json::arrayValue );
      for ( const auto &pt : piecewisePoints )
      {
        Json::Value pair( Json::arrayValue );
        pair.append( pt.x() );
        pair.append( pt.y() );
        params["piecewisePoints"].append( pair );
      }
      break;
    }
    case 2:
      params["method"] = "percent_clip";
      params["clipPercent"] = clipValue;
      break;
    case 3:
      params["method"] = "stddev";
      params["stddevK"] = stddevValue;
      break;
    case 4:
      params["method"] = "histogram_equalize";
      break;
    default:
      params["method"] = "linear";
      break;
  }
  runOperatorTask( QStringLiteral( "rs:contrast_stretch" ), params );
}
