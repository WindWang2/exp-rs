// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"
#include "dialog_help_catalog.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QMessageBox>

PcaDialog::PcaDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void PcaDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    setupHelpBanner(mainLayout);
auto *compLayout = new QHBoxLayout();
    compLayout->addWidget(new QLabel(tr("Components:"), this));
    m_componentsSpin = new QSpinBox(this);
    m_componentsSpin->setRange(1, 10);
    m_componentsSpin->setValue(3);
    SicnuDialogHelp::tip( m_componentsSpin, tr(
      "输出主成分个数，必须 ≤ 输入波段数。"
      "前几个 PC 通常含大部分方差，用于去相关与降维。" ) );
    compLayout->addWidget(m_componentsSpin);
    mainLayout->addLayout(compLayout);

    setupOutputRow(mainLayout);
    setupButtonBar(mainLayout);
}

void PcaDialog::onRun()
{
    if (!m_rasterLayer) {
        QMessageBox::warning(this, tr("PCA"), tr("No raster layer selected."));
        return;
    }

    const int numComponents = m_componentsSpin->value();
    if (numComponents > m_rasterLayer->bandCount()) {
        QMessageBox::warning(this, tr("PCA"),
                             tr("Number of components (%1) exceeds band count (%2).")
                                 .arg(numComponents).arg(m_rasterLayer->bandCount()));
        return;
    }

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["numComponents"] = numComponents;

    runOperatorTask(QStringLiteral("rs:pca"), params);
}
