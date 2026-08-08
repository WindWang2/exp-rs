// src/app/dialogs/batch_processing_dialog.cpp
#include "batch_processing_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <json/json.h>

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
#include <QFormLayout>
#include <QFrame>
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

namespace
{

using sicnu::processing::AlgorithmDescriptor;
using sicnu::processing::DataType;
using sicnu::processing::AtomicAlgorithmRegistry;

/// Name of the single required raster/vector input of an RS operator, or an
/// empty string when there is no such unique input.
std::string findMainInputName( const AlgorithmDescriptor &desc )
{
  std::string mainInput;
  for ( const auto &in : desc.inputs )
  {
    if ( in.name.rfind( "output", 0 ) != 0
         && in.required && ( in.type == DataType::Raster || in.type == DataType::Vector ) )
    {
      if ( !mainInput.empty() )
        return {}; // more than one required raster/vector input
      mainInput = in.name;
    }
  }
  return mainInput;
}

/// True when an RS operator can be batch-run with only per-file input/output
/// substitution and its declared defaults: exactly one required raster/vector
/// input, an "output" port, and every other required parameter either has a
/// default or is an output path (name starts with "output").
bool isBatchableRsOperator( const AlgorithmDescriptor &desc )
{
  if ( desc.id.rfind( "rs:", 0 ) != 0 )
    return false;

  bool hasOutputPort = false;
  for ( const auto &out : desc.outputs )
  {
    if ( out.name == "output" && out.type == DataType::Raster )
    {
      hasOutputPort = true;
      break;
    }
  }
  if ( !hasOutputPort )
    return false;

  const std::string mainInput = findMainInputName( desc );
  if ( mainInput.empty() )
    return false;

  for ( const auto &in : desc.inputs )
  {
    if ( in.name == mainInput || in.name.rfind( "output", 0 ) == 0 )
      continue;
    if ( in.required && in.defaultValue.empty() )
      return false;
  }
  return true;
}

/// Builds the operator JSON parameters for one batch item: the single
/// required raster/vector input becomes the file path, optional parameters
/// take their declared defaults, output-role parameters and "output" take the
/// output path. Returns null and sets errorMessage when the operator cannot
/// be parameterized (should not happen for batchable ones).
Json::Value buildRsParams( const AlgorithmDescriptor &desc,
                           const QString &inputPath,
                           const QString &outputPath,
                           QString *errorMessage )
{
  Json::Value params( Json::objectValue );

  const std::string mainInput = findMainInputName( desc );
  if ( mainInput.empty() )
  {
    if ( errorMessage )
      *errorMessage = QStringLiteral( "RS operator has no single raster input" );
    return Json::Value( Json::nullValue );
  }
  params[mainInput] = inputPath.toStdString();

  for ( const auto &in : desc.inputs )
  {
    const std::string &name = in.name;
    if ( name == mainInput )
      continue;
    if ( name.rfind( "output", 0 ) == 0 )
    {
      if ( in.required )
        params[name] = outputPath.toStdString();
      continue;
    }
    if ( in.defaultValue.empty() )
      continue; // optional parameter without a default stays absent

    const QString def = QString::fromStdString( in.defaultValue );
    switch ( in.type )
    {
      case DataType::Numeric:
        params[name] = def.toDouble();
        break;
      case DataType::Integer:
        params[name] = static_cast<int>( def.toDouble() );
        break;
      case DataType::Boolean:
        params[name] = ( def == QLatin1String( "true" ) );
        break;
      default:
        params[name] = def.toStdString();
        break;
    }
  }

  params["output"] = outputPath.toStdString();
  return params;
}

} // namespace

BatchProcessingDialog::BatchProcessingDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "批量处理" ) );
  SicnuUi::polishDialog( this, 560 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "batch_processing" ) );
  resize( 620, 520 );
  setupUi();
}

void BatchProcessingDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  // Help strip
  auto *banner = SicnuUi::makeSection(
    this, tr( "流程" ),
    tr( "选算法 → 添加文件 → 设输出目录 → 运行批量。" ) );
  qobject_cast<QVBoxLayout *>( banner->layout() )->addWidget(  SicnuUi::makeHintLabel(
    banner, SicnuDialogHelp::shortForTool(
              QStringLiteral( "batch_processing" ), tr( "批量处理" ) ) ) );
  mainLayout->addWidget( banner );

  QFrame *algSec = SicnuUi::makeSection( this, tr( "算法" ) );
  auto *algForm = new QFormLayout();
  algForm->setContentsMargins( 0, 0, 0, 0 );
  m_algorithmCombo = new QComboBox( algSec );
  m_algorithmCombo->setObjectName( QStringLiteral( "batchAlgorithmCombo" ) );
  m_algorithmCombo->setMinimumWidth( 300 );
  SicnuDialogHelp::tip( m_algorithmCombo, tr(
    "选择要批量运行的算法。建议先在工具箱对单文件验证。" ) );
  connect( m_algorithmCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BatchProcessingDialog::onAlgorithmChanged );
  algForm->addRow( tr( "处理算法" ), m_algorithmCombo );
  qobject_cast<QVBoxLayout *>( algSec->layout() )->addLayout( algForm );
  mainLayout->addWidget( algSec );

  const auto algorithms = QgsApplication::processingRegistry()->algorithms();
  for ( const QgsProcessingAlgorithm *alg : algorithms )
  {
    m_algorithmCombo->addItem(
      QStringLiteral( "%1 (%2)" ).arg( alg->displayName(), alg->provider()->name() ),
      alg->id() );
  }

  // RS operators (single-input, default-parameterizable ones) are batchable
  // with per-file input/output substitution; surface them as a separate group.
  const auto rsDescriptors = AtomicAlgorithmRegistry::instance().listDescriptors();
  for ( const AlgorithmDescriptor &desc : rsDescriptors )
  {
    if ( !isBatchableRsOperator( desc ) )
      continue;
    m_algorithmCombo->addItem(
      QStringLiteral( "%1 (RS)" ).arg( QString::fromStdString( desc.displayName ) ),
      QString::fromStdString( desc.id ) );
  }

  QFrame *fileSec = SicnuUi::makeSection( this, tr( "输入文件" ) );
  m_fileList = new QListWidget( fileSec );
  m_fileList->setObjectName( QStringLiteral( "batchFileList" ) );
  m_fileList->setSelectionMode( QListWidget::ExtendedSelection );
  m_fileList->setMinimumHeight( 140 );
  SicnuDialogHelp::tip( m_fileList, tr( "待处理文件列表。" ) );
  qobject_cast<QVBoxLayout *>( fileSec->layout() )->addWidget(  m_fileList );
  auto *fileButtonLayout = new QHBoxLayout();
  auto *addFilesBtn = new QPushButton( tr( "添加文件…" ), fileSec );
  SicnuUi::markSecondary( addFilesBtn );
  connect( addFilesBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onAddFiles );
  fileButtonLayout->addWidget( addFilesBtn );
  auto *removeBtn = new QPushButton( tr( "移除选中" ), fileSec );
  SicnuUi::markSecondary( removeBtn );
  connect( removeBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onRemoveSelected );
  fileButtonLayout->addWidget( removeBtn );
  fileButtonLayout->addStretch();
  qobject_cast<QVBoxLayout *>( fileSec->layout() )->addLayout( fileButtonLayout );
  mainLayout->addWidget( fileSec );

  QFrame *outSec = SicnuUi::makeSection( this, tr( "输出" ) );
  auto *outForm = new QFormLayout();
  outForm->setContentsMargins( 0, 0, 0, 0 );
  m_outputDirEdit = new QLineEdit( outSec );
  m_outputDirEdit->setObjectName( QStringLiteral( "batchOutputDirEdit" ) );
  SicnuDialogHelp::tip( m_outputDirEdit, tr( "所有结果写入此目录。" ) );
  auto *browseBtn = new QPushButton( tr( "浏览…" ), outSec );
  SicnuUi::markSecondary( browseBtn );
  connect( browseBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onBrowseOutputDir );
  auto *outRow = new QHBoxLayout();
  outRow->addWidget( m_outputDirEdit, 1 );
  outRow->addWidget( browseBtn );
  outForm->addRow( tr( "输出目录" ), outRow );
  qobject_cast<QVBoxLayout *>( outSec->layout() )->addLayout( outForm );
  mainLayout->addWidget( outSec );

  m_progressBar = new QProgressBar( this );
  m_progressBar->setObjectName( QStringLiteral( "batchProgressBar" ) );
  m_progressBar->setVisible( false );
  mainLayout->addWidget( m_progressBar );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( m_statusLabel );

  auto *btnLayout = SicnuUi::makeActionRow( this );
  auto *helpBtn = new QPushButton( tr( "帮助" ), this );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "batch_processing" ), windowTitle() );
  } );
  btnLayout->addWidget( helpBtn );
  btnLayout->addStretch();
  auto *closeBtn = new QPushButton( tr( "关闭" ), this );
  SicnuUi::markSecondary( closeBtn );
  connect( closeBtn, &QPushButton::clicked, this, &QDialog::accept );
  btnLayout->addWidget( closeBtn );
  m_runButton = new QPushButton( tr( "运行批量" ), this );
  SicnuUi::markPrimary( m_runButton );
  SicnuDialogHelp::tip( m_runButton, tr( "按列表顺序运行。运行中请勿关闭。" ) );
  connect( m_runButton, &QPushButton::clicked, this, &BatchProcessingDialog::onRun );
  btnLayout->addWidget( m_runButton );
  mainLayout->addLayout( btnLayout );
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

    // Disable UI during batch
    m_runButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, m_inputFiles.size());
    m_progressBar->setValue(0);

    const QString algorithmId = m_algorithmCombo->currentData().toString();
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
            QString itemError;
            if (runBatchItem(algorithmId, inputFile, outputPath, &itemError)) {
                successCount++;
            } else {
                failCount++;
                const QString entry = tr("%1: %2")
                    .arg(QFileInfo(inputFile).fileName(),
                         itemError.isEmpty() ? tr("Invalid parameters") : itemError);
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
    // SetupUi wires currentIndexChanged before the status label exists;
    // the first addItem() may fire it, so guard against the null widget.
    if (!m_statusLabel)
        return;

    // Update UI based on selected algorithm
    // For now, just update the status
    QString algorithmId = m_algorithmCombo->currentData().toString();
    if (algorithmId.startsWith(QStringLiteral("rs:"))) {
        const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter(algorithmId.toStdString());
        if (adapter) {
            m_statusLabel->setText(tr("Selected: %1 (RS, 默认参数)").arg(
                QString::fromStdString(adapter->descriptor().displayName)));
        }
        return;
    }
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (alg) {
        m_statusLabel->setText(tr("Selected: %1").arg(alg->displayName()));
    }
}

bool BatchProcessingDialog::runBatchItem(const QString &algorithmId,
                                         const QString &inputFile,
                                         const QString &outputPath,
                                         QString *errorMessage)
{
    if (algorithmId.startsWith(QStringLiteral("rs:"))) {
        const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter(algorithmId.toStdString());
        if (!adapter) {
            if (errorMessage)
                *errorMessage = tr("RS operator not found: %1").arg(algorithmId);
            return false;
        }
        try {
            QString paramError;
            const Json::Value params = buildRsParams(adapter->descriptor(), inputFile, outputPath, &paramError);
            if (params.isNull()) {
                if (errorMessage)
                    *errorMessage = paramError;
                return false;
            }
            const Json::Value result = adapter->execute(params);
            const bool ok = result.isObject() && result.isMember("output")
                            && result["output"].isString()
                            && !result["output"].asString().empty();
            if (!ok && errorMessage)
                *errorMessage = tr("RS operator returned no output");
            return ok;
        } catch (const std::exception &e) {
            if (errorMessage)
                *errorMessage = QString::fromUtf8(e.what());
            return false;
        }
    }

    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (!alg) {
        if (errorMessage)
            *errorMessage = tr("Algorithm not found: %1").arg(algorithmId);
        return false;
    }

    QVariantMap params;
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

    QgsProcessingContext context;
    QString checkError;
    if (!alg->checkParameterValues(params, context, &checkError)) {
        if (errorMessage)
            *errorMessage = checkError;
        return false;
    }
    QgsProcessingFeedback feedback;
    const QVariantMap results = alg->run(params, context, &feedback);
    if (results.isEmpty()) {
        if (errorMessage)
            *errorMessage = feedback.textLog().isEmpty()
                ? tr("Algorithm returned no results")
                : feedback.textLog();
        return false;
    }
    return true;
}

void BatchProcessingDialog::setInputFiles(const QStringList &files)
{
    m_inputFiles = files;
    m_fileList->clear();
    for (const QString &file : m_inputFiles)
        m_fileList->addItem(QFileInfo(file).fileName());
    m_statusLabel->setText(tr("%1 files selected").arg(m_inputFiles.size()));
}

void BatchProcessingDialog::setOutputDir(const QString &dir)
{
    m_outputDir = dir;
    m_outputDirEdit->setText(dir);
}
