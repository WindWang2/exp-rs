// src/app/dialogs/spectral_index_dialog.cpp
#include "spectral_index_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>

SpectralIndexDialog::SpectralIndexDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Spectral Index"));
    setupUi();
}

void SpectralIndexDialog::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
}

void SpectralIndexDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Index selection
    auto *idxLayout = new QHBoxLayout();
    idxLayout->addWidget(new QLabel(tr("Index:")));
    m_indexCombo = new QComboBox(this);
    m_indexCombo->addItems({tr("NDVI"), tr("EVI"), tr("SAVI"), tr("NDWI"), tr("NDBI"), tr("MNDWI")});
    connect(m_indexCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectralIndexDialog::onIndexChanged);
    idxLayout->addWidget(m_indexCombo);
    mainLayout->addLayout(idxLayout);

    // Band selectors
    auto *bandLayout = new QHBoxLayout();
    bandLayout->addWidget(new QLabel(tr("NIR:")));
    m_nirCombo = new QComboBox(this);
    bandLayout->addWidget(m_nirCombo);
    bandLayout->addWidget(new QLabel(tr("Red:")));
    m_redCombo = new QComboBox(this);
    bandLayout->addWidget(m_redCombo);
    mainLayout->addLayout(bandLayout);

    auto *bandLayout2 = new QHBoxLayout();
    bandLayout2->addWidget(new QLabel(tr("Green:")));
    m_greenCombo = new QComboBox(this);
    bandLayout2->addWidget(m_greenCombo);
    bandLayout2->addWidget(new QLabel(tr("Blue:")));
    m_blueCombo = new QComboBox(this);
    bandLayout2->addWidget(m_blueCombo);
    bandLayout2->addWidget(new QLabel(tr("SWIR:")));
    m_swirCombo = new QComboBox(this);
    bandLayout2->addWidget(m_swirCombo);
    mainLayout->addLayout(bandLayout2);

    // Output file
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output:")));
    m_outputEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &SpectralIndexDialog::onBrowseOutput);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run"), this);
    connect(m_runButton, &QPushButton::clicked, this, &SpectralIndexDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void SpectralIndexDialog::onIndexChanged(int index)
{
    // TODO: Show/hide band selectors based on selected index
    Q_UNUSED(index);
}

void SpectralIndexDialog::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Output File"), QString(),
                                                tr("GeoTIFF (*.tif)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

void SpectralIndexDialog::onRun()
{
    // TODO: Implement spectral index execution using SpectralIndices namespace
    QMessageBox::information(this, tr("Spectral Index"), tr("Spectral index execution not yet implemented."));
}
