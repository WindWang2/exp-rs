// raster_processing_dialog_base.cpp — Base class for raster processing dialogs
#include "raster_processing_dialog_base.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "shell/processing_job_adapter.h"

#include "jobs/job_types.h"
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
  SicnuUi::polishDialog( this, 460 );
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

void RasterProcessingDialogBase::setupOutputRow( QVBoxLayout *layout )
{
  QFrame *sec = SicnuUi::makeSection(
    this, tr( "输出" ),
    tr( "结果保存路径。运行前必须填写；建议使用 .tif 扩展名。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );
  form->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );

  m_outputEdit = new QLineEdit( sec );
  m_outputEdit->setObjectName( QStringLiteral( "rsDialogOutputEdit" ) );
  m_outputEdit->setPlaceholderText( tr( "/path/to/output.tif" ) );
  SicnuDialogHelp::tip( m_outputEdit, tr( "结果保存路径。建议使用 .tif 扩展名。" ) );

  auto *browseBtn = new QPushButton( tr( "浏览…" ), sec );
  browseBtn->setObjectName( QStringLiteral( "rsDialogBrowseBtn" ) );
  SicnuUi::markSecondary( browseBtn );
  SicnuDialogHelp::tip( browseBtn, tr( "浏览选择输出文件位置。" ) );
  connect( browseBtn, &QPushButton::clicked, this, &RasterProcessingDialogBase::browseOutput );

  auto *row = new QHBoxLayout();
  row->setContentsMargins( 0, 0, 0, 0 );
  row->setSpacing( 8 );
  row->addWidget( m_outputEdit, 1 );
  row->addWidget( browseBtn );

  auto *pathLabel = new QLabel( tr( "文件路径" ), sec );
  SicnuDialogHelp::tip( pathLabel, tr( "输出 GeoTIFF 路径。运行前必须填写。" ) );
  form->addRow( pathLabel, row );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );

  auto *hint = SicnuUi::makeHintLabel(
    sec, tr( "提示：处理完成后可从日志或工程图层中加载结果。" ) );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  hint );

  layout->addWidget( sec );
}

void RasterProcessingDialogBase::setupButtonBar( QVBoxLayout *layout )
{
  auto *btnLayout = SicnuUi::makeActionRow( this );

  auto *helpBtn = new QPushButton( tr( "帮助" ), this );
  helpBtn->setObjectName( QStringLiteral( "rsDialogHelpButton" ) );
  SicnuUi::markSecondary( helpBtn );
  SicnuDialogHelp::tip( helpBtn, tr( "查看本功能的说明文档与使用提示。" ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showHelpBox( this, dialogTitle(), dialogHelpHtml() );
  } );
  btnLayout->addWidget( helpBtn );
  btnLayout->addStretch();

  auto *cancelBtn = new QPushButton( tr( "取消" ), this );
  cancelBtn->setObjectName( QStringLiteral( "rsDialogCancelBtn" ) );
  SicnuUi::markSecondary( cancelBtn );
  SicnuDialogHelp::tip( cancelBtn, tr( "关闭对话框（任务运行中将忽略关闭）。" ) );
  connect( cancelBtn, &QPushButton::clicked, this, &QDialog::reject );
  btnLayout->addWidget( cancelBtn );

  m_runButton = new QPushButton( tr( "运行" ), this );
  m_runButton->setObjectName( QStringLiteral( "rsPrimaryButton" ) );
  SicnuUi::markPrimary( m_runButton );
  SicnuDialogHelp::tip( m_runButton, tr( "校验输入后开始处理。运行中请勿关闭对话框。" ) );
  connect( m_runButton, &QPushButton::clicked, this, [this]() {
    if ( validateInputs() )
      onRun();
  } );
  btnLayout->addWidget( m_runButton );

  layout->addLayout( btnLayout );
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
      ctx.logInfo( "Running dialog GDAL task" );
      const QString result = task();
      if ( result.isEmpty() )
      {
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
      Json::Value out( Json::objectValue );
      out["output"] = result.toStdString();
      return out;
    },
    /*cancelCallback=*/nullptr,
    /*autoLoad=*/false,
    [this]( const QString &outPath, const Json::Value & ) {
      onCompleted( outPath );
    },
    [this]( const QString &err, bool isCanceled ) {
      onFailed( isCanceled ? tr( "已取消" ) : err );
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

  // Worker-local context (copy thread-safe settings) so prepare / runPrepared /
  // postProcess all run on the JobEngine worker — same pattern as the
  // processing: prefix executor.
  const QString algId = algorithm->id();
  const QString displayName = algorithm->displayName();
  std::shared_ptr<QgsProcessingAlgorithm> algClone( algorithm->create() );
  if ( !algClone )
  {
    cleanupRunResources();
    onFailed( tr( "Failed to create algorithm instance" ) );
    return;
  }

  QgsProject *project = context.project();

  sicnu::jobs::JobRequest req;
  req.algorithmId = ProcessingJobAdapter::processingAlgorithmId( algId );
  req.title = displayName.toStdString();
  req.source = "dialog";
  req.clientTag = toolName().toStdString();

  const QString fallbackOutput = outputPath();
  m_jobHandle.submitJob(
    req,
    [algClone, parameters, project, fallbackOutput](
      const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running dialog processing algorithm" );
      QgsProcessingContext localCtx;
      if ( project )
      {
        localCtx.setProject( project );
        localCtx.setTransformContext( project->transformContext() );
      }

      QgsProcessingFeedback feedback;
      try
      {
        if ( !algClone->prepare( parameters, localCtx, &feedback ) )
        {
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::QgisProcessingError, "Algorithm prepare() returned false" );
        }
        QVariantMap runRes =
          algClone->runPrepared( parameters, localCtx, &feedback );
        if ( feedback.isCanceled() || ctx.isCancelled() )
        {
          algClone->postProcess( localCtx, &feedback, false );
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
        }
        QVariantMap results = algClone->postProcess( localCtx, &feedback, true );
        if ( results.isEmpty() )
          results = runRes;

        Json::Value out = ProcessingJobAdapter::resultsToJson( results );
        if ( !out.isMember( "output" ) && !fallbackOutput.isEmpty() )
          out["output"] = fallbackOutput.toStdString();
        if ( !out.isMember( "output" ) )
        {
          for ( auto it = results.constBegin(); it != results.constEnd(); ++it )
          {
            if ( it.value().userType() == QMetaType::QString
                 && !it.value().toString().isEmpty() )
            {
              out["output"] = it.value().toString().toStdString();
              break;
            }
          }
        }
        if ( !out.isMember( "output" ) )
        {
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::ComputationError,
            "Algorithm did not produce an output path" );
        }
        return out;
      }
      catch ( const QgsProcessingException &e )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::QgisProcessingError, e.what().toStdString() );
      }
    },
    /*cancelCallback=*/nullptr,
    /*autoLoad=*/false,
    [this]( const QString &outPath, const Json::Value & ) {
      onCompleted( outPath );
    },
    [this]( const QString &err, bool isCanceled ) {
      onFailed( isCanceled ? tr( "已取消" ) : err );
    }
  );
}

void RasterProcessingDialogBase::runOperatorTask( const QString &operatorId,
                                                  const Json::Value &params )
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
    [this]( const QString &outputPath, const Json::Value & ) {
      onCompleted( outputPath );
    },
    [this]( const QString &err, bool isCanceled ) {
      onFailed( isCanceled ? tr( "已取消" ) : err );
    }
  );
}

void RasterProcessingDialogBase::handleCompleted( const QString &outputPath )
{
  cleanupRunResources();
  finishRun();
  QgsMessageLog::logMessage( tr( "%1 完成。输出：%2" ).arg( toolName(), outputPath ),
                             toolName(), Qgis::MessageLevel::Success );
  accept();
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
