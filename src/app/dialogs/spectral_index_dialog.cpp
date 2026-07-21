// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"
#include "dialog_help_catalog.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>

SpectralIndexDialog::SpectralIndexDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void SpectralIndexDialog::setRasterLayer(QgsRasterLayer *layer)
{
    RasterProcessingDialogBase::setRasterLayer(layer);
    populateBandCombos();
}

void SpectralIndexDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    setupHelpBanner(mainLayout);
// Index selection — userData holds the operator enum string
    auto *idxLayout = new QHBoxLayout();
    idxLayout->addWidget(new QLabel(tr("Index:")));
    m_indexCombo = new QComboBox(this);
    m_indexCombo->addItem(tr("NDVI"), QStringLiteral("NDVI"));
    m_indexCombo->addItem(tr("EVI"), QStringLiteral("EVI"));
    m_indexCombo->addItem(tr("SAVI"), QStringLiteral("SAVI"));
    m_indexCombo->addItem(tr("NDWI"), QStringLiteral("NDWI"));
    m_indexCombo->addItem(tr("NDBI"), QStringLiteral("NDBI"));
    m_indexCombo->addItem(tr("MNDWI"), QStringLiteral("MNDWI"));
    SicnuDialogHelp::tip( m_indexCombo, tr(
      "光谱指数类型：\n"
      "• NDVI：植被 (NIR,Red)\n• EVI：增强植被 (NIR,Red,Blue)\n"
      "• SAVI：土壤调节植被 (NIR,Red)\n• NDWI：水体 (Green,NIR)\n"
      "• NDBI：建成区 (SWIR,NIR)\n• MNDWI：改进水体 (Green,SWIR)" ) );
    connect(m_indexCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectralIndexDialog::onIndexChanged);
    idxLayout->addWidget(m_indexCombo);
    mainLayout->addLayout(idxLayout);

    // Band selectors - Row 1
    auto *bandLayout = new QHBoxLayout();
    m_nirLabel = new QLabel(tr("NIR:"), this);
    bandLayout->addWidget(m_nirLabel);
    m_nirCombo = new QComboBox(this);
    SicnuDialogHelp::tip( m_nirCombo, tr( "近红外波段号（从 1 起）。Landsat8 常为 5，Sentinel-2 常为 8/8A。" ) );
    bandLayout->addWidget(m_nirCombo);
    m_redLabel = new QLabel(tr("Red:"), this);
    bandLayout->addWidget(m_redLabel);
    m_redCombo = new QComboBox(this);
    SicnuDialogHelp::tip( m_redCombo, tr( "红光波段。用于 NDVI/EVI/SAVI。" ) );
    bandLayout->addWidget(m_redCombo);
    mainLayout->addLayout(bandLayout);

    // Band selectors - Row 2
    auto *bandLayout2 = new QHBoxLayout();
    m_greenLabel = new QLabel(tr("Green:"), this);
    bandLayout2->addWidget(m_greenLabel);
    m_greenCombo = new QComboBox(this);
    SicnuDialogHelp::tip( m_greenCombo, tr( "绿光波段。用于 NDWI/MNDWI。" ) );
    bandLayout2->addWidget(m_greenCombo);
    m_blueLabel = new QLabel(tr("Blue:"), this);
    bandLayout2->addWidget(m_blueLabel);
    m_blueCombo = new QComboBox(this);
    SicnuDialogHelp::tip( m_blueCombo, tr( "蓝光波段。仅 EVI 需要。" ) );
    bandLayout2->addWidget(m_blueCombo);
    m_swirLabel = new QLabel(tr("SWIR:"), this);
    bandLayout2->addWidget(m_swirLabel);
    m_swirCombo = new QComboBox(this);
    SicnuDialogHelp::tip( m_swirCombo, tr( "短波红外。用于 NDBI/MNDWI。" ) );
    bandLayout2->addWidget(m_swirCombo);
    mainLayout->addLayout(bandLayout2);

    setupOutputRow(mainLayout);
    setupButtonBar(mainLayout);

    updateBandVisibility();
}

void SpectralIndexDialog::populateBandCombos()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    int bandCount = m_rasterLayer->bandCount();

    m_nirCombo->clear();
    m_redCombo->clear();
    m_greenCombo->clear();
    m_blueCombo->clear();
    m_swirCombo->clear();

    for (int i = 1; i <= bandCount; ++i) {
        QString bandName = tr("Band %1").arg(i);
        m_nirCombo->addItem(bandName, i);
        m_redCombo->addItem(bandName, i);
        m_greenCombo->addItem(bandName, i);
        m_blueCombo->addItem(bandName, i);
        m_swirCombo->addItem(bandName, i);
    }

    // Default Landsat/Sentinel-style mapping
    if (bandCount >= 4) {
        m_nirCombo->setCurrentIndex(3);
        m_redCombo->setCurrentIndex(2);
        m_greenCombo->setCurrentIndex(1);
        m_blueCombo->setCurrentIndex(0);
    }
    if (bandCount >= 5) {
        m_swirCombo->setCurrentIndex(4);
    }
}

void SpectralIndexDialog::updateBandVisibility()
{
    const int index = m_indexCombo->currentIndex();

    m_nirCombo->setVisible(true);
    m_nirLabel->setVisible(true);

    m_redCombo->setVisible(index == 0 || index == 1 || index == 2); // NDVI, EVI, SAVI
    m_redLabel->setVisible(index == 0 || index == 1 || index == 2);

    m_greenCombo->setVisible(index == 3 || index == 5); // NDWI, MNDWI
    m_greenLabel->setVisible(index == 3 || index == 5);

    m_blueCombo->setVisible(index == 1); // EVI only
    m_blueLabel->setVisible(index == 1);

    m_swirCombo->setVisible(index == 4 || index == 5); // NDBI, MNDWI
    m_swirLabel->setVisible(index == 4 || index == 5);
}

void SpectralIndexDialog::onIndexChanged(int /*index*/)
{
    updateBandVisibility();
}

void SpectralIndexDialog::onRun()
{
    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["index"] = m_indexCombo->currentData().toString().toStdString();
    params["nir"] = m_nirCombo->currentData().toInt();
    params["red"] = m_redCombo->currentData().toInt();
    params["green"] = m_greenCombo->currentData().toInt();
    params["blue"] = m_blueCombo->currentData().toInt();
    params["swir"] = m_swirCombo->currentData().toInt();

    runOperatorTask(QStringLiteral("rs:spectral_index"), params);
}
