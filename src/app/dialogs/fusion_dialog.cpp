// fusion_dialog.cpp — Phase 11.1
#include "fusion_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include "processing/tools/tool_path_manager.h"

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
#include <QProcess>

FusionDialog::FusionDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 520 );

  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "输入与融合方法" ) );
  inputGroup->setToolTip(
    tr( "全色与多光谱影像须空间覆盖一致并已完成高精度几何配准。" ) );
  auto *inputLayout = SicnuUi::makeFormLayout();
  qobject_cast<QVBoxLayout *>( inputGroup->layout() )->addLayout( inputLayout );

  mPanCombo = new QComboBox( inputGroup );
  mPanCombo->setObjectName( QStringLiteral( "fusionPanCombo" ) );
  SicnuDialogHelp::tip( mPanCombo, tr( "全色高分辨率单波段栅格。" ) );
  inputLayout->addRow( tr( "全色影像 (高分)" ), mPanCombo );

  mMsCombo = new QComboBox( inputGroup );
  mMsCombo->setObjectName( QStringLiteral( "fusionMsCombo" ) );
  SicnuDialogHelp::tip( mMsCombo, tr( "多光谱低分辨率多波段影像。" ) );
  inputLayout->addRow( tr( "多光谱影像 (低分)" ), mMsCombo );

  mMethodCombo = new QComboBox( inputGroup );
  mMethodCombo->setObjectName( QStringLiteral( "fusionMethodCombo" ) );
  mMethodCombo->addItem( tr( "线性加权" ), QStringLiteral( "linear" ) );
  mMethodCombo->addItem( tr( "Brovey 变换" ), QStringLiteral( "brovey" ) );
  mMethodCombo->addItem( tr( "IHS 融合 (需 RGB)" ), QStringLiteral( "ihs" ) );
  mMethodCombo->addItem( tr( "PCA 融合" ), QStringLiteral( "pca" ) );
  mMethodCombo->addItem( tr( "OTB BundleToPerfectSensor" ), QStringLiteral( "otb_btps" ) );
  mMethodCombo->addItem( tr( "GDAL Pansharpen" ), QStringLiteral( "gdal_pansharp" ) );
  SicnuDialogHelp::tip( mMethodCombo, tr(
    "内置方法：Linear / Brovey / IHS / PCA；外部工具：OTB / GDAL 全色锐化。" ) );
  inputLayout->addRow( tr( "融合方法" ), mMethodCombo );

  mWeightLabel = new QLabel( tr( "全色权重" ), inputGroup );
  mWeightSpin = new QDoubleSpinBox( inputGroup );
  mWeightSpin->setObjectName( QStringLiteral( "fusionWeightSpin" ) );
  mWeightSpin->setRange( 0.0, 1.0 );
  mWeightSpin->setValue( 0.5 );
  mWeightSpin->setSingleStep( 0.1 );
  mWeightSpin->setDecimals( 2 );
  SicnuDialogHelp::tip( mWeightSpin, tr( "线性融合中全色波段占比 (0.0~1.0)。" ) );
  inputLayout->addRow( mWeightLabel, mWeightSpin );

  mBandWeightsWidget = new QWidget( inputGroup );
  mBandWeightsLayout = new QFormLayout( mBandWeightsWidget );
  mBandWeightsLayout->setContentsMargins( 0, 0, 0, 0 );
  mBandWeightsLayout->setHorizontalSpacing( 10 );
  mBandWeightsLayout->setVerticalSpacing( 8 );
  inputLayout->addRow( tr( "分波段权重" ), mBandWeightsWidget );

  mWeightLabel->setVisible( false );
  mWeightSpin->setVisible( false );
  mBandWeightsWidget->setVisible( false );

  mRedCombo = new QComboBox( inputGroup );
  mGreenCombo = new QComboBox( inputGroup );
  mBlueCombo = new QComboBox( inputGroup );
  SicnuDialogHelp::tip( mRedCombo, tr( "IHS 变换对应的红波段。" ) );
  SicnuDialogHelp::tip( mGreenCombo, tr( "IHS 变换对应的绿波段。" ) );
  SicnuDialogHelp::tip( mBlueCombo, tr( "IHS 变换对应的蓝波段。" ) );
  mRedLabel = new QLabel( tr( "红波段 (R)" ), inputGroup );
  mGreenLabel = new QLabel( tr( "绿波段 (G)" ), inputGroup );
  mBlueLabel = new QLabel( tr( "蓝波段 (B)" ), inputGroup );
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
  mStatusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
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
          name = tr( "波段 %1" ).arg( i );
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
    QMessageBox::warning( this, dialogTitle(), tr( "请选择有效的全色和多光谱栅格图层。" ) );
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
        QMessageBox::warning( this, dialogTitle(), tr( "请同时选择全色和多光谱图层。" ) );
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
            tr( "全色与多光谱栅格像元网格未配准：\n%1" )
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
                tr( "IHS 融合需要为多光谱影像指定有效的红、绿、蓝波段。" ) );
            return;
        }
    }

    // External CLI methods stay on the process path (not in rs:image_fusion).
    if ( method == QStringLiteral( "otb_btps" ) || method == QStringLiteral( "gdal_pansharp" ) )
    {
        runGdalTask( [method, panPath, msPath, outPath]() -> QString {
            QString program;
            QStringList args;
            if ( method == QStringLiteral( "otb_btps" ) )
            {
                program = ToolPathManager::instance().otbToolPath( QStringLiteral( "BundleToPerfectSensor" ) );
                if ( program.isEmpty() )
                    return QString();
                args << QStringLiteral( "-in" ) << msPath
                     << QStringLiteral( "-inp" ) << panPath
                     << QStringLiteral( "-out" ) << outPath;
            }
            else
            {
                // gdal_pansharpen.py pan_dataset spectral_dataset out_dataset
                program = ToolPathManager::instance().gdalToolPath( QStringLiteral( "gdal_pansharpen.py" ) );
                if ( program.isEmpty() )
                    return QString();
                args << panPath << msPath << outPath
                     << QStringLiteral( "-r" ) << QStringLiteral( "bilinear" )
                     << QStringLiteral( "-of" ) << QStringLiteral( "GTiff" )
                     << QStringLiteral( "-co" ) << QStringLiteral( "COMPRESS=LZW" );
            }

            QProcess proc;
            proc.setProcessChannelMode( QProcess::MergedChannels );
            proc.start( program, args );
            // waitForFinished returns bool (true = finished); do not compare to 0.
            if ( !proc.waitForStarted( 5000 )
                 || !proc.waitForFinished( -1 )
                 || proc.exitCode() != 0
                 || proc.exitStatus() != QProcess::NormalExit )
                return QString();
            return outPath;
        } );
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
    QString path = QFileDialog::getOpenFileName(this, tr("选择全色影像"), QString(),
                                                tr("GeoTIFF 栅格 (*.tif *.tiff);;所有文件 (*)"));
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
    QString path = QFileDialog::getOpenFileName(this, tr("选择多光谱影像"), QString(),
                                                tr("GeoTIFF 栅格 (*.tif *.tiff);;所有文件 (*)"));
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


