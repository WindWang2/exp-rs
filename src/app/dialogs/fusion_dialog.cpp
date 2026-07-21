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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QProcess>

FusionDialog::FusionDialog( QWidget *parent )
    : RasterProcessingDialogBase( parent )
{
    setWindowTitle( tr( "Image Fusion / Pan-sharpening" ) );
    setMinimumWidth( 500 );

    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    
    setupHelpBanner(mainLayout);
}

    // Input section
    auto *inputGroup = new QGroupBox( tr( "Input" ) );
    auto *inputLayout = new QFormLayout( inputGroup );

    mPanCombo = new QComboBox;
    SicnuDialogHelp::tip( mPanCombo, tr( "全色高分辨率单波段。须与多光谱大致同范围并已配准。" ) );
    inputLayout->addRow( tr( "Panchromatic (high-res):" ), mPanCombo );

    mMsCombo = new QComboBox;
    SicnuDialogHelp::tip( mMsCombo, tr( "多光谱低分辨率影像。融合后空间分辨率接近全色。" ) );
    inputLayout->addRow( tr( "Multispectral (low-res):" ), mMsCombo );

    mMethodCombo = new QComboBox;
    mMethodCombo->addItem( tr( "Linear Weighted Fusion" ), "linear" );
    mMethodCombo->addItem( tr( "Brovey Transform" ), "brovey" );
    mMethodCombo->addItem( tr( "IHS Fusion (requires RGB)" ), "ihs" );
    mMethodCombo->addItem( tr( "PCA Fusion" ), "pca" );
    mMethodCombo->addItem( tr( "OTB BundleToPerfectSensor" ), "otb_btps" );
    mMethodCombo->addItem( tr( "GDAL Pansharpen" ), "gdal_pansharp" );
    SicnuDialogHelp::tip( mMethodCombo, tr(
      "融合方法：Linear（可调权重）/ Brovey / IHS（需 RGB 波段）/ PCA；"
      "或调用 OTB/GDAL 外部 pansharpen。" ) );
    inputLayout->addRow( tr( "Method:" ), mMethodCombo );

    // Pan weight for linear fusion
    mWeightLabel = new QLabel( tr( "Pan Weight:" ) );
    mWeightSpin = new QDoubleSpinBox();
    mWeightSpin->setRange( 0.0, 1.0 );
    mWeightSpin->setValue( 0.5 );
    mWeightSpin->setSingleStep( 0.1 );
    mWeightSpin->setDecimals( 2 );
    SicnuDialogHelp::tip( mWeightSpin, tr( "线性融合中全色占比 0–1。越大空间细节越强，光谱可能更偏。" ) );
    inputLayout->addRow( mWeightLabel, mWeightSpin );

    // Per-band weights container (populated when layer is selected)
    mBandWeightsWidget = new QWidget;
    mBandWeightsLayout = new QFormLayout( mBandWeightsWidget );
    mBandWeightsLayout->setContentsMargins( 0, 0, 0, 0 );
    inputLayout->addRow( tr( "Band Weights:" ), mBandWeightsWidget );

    // Hide weight controls by default, show only for linear fusion
    mWeightLabel->setVisible( false );
    mWeightSpin->setVisible( false );
    mBandWeightsWidget->setVisible( false );

    connect( mMethodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [this]( int idx ) {
                 QString method = mMethodCombo->itemData( idx ).toString();
                 bool isLinear = ( method == "linear" );
                 mWeightLabel->setVisible( isLinear );
                 mWeightSpin->setVisible( isLinear );
                 mBandWeightsWidget->setVisible( isLinear );
                 bool isIhs = ( method == "ihs" );
                 mRedLabel->setVisible( isIhs );
                 mRedCombo->setVisible( isIhs );
                 mGreenLabel->setVisible( isIhs );
                 mGreenCombo->setVisible( isIhs );
                 mBlueLabel->setVisible( isIhs );
                 mBlueCombo->setVisible( isIhs );
             } );

    // RGB band selection for IHS fusion
    mRedCombo = new QComboBox;
    mGreenCombo = new QComboBox;
    mBlueCombo = new QComboBox;
    SicnuDialogHelp::tip( mRedCombo, tr( "IHS 融合：多光谱红波段。" ) );
    SicnuDialogHelp::tip( mGreenCombo, tr( "IHS 融合：多光谱绿波段。" ) );
    SicnuDialogHelp::tip( mBlueCombo, tr( "IHS 融合：多光谱蓝波段。" ) );
    mRedLabel = new QLabel( tr( "Red Band:" ) );
    mGreenLabel = new QLabel( tr( "Green Band:" ) );
    mBlueLabel = new QLabel( tr( "Blue Band:" ) );
    inputLayout->addRow( mRedLabel, mRedCombo );
    inputLayout->addRow( mGreenLabel, mGreenCombo );
    inputLayout->addRow( mBlueLabel, mBlueCombo );

    // Hide RGB combos by default, show only for IHS
    mRedLabel->setVisible( false );
    mRedCombo->setVisible( false );
    mGreenLabel->setVisible( false );
    mGreenCombo->setVisible( false );
    mBlueLabel->setVisible( false );
    mBlueCombo->setVisible( false );

    connect( mMethodCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             [this]( int idx ) {
                 bool isIhs = ( mMethodCombo->itemData( idx ).toString() == "ihs" );
                 mRedLabel->setVisible( isIhs );
                 mRedCombo->setVisible( isIhs );
                 mGreenLabel->setVisible( isIhs );
                 mGreenCombo->setVisible( isIhs );
                 mBlueLabel->setVisible( isIhs );
                 mBlueCombo->setVisible( isIhs );
             } );

    mainLayout->addWidget( inputGroup );

    // Output section (using base class)
    setupOutputRow(mainLayout);

    // Status
    mStatusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(mStatusLabel);

    // Buttons (using base class)
    setupButtonBar(mainLayout);

    // Populate layers
    populateRasterLayerCombo( mPanCombo );
    populateRasterLayerCombo( mMsCombo );

    // Populate RGB band combos and per-band weights when multispectral layer changes
    connect( mMsCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this]( int idx ) {
        mRedCombo->clear();
        mGreenCombo->clear();
        mBlueCombo->clear();
        mBandWeightSpins.clear();

        // Clear old band weight widgets
        while ( mBandWeightsLayout->count() > 0 ) {
            QLayoutItem *item = mBandWeightsLayout->takeAt( 0 );
            delete item->widget();
            delete item;
        }

        auto *rl = mMsCombo->itemData( idx ).value<QgsRasterLayer *>();
        if ( rl ) {
            int nBands = rl->bandCount();
            for ( int i = 1; i <= nBands; ++i ) {
                QString name = rl->bandName( i );
                if ( name.isEmpty() ) name = tr( "Band %1" ).arg( i );
                mRedCombo->addItem( name, i );
                mGreenCombo->addItem( name, i );
                mBlueCombo->addItem( name, i );

                // Add per-band weight spin
                auto *spin = new QDoubleSpinBox();
                spin->setRange( 0.0, 1.0 );
                spin->setValue( 0.5f );
                spin->setSingleStep( 0.1 );
                spin->setDecimals( 2 );
                mBandWeightsLayout->addRow( name + ":", spin );
                mBandWeightSpins.append( spin );
            }
            // Default R=1, G=2, B=3
            if ( mRedCombo->count() > 0 ) mRedCombo->setCurrentIndex( 0 );
            if ( mGreenCombo->count() > 1 ) mGreenCombo->setCurrentIndex( 1 );
            if ( mBlueCombo->count() > 2 ) mBlueCombo->setCurrentIndex( 2 );
        }
    } );

    // Trigger initial population
    if ( mMsCombo->count() > 0 )
        emit mMsCombo->currentIndexChanged( 0 );
}

void FusionDialog::onRun()
{
    auto *panLayer = mPanCombo->currentData().value<QgsRasterLayer *>();
    auto *msLayer = mMsCombo->currentData().value<QgsRasterLayer *>();

    if ( !panLayer || !msLayer )
    {
        QMessageBox::warning( this, tr( "Error" ), tr( "Select both panchromatic and multispectral layers." ) );
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
            QMessageBox::warning( this, tr( "Error" ),
                tr( "IHS fusion requires selecting R, G, B bands from the multispectral image." ) );
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
    QString path = QFileDialog::getOpenFileName(this, tr("Select Panchromatic Image"), QString(),
                                                tr("GeoTIFF (*.tif *.tiff);;All Files (*)"));
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
                                                tr("GeoTIFF (*.tif *.tiff);;All Files (*)"));
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


