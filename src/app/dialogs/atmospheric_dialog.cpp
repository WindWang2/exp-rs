// src/app/dialogs/atmospheric_dialog.cpp
#include "atmospheric_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

AtmosphericDialog::AtmosphericDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Atmospheric Correction"));
    setupUi();
}

void AtmosphericDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

void AtmosphericDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Method selection
    auto *methodLayout = new QHBoxLayout();
    methodLayout->addWidget(new QLabel(tr("Method:")));
    m_methodCombo = new QComboBox(this);
    m_methodCombo->addItems({tr("DN to Radiance"), tr("DOS1"), tr("DOS2")});
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AtmosphericDialog::onMethodChanged);
    methodLayout->addWidget(m_methodCombo);
    mainLayout->addLayout(methodLayout);

    // Band selection
    auto *bandLayout = new QHBoxLayout();
    bandLayout->addWidget(new QLabel(tr("Band:")));
    m_bandCombo = new QComboBox(this);
    bandLayout->addWidget(m_bandCombo);
    mainLayout->addLayout(bandLayout);

    // Gain and Bias
    auto *gainLayout = new QHBoxLayout();
    gainLayout->addWidget(new QLabel(tr("Gain:")));
    m_gainSpin = new QDoubleSpinBox(this);
    m_gainSpin->setRange(-999.0, 999.0);
    m_gainSpin->setDecimals(6);
    m_gainSpin->setValue(1.0);
    gainLayout->addWidget(m_gainSpin);
    gainLayout->addWidget(new QLabel(tr("Bias:")));
    m_biasSpin = new QDoubleSpinBox(this);
    m_biasSpin->setRange(-999.0, 999.0);
    m_biasSpin->setDecimals(6);
    m_biasSpin->setValue(0.0);
    gainLayout->addWidget(m_biasSpin);
    mainLayout->addLayout(gainLayout);

    // Airmass (for DOS2)
    auto *airmassLayout = new QHBoxLayout();
    airmassLayout->addWidget(new QLabel(tr("Airmass:")));
    m_airmassSpin = new QDoubleSpinBox(this);
    m_airmassSpin->setRange(1.0, 10.0);
    m_airmassSpin->setDecimals(2);
    m_airmassSpin->setValue(1.0);
    airmassLayout->addWidget(m_airmassSpin);
    airmassLayout->addStretch();
    mainLayout->addLayout(airmassLayout);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &AtmosphericDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &AtmosphericDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void AtmosphericDialog::onMethodChanged(int index)
{
    // TODO: Show/hide airmass field based on method (DOS2 only)
    Q_UNUSED(index);
}

void AtmosphericDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void AtmosphericDialog::onRun()
{
    // TODO: Implement atmospheric correction using AtmosphericCorrection namespace
    QMessageBox::information(this, tr("Atmospheric Correction"),
                             tr("Atmospheric correction execution not yet implemented."));
}
