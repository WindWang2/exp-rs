// fusion_dialog.cpp — Phase 11.1
#include "fusion_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

FusionDialog::FusionDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 520 );

  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Inputs and Fusion Method" ) );
  inputGroup->setToolTip(
    tr( "The panchromatic and multispectral images must cover the same area and be precisely co-registered." ) );
  auto *inputLayout = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputLayout );

  mPanCombo = new QComboBox( inputGroup );
  mPanCombo->setObjectName( QStringLiteral( "fusionPanCombo" ) );
  SicnuDialogHelp::tip( mPanCombo, tr( "High-resolution single-band panchromatic raster." ) );
  inputLayout->addRow( tr( "Panchromatic Image (high resolution)" ), mPanCombo );

  mMsCombo = new QComboBox( inputGroup );
  mMsCombo->setObjectName( QStringLiteral( "fusionMsCombo" ) );
  SicnuDialogHelp::tip( mMsCombo, tr( "Low-resolution multiband multispectral image." ) );
  inputLayout->addRow( tr( "Multispectral Image (lower resolution)" ), mMsCombo );

  mMethodCombo = new QComboBox( inputGroup );
  mMethodCombo->setObjectName( QStringLiteral( "fusionMethodCombo" ) );
  mMethodCombo->addItem( tr( "Linear Weighting" ), QStringLiteral( "linear" ) );
  mMethodCombo->addItem( tr( "Brovey Transform" ), QStringLiteral( "brovey" ) );
  mMethodCombo->addItem( tr( "IHS Fusion (RGB required)" ), QStringLiteral( "ihs" ) );
  mMethodCombo->addItem( tr( "PCA Fusion" ), QStringLiteral( "pca" ) );
  mMethodCombo->addItem( tr( "OTB BundleToPerfectSensor" ), QStringLiteral( "otb_btps" ) );
  mMethodCombo->addItem( tr( "GDAL Pansharpen" ), QStringLiteral( "gdal_pansharp" ) );
  SicnuDialogHelp::tip( mMethodCombo, tr(
    tr("Built-in methods: Linear / Brovey / IHS / PCA; external tools: OTB / GDAL pansharpening.") ) );
  inputLayout->addRow( tr( "Fusion Method" ), mMethodCombo );

  mWeightLabel = new QLabel( tr( "Pan Weight" ), inputGroup );
  mWeightSpin = new QDoubleSpinBox( inputGroup );
  mWeightSpin->setObjectName( QStringLiteral( "fusionWeightSpin" ) );
  mWeightSpin->setRange( 0.0, 1.0 );
  mWeightSpin->setValue( 0.5 );
  mWeightSpin->setSingleStep( 0.1 );
  mWeightSpin->setDecimals( 2 );
  SicnuDialogHelp::tip( mWeightSpin, tr( "Panchromatic band share in linear fusion (0.0–1.0)." ) );
  inputLayout->addRow( mWeightLabel, mWeightSpin );

  mBandWeightsWidget = new QWidget( inputGroup );
  mBandWeightsLayout = new QFormLayout( mBandWeightsWidget );
  mBandWeightsLayout->setContentsMargins( 0, 0, 0, 0 );
  mBandWeightsLayout->setHorizontalSpacing( 10 );
  mBandWeightsLayout->setVerticalSpacing( 8 );
  inputLayout->addRow( tr( "Per-Band Weights" ), mBandWeightsWidget );

  mWeightLabel->setVisible( false );
  mWeightSpin->setVisible( false );
  mBandWeightsWidget->setVisible( false );

  mRedCombo = new QComboBox( inputGroup );
  mGreenCombo = new QComboBox( inputGroup );
  mBlueCombo = new QComboBox( inputGroup );
  SicnuDialogHelp::tip( mRedCombo, tr( "Red band used by the IHS transform." ) );
  SicnuDialogHelp::tip( mGreenCombo, tr( "Green band used by the IHS transform." ) );
  SicnuDialogHelp::tip( mBlueCombo, tr( "Blue band used by the IHS transform." ) );
  mRedLabel = new QLabel( tr( "Red Band (R)" ), inputGroup );
  mGreenLabel = new QLabel( tr( "Green Band (G)" ), inputGroup );
  mBlueLabel = new QLabel( tr( "Blue Band (B)" ), inputGroup );
  inputLayout->addRow( mRedLabel, mRedCombo );
  inputLayout->addRow( mGreenLabel, mGreenCombo );
  inputLayout->addRow( mBlueLabel, mBlueCombo );
  mRedLabel->setVisible( false );
  mRedCombo->setVisible( false );
  mGreenLabel->setVisible( false );
  mGreenCombo->setVisible( false );
  mBlueLabel->setVisible( false );
  mBlueCombo->setVisible( false );

  auto updateMethodUi = [this]( int idx ) {
    QString method = mMethodCombo->itemData( idx ).toString();
    bool isLinear = ( method == QLatin1String( "linear" ) );
    mWeightLabel->setVisible( isLinear );
    mWeightSpin->setVisible( isLinear );
    mBandWeightsWidget->setVisible( isLinear );
    bool isIhs = ( method == QLatin1String( "ihs" ) );
    mRedLabel->setVisible( isIhs );
    mRedCombo->setVisible( isIhs );
    mGreenLabel->setVisible( isIhs );
    mGreenCombo->setVisible( isIhs );
    mBlueLabel->setVisible( isIhs );
    mBlueCombo->setVisible( isIhs );
  };
  connect( mMethodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, updateMethodUi );

  setupOutputRow( mainLayout );
  mStatusLabel = SicnuUi::makeHintLabel( this, tr( "Ready" ) );
  mainLayout->addWidget( mStatusLabel );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  populateRasterLayerCombo( mPanCombo );
  populateRasterLayerCombo( mMsCombo );

  connect( mMsCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int idx ) {
    mRedCombo->clear();
    mGreenCombo->clear();
    mBlueCombo->clear();
    mBandWeightSpins.clear();
    while ( mBandWeightsLayout->count() > 0 )
    {
      QLayoutItem *item = mBandWeightsLayout->takeAt( 0 );
      delete item->widget();
      delete item;
    }
    auto *rl = mMsCombo->itemData( idx ).value<QgsRasterLayer *>();
    if ( rl )
    {
      int nBands = rl->bandCount();
      for ( int i = 1; i <= nBands; ++i )
      {
        QString name = rl->bandName( i );
        if ( name.isEmpty() )
          name = tr( "Band %1" ).arg( i );
        mRedCombo->addItem( name, i );
        mGreenCombo->addItem( name, i );
        mBlueCombo->addItem( name, i );
        auto *spin = new QDoubleSpinBox();
        spin->setRange( 0.0, 1.0 );
        spin->setValue( 0.5 );
        spin->setSingleStep( 0.1 );
        spin->setDecimals( 2 );
        mBandWeightsLayout->addRow( name, spin );
        mBandWeightSpins.append( spin );
      }
      if ( mRedCombo->count() > 0 )
        mRedCombo->setCurrentIndex( 0 );
      if ( mGreenCombo->count() > 1 )
        mGreenCombo->setCurrentIndex( 1 );
      if ( mBlueCombo->count() > 2 )
        mBlueCombo->setCurrentIndex( 2 );
    }
  } );
  if ( mMsCombo->count() > 0 )
    emit mMsCombo->currentIndexChanged( 0 );
}

bool FusionDialog::validateInputs()
{
  auto *panLayer = mPanCombo ? mPanCombo->currentData().value<QgsRasterLayer *>() : nullptr;
  auto *msLayer = mMsCombo ? mMsCombo->currentData().value<QgsRasterLayer *>() : nullptr;

  if ( !panLayer || !panLayer->isValid() || !msLayer || !msLayer->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Select valid panchromatic and multispectral raster layers." ) );
    return false;
  }

  setRasterLayer( msLayer );

  QString outPath = outputPath();
  if ( outPath.isEmpty() )
  {
    outPath = QFileInfo( msLayer->source() ).path() + QLatin1Char( '/' )
              + QFileInfo( msLayer->source() ).completeBaseName() + QStringLiteral( "_fused.tif" );
    if ( m_outputEdit )
      m_outputEdit->setText( outPath );
  }

  return true;
}

void FusionDialog::onRun()
{
    auto *panLayer = mPanCombo->currentData().value<QgsRasterLayer *>();
    auto *msLayer = mMsCombo->currentData().value<QgsRasterLayer *>();

    if ( !panLayer || !msLayer )
    {
        QMessageBox::warning( this, dialogTitle(), tr( "Select both the panchromatic and multispectral layers." ) );
        return;
    }

    // Grid preflight: pan-sharpening intentionally mixes resolutions (the MS
    // raster is resampled onto the pan grid), so pixel-size differences are
    // allowed; other blocking grid issues are rejected up front.
    const QString gridMessage = rasterGridCompatibilityMessage(
        panLayer->source(), msLayer->source(), /*allowPixelSizeMismatch=*/true );
    if ( !gridMessage.isEmpty() )
    {
        QMessageBox::warning( this, dialogTitle(),
            tr( "The panchromatic and multispectral rasters are not co-registered:\n%1" )
                .arg( gridMessage ) );
        return;
    }

    QString outPath = outputPath();
    if ( outPath.isEmpty() )
    {
        outPath = QFileInfo( msLayer->source() ).path() + "/"
                     + QFileInfo( msLayer->source() ).baseName() + "_fused.tif";
        m_outputEdit->setText( outPath );
    }

    const QString method = mMethodCombo->currentData().toString();
    const QString panPath = panLayer->source();
    const QString msPath = msLayer->source();

    if ( method == QStringLiteral( "ihs" ) )
    {
        const int nMsBands = std::min( msLayer->bandCount(), 4 );
        const int rIdx = mRedCombo->currentIndex();
        const int gIdx = mGreenCombo->currentIndex();
        const int bIdx = mBlueCombo->currentIndex();
        if ( rIdx < 0 || gIdx < 0 || bIdx < 0 ||
             rIdx >= nMsBands || gIdx >= nMsBands || bIdx >= nMsBands )
        {
            QMessageBox::warning( this, dialogTitle(),
                tr( "IHS fusion requires valid red, green and blue bands for the multispectral image." ) );
            return;
        }
    }

    // External CLI methods run as operators too (thin client): the dialogs
    // never spawn subprocesses — otb_btps → otb:bundle_to_perfect_sensor,
    // gdal_pansharp → gdal:pansharpen.
    if ( method == QStringLiteral( "otb_btps" ) || method == QStringLiteral( "gdal_pansharp" ) )
    {
        Json::Value cliParams( Json::objectValue );
        cliParams["pan"] = panPath.toStdString();
        cliParams["ms"] = msPath.toStdString();
        cliParams["output"] = outPath.toStdString();
        const QString operatorId = ( method == QStringLiteral( "otb_btps" ) )
                                       ? QStringLiteral( "otb:bundle_to_perfect_sensor" )
                                       : QStringLiteral( "gdal:pansharpen" );
        runOperatorTask( operatorId, cliParams );
        return;
    }

    // Native methods share the RSOperator kernel with CLI/MCP.
    Json::Value params( Json::objectValue );
    params["pan"] = panPath.toStdString();
    params["ms"] = msPath.toStdString();
    params["output"] = outPath.toStdString();
    params["method"] = method.toStdString();
    params["panWeight"] = mWeightSpin ? mWeightSpin->value() : 0.5;
    params["redIdx"] = mRedCombo ? mRedCombo->currentIndex() : 0;
    params["greenIdx"] = mGreenCombo ? mGreenCombo->currentIndex() : 1;
    params["blueIdx"] = mBlueCombo ? mBlueCombo->currentIndex() : 2;
    if ( !mBandWeightSpins.isEmpty() )
    {
        params["msWeights"] = Json::Value( Json::arrayValue );
        for ( auto *spin : mBandWeightSpins )
            params["msWeights"].append( spin ? spin->value() : 0.5 );
    }

    runOperatorTask( QStringLiteral( "rs:image_fusion" ), params );
}

void FusionDialog::onMethodChanged(int index) { Q_UNUSED(index); }

void FusionDialog::onBrowsePan()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select Panchromatic Image"), QString(),
                                                tr("GeoTIFF Raster (*.tif *.tiff);;All Files (*)"));
    if (!path.isEmpty()) {
        // Find or add layer
        for (int i = 0; i < mPanCombo->count(); ++i) {
            auto *rl = mPanCombo->itemData(i).value<QgsRasterLayer*>();
            if (rl && rl->source() == path) {
                mPanCombo->setCurrentIndex(i);
                return;
            }
        }
    }
}

void FusionDialog::onBrowseMs()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select Multispectral Image"), QString(),
                                                tr("GeoTIFF Raster (*.tif *.tiff);;All Files (*)"));
    if (!path.isEmpty()) {
        for (int i = 0; i < mMsCombo->count(); ++i) {
            auto *rl = mMsCombo->itemData(i).value<QgsRasterLayer*>();
            if (rl && rl->source() == path) {
                mMsCombo->setCurrentIndex(i);
                return;
            }
        }
    }
}


