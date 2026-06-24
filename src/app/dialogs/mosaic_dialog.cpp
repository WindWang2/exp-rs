// src/app/dialogs/mosaic_dialog.cpp
#include "mosaic_dialog.h"
#include "async_gdal_runner.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_safe_call.h"
#include "processing/algorithms/mosaic.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>

#include <qgsmessagelog.h>
#include <qgis.h>

#include <gdal_priv.h>
#include <cpl_string.h>
#include <qgscoordinatereferencesystem.h>

#include <cmath>
#include <limits>
#include <vector>

MosaicDialog::MosaicDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setMinimumWidth(500);
    setupUi();
}

void MosaicDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Input files group ---
    auto *inputGroup = new QGroupBox(tr("Input Rasters"), this);
    auto *inputLayout = new QVBoxLayout(inputGroup);

    m_inputList = new QListWidget(this);
    inputLayout->addWidget(m_inputList);

    auto *inputBtnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("Add..."), this);
    connect(addBtn, &QPushButton::clicked, this, &MosaicDialog::addInputFile);
    inputBtnLayout->addWidget(addBtn);

    auto *removeBtn = new QPushButton(tr("Remove"), this);
    connect(removeBtn, &QPushButton::clicked, this, &MosaicDialog::removeInputFile);
    inputBtnLayout->addWidget(removeBtn);

    inputBtnLayout->addStretch();
    inputLayout->addLayout(inputBtnLayout);

    mainLayout->addWidget(inputGroup);

    // --- Output file (from base class) ---
    setupOutputRow(mainLayout);

    // --- Buttons (from base class) ---
    setupButtonBar(mainLayout);
}

void MosaicDialog::addInputFile()
{
    QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add Input Rasters"), QString(),
                                                      tr("Raster files (*.tif *.tiff *.img *.asc);;All files (*)"));
    for (const QString &path : paths) {
        if (!path.isEmpty())
            m_inputList->addItem(path);
    }
}

void MosaicDialog::removeInputFile()
{
    QList<QListWidgetItem *> selected = m_inputList->selectedItems();
    for (QListWidgetItem *item : selected) {
        delete m_inputList->takeItem(m_inputList->row(item));
    }
}

void MosaicDialog::onRun()
{
    // --- Validate ---
    if (m_inputList->count() < 2) {
        QMessageBox::warning(this, tr("Mosaic"), tr("At least 2 input rasters are required."));
        return;
    }

    QString outPath = outputPath();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("Mosaic"), tr("Please specify an output file."));
        return;
    }

    // Capture input paths for async execution
    QStringList inputPaths;
    for (int i = 0; i < m_inputList->count(); ++i) {
        inputPaths.append(m_inputList->item(i)->text());
    }

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &MosaicDialog::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &MosaicDialog::onFailed);
    }

    m_runButton->setEnabled(false);

    m_runner->run([inputPaths, outPath]() -> QString {
    try {
        const int inputCount = inputPaths.size();
        struct InputInfo {
            QString path;
            std::vector<float> data;
            int width;
            int height;
            std::array<double, 6> geotransform;
            QString projection;
        };

        std::vector<InputInfo> inputs(inputCount);

        // --- Open each input, read band 1 ---
        for (int i = 0; i < inputCount; ++i) {
            inputs[i].path = inputPaths[i];

            GdalDatasetWrapper ds;
            if (!ds.open(inputs[i].path)) return QString();

            inputs[i].width = ds.width();
            inputs[i].height = ds.height();
            inputs[i].geotransform = ds.geoTransform();
            inputs[i].projection = ds.projection();

            size_t pixelCount = static_cast<size_t>(inputs[i].width) * static_cast<size_t>(inputs[i].height);
            inputs[i].data.resize(pixelCount);

            if (!ds.readBandData(1, inputs[i].data.data(), inputs[i].width, inputs[i].height)) return QString();
        }

        // --- Validate CRS consistency ---
        QgsCoordinateReferenceSystem refCrs;
        refCrs.createFromWkt(inputs[0].projection);
        for (int i = 1; i < inputCount; ++i) {
            QgsCoordinateReferenceSystem crs;
            crs.createFromWkt(inputs[i].projection);
            if (refCrs != crs) return QString();
        }

        // --- Compute union extent ---
        double unionMinX = std::numeric_limits<double>::max();
        double unionMinY = std::numeric_limits<double>::max();
        double unionMaxX = std::numeric_limits<double>::lowest();
        double unionMaxY = std::numeric_limits<double>::lowest();

        double refPixelW = inputs[0].geotransform[1];
        double refPixelH = inputs[0].geotransform[5];

        for (int i = 0; i < inputCount; ++i) {
            const auto &gt = inputs[i].geotransform;
            double originX = gt[0];
            double originY = gt[3];
            double pixelW = gt[1];
            double pixelH = gt[5];

            double tlX = originX;
            double tlY = originY;
            double brX = originX + inputs[i].width * pixelW;
            double brY = originY + inputs[i].height * pixelH;

            unionMinX = std::min(unionMinX, std::min(tlX, brX));
            unionMinY = std::min(unionMinY, std::min(tlY, brY));
            unionMaxX = std::max(unionMaxX, std::max(tlX, brX));
            unionMaxY = std::max(unionMaxY, std::max(tlY, brY));

            if (std::abs(pixelW - refPixelW) > 1e-9 || std::abs(pixelH - refPixelH) > 1e-9) {
                QgsMessageLog::logMessage(
                    tr("Pixel size mismatch for %1 (pw=%2, ph=%3 vs ref pw=%4, ph=%5)")
                        .arg(inputs[i].path)
                        .arg(pixelW).arg(pixelH)
                        .arg(refPixelW).arg(refPixelH),
                    "mosaic", Qgis::MessageLevel::Warning);
            }
        }

        // Output dimensions from union extent
        int outWidth = static_cast<int>(std::round((unionMaxX - unionMinX) / std::abs(refPixelW)));
        int outHeight = static_cast<int>(std::round((unionMaxY - unionMinY) / std::abs(refPixelH)));
        if (outWidth <= 0 || outHeight <= 0) return QString();

        size_t outPixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);

        // Output geotransform
        std::array<double, 6> outGT;
        outGT[0] = unionMinX;
        outGT[1] = refPixelW;
        outGT[2] = inputs[0].geotransform[2];
        outGT[3] = unionMaxY;
        outGT[4] = inputs[0].geotransform[4];
        outGT[5] = refPixelH;

        // --- Build mosaic sources ---
        std::vector<Mosaic::MosaicSource> sources(inputCount);
        for (int i = 0; i < inputCount; ++i) {
            const auto &gt = inputs[i].geotransform;

            size_t offX = static_cast<size_t>(std::round((gt[0] - unionMinX) / std::abs(refPixelW)));
            size_t offY = static_cast<size_t>(std::round((unionMaxY - gt[3]) / std::abs(refPixelH)));

            sources[i].data = inputs[i].data.data();
            sources[i].width = static_cast<size_t>(inputs[i].width);
            sources[i].height = static_cast<size_t>(inputs[i].height);
            sources[i].offsetX = offX;
            sources[i].offsetY = offY;
            sources[i].nodata = std::numeric_limits<float>::quiet_NaN();
        }

        // --- Allocate output and merge ---
        std::vector<float> outBuf(outPixelCount, std::numeric_limits<float>::quiet_NaN());
        if (!Mosaic::merge(sources.data(), sources.size(), outBuf.data(),
                           static_cast<size_t>(outWidth), static_cast<size_t>(outHeight))) return QString();

        // --- Write output GeoTIFF ---
        GDALAllRegister();

        QString error;
        GdalDatasetGuard dstGuard(createOutputTiff(outPath, outWidth, outHeight, 1,
                                                    GDT_Float32, outGT, inputs[0].projection, &error));
        if (!dstGuard) return QString();

        GDALRasterBandH band = GDALGetRasterBand(dstGuard.get(), 1);
        if (!band) return QString();

        GDAL_SAFE_CALL( GDALRasterIO(band, GF_Write, 0, 0, outWidth, outHeight,
                        outBuf.data(), outWidth, outHeight, GDT_Float32, 0, 0),
                        "Failed to write mosaic output" );

        float nodata = std::numeric_limits<float>::quiet_NaN();
        GDALSetRasterNoDataValue(band, nodata);

        return outPath;
    } catch (const std::runtime_error &) {
        return QString();
    }
    });
}

void MosaicDialog::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void MosaicDialog::onFailed(const QString &error)
{
    handleFailed(error);
}
