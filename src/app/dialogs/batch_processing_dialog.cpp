// src/app/dialogs/batch_processing_dialog.cpp
#include "batch_processing_dialog.h"
#include "dialog_help_catalog.h"

#include <qgsapplication.h>
#include <qgsprocessingregistry.h>
#include <qgsprocessingalgorithm.h>

#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>

#include <QApplication>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QStringList>

BatchProcessingDialog::BatchProcessingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Batch Processing"));
    
    SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "batch_processing" ) );
    resize(600, 500);
    setupUi();
}

void BatchProcessingDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Algorithm selection
    auto *algLayout = new QHBoxLayout();
    algLayout->addWidget(new QLabel(tr("Algorithm:"), this));
    m_algorithmCombo = new QComboBox(this);
    m_algorithmCombo->setMinimumWidth(300);
    connect(m_algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BatchProcessingDialog::onAlgorithmChanged);
    algLayout->addWidget(m_algorithmCombo);
    mainLayout->addLayout(algLayout);

    // Populate algorithm combo
    const auto algorithms = QgsApplication::processingRegistry()->algorithms();
    for (const QgsProcessingAlgorithm *alg : algorithms) {
        m_algorithmCombo->addItem(
            QStringLiteral("%1 (%2)").arg(alg->displayName(), alg->provider()->name()),
            alg->id());
    }

    // Input files
    mainLayout->addWidget(new QLabel(tr("Input Files:"), this));

    auto *fileButtonLayout = new QHBoxLayout();
    auto *addFilesBtn = new QPushButton(tr("Add Files..."), this);
    connect(addFilesBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onAddFiles);
    fileButtonLayout->addWidget(addFilesBtn);

    auto *removeBtn = new QPushButton(tr("Remove Selected"), this);
    connect(removeBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onRemoveSelected);
    fileButtonLayout->addWidget(removeBtn);
    fileButtonLayout->addStretch();
    mainLayout->addLayout(fileButtonLayout);

    m_fileList = new QListWidget(this);
    m_fileList->setSelectionMode(QListWidget::ExtendedSelection);
    mainLayout->addWidget(m_fileList);

    // Output directory
    auto *outLayout = new QHBoxLayout();
    outLayout->addWidget(new QLabel(tr("Output Directory:"), this));
    m_outputDirEdit = new QLineEdit(this);
    outLayout->addWidget(m_outputDirEdit);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    connect(browseBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onBrowseOutputDir);
    outLayout->addWidget(browseBtn);
    mainLayout->addLayout(outLayout);

    // Progress
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_runButton = new QPushButton(tr("Run Batch"), this);
    connect(m_runButton, &QPushButton::clicked, this, &BatchProcessingDialog::onRun);
    btnLayout->addWidget(m_runButton);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    auto *helpRow = new QHBoxLayout();
    helpRow->addStretch();
    auto *helpBtn = new QPushButton( tr( "帮助" ), this );
    SicnuDialogHelp::tip( helpBtn, tr( "查看本对话框说明。" ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
        SicnuDialogHelp::showToolHelp( this, QStringLiteral( "batch_processing" ), windowTitle() );
    } );
    helpRow->addWidget( helpBtn );
    mainLayout->addLayout( helpRow );
}

void BatchProcessingDialog::setAlgorithmId(const QString &algorithmId)
{
    for (int i = 0; i < m_algorithmCombo->count(); ++i) {
        if (m_algorithmCombo->itemData(i).toString() == algorithmId) {
            m_algorithmCombo->setCurrentIndex(i);
            break;
        }
    }
}

void BatchProcessingDialog::onAddFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select Input Files"), QString(),
        tr("GeoTIFF (*.tif *.tiff);;Shapefile (*.shp);;All Files (*)"));

    for (const QString &file : files) {
        if (!m_inputFiles.contains(file)) {
            m_inputFiles.append(file);
            m_fileList->addItem(QFileInfo(file).fileName());
        }
    }

    m_statusLabel->setText(tr("%1 files selected").arg(m_inputFiles.size()));
}

void BatchProcessingDialog::onRemoveSelected()
{
    QList<QListWidgetItem*> selected = m_fileList->selectedItems();
    for (QListWidgetItem *item : selected) {
        int row = m_fileList->row(item);
        m_inputFiles.removeAt(row);
        delete item;
    }

    m_statusLabel->setText(tr("%1 files selected").arg(m_inputFiles.size()));
}

void BatchProcessingDialog::onBrowseOutputDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"));
    if (!dir.isEmpty()) {
        m_outputDir = dir;
        m_outputDirEdit->setText(dir);
    }
}

void BatchProcessingDialog::onRun()
{
    if (m_inputFiles.isEmpty()) {
        QMessageBox::warning(this, tr("Batch Processing"),
                             tr("Please add input files."));
        return;
    }

    if (m_outputDir.isEmpty()) {
        QMessageBox::warning(this, tr("Batch Processing"),
                             tr("Please select an output directory."));
        return;
    }

    QString algorithmId = m_algorithmCombo->currentData().toString();
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (!alg) {
        QMessageBox::warning(this, tr("Batch Processing"),
                             tr("Algorithm not found."));
        return;
    }

    // Disable UI during batch
    m_runButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, m_inputFiles.size());
    m_progressBar->setValue(0);

    int successCount = 0;
    int failCount = 0;
    QStringList errorMessages;

    for (int i = 0; i < m_inputFiles.size(); ++i) {
        const QString &inputFile = m_inputFiles[i];
        m_statusLabel->setText(tr("Processing %1...").arg(QFileInfo(inputFile).fileName()));
        // Keep UI responsive without re-entering user-input handlers (nested dialogs/clicks).
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        // Build output path
        QString baseName = QFileInfo(inputFile).completeBaseName();
        QString outputPath = m_outputDir + QStringLiteral("/") + baseName + QStringLiteral("_processed.tif");

        try {
            // Build parameters
            QVariantMap params;
            // Try common parameter names
            if (alg->parameterDefinition(QStringLiteral("INPUT"))) {
                params[QStringLiteral("INPUT")] = inputFile;
            } else if (alg->parameterDefinition(QStringLiteral("INPUT_LAYER"))) {
                params[QStringLiteral("INPUT_LAYER")] = inputFile;
            }

            if (alg->parameterDefinition(QStringLiteral("OUTPUT"))) {
                params[QStringLiteral("OUTPUT")] = outputPath;
            } else if (alg->parameterDefinition(QStringLiteral("OUTPUT_LAYER"))) {
                params[QStringLiteral("OUTPUT_LAYER")] = outputPath;
            }

            // Run algorithm
            QgsProcessingContext context;
            QString errorMsg;
            if (alg->checkParameterValues(params, context, &errorMsg)) {
                QgsProcessingFeedback feedback;
                QVariantMap results = alg->run(params, context, &feedback);
                if (!results.isEmpty()) {
                    successCount++;
                } else {
                    failCount++;
                    const QString detail = feedback.textLog().isEmpty()
                        ? tr("Algorithm returned no results")
                        : feedback.textLog();
                    const QString entry = tr("%1: %2").arg(QFileInfo(inputFile).fileName(), detail);
                    errorMessages.append(entry);
                    QgsMessageLog::logMessage(entry, QStringLiteral("batch"), Qgis::MessageLevel::Warning);
                }
            } else {
                failCount++;
                const QString entry = tr("%1: %2")
                    .arg(QFileInfo(inputFile).fileName(),
                         errorMsg.isEmpty() ? tr("Invalid parameters") : errorMsg);
                errorMessages.append(entry);
                QgsMessageLog::logMessage(entry, QStringLiteral("batch"), Qgis::MessageLevel::Warning);
            }
        } catch (const std::exception &e) {
            failCount++;
            const QString entry = tr("%1: %2")
                .arg(QFileInfo(inputFile).fileName(), QString::fromUtf8(e.what()));
            errorMessages.append(entry);
            QgsMessageLog::logMessage(entry, QStringLiteral("batch"), Qgis::MessageLevel::Critical);
        } catch (...) {
            failCount++;
            const QString entry = tr("%1: unknown error").arg(QFileInfo(inputFile).fileName());
            errorMessages.append(entry);
            QgsMessageLog::logMessage(entry, QStringLiteral("batch"), Qgis::MessageLevel::Critical);
        }

        m_progressBar->setValue(i + 1);
    }

    m_statusLabel->setText(tr("Batch complete: %1 succeeded, %2 failed")
                               .arg(successCount).arg(failCount));
    m_runButton->setEnabled(true);

    if (failCount > 0) {
        QString details = tr("Batch complete with errors:\n%1 succeeded, %2 failed")
                              .arg(successCount).arg(failCount);
        if (!errorMessages.isEmpty()) {
            const int maxShow = 8;
            details += QLatin1Char('\n') + errorMessages.mid(0, maxShow).join(QLatin1Char('\n'));
            if (errorMessages.size() > maxShow)
                details += tr("\n… and %1 more (see log)").arg(errorMessages.size() - maxShow);
        }
        QMessageBox::warning(this, tr("Batch Processing"), details);
    } else {
        QMessageBox::information(this, tr("Batch Processing"),
                                 tr("Batch complete:\n%1 files processed successfully")
                                     .arg(successCount));
    }
}

void BatchProcessingDialog::onAlgorithmChanged(int index)
{
    Q_UNUSED(index);
    updateAlgorithmParameters();
}

void BatchProcessingDialog::updateAlgorithmParameters()
{
    // Update UI based on selected algorithm
    // For now, just update the status
    QString algorithmId = m_algorithmCombo->currentData().toString();
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (alg) {
        m_statusLabel->setText(tr("Selected: %1").arg(alg->displayName()));
    }
}
