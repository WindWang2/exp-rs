// src/app/dialogs/atmospheric_dialog.cpp — Atmospheric correction dialog
#include "atmospheric_dialog.h"
#include "dialog_utils.h"
#include "processing/algorithms/atmospheric_correction.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

#include <qgsproject.h>
#include <qgsmessagelog.h>
#include <qgis.h>

AtmosphericDialog::AtmosphericDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("Atmospheric Correction"));
    setMinimumWidth(400);
    setupUi();
}

void AtmosphericDialog::setRasterLayer(QgsRasterLayer *layer)
{
    RasterProcessingDialogBase::setRasterLayer(layer);
    populateBandCombo();
}

void AtmosphericDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Method group ---
    auto *methodGroup = new QGroupBox(tr("Correction Method"), this);
    auto *formLayout = new QFormLayout(methodGroup);

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("DN to Radiance"));
    m_methodCombo->addItem(tr("DOS1 (Dark Object Subtraction)"));
    m_methodCombo->addItem(tr("DOS2 (with Transmittance)"));
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AtmosphericDialog::onMethodChanged);
    formLayout->addRow(tr("Method:"), m_methodCombo);

    m_bandCombo = new QComboBox(this);
    formLayout->addRow(tr("Band:"), m_bandCombo);

    m_gainSpin = new QDoubleSpinBox(this);
    m_gainSpin->setRange(0.0001, 1000.0);
    m_gainSpin->setDecimals(6);
    m_gainSpin->setValue(0.01);
    formLayout->addRow(tr("Gain:"), m_gainSpin);

    m_biasSpin = new QDoubleSpinBox(this);
    m_biasSpin->setRange(-1000.0, 1000.0);
    m_biasSpin->setDecimals(6);
    m_biasSpin->setValue(0.0);
    formLayout->addRow(tr("Bias:"), m_biasSpin);

    m_airmassLabel = new QLabel(tr("Airmass:"), this);
    m_airmassSpin = new QDoubleSpinBox(this);
    m_airmassSpin->setRange(1.0, 10.0);
    m_airmassSpin->setDecimals(2);
    m_airmassSpin->setValue(1.0);
    m_airmassSpin->setVisible(false);
    m_airmassLabel->setVisible(false);
    formLayout->addRow(m_airmassLabel, m_airmassSpin);

    mainLayout->addWidget(methodGroup);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

void AtmosphericDialog::populateBandCombo()
{
    m_bandCombo->clear();
    if (!m_rasterLayer || !m_rasterLayer->isValid())
        return;

    const int bandCount = m_rasterLayer->bandCount();
    for (int i = 1; i <= bandCount; ++i) {
        m_bandCombo->addItem(tr("Band %1").arg(i), i);
    }
}

void AtmosphericDialog::onMethodChanged(int index)
{
    const bool showAirmass = (index == 2);
    m_airmassSpin->setVisible(showAirmass);
    m_airmassLabel->setVisible(showAirmass);
}

void AtmosphericDialog::onRun()
{
    const int bandNum = m_bandCombo->currentData().toInt();
    const float gain = static_cast<float>(m_gainSpin->value());
    const float bias = static_cast<float>(m_biasSpin->value());
    const int method = m_methodCombo->currentIndex();
    const float airmass = static_cast<float>(m_airmassSpin->value());
    const QString sourcePath = m_rasterLayer->source();
    const QString outPath = outputPath();

    runGdalTask([sourcePath, outPath, bandNum, method, gain, bias, airmass]() -> QString {
        QString error;
        if (!AtmosphericCorrection::processFile(sourcePath, outPath, bandNum, method,
                                                gain, bias, airmass, &error))
            return QString();
        return outPath;
    });
}