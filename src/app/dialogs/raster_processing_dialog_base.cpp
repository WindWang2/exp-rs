// raster_processing_dialog_base.cpp — Base class for raster processing dialogs
#include "raster_processing_dialog_base.h"
#include "async_gdal_runner.h"
#include "async_algorithm_runner.h"

#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>

#include <raster/qgsrasterlayer.h>
#include <qgsmessagelog.h>
#include <qgis.h>

RasterProcessingDialogBase::RasterProcessingDialogBase(QWidget *parent)
    : QDialog(parent)
{
}

void RasterProcessingDialogBase::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

QString RasterProcessingDialogBase::outputPath() const
{
    return m_outputEdit ? m_outputEdit->text().trimmed() : QString();
}

bool RasterProcessingDialogBase::validateInputs()
{
    QString path = outputPath();
    if (path.isEmpty()) {
        QMessageBox::warning(this, dialogTitle(), tr("Please specify an output file."));
        return false;
    }

    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("No valid raster layer selected."));
        return false;
    }

    return true;
}

void RasterProcessingDialogBase::setupOutputRow(QVBoxLayout *layout)
{
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:"), this));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &RasterProcessingDialogBase::browseOutput);
    outLayout->addWidget(browseBtn);
    layout->addLayout(outLayout);
}

void RasterProcessingDialogBase::setupButtonBar(QVBoxLayout *layout)
{
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, [this]() {
        if (validateInputs()) {
            onRun();
        }
    });
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
}

void RasterProcessingDialogBase::browseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void RasterProcessingDialogBase::startRun()
{
    m_running = true;
    if (m_runButton)
        m_runButton->setEnabled(false);
}

void RasterProcessingDialogBase::finishRun()
{
    m_running = false;
    if (m_runButton)
        m_runButton->setEnabled(true);
}

void RasterProcessingDialogBase::runGdalTask(const std::function<QString()> &task)
{
    if (isRunning())
        return;

    if (!m_runner) {
        m_runner = new AsyncGdalRunner(this, this);
        connect(m_runner, &AsyncGdalRunner::completed, this, &RasterProcessingDialogBase::onCompleted);
        connect(m_runner, &AsyncGdalRunner::failed, this, &RasterProcessingDialogBase::onFailed);
    }

    startRun();
    m_runner->run(task);
}

void RasterProcessingDialogBase::runAlgorithmTask(const QgsProcessingAlgorithm *algorithm,
                                                const QVariantMap &parameters,
                                                QgsProcessingContext &context)
{
    if (isRunning())
        return;

    if (!m_algorithmRunner) {
        m_algorithmRunner = new AsyncAlgorithmRunner(this, this);
        connect(m_algorithmRunner, &AsyncAlgorithmRunner::completed, this,
                [this](const QVariantMap &results) {
                    Q_UNUSED(results);
                    onCompleted(outputPath());
                });
        connect(m_algorithmRunner, &AsyncAlgorithmRunner::failed, this,
                &RasterProcessingDialogBase::onFailed);
    }

    startRun();
    m_algorithmRunner->run(algorithm, parameters, context);
}

void RasterProcessingDialogBase::handleCompleted(const QString &outputPath)
{
    cleanupRunResources();
    finishRun();
    QgsMessageLog::logMessage(tr("%1 completed! Output: %2").arg(toolName(), outputPath),
                              toolName(), Qgis::MessageLevel::Success);
    accept();
}

void RasterProcessingDialogBase::handleFailed(const QString &error)
{
    cleanupRunResources();
    finishRun();
    QgsMessageLog::logMessage(error, toolName(), Qgis::MessageLevel::Critical);
    QMessageBox::critical(this, dialogTitle(), tr("Operation failed. See log for details."));
}

void RasterProcessingDialogBase::onCompleted(const QString &outputPath)
{
    handleCompleted(outputPath);
}

void RasterProcessingDialogBase::onFailed(const QString &errorMessage)
{
    handleFailed(errorMessage);
}
