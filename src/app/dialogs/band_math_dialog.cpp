// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

BandMathDialog::BandMathDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Band Math"));
    setupUi();
}

void BandMathDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

void BandMathDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Expression
    auto *exprLayout = new QHBoxLayout();
    exprLayout->addWidget(new QLabel(tr("Expression:")));
    m_expressionEdit = new QLineEdit(this);
    m_expressionEdit->setPlaceholderText(tr("e.g., (b1 - b2) / (b1 + b2)"));
    exprLayout->addWidget(m_expressionEdit);
    mainLayout->addLayout(exprLayout);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &BandMathDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &BandMathDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void BandMathDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void BandMathDialog::onRun()
{
    // TODO: Implement band math execution using BandMath::evaluate()
    QMessageBox::information(this, tr("Band Math"), tr("Band Math execution not yet implemented."));
}
