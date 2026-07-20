// src/app/dialogs/mosaic_dialog.cpp
#include "mosaic_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>

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

bool MosaicDialog::validateInputs()
{
    if (m_inputList->count() < 2) {
        QMessageBox::warning(this, dialogTitle(),
                             tr("At least 2 input rasters are required."));
        return false;
    }
    if (outputPath().isEmpty()) {
        QMessageBox::warning(this, dialogTitle(), tr("Please specify an output file."));
        return false;
    }
    return true;
}

void MosaicDialog::onRun()
{
    Json::Value params(Json::objectValue);
    params["inputs"] = Json::Value(Json::arrayValue);
    for (int i = 0; i < m_inputList->count(); ++i) {
        params["inputs"].append(m_inputList->item(i)->text().toStdString());
    }
    params["output"] = outputPath().toStdString();

    runOperatorTask(QStringLiteral("rs:mosaic"), params);
}


