// src/app/dialogs/batch_processing_dialog.cpp
#include "batch_processing_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include "processing/framework/algorithm_descriptor.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"

#include <json/json.h>

#include <qgsapplication.h>
#include <qgsprocessingregistry.h>
#include <qgsprocessingalgorithm.h>
#include <qgsproject.h>
#include <gui/qgsgui.h>
#include <gui/processing/qgsprocessingguiregistry.h>
#include <gui/processing/qgsprocessingwidgetwrapper.h>
#include <qgsprocessingcontext.h>

#include <qgsmessagelog.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QGroupBox>
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
#include <QSpinBox>
#include <QDir>
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
  // Async batch (execution-plane goal): item completions arrive as TaskCenter
  // terminal transitions delivered queued onto the GUI thread; auto-disconnects
  // when the dialog is destroyed.
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &BatchProcessingDialog::onBatchTaskUpdated );
}

BatchProcessingDialog::~BatchProcessingDialog() = default;

void BatchProcessingDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  // Help strip
  auto *banner = SicnuUi::makeGroup(
    this, tr( "批处理流程指引" ),
    tr( "选择算法 → 添加待处理文件 → 设置输出目录 → 启动批量处理任务。" ) );
  auto *bannerLayout = new QVBoxLayout( banner );
  bannerLayout->setContentsMargins( 10, 8, 10, 8 );
  bannerLayout->addWidget( SicnuUi::makeHintLabel(
    banner, SicnuDialogHelp::shortForTool(
              QStringLiteral( "batch_processing" ), tr( "批量处理" ) ) ) );
  mainLayout->addWidget( banner );

  QGroupBox *algGroup = SicnuUi::makeGroup( this, tr( "算法选择" ) );
  auto *algForm = SicnuUi::makeFormLayout( algGroup );
  m_algorithmCombo = new QComboBox( algGroup );
  m_algorithmCombo->setObjectName( QStringLiteral( "batchAlgorithmCombo" ) );
  m_algorithmCombo->setMinimumWidth( 320 );
  SicnuDialogHelp::tip( m_algorithmCombo, tr(
    "选择要批量运行的遥感或地理处理算法。建议先在工具箱对单个文件验证效果。" ) );
  connect( m_algorithmCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &BatchProcessingDialog::onAlgorithmChanged );
  algForm->addRow( tr( "处理算法" ), m_algorithmCombo );
  mainLayout->addWidget( algGroup );

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

  // Operator parameter overrides (advanced): rebuilt on algorithm change.
  m_paramFrame = SicnuUi::makeGroup( this, tr( "算法参数覆盖 (可选)" ),
                                     tr( "覆盖批量运行所用的算法参数（输入与输出由待处理文件列表自动决定）。" ) );
  m_paramForm = SicnuUi::makeFormLayout( m_paramFrame );
  m_paramFrame->setVisible( false );
  mainLayout->addWidget( m_paramFrame );

  QGroupBox *fileGroup = SicnuUi::makeGroup( this, tr( "输入文件列表" ) );
  auto *fileGroupLayout = new QVBoxLayout( fileGroup );
  fileGroupLayout->setContentsMargins( 10, 8, 10, 8 );
  fileGroupLayout->setSpacing( 8 );

  m_fileList = new QListWidget( fileGroup );
  m_fileList->setObjectName( QStringLiteral( "batchFileList" ) );
  m_fileList->setSelectionMode( QListWidget::ExtendedSelection );
  m_fileList->setMinimumHeight( 140 );
  m_fileList->setAlternatingRowColors( true );
  SicnuDialogHelp::tip( m_fileList, tr( "待处理的文件列表。" ) );
  fileGroupLayout->addWidget( m_fileList );

  auto *fileButtonLayout = new QHBoxLayout();
  fileButtonLayout->setSpacing( 8 );
  m_addFilesBtn = new QPushButton( tr( "添加文件…" ), fileGroup );
  SicnuUi::markSecondary( m_addFilesBtn );
  connect( m_addFilesBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onAddFiles );
  fileButtonLayout->addWidget( m_addFilesBtn );
  m_removeBtn = new QPushButton( tr( "移除选中" ), fileGroup );
  SicnuUi::markSecondary( m_removeBtn );
  connect( m_removeBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onRemoveSelected );
  fileButtonLayout->addWidget( m_removeBtn );
  fileButtonLayout->addStretch();
  fileGroupLayout->addLayout( fileButtonLayout );
  mainLayout->addWidget( fileGroup );

  QGroupBox *outGroup = SicnuUi::makeGroup( this, tr( "输出目录设置" ) );
  auto *outForm = SicnuUi::makeFormLayout( outGroup );
  m_outputDirEdit = new QLineEdit( outGroup );
  m_outputDirEdit->setObjectName( QStringLiteral( "batchOutputDirEdit" ) );
  m_outputDirEdit->setPlaceholderText( tr( "选择批量处理结果保存目录…" ) );
  SicnuDialogHelp::tip( m_outputDirEdit, tr( "所有批量运行的结果文件均写入此目录。" ) );
  connect( m_outputDirEdit, &QLineEdit::textChanged, this, [this]( const QString &text ) {
    m_outputDir = text.trimmed();
  } );
  m_browseBtn = new QPushButton( tr( "浏览…" ), outGroup );
  SicnuUi::markSecondary( m_browseBtn );
  connect( m_browseBtn, &QPushButton::clicked, this, &BatchProcessingDialog::onBrowseOutputDir );
  auto *outRow = new QHBoxLayout();
  outRow->addWidget( m_outputDirEdit, 1 );
  outRow->addWidget( m_browseBtn );
  outForm->addRow( tr( "输出目录" ), outRow );
  mainLayout->addWidget( outGroup );

  m_progressBar = new QProgressBar( this );
  m_progressBar->setObjectName( QStringLiteral( "batchProgressBar" ) );
  m_progressBar->setVisible( false );
  mainLayout->addWidget( m_progressBar );
  m_statusLabel = SicnuUi::makeHintLabel( this, tr( "就绪" ) );
  mainLayout->addWidget( m_statusLabel );

  auto *buttonBox = new QDialogButtonBox( this );
  buttonBox->setObjectName( QStringLiteral( "rsDialogButtonBox" ) );
  auto *helpBtn = buttonBox->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "batch_processing" ), windowTitle() );
  } );

  auto *closeBtn = buttonBox->addButton( tr( "关闭" ), QDialogButtonBox::RejectRole );
  SicnuUi::markSecondary( closeBtn );
  connect( closeBtn, &QPushButton::clicked, this, &QDialog::accept );

  m_runButton = buttonBox->addButton( tr( "运行批量" ), QDialogButtonBox::AcceptRole );
  SicnuUi::markPrimary( m_runButton );
  SicnuDialogHelp::tip( m_runButton, tr( "按列表顺序逐个执行处理任务。批量运行中请勿强制关闭对话框。" ) );
  connect( m_runButton, &QPushButton::clicked, this, &BatchProcessingDialog::onRun );

  mainLayout->addWidget( buttonBox );

  // The first addItem() fired currentIndexChanged before the widgets existed;
  // rebuild the parameter section for the initially selected algorithm now.
  updateAlgorithmParameters();
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
        this, tr("选择输入文件"), QString(),
        tr("遥感栅格与矢量 (*.tif *.tiff *.img *.shp *.gpkg);;所有文件 (*)"));

    for (const QString &file : files) {
        if (!m_inputFiles.contains(file)) {
            m_inputFiles.append(file);
            m_fileList->addItem(QFileInfo(file).fileName());
        }
    }

    m_statusLabel->setText(tr("已选择 %1 个输入文件").arg(m_inputFiles.size()));
}

void BatchProcessingDialog::onRemoveSelected()
{
    QList<QListWidgetItem*> selected = m_fileList->selectedItems();
    for (QListWidgetItem *item : selected) {
        int row = m_fileList->row(item);
        m_inputFiles.removeAt(row);
        delete item;
    }

    m_statusLabel->setText(tr("已选择 %1 个输入文件").arg(m_inputFiles.size()));
}

void BatchProcessingDialog::onBrowseOutputDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择输出目录"));
    if (!dir.isEmpty()) {
        m_outputDir = dir;
        m_outputDirEdit->setText(dir);
    }
}

/// this-free batch-item runner (defined below, shared by the public member
/// and the JobEngine executor so the async batch holds no `this` capture).
bool runBatchItemImpl(const QString &algorithmId,
                      const QString &inputFile,
                      const QString &outputPath,
                      QString *errorMessage,
                      const QVariantMap &paramOverrides);

void BatchProcessingDialog::reject()
{
    if (m_isRunning) {
        // Running batch: ignore Esc/X (#704). Cancelling via the button
        // advances the chain to a clean stop and re-enables closing.
        return;
    }
    QDialog::reject();
}

void BatchProcessingDialog::onRun()
{
    if (m_isRunning) {
        m_canceled = true;
        m_runButton->setEnabled(false);
        m_statusLabel->setText(tr("正在取消…"));
        // Actually cancel the in-flight item; its terminal record advances the
        // chain (submitNextBatchItem stops on m_canceled).
        if (m_batchTaskId > 0)
            sicnu::TaskCenter::instance().cancelTask(m_batchTaskId);
        return;
    }

    if (m_inputFiles.isEmpty()) {
        QMessageBox::warning(this, tr("批量处理"),
                             tr("请先添加待处理的输入文件。"));
        return;
    }

    if (m_outputDir.isEmpty() && m_outputDirEdit) {
        m_outputDir = m_outputDirEdit->text().trimmed();
    }

    if (m_outputDir.isEmpty()) {
        QMessageBox::warning(this, tr("批量处理"),
                             tr("请指定输出保存目录。"));
        return;
    }

    // DLGB-11: ensure output directory exists (typed path may be new).
    {
        QDir outDir(m_outputDir);
        if (!outDir.exists() && !outDir.mkpath(QStringLiteral("."))) {
            QMessageBox::warning(this, tr("批量处理"),
                                 tr("创建输出目录失败：\n%1").arg(m_outputDir));
            return;
        }
    }

    // Toggle button to Cancel during batch; lock configuration controls against concurrent mutation
    m_isRunning = true;
    m_canceled = false;
    m_runButton->setText(tr("取消"));
    m_runButton->setEnabled(true);
    if (m_addFilesBtn) m_addFilesBtn->setEnabled(false);
    if (m_removeBtn) m_removeBtn->setEnabled(false);
    if (m_browseBtn) m_browseBtn->setEnabled(false);
    if (m_algorithmCombo) m_algorithmCombo->setEnabled(false);
    if (m_paramFrame) m_paramFrame->setEnabled(false);

    const QStringList filesToProcess = m_inputFiles;
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, filesToProcess.size());
    m_progressBar->setValue(0);

    const QString algorithmId = m_algorithmCombo->currentData().toString();
    // The parameter form is frozen while the batch runs; collect once.
    const QVariantMap overrides = collectParamOverrides();

    // Output extension follows the algorithm's sink type: rasters → .tif,
    // vector outputs → .gpkg (a fixed .tif would fail for vector algorithms).
    QString outputExt = QStringLiteral( ".tif" );
    if ( !algorithmId.startsWith( QStringLiteral( "rs:" ) ) )
    {
      const QgsProcessingAlgorithm *alg =
        QgsApplication::processingRegistry()->algorithmById( algorithmId );
      if ( alg )
      {
        if ( const QgsProcessingParameterDefinition *out =
               alg->parameterDefinition( QStringLiteral( "OUTPUT" ) ) )
        {
          if ( out->type() == QStringLiteral( "vectorDestination" ) )
            outputExt = QStringLiteral( ".gpkg" );
        }
      }
    }

    // Async batch (execution-plane goal): each item is submitted to the
    // TaskCenter as a one-shot JobEngine job and the loop advances in
    // onBatchTaskUpdated. The GUI thread never blocks — the previous
    // processEvents(ExcludeUserInputEvents) loop starved user input, so the
    // Cancel button could not actually fire between items.
    m_batchFiles = filesToProcess;
    m_batchIndex = 0;
    m_batchSuccess = 0;
    m_batchFail = 0;
    m_batchErrors.clear();
    m_batchAlgorithmId = algorithmId;
    m_batchOverrides = overrides;
    m_batchOutputExt = outputExt;
    m_batchTaskId = -1;

    submitNextBatchItem();
}

void BatchProcessingDialog::submitNextBatchItem()
{
    if (m_canceled) {
        m_batchErrors.append(tr("批量处理已被用户取消"));
        finishBatch();
        return;
    }
    if (m_batchIndex >= m_batchFiles.size()) {
        finishBatch();
        return;
    }

    const QString inputFile = m_batchFiles.at(m_batchIndex);
    m_statusLabel->setText(tr("正在处理 %1…").arg(QFileInfo(inputFile).fileName()));
    const QString outputPath = uniqueOutputPath(inputFile);

    sicnu::jobs::JobRequest request;
    request.algorithmId = QStringLiteral("batch:%1").arg(m_batchAlgorithmId).toStdString();
    request.title = tr("批量: %1").arg(QFileInfo(inputFile).fileName()).toStdString();
    request.source = "batch_dialog";

    // The executor wraps the (unchanged) synchronous item runner; parameter
    // checks throw before any work, matching the old try/catch reporting.
    const QString algorithmId = m_batchAlgorithmId;
    const QVariantMap overrides = m_batchOverrides;
    sicnu::TaskCenter::JobExecutor executor =
        [algorithmId, inputFile, outputPath, overrides](
            const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value {
            QString itemError;
            if (!runBatchItemImpl(algorithmId, inputFile, outputPath, &itemError, overrides))
                throw std::runtime_error(
                    itemError.isEmpty() ? "Invalid parameters" : itemError.toStdString());
            Json::Value result(Json::objectValue);
            result["output"] = outputPath.toStdString();
            return result;
        };

    m_batchTaskId = sicnu::TaskCenter::instance().submitJob(request, executor);
    if (m_batchTaskId <= 0) {
        m_batchFail++;
        m_batchErrors.append(tr("%1: 任务提交失败")
                                 .arg(QFileInfo(inputFile).fileName()));
        ++m_batchIndex;
        submitNextBatchItem();
    }
}

void BatchProcessingDialog::onBatchTaskUpdated(const sicnu::AlgorithmTaskInfo &info)
{
    if (!m_isRunning || info.taskId != m_batchTaskId)
        return;
    if (!sicnu::isTerminalStatus(info.status))
        return;

    const QString inputFile = m_batchFiles.value(m_batchIndex);
    if (info.status == sicnu::TaskStatus::Completed) {
        m_batchSuccess++;
    } else if (info.status == sicnu::TaskStatus::Canceled) {
        // A user cancel of the in-flight item is a batch stop, not an item
        // failure; submitNextBatchItem records the single cancellation note.
    } else {
        m_batchFail++;
        QString reason = info.errorMessage;
        if (reason.isEmpty())
            reason = tr("参数无效");
        const QString entry = tr("%1: %2").arg(QFileInfo(inputFile).fileName(), reason);
        m_batchErrors.append(entry);
        QgsMessageLog::logMessage(entry, QStringLiteral("batch"), Qgis::MessageLevel::Warning);
    }

    m_progressBar->setValue(++m_batchIndex);
    submitNextBatchItem();
}

void BatchProcessingDialog::finishBatch()
{
    m_isRunning = false;
    m_batchTaskId = -1;
    m_runButton->setText(tr("运行批量"));
    m_runButton->setEnabled(true);
    if (m_addFilesBtn) m_addFilesBtn->setEnabled(true);
    if (m_removeBtn) m_removeBtn->setEnabled(true);
    if (m_browseBtn) m_browseBtn->setEnabled(true);
    if (m_algorithmCombo) m_algorithmCombo->setEnabled(true);
    if (m_paramFrame) m_paramFrame->setEnabled(true);

    if (m_canceled) {
        m_statusLabel->setText(tr("批量处理已取消：%1 个成功，%2 个失败")
                                   .arg(m_batchSuccess).arg(m_batchFail));
    } else {
        m_statusLabel->setText(tr("批量处理完成：%1 个成功，%2 个失败")
                                   .arg(m_batchSuccess).arg(m_batchFail));
    }

    if (m_batchFail > 0) {
        QString details = tr("批量处理完成（存在部分失败）：\n%1 个成功，%2 个失败")
                              .arg(m_batchSuccess).arg(m_batchFail);
        if (!m_batchErrors.isEmpty()) {
            const int maxShow = 8;
            details += QLatin1Char('\n') + m_batchErrors.mid(0, maxShow).join(QLatin1Char('\n'));
            if (m_batchErrors.size() > maxShow)
                details += tr("\n… 及其余 %1 个错误（详见系统日志）").arg(m_batchErrors.size() - maxShow);
        }
        QMessageBox::warning(this, tr("批量处理"), details);
    } else {
        QMessageBox::information(this, tr("批量处理"),
                                 tr("批量处理完成：\n成功处理 %1 个文件")
                                     .arg(m_batchSuccess));
    }
}

QString BatchProcessingDialog::uniqueOutputPath(const QString &inputFile) const
{
    // DLGB-11 collision policy: same basename from different folders gets a
    // numeric suffix (_1, _2, ...) before the extension.
    const QString baseName = QFileInfo(inputFile).completeBaseName();
    const QString nameWithoutExt = baseName + QStringLiteral("_processed");
    QString outputPath = m_outputDir + QStringLiteral("/") + nameWithoutExt + m_batchOutputExt;
    if (QFileInfo::exists(outputPath)) {
        int suffix = 1;
        QString candidate;
        do {
            candidate = m_outputDir + QStringLiteral("/") + nameWithoutExt
                        + QStringLiteral("_%1").arg(suffix++) + m_batchOutputExt;
        } while (QFileInfo::exists(candidate) && suffix < 10000);
        outputPath = candidate;
    }
    return outputPath;
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
            rebuildParamForm(adapter->descriptor());
            m_statusLabel->setText(tr("Selected: %1 (RS, 默认参数)").arg(
                QString::fromStdString(adapter->descriptor().displayName)));
        }
        return;
    }
    // QGIS provider algorithms get QGIS parameter-widget wrappers.
    const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById(algorithmId);
    if (alg) {
        rebuildQgisParamForm(alg);
        m_statusLabel->setText(tr("Selected: %1").arg(alg->displayName()));
    }
}

void BatchProcessingDialog::clearParamForm()
{
    if (!m_paramForm)
        return;
    while (m_paramForm->rowCount() > 0)
    {
        const QFormLayout::TakeRowResult result = m_paramForm->takeRow(0);
        if (result.labelItem)
        {
            if (QWidget *w = result.labelItem->widget())
                w->deleteLater();
            delete result.labelItem;
        }
        if (result.fieldItem)
        {
            if (QWidget *w = result.fieldItem->widget())
                w->deleteLater();
            delete result.fieldItem;
        }
    }
    m_paramWidgets.clear();
    qDeleteAll(m_qgisWrappers);
    m_qgisWrappers.clear();
}

void BatchProcessingDialog::rebuildQgisParamForm(const QgsProcessingAlgorithm *alg)
{
    if (!m_paramForm || !alg)
        return;
    clearParamForm();

    if (!m_qgisContext)
        m_qgisContext = std::make_unique<QgsProcessingContext>();
    QgsProject *project = QgsProject::instance();
    if (project)
    {
        m_qgisContext->setProject(project);
        m_qgisContext->setTransformContext(project->transformContext());
    }

    QgsProcessingParameterWidgetContext widgetContext;
    widgetContext.setProject(project);

    const auto paramDefs = alg->parameterDefinitions();
    for (const QgsProcessingParameterDefinition *param : paramDefs)
    {
        if (!param)
            continue;
        const QString name = param->name();
        if (name == QStringLiteral("INPUT") || name == QStringLiteral("INPUT_LAYER")
            || name == QStringLiteral("OUTPUT") || name == QStringLiteral("OUTPUT_LAYER"))
            continue; // decided by the batch item

        QgsAbstractProcessingParameterWidgetWrapper *wrapper =
            QgsGui::processingGuiRegistry()->createParameterWidgetWrapper(
                param, Qgis::ProcessingMode::Standard);
        if (!wrapper)
            continue;
        wrapper->setWidgetContext(widgetContext);
        QWidget *widget = wrapper->createWrappedWidget(*m_qgisContext);
        if (!widget)
        {
            delete wrapper;
            continue;
        }
        widget->setObjectName(QStringLiteral("qgisParam_%1").arg(name));
        m_paramForm->addRow(QStringLiteral("%1").arg(param->description()), widget);
        m_qgisWrappers.append(wrapper);
    }

    m_paramFrame->setVisible(m_qgisWrappers.size() > 0);
}

void BatchProcessingDialog::rebuildParamForm(const AlgorithmDescriptor &desc)
{
    if (!m_paramForm)
        return;
    clearParamForm();

    const std::string mainInput = findMainInputName(desc);
    for (const auto &in : desc.inputs)
    {
        const QString name = QString::fromStdString(in.name);
        if (name == QString::fromStdString(mainInput))
            continue;
        if (name.startsWith(QStringLiteral("output")))
            continue; // derived from the output path

        QWidget *editor = nullptr;
        switch (in.type)
        {
            case DataType::Boolean:
            {
                auto *check = new QCheckBox(m_paramFrame);
                check->setObjectName(QStringLiteral("rsParam_%1").arg(name));
                check->setChecked(in.defaultValue == QStringLiteral("true"));
                editor = check;
                break;
            }
            case DataType::Enum:
            {
                auto *combo = new QComboBox(m_paramFrame);
                combo->setObjectName(QStringLiteral("rsParam_%1").arg(name));
                for (const auto &option : in.enumOptions)
                    combo->addItem(QString::fromStdString(option), QString::fromStdString(option));
                const int idx = combo->findData(QString::fromStdString(in.defaultValue));
                if (idx >= 0)
                    combo->setCurrentIndex(idx);
                editor = combo;
                break;
            }
            case DataType::Integer:
            {
                auto *spin = new QSpinBox(m_paramFrame);
                spin->setObjectName(QStringLiteral("rsParam_%1").arg(name));
                spin->setRange(-1000000000, 1000000000);
                spin->setValue(in.defaultValue.empty()
                                  ? 0
                                  : static_cast<int>(QString::fromStdString(in.defaultValue).toDouble()));
                editor = spin;
                break;
            }
            case DataType::Numeric:
            {
                auto *spin = new QDoubleSpinBox(m_paramFrame);
                spin->setObjectName(QStringLiteral("rsParam_%1").arg(name));
                spin->setDecimals(6);
                spin->setRange(-1e12, 1e12);
                spin->setValue(in.defaultValue.empty()
                                  ? 0.0
                                  : QString::fromStdString(in.defaultValue).toDouble());
                editor = spin;
                break;
            }
            default:
            {
                auto *edit = new QLineEdit(m_paramFrame);
                edit->setObjectName(QStringLiteral("rsParam_%1").arg(name));
                edit->setText(QString::fromStdString(in.defaultValue));
                editor = edit;
                break;
            }
        }
        if (!editor)
            continue;

        const QString displayName = QString::fromStdString(
            in.displayName == in.name ? in.name : in.displayName);
        m_paramForm->addRow(QStringLiteral("%1").arg(displayName), editor);
        m_paramWidgets.insert(name, editor);
    }

    m_paramFrame->setVisible(true);
}

QVariantMap BatchProcessingDialog::collectParamOverrides() const
{
    QVariantMap overrides;
    if (!m_qgisWrappers.isEmpty())
    {
        // QGIS provider algorithm: values from the parameter-widget wrappers.
        for (const QgsAbstractProcessingParameterWidgetWrapper *wrapper : m_qgisWrappers)
        {
            if (!wrapper || !wrapper->parameterDefinition())
                continue;
            const QString name = wrapper->parameterDefinition()->name();
            overrides[name] = wrapper->parameterValue();
        }
        return overrides;
    }

    for (auto it = m_paramWidgets.constBegin(); it != m_paramWidgets.constEnd(); ++it)
    {
        const QString &name = it.key();
        const QWidget *widget = it.value();
        if (const auto *check = qobject_cast<const QCheckBox *>(widget))
        {
            overrides[name] = check->isChecked();
        }
        else if (const auto *combo = qobject_cast<const QComboBox *>(widget))
        {
            overrides[name] = combo->currentData().toString();
        }
        else if (const auto *spin = qobject_cast<const QDoubleSpinBox *>(widget))
        {
            overrides[name] = spin->value();
        }
        else if (const auto *spin = qobject_cast<const QSpinBox *>(widget))
        {
            overrides[name] = spin->value();
        }
        else if (const auto *edit = qobject_cast<const QLineEdit *>(widget))
        {
            overrides[name] = edit->text();
        }
    }
    return overrides;
}

bool BatchProcessingDialog::runBatchItem(const QString &algorithmId,
                                         const QString &inputFile,
                                         const QString &outputPath,
                                         QString *errorMessage,
                                         const QVariantMap &paramOverrides)
{
    return runBatchItemImpl(algorithmId, inputFile, outputPath, errorMessage, paramOverrides);
}

/// this-free implementation shared by the public member (GUI/tests) and the
/// JobEngine executor (worker thread): touching no dialog state lets the
/// async batch survive a dialog destroyed while an item is still running.
bool runBatchItemImpl(const QString &algorithmId,
                      const QString &inputFile,
                      const QString &outputPath,
                      QString *errorMessage,
                      const QVariantMap &paramOverrides)
{
    if (algorithmId.startsWith(QStringLiteral("rs:"))) {
        const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter(algorithmId.toStdString());
        if (!adapter) {
            if (errorMessage)
                *errorMessage = BatchProcessingDialog::tr("RS operator not found: %1").arg(algorithmId);
            return false;
        }
        try {
            QString paramError;
            Json::Value params = buildRsParams(adapter->descriptor(), inputFile, outputPath, &paramError);
            if (params.isNull()) {
                if (errorMessage)
                    *errorMessage = paramError;
                return false;
            }
            // User overrides win over defaults, except the main input and the
            // output path, which always come from the batch item.
            const std::string mainInput = findMainInputName(adapter->descriptor());
            for (auto it = paramOverrides.constBegin(); it != paramOverrides.constEnd(); ++it)
            {
                const std::string key = it.key().toStdString();
                if (key == mainInput || key == "output")
                    continue;
                const QVariant &value = it.value();
                if (value.userType() == QMetaType::Bool)
                    params[key] = value.toBool();
                else if (value.userType() == QMetaType::Double)
                    params[key] = value.toDouble();
                else if (value.userType() == QMetaType::LongLong
                         || value.userType() == QMetaType::ULongLong)
                    params[key] = static_cast<Json::Int64>(value.toLongLong());
                else if (value.userType() == QMetaType::Int
                         || value.userType() == QMetaType::UInt)
                    params[key] = value.toInt();
                else
                    params[key] = value.toString().toStdString();
            }
            const Json::Value result = adapter->execute(params);
            const bool ok = result.isObject() && result.isMember("output")
                            && result["output"].isString()
                            && !result["output"].asString().empty();
            if (!ok && errorMessage)
                *errorMessage = BatchProcessingDialog::tr("RS operator returned no output");
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
            *errorMessage = BatchProcessingDialog::tr("Algorithm not found: %1").arg(algorithmId);
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
    // User overrides win over QGIS defaults; input/output stay fixed.
    for (auto it = paramOverrides.constBegin(); it != paramOverrides.constEnd(); ++it)
    {
        const QString &key = it.key();
        if (key == QStringLiteral("INPUT") || key == QStringLiteral("INPUT_LAYER")
            || key == QStringLiteral("OUTPUT") || key == QStringLiteral("OUTPUT_LAYER"))
            continue;
        params[key] = it.value();
    }

    std::unique_ptr<QgsProcessingAlgorithm> algClone(alg->create());
    if (!algClone) {
        if (errorMessage)
            *errorMessage = BatchProcessingDialog::tr("Failed to create algorithm instance: %1").arg(algorithmId);
        return false;
    }
    QgsProcessingContext context;
    if (QgsProject *project = QgsProject::instance()) {
        context.setProject(project);
        context.setTransformContext(project->transformContext());
    }
    QString checkError;
    if (!algClone->checkParameterValues(params, context, &checkError)) {
        if (errorMessage)
            *errorMessage = checkError;
        return false;
    }
    QgsProcessingFeedback feedback;
    const QVariantMap results = algClone->run(params, context, &feedback);
    if (results.isEmpty()) {
        if (errorMessage)
            *errorMessage = feedback.textLog().isEmpty()
                ? BatchProcessingDialog::tr("Algorithm returned no results")
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
