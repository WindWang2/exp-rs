// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "dialog_help_catalog.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>

SpatialFilterDialog::SpatialFilterDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void SpatialFilterDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    setupHelpBanner(mainLayout);
auto *typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel(tr("Filter:"), this));
    m_filterTypeCombo = new QComboBox(this);
    m_filterTypeCombo->addItem(tr("Mean"), QStringLiteral("opencv:mean_blur"));
    m_filterTypeCombo->addItem(tr("Gaussian"), QStringLiteral("opencv:gaussian_blur"));
    m_filterTypeCombo->addItem(tr("Median"), QStringLiteral("opencv:median_blur"));
    m_filterTypeCombo->addItem(tr("Sobel"), QStringLiteral("opencv:sobel"));
    m_filterTypeCombo->addItem(tr("Laplacian"), QStringLiteral("opencv:laplacian"));
    SicnuDialogHelp::tip( m_filterTypeCombo, tr(
      "• Mean/Gaussian/Median：平滑\n"
      "• Sobel/Laplacian：边缘增强\n"
      "中值滤波对椒盐噪声更稳。" ) );
    typeLayout->addWidget(m_filterTypeCombo);
    mainLayout->addLayout(typeLayout);

    auto *kernelLayout = new QHBoxLayout();
    kernelLayout->addWidget(new QLabel(tr("Kernel Size:"), this));
    m_kernelSizeCombo = new QComboBox(this);
    m_kernelSizeCombo->addItem(tr("3x3"), 3);
    m_kernelSizeCombo->addItem(tr("5x5"), 5);
    SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "卷积核大小。越大平滑/边缘响应范围越大。" ) );
    kernelLayout->addWidget(m_kernelSizeCombo);
    mainLayout->addLayout(kernelLayout);

    setupOutputRow(mainLayout);
    setupButtonBar(mainLayout);
}

void SpatialFilterDialog::onRun()
{
    const QString operatorId = m_filterTypeCombo->currentData().toString();
    const int kernelSize = m_kernelSizeCombo->currentData().toInt();

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["kernelSize"] = kernelSize;
    if (operatorId == QLatin1String("opencv:gaussian_blur")) {
        params["sigma"] = 1.0;
    }
    if (operatorId == QLatin1String("opencv:sobel")) {
        params["dx"] = 1;
        params["dy"] = 1;
    }
    runOperatorTask(operatorId, params);
}
