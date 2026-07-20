// src/app/dialogs/change_detection_dialog.cpp
#include "change_detection_dialog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMessageBox>

#include <qgsproject.h>

ChangeDetectionDialog::ChangeDetectionDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(tr("Change Detection"));
    setMinimumWidth(450);
    setupUi();
}

void ChangeDetectionDialog::setupUi()
{
    auto *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) {
        mainLayout = new QVBoxLayout(this);
    }

    // --- Input group ---
    auto *inputGroup = new QGroupBox(tr("Input Images"), this);
    auto *formLayout = new QFormLayout(inputGroup);

    m_beforeLayerCombo = new QComboBox(this);
    m_afterLayerCombo = new QComboBox(this);
    m_beforeBandCombo = new QComboBox(this);
    m_afterBandCombo = new QComboBox(this);

    formLayout->addRow(tr("Before Image:"), m_beforeLayerCombo);
    formLayout->addRow(tr("Before Band:"), m_beforeBandCombo);
    formLayout->addRow(tr("After Image:"), m_afterLayerCombo);
    formLayout->addRow(tr("After Band:"), m_afterBandCombo);

    mainLayout->addWidget(inputGroup);

    // --- Method group ---
    auto *methodGroup = new QGroupBox(tr("Detection Method"), this);
    auto *methodLayout = new QFormLayout(methodGroup);

    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItem(tr("Difference"), QStringLiteral("difference"));
    m_methodCombo->addItem(tr("Normalized Difference"), QStringLiteral("normalized_difference"));
    m_methodCombo->addItem(tr("Change Mask"), QStringLiteral("change_mask"));

    m_thresholdLabel = new QLabel(tr("Threshold:"), this);
    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(0.0, 10000.0);
    m_thresholdSpin->setDecimals(2);
    m_thresholdSpin->setValue(10.0);
    m_thresholdSpin->setVisible(false);
    m_thresholdLabel->setVisible(false);

    methodLayout->addRow(tr("Method:"), m_methodCombo);
    methodLayout->addRow(m_thresholdLabel, m_thresholdSpin);

    mainLayout->addWidget(methodGroup);

    setupOutputRow(mainLayout);

    m_statusLabel = new QLabel(tr("Ready"), this);
    mainLayout->addWidget(m_statusLabel);

    setupButtonBar(mainLayout);

    connect(m_beforeLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::updateBandSelectors);
    connect(m_afterLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::updateBandSelectors);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChangeDetectionDialog::onMethodChanged);
}

void ChangeDetectionDialog::populateLayers()
{
    m_beforeLayerCombo->clear();
    m_afterLayerCombo->clear();

    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        QgsRasterLayer *rasterLayer = qobject_cast<QgsRasterLayer *>(it.value());
        if (rasterLayer && rasterLayer->isValid()) {
            m_beforeLayerCombo->addItem(rasterLayer->name(), rasterLayer->id());
            m_afterLayerCombo->addItem(rasterLayer->name(), rasterLayer->id());
        }
    }

    updateBandSelectors();
}

void ChangeDetectionDialog::updateBandSelectors()
{
    if (m_beforeLayerCombo->count() > 0) {
        QString beforeId = m_beforeLayerCombo->currentData().toString();
        QgsRasterLayer *beforeLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(beforeId);
        m_beforeBandCombo->clear();
        if (beforeLayer && beforeLayer->isValid()) {
            for (int i = 1; i <= beforeLayer->bandCount(); ++i) {
                m_beforeBandCombo->addItem(tr("Band %1").arg(i), i);
            }
        }
    } else {
        m_beforeBandCombo->clear();
    }

    if (m_afterLayerCombo->count() > 0) {
        QString afterId = m_afterLayerCombo->currentData().toString();
        QgsRasterLayer *afterLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(afterId);
        m_afterBandCombo->clear();
        if (afterLayer && afterLayer->isValid()) {
            for (int i = 1; i <= afterLayer->bandCount(); ++i) {
                m_afterBandCombo->addItem(tr("Band %1").arg(i), i);
            }
        }
    } else {
        m_afterBandCombo->clear();
    }
}

void ChangeDetectionDialog::onMethodChanged(int index)
{
    const bool isChangeMask = (index == 2);
    m_thresholdSpin->setVisible(isChangeMask);
    m_thresholdLabel->setVisible(isChangeMask);
}

bool ChangeDetectionDialog::validateInputs()
{
    if (outputPath().isEmpty()) {
        QMessageBox::warning(this, dialogTitle(), tr("Please specify an output file."));
        return false;
    }
    if (m_beforeLayerCombo->count() == 0 || m_afterLayerCombo->count() == 0) {
        QMessageBox::warning(this, dialogTitle(),
                             tr("Please ensure both before and after images are available."));
        return false;
    }
    return true;
}

void ChangeDetectionDialog::onRun()
{
    const QString beforeId = m_beforeLayerCombo->currentData().toString();
    const QString afterId = m_afterLayerCombo->currentData().toString();
    QgsRasterLayer *beforeLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(beforeId);
    QgsRasterLayer *afterLayer = QgsProject::instance()->mapLayer<QgsRasterLayer *>(afterId);

    if (!beforeLayer || !beforeLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("Invalid before image layer."));
        return;
    }
    if (!afterLayer || !afterLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("Invalid after image layer."));
        return;
    }

    if (m_statusLabel)
        m_statusLabel->setText(tr("Processing..."));

    Json::Value params(Json::objectValue);
    params["before"] = beforeLayer->source().toStdString();
    params["after"] = afterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["method"] = m_methodCombo->currentData().toString().toStdString();
    params["threshold"] = m_thresholdSpin->value();
    params["beforeBand"] = m_beforeBandCombo->currentData().toInt();
    params["afterBand"] = m_afterBandCombo->currentData().toInt();

    runOperatorTask(QStringLiteral("rs:change_detection"), params);
}
