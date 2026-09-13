// src/app/dialogs/post_classification_dialog.cpp — Post-Classification Compare
#include "post_classification_dialog.h"
#include "dialog_help_catalog.h"
#include "widgets/raster_layer_combo.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include <qgsproject.h>

PostClassificationDialog::PostClassificationDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 480 );
  setupUi();
}

void PostClassificationDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Two-date classification result input" ) );
  inputGroup->setToolTip(
    tr( "Compares two classification dates: outputs a per-class transition matrix (rows = earlier classes, columns = later classes),"
        "Per-class gains / losses and a change-type map." ) );
  auto *form = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( form );

  m_beforeLayerCombo = new RasterLayerCombo( inputGroup );
  m_beforeLayerCombo->setObjectName( QStringLiteral( "postClassBeforeCombo" ) );
  m_afterLayerCombo = new RasterLayerCombo( inputGroup );
  m_afterLayerCombo->setObjectName( QStringLiteral( "postClassAfterCombo" ) );
  m_beforeBandCombo = new QComboBox( inputGroup );
  m_beforeBandCombo->setObjectName( QStringLiteral( "postClassBeforeBandCombo" ) );
  m_afterBandCombo = new QComboBox( inputGroup );
  m_afterBandCombo->setObjectName( QStringLiteral( "postClassAfterBandCombo" ) );
  SicnuDialogHelp::tip( m_beforeLayerCombo, tr( "Earlier classification raster (theme map)." ) );
  SicnuDialogHelp::tip( m_afterLayerCombo, tr( "Later classification raster (theme map)." ) );
  SicnuDialogHelp::tip( m_beforeBandCombo, tr( "Earlier classification band." ) );
  SicnuDialogHelp::tip( m_afterBandCombo, tr( "Later classification band." ) );
  form->addRow( tr( "Earlier Classification" ), m_beforeLayerCombo );
  form->addRow( tr( "Earlier Band" ), m_beforeBandCombo );
  form->addRow( tr( "Later Classification" ), m_afterLayerCombo );
  form->addRow( tr( "Later Band" ), m_afterBandCombo );

  m_classCountSpin = new QSpinBox( inputGroup );
  m_classCountSpin->setObjectName( QStringLiteral( "postClassCountSpin" ) );
  m_classCountSpin->setRange( 0, 255 );
  m_classCountSpin->setValue( 0 );
  m_classCountSpin->setSpecialValueText( tr( "Automatic (max observed class + 1)" ) );
  SicnuDialogHelp::tip( m_classCountSpin, tr(
    "Total classes (the change code before*classCount+after must fit a UInt16, hence ≤ 255)."
    "0 = inferred automatically from the maximum class observed across the two images.")  );
  form->addRow( tr( "Total Classes" ), m_classCountSpin );

  setupOutputRow( mainLayout );

  m_summaryLabel = SicnuUi::makeHintLabel( this, tr( "After running, the change statistics summary and transition matrix appear here." ) );
  m_summaryLabel->setObjectName( QStringLiteral( "postClassSummaryLabel" ) );
  m_summaryLabel->setWordWrap( true );
  mainLayout->addWidget( m_summaryLabel );

  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  connect( m_beforeLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [this] { populateBandCombo( m_beforeLayerCombo, m_beforeBandCombo ); } );
  connect( m_afterLayerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [this] { populateBandCombo( m_afterLayerCombo, m_afterBandCombo ); } );

  populateLayers();
}

void PostClassificationDialog::populateBandCombo( RasterLayerCombo *layerCombo, QComboBox *bandCombo )
{
  bandCombo->clear();
  auto *rl = layerCombo->currentRasterLayer();
  if ( !rl )
    return;
  for ( int i = 1; i <= rl->bandCount(); ++i )
    bandCombo->addItem( tr( "Band %1" ).arg( i ), i );
}

void PostClassificationDialog::populateLayers()
{
  m_beforeLayerCombo->populate();
  m_afterLayerCombo->populate();
  populateBandCombo( m_beforeLayerCombo, m_beforeBandCombo );
  populateBandCombo( m_afterLayerCombo, m_afterBandCombo );
}

Json::Value PostClassificationDialog::buildParams() const
{
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );

  Json::Value params( Json::objectValue );
  params["before"] = before ? before->source().toStdString() : std::string();
  params["after"] = after ? after->source().toStdString() : std::string();
  params["output"] = outputPath().toStdString();
  params["band"] = m_beforeBandCombo->currentData().toInt();
  params["afterBand"] = m_afterBandCombo->currentData().toInt();
  if ( m_classCountSpin->value() > 0 )
    params["class_count"] = m_classCountSpin->value();
  return params;
}

bool PostClassificationDialog::validateInputs()
{
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Specify the output file." ) );
    return false;
  }
  if ( m_beforeLayerCombo->currentData().toString().isEmpty()
       || m_afterLayerCombo->currentData().toString().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select the earlier and later classification rasters." ) );
    return false;
  }
  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The earlier or later classification raster is invalid." ) );
    return false;
  }
  const QString gridMessage = rasterGridCompatibilityMessage(
    before->source(), after->source() );
  if ( !gridMessage.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(),
                          tr( "The pixel grids of the two classification rasters are incompatible; cannot compare:\n%1" )
                            .arg( gridMessage ) );
    return false;
  }
  return true;
}

void PostClassificationDialog::onRun()
{
  if ( !validateInputs() )
    return;

  auto *before = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_beforeLayerCombo->currentData().toString() ) );
  auto *after = qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( m_afterLayerCombo->currentData().toString() ) );
  if ( !before || !before->isValid() || !after || !after->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "The selected classification raster is invalid." ) );
    return;
  }

  setRasterLayer( before );
  runOperatorTask( QStringLiteral( "rs:post_classification_change" ), buildParams(),
                   [this]( const Json::Value &result ) { showResultSummary( result ); } );
}

void PostClassificationDialog::showResultSummary( const Json::Value &result )
{
  if ( !m_summaryLabel || !result.isObject() )
    return;

  QStringList lines;
  const uint64_t total = result.isMember( "totalPixels" ) ? result["totalPixels"].asUInt64() : 0;
  if ( result.isMember( "changedPixels" ) )
  {
    lines << tr( "Changed pixels: %1 / %2 (%3%)" )
               .arg( result["changedPixels"].asUInt64() )
               .arg( total )
               .arg( result["changedPercent"].asDouble(), 0, 'f', 2 );
  }

  if ( result.isMember( "fromTotals" ) && result["fromTotals"].isArray() &&
       result.isMember( "toTotals" ) && result["toTotals"].isArray() )
  {
    lines << tr( "Per-class pixel counts and share changes (earlier → later):" );
    const int n = static_cast<int>( result["fromTotals"].size() );
    for ( int c = 0; c < n; ++c )
    {
      const uint64_t fCount = result["fromTotals"][c].asUInt64();
      const uint64_t tCount = result["toTotals"][c].asUInt64();
      const double fRatio = total > 0 ? ( 100.0 * static_cast<double>( fCount ) / total ) : 0.0;
      const double tRatio = total > 0 ? ( 100.0 * static_cast<double>( tCount ) / total ) : 0.0;
      const int64_t net = result.isMember( "netChange" ) && static_cast<int>( result["netChange"].size() ) > c
                            ? result["netChange"][c].asInt64()
                            : ( static_cast<int64_t>( tCount ) - static_cast<int64_t>( fCount ) );
      lines << tr("  class %1: %2 (%3%) → %4 (%5%) [net change: %6%7]" )
                   .arg( c )
                   .arg( fCount )
                   .arg( fRatio, 0, 'f', 2 )
                   .arg( tCount )
                   .arg( tRatio, 0, 'f', 2 )
                   .arg( net >= 0 ? "+" : "" )
                   .arg( net );
    }
  }

  if ( result.isMember( "transitionMatrix" ) && result["transitionMatrix"].isArray()
       && result["transitionMatrix"].size() > 0 )
  {
    lines << tr( "Transition matrix (rows = earlier epoch, columns = later epoch; non-zero transitions only):" );
    const int n = static_cast<int>( result["transitionMatrix"].size() );
    for ( int from = 0; from < n; ++from )
    {
      const Json::Value &row = result["transitionMatrix"][from];
      for ( int to = 0; to < n; ++to )
      {
        if ( from != to && row[to].asUInt64() > 0 )
        {
          lines << tr("  class %1 → class %2: %3 pixels" )
                       .arg( from )
                       .arg( to )
                       .arg( row[to].asUInt64() );
        }
      }
    }
  }
  m_summaryLabel->setText( lines.isEmpty() ? tr( "Run finished (no summary data)." )
                                           : lines.join( QLatin1Char( '\n' ) ) );
}
