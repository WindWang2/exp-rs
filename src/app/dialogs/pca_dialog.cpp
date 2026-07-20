// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"

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

    auto *compLayout = new QHBoxLayout();
    compLayout->addWidget(new QLabel(tr("Components:"), this));
    m_componentsSpin = new QSpinBox(this);
    m_componentsSpin->setRange(1, 10);
    m_componentsSpin->setValue(3);
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
