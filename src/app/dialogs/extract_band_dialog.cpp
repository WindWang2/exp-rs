// extract_band_dialog.cpp — Extract single band from multi-band raster
#include "extract_band_dialog.h"
#include "async_gdal_runner.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_conv.h>

#include "processing/gdal/gdal_safe_call.h"

ExtractBandDialog::ExtractBandDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Extract Band"));
    setupUi();
}

void ExtractBandDialog::setRasterLayer(QgsRasterLayer *layer)
{
    if (!layer) return;

    // Find and select the layer in the combo box
    for (int i = 0; i < m_layerCombo->count(); ++i) {
        if (m_layerCombo->itemData(i).value<QgsRasterLayer *>() == layer) {
            m_layerCombo->setCurrentIndex(i);
            break;
        }
    }
}

QString ExtractBandDialog::outputPath() const
{
    return m_outputEdit ? m_outputEdit->text().trimmed() : QString();
}

void ExtractBandDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Input section
    auto *inputGroup = new QGroupBox(tr("Input"));
    auto *inputLayout = new QFormLayout(inputGroup);

    m_layerCombo = new QComboBox;
    m_layerCombo->setToolTip(tr("Select a multi-band raster layer"));
    inputLayout->addRow(tr("Raster Layer:"), m_layerCombo);

    m_bandCombo = new QComboBox;
    m_bandCombo->setToolTip(tr("Select the band to extract"));
    inputLayout->addRow(tr("Band:"), m_bandCombo);

    mainLayout->addWidget(inputGroup);

    // Output section
    auto *outputGroup = new QGroupBox(tr("Output"));
    auto *outputLayout = new QFormLayout(outputGroup);

    auto *outputRow = new QWidget;
    auto *outputRowLayout = new QHBoxLayout(outputRow);
    outputRowLayout->setContentsMargins(0, 0, 0, 0);
    m_outputEdit = new QLineEdit;
    m_outputEdit->setPlaceholderText(tr("Output raster path"));
    auto *browseBtn = new QPushButton(tr("Browse..."));
    outputRowLayout->addWidget(m_outputEdit);
    outputRowLayout->addWidget(browseBtn);
    outputLayout->addRow(tr("Output File:"), outputRow);

    mainLayout->addWidget(outputGroup);

    // Buttons
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_runButton = buttons->button(QDialogButtonBox::Ok);
    m_runButton->setText(tr("Extract"));
    mainLayout->addWidget(buttons);

    // Connections
    connect(browseBtn, &QPushButton::clicked, this, &ExtractBandDialog::onBrowseOutput);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExtractBandDialog::onRun);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExtractBandDialog::onLayerChanged);

    // Populate layers
    const auto layers = QgsProject::instance()->mapLayers().values();
    for (auto *layer : layers) {
        if (auto *rl = qobject_cast<QgsRasterLayer *>(layer)) {
            if (rl->bandCount() > 1) {
                m_layerCombo->addItem(rl->name(), QVariant::fromValue(rl));
            }
        }
    }

    // Populate bands for first layer
    if (m_layerCombo->count() > 0) {
        populateBandCombo();
    }
}

void ExtractBandDialog::populateBandCombo()
{
    m_bandCombo->clear();

    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl) return;

    for (int i = 1; i <= rl->bandCount(); ++i) {
        QString bandName = rl->bandName(i);
        if (bandName.isEmpty()) {
            bandName = tr("Band %1").arg(i);
        }
        m_bandCombo->addItem(bandName, i);
    }
}

void ExtractBandDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Extracted Band"), QString(),
                                                 tr("GeoTIFF (*.tif *.tiff)"));
    if (!path.isEmpty()) {
        m_outputEdit->setText(path);
    }
}

void ExtractBandDialog::onRun()
{
    auto *rl = m_layerCombo->currentData().value<QgsRasterLayer *>();
    if (!rl) {
        QMessageBox::warning(this, tr("Error"), tr("Select a raster layer."));
        return;
    }

    int bandIndex = m_bandCombo->currentData().toInt();
    if (bandIndex < 1) {
        QMessageBox::warning(this, tr("Error"), tr("Select a band to extract."));
        return;
    }

    QString outputPath = m_outputEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        // Auto-generate output path
        QString inputPath = rl->source();
        outputPath = QFileInfo(inputPath).path() + "/" + QFileInfo(inputPath).baseName()
                     + tr("_band%1.tif").arg(bandIndex);
        m_outputEdit->setText(outputPath);
    }

    // Capture parameters for async execution
    QString sourcePath = rl->source();

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &ExtractBandDialog::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &ExtractBandDialog::onFailed);
    }

    m_runButton->setEnabled(false);

    m_runner->run([sourcePath, bandIndex, outputPath]() -> QString {
    try {
        // Open source raster
        GdalDatasetGuard srcGuard( GDALOpen(sourcePath.toUtf8().constData(), GA_ReadOnly) );
        if (!srcGuard) return QString();

        int w = GDALGetRasterXSize(srcGuard.get());
        int h = GDALGetRasterYSize(srcGuard.get());

        GDALRasterBandH srcBand = GDALGetRasterBand(srcGuard.get(), bandIndex);
        if (!srcBand) return QString();

        // Create output
        QString error;
        GdalDatasetGuard dstGuard(createOutputTiff(outputPath, width, height, bandCount,
                                                   GDT_Float32, srcDataset.geoTransform(),
                                                   srcDataset.projection(), &error));
        if (!dstGuard) return QString();
