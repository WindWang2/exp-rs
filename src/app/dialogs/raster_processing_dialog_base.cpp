// raster_processing_dialog_base.cpp — Base class for raster processing dialogs
#include "raster_processing_dialog_base.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "shell/processing_job_adapter.h"

#include "jobs/job_types.h"
#include "processing/framework/json_params_converter.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingcontext.h>
#include <qgsexception.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsproject.h>

#include <memory>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>
#include <QIcon>

#include <raster/qgsrasterlayer.h>
#include <qgsmessagelog.h>
#include <qgis.h>

namespace {
// Same contract as AsyncGdalRunner::errorMarker() for structured failure returns.
QString gdalErrorMarker()
{
  return QStringLiteral( "\x01SICNU_ERR\x01" );
}
} // namespace

RasterProcessingDialogBase::RasterProcessingDialogBase( QWidget *parent )
  : QDialog( parent )
{
  SicnuUi::polishDialog( this, 520 );
}

QSize RasterProcessingDialogBase::minimumSizeHint() const
{
  const int charWidth = fontMetrics().horizontalAdvance( QLatin1Char( 'M' ) );
  const int lineHeight = fontMetrics().lineSpacing();
  const int minW = std::max( 520, charWidth * 38 );
  const int minH = std::max( 420, lineHeight * 16 );

  QSize baseHint = QDialog::minimumSizeHint();
  return QSize( std::max( minW, baseHint.width() ), std::max( minH, baseHint.height() ) );
}

QSize RasterProcessingDialogBase::sizeHint() const
{
  const int charWidth = fontMetrics().horizontalAdvance( QLatin1Char( 'M' ) );
  const int lineHeight = fontMetrics().lineSpacing();
  const int hintW = std::max( 560, charWidth * 42 );
  const int hintH = std::max( 460, lineHeight * 18 );

  QSize baseHint = QDialog::sizeHint();
  return QSize( std::max( hintW, baseHint.width() ), std::max( hintH, baseHint.height() ) );
}

void RasterProcessingDialogBase::reject()
{
  // Guard close/Cancel while a GDAL or algorithm task is in flight.
  if ( isRunning() )
    return;
  QDialog::reject();
}

void RasterProcessingDialogBase::setRasterLayer( QgsRasterLayer *layer )
{
  m_rasterLayer = layer;
}

QString RasterProcessingDialogBase::outputPath() const
{
  return m_outputEdit ? m_outputEdit->text().trimmed() : QString();
}

bool RasterProcessingDialogBase::validateInputs()
{
  QString path = outputPath();
  if ( path.isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件路径。" ) );
    if ( m_outputEdit )
      m_outputEdit->setFocus();
    return false;
  }

  if ( !m_rasterLayer || !m_rasterLayer->isValid() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "未选择有效的栅格图层。" ) );
    return false;
  }

  return true;
}

void RasterProcessingDialogBase::setupHelpBanner( QVBoxLayout *layout )
{
  if ( !layout )
    return;

  SicnuDialogHelp::applyDialogChrome( this, toolName() );

  auto *frame = new QFrame( this );
  frame->setObjectName( QStringLiteral( "rsDialogHelpBanner" ) );
  frame->setFrameShape( QFrame::StyledPanel );
  auto *hl = new QHBoxLayout( frame );
  hl->setContentsMargins( 12, 10, 12, 10 );
  hl->setSpacing( 10 );

  auto *stepCol = new QVBoxLayout();
  stepCol->setSpacing( 2 );
  auto *step = new QLabel( tr( "流程" ), frame );
  step->setObjectName( QStringLiteral( "rsDialogStepBadge" ) );
  auto *lbl = new QLabel( SicnuDialogHelp::shortForTool( toolName(), dialogTitle() ), frame );
  lbl->setWordWrap( true );
  lbl->setObjectName( QStringLiteral( "rsDialogHelpSummary" ) );
  SicnuDialogHelp::tip( lbl, tr( "本工具功能简介。点「帮助」查看参数说明。" ) );
  stepCol->addWidget( step );
  stepCol->addWidget( lbl );

  hl->addLayout( stepCol, 1 );
  auto *more = new QPushButton( tr( "参数说明" ), frame );
  more->setObjectName( QStringLiteral( "rsDialogHelpBtn" ) );
  SicnuUi::markSecondary( more );
  SicnuDialogHelp::tip( more, tr( "打开本功能的说明文档。" ) );
  connect( more, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showHelpBox( this, dialogTitle(), dialogHelpHtml() );
  } );
  hl->addWidget( more, 0, Qt::AlignTop );
  layout->insertWidget( 0, frame );
}

QString RasterProcessingDialogBase::dialogHelpHtml() const
{
  return SicnuDialogHelp::htmlForTool( toolName(), dialogTitle() );
}

QGroupBox *RasterProcessingDialogBase::setupInputGroup( QVBoxLayout *layout, const QString &title )
{
  if ( !layout )
    return nullptr;
  const QString gTitle = title.isEmpty() ? tr( "输入数据" ) : title;
  auto *group = SicnuUi::makeGroup( this, gTitle );
  auto *gl = new QVBoxLayout( group );
  gl->setContentsMargins( 12, 10, 12, 10 );
  gl->setSpacing( 8 );
  layout->addWidget( group );
  return group;
}

QGroupBox *RasterProcessingDialogBase::setupParamGroup( QVBoxLayout *layout, const QString &title )
{
  if ( !layout )
    return nullptr;
  const QString gTitle = title.isEmpty() ? tr( "算法参数" ) : title;
  auto *group = SicnuUi::makeGroup( this, gTitle );
  auto *gl = new QVBoxLayout( group );
  gl->setContentsMargins( 12, 10, 12, 10 );
  gl->setSpacing( 8 );
  layout->addWidget( group );
  return group;
}

QGroupBox *RasterProcessingDialogBase::setupAdvancedGroup( QVBoxLayout *layout, const QString &title )
{
  if ( !layout )
    return nullptr;
  const QString gTitle = title.isEmpty() ? tr( "高级选项" ) : title;
  auto *group = SicnuUi::makeGroup( this, gTitle );
  auto *gl = new QVBoxLayout( group );
  gl->setContentsMargins( 12, 10, 12, 10 );
  gl->setSpacing( 8 );
  layout->addWidget( group );
  return group;
}

QGroupBox *RasterProcessingDialogBase::setupOutputGroup( QVBoxLayout *layout, const QString &title )
{
  if ( !layout )
    return nullptr;
  const QString gTitle = title.isEmpty() ? tr( "输出配置" ) : title;
  auto *group = SicnuUi::makeGroup( this, gTitle, tr( "结果保存路径与输出选项。运行前必须填写；建议使用 .tif 扩展名。" ) );
  auto *gl = new QVBoxLayout( group );
  gl->setContentsMargins( 12, 10, 12, 10 );
  gl->setSpacing( 8 );

  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_outputEdit = new QLineEdit( group );
  m_outputEdit->setObjectName( QStringLiteral( "rsDialogOutputEdit" ) );
  m_outputEdit->setPlaceholderText( tr( "选择或输入输出 GeoTIFF 文件路径 (*.tif)..." ) );
  SicnuDialogHelp::tip( m_outputEdit, tr( "结果保存路径。建议使用 .tif 扩展名。" ) );

  auto *browseBtn = new QPushButton( tr( "浏览…" ), group );
  browseBtn->setObjectName( QStringLiteral( "rsDialogBrowseBtn" ) );
  SicnuUi::markSecondary( browseBtn );
  browseBtn->setFixedWidth( 76 );
  SicnuDialogHelp::tip( browseBtn, tr( "浏览选择输出文件位置。" ) );
  connect( browseBtn, &QPushButton::clicked, this, &RasterProcessingDialogBase::browseOutput );

  auto *row = new QHBoxLayout();
  row->setContentsMargins( 0, 0, 0, 0 );
  row->setSpacing( 8 );
  row->addWidget( m_outputEdit, 1 );
  row->addWidget( browseBtn );

  auto *pathLabel = new QLabel( tr( "输出路径" ), group );
  SicnuDialogHelp::tip( pathLabel, tr( "输出 GeoTIFF 路径。运行前必须填写。" ) );
  form->addRow( pathLabel, row );

  gl->addLayout( form );

  auto *hint = SicnuUi::makeHintLabel(
    group, tr( "提示：处理完成后可从日志或工程图层中加载结果。" ) );
  gl->addWidget( hint );

  layout->addWidget( group );
  return group;
}

void RasterProcessingDialogBase::setupOutputRow( QVBoxLayout *layout )
{
  setupOutputGroup( layout );
}

void RasterProcessingDialogBase::setupButtonBar( QVBoxLayout *layout )
{
  if ( !layout )
    return;

  m_buttonBox = new QDialogButtonBox( this );
  m_buttonBox->setObjectName( QStringLiteral( "rsDialogButtonBox" ) );

  // Help button (HelpRole)
  m_helpButton = m_buttonBox->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  m_helpButton->setObjectName( QStringLiteral( "rsDialogHelpButton" ) );
  SicnuUi::markSecondary( m_helpButton );
  SicnuDialogHelp::tip( m_helpButton, tr( "查看本功能的说明文档与使用提示。" ) );

  // Reset button (ResetRole)
  m_resetButton = m_buttonBox->addButton( tr( "重置" ), QDialogButtonBox::ResetRole );
  m_resetButton->setObjectName( QStringLiteral( "rsDialogResetButton" ) );
  SicnuUi::markSecondary( m_resetButton );
  SicnuDialogHelp::tip( m_resetButton, tr( "恢复所有参数到默认初始状态。" ) );

  // Cancel button (RejectRole)
  m_cancelButton = m_buttonBox->addButton( tr( "取消" ), QDialogButtonBox::RejectRole );
  m_cancelButton->setObjectName( QStringLiteral( "rsDialogCancelBtn" ) );
  SicnuUi::markSecondary( m_cancelButton );
  SicnuDialogHelp::tip( m_cancelButton, tr( "任务运行中取消任务，否则关闭对话框。" ) );

  // Run button (AcceptRole)
  m_runButton = m_buttonBox->addButton( tr( "运行" ), QDialogButtonBox::AcceptRole );
  m_runButton->setObjectName( QStringLiteral( "rsPrimaryButton" ) );
  m_runButton->setDefault( true );
  SicnuUi::markPrimary( m_runButton );
  SicnuDialogHelp::tip( m_runButton, tr( "校验输入后开始处理。运行中请勿关闭对话框。" ) );

  // Connect signals
  connect( m_buttonBox, &QDialogButtonBox::accepted, this, &RasterProcessingDialogBase::onRunClicked );
  connect( m_buttonBox, &QDialogButtonBox::rejected, this, [this]() {
    if ( isRunning() )
    {
      // Stay in "cancelling" until the terminal record lands (#696): the
      // handle remains busy, so Run stays disabled and the dialog cannot
      // close or restart while the worker finishes writing.
      m_jobHandle.cancel();
      if ( isRunning() && m_cancelButton )
      {
        m_cancelButton->setEnabled( false );
        m_cancelButton->setText( tr( "取消中…" ) );
      }
    }
    else
      QDialog::reject();
  } );
  connect( m_buttonBox, &QDialogButtonBox::helpRequested, this, &RasterProcessingDialogBase::onHelpClicked );
  if ( m_resetButton )
  {
    connect( m_resetButton, &QPushButton::clicked, this, &RasterProcessingDialogBase::onResetClicked );
  }

  layout->addWidget( m_buttonBox );
}

void RasterProcessingDialogBase::onRunClicked()
{
  if ( isRunning() )
    return;
  if ( validateInputs() )
    onRun();
}

void RasterProcessingDialogBase::onResetClicked()
{
  if ( m_outputEdit )
    m_outputEdit->clear();
}

void RasterProcessingDialogBase::onHelpClicked()
{
  SicnuDialogHelp::showHelpBox( this, dialogTitle(), dialogHelpHtml() );
}

void RasterProcessingDialogBase::browseOutput()
{
  QString path = QFileDialog::getSaveFileName( this, tr( "选择输出文件" ), QString(),
                                               tr( "GeoTIFF (*.tif *.tiff);;所有文件 (*)" ) );
  if ( !path.isEmpty() )
  {
    if ( !path.endsWith( QLatin1String( ".tif" ), Qt::CaseInsensitive )
         && !path.endsWith( QLatin1String( ".tiff" ), Qt::CaseInsensitive ) )
      path += QStringLiteral( ".tif" );
    m_outputEdit->setText( path );
  }
}

void RasterProcessingDialogBase::startRun()
{
  m_running = true;
  if ( m_runButton )
  {
    m_runButton->setEnabled( false );
    m_runButton->setText( tr( "运行中…" ) );
  }
  if ( m_resetButton )
    m_resetButton->setEnabled( false );
  // Prevent Esc / window-close from destroying the dialog mid-run.
  setCursor( Qt::WaitCursor );
}

void RasterProcessingDialogBase::finishRun()
{
  m_running = false;
  if ( m_runButton )
  {
    m_runButton->setEnabled( true );
    m_runButton->setText( tr( "运行" ) );
  }
  if ( m_cancelButton )
  {
    m_cancelButton->setEnabled( true );
    m_cancelButton->setText( tr( "取消" ) );
  }
  if ( m_resetButton )
    m_resetButton->setEnabled( true );
  unsetCursor();
}

void RasterProcessingDialogBase::runGdalTask( const std::function<QString()> &task )
{
  if ( isRunning() )
    return;

  startRun();

  sicnu::jobs::JobRequest req;
  req.algorithmId = "callable:gdal_task";
  req.title = dialogTitle().toStdString();
  req.source = "dialog";
  req.clientTag = toolName().toStdString();

  const QString errMarker = gdalErrorMarker();
  m_jobHandle.submitJob(
    req,
    [task, errMarker]( const sicnu::jobs::JobRequest &,
                       sicnu::operators::RSOperatorContext &ctx ) {
      ctx.throwIfCancelled();
      ctx.logInfo( "Running dialog GDAL task" );
      ctx.throwIfCancelled();
      const QString result = task();
      // Cooperative cancel: task may be long GDAL I/O; check flag after.
      // For GDAL operations that support progress callbacks, wire
      // GDALProgressFunc to ctx.isCancelled() so mid-operation cancel is prompt.
      ctx.throwIfCancelled();
      if ( result.isEmpty() )
      {
        // Empty may be cancel-induced; prefer Cancelled over generic failure.
        if ( ctx.isCancelled() )
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          "Operation failed. Check log for details." );
      }
      if ( result.startsWith( errMarker ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          result.mid( errMarker.size() ).toStdString() );
      }
      ctx.throwIfCancelled();
      Json::Value out( Json::objectValue );
      out["output"] = result.toStdString();
      return out;
    },
    []() {
      // Cancel hook wired — JobEngine arms ctx flag; executor polls it.
      // For GDAL, progress callbacks should forward isCancelled().
    },
    /*autoLoad=*/false,
    [this]( const QString &outPath, const Json::Value & ) {
      onCompleted( outPath );
    },
    [this]( const QString &err, bool isCanceled ) {
      if ( isCanceled )
      {
        cleanupRunResources();
        finishRun();
      }
      else
      {
        onFailed( err );
      }
    }
  );
}

void RasterProcessingDialogBase::runAlgorithmTask( const QgsProcessingAlgorithm *algorithm,
                                                   const QVariantMap &parameters,
                                                   QgsProcessingContext &context )
{
  if ( isRunning() || !algorithm )
    return;

  startRun();

  // Delegate to the shared "processing:" prefix executor (ProviderAlgorithmAdapter
  // via AtomicAlgorithmRegistry) rather than re-implementing the
  // prepare → runPrepared → postProcess lifecycle here. That lifecycle now has a
  // single owner (perf/architecture goal 2026-08-08: de-duplicate the QGIS
  // algorithm execution seam). Project affinity: the executor uses
  // QgsProject::instance(); the @a context's project is expected to be that same
  // instance for dialog-driven runs.
  const QString algId = algorithm->id();
  const QString displayName = algorithm->displayName();

  sicnu::jobs::JobRequest req;
  req.algorithmId = ProcessingJobAdapter::processingAlgorithmId( algId );
  req.title = displayName.toStdString();
  req.source = "dialog";
  req.clientTag = toolName().toStdString();
  req.params = sicnu::processing::variantToJsonValue( QVariant::fromValue( parameters ) );

  // No per-job executor: JobEngine resolves the registered "processing:" prefix
  // executor → runProcessingPrefixJob → ProviderAlgorithmAdapter::execute.
  const QString fallbackOutput = outputPath();
  m_jobHandle.submitJob(
    req,
    [this, fallbackOutput]( const QString &outPath, const Json::Value &result ) {
      // Honor the dialog's output-path contract: prefer the resolved path, then
      // any "output" field the algorithm reported, then the dialog fallback.
      QString resolved = outPath;
      if ( resolved.isEmpty() && result.isMember( "output" ) && result["output"].isString() )
        resolved = QString::fromStdString( result["output"].asString() );
      if ( resolved.isEmpty() )
        resolved = fallbackOutput;
      onCompleted( resolved );
    },
    [this]( const QString &err, bool isCanceled ) {
      if ( isCanceled )
      {
        cleanupRunResources();
        finishRun();
      }
      else
      {
        onFailed( err );
      }
    }
  );
}

void RasterProcessingDialogBase::runOperatorTask( const QString &operatorId,
                                                  const Json::Value &params )
{
  runOperatorTask( operatorId, params, {} );
}

void RasterProcessingDialogBase::runOperatorTask( const QString &operatorId,
                                                  const Json::Value &params,
                                                  const std::function<void( const Json::Value & )> &onResult )
{
  if ( isRunning() )
    return;

  startRun();

  sicnu::jobs::JobRequest req;
  req.algorithmId = operatorId.toStdString();
  req.params = params;
  req.title = dialogTitle().toStdString();
  req.source = "dialog";

  m_jobHandle.submitJob(
    req,
    [this, onResult]( const QString &outputPath, const Json::Value &resultJson ) {
      if ( onResult )
        onResult( resultJson );
      onCompleted( outputPath );
    },
    [this]( const QString &err, bool isCanceled ) {
      if ( isCanceled )
      {
        cleanupRunResources();
        finishRun();
      }
      else
      {
        onFailed( err );
      }
    }
  );
}

void RasterProcessingDialogBase::handleCompleted( const QString &outputPath )
{
  cleanupRunResources();
  finishRun();
  QgsMessageLog::logMessage( tr( "%1 完成。输出：%2" ).arg( toolName(), outputPath ),
                             toolName(), Qgis::MessageLevel::Success );
  if ( shouldAutoAcceptOnSuccess() )
    accept();
  else
    QgsMessageLog::logMessage( tr( "%1 结果已在对话框中展示，请查看后手动关闭。" ).arg( dialogTitle() ),
                               toolName(), Qgis::MessageLevel::Info );
}

void RasterProcessingDialogBase::handleFailed( const QString &error )
{
  cleanupRunResources();
  finishRun();
  QgsMessageLog::logMessage( error, toolName(), Qgis::MessageLevel::Critical );
  QMessageBox::critical( this, dialogTitle(),
                         error.isEmpty() ? tr( "处理失败，详见日志面板。" ) : error );
}

void RasterProcessingDialogBase::onCompleted( const QString &outputPath )
{
  handleCompleted( outputPath );
}

void RasterProcessingDialogBase::onFailed( const QString &errorMessage )
{
  handleFailed( errorMessage );
}
