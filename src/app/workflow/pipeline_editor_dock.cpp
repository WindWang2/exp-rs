// src/app/workflow/pipeline_editor_dock.cpp
#include "pipeline_editor_dock.h"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>
#include <fstream>
#include <iostream>

#include "workflow_definition.h"

namespace sicnu::workflow::gui {

PipelineEditorDock::PipelineEditorDock( QWidget *parent )
  : QDockWidget( tr( "任务流程编辑器 / Task Pipeline Editor" ), parent )
{
  setObjectName( QStringLiteral( "rsPipelineEditorDock" ) );
  setAllowedAreas( Qt::AllDockWidgetAreas );

  auto *mainWidget = new QWidget( this );
  auto *layout = new QVBoxLayout( mainWidget );
  layout->setContentsMargins( 0, 0, 0, 0 );
  layout->setSpacing( 0 );

  mSplitter = new QSplitter( Qt::Horizontal, mainWidget );
  mCanvasWidget = new PipelineCanvasWidget( mSplitter );
  mPresetWidget = new PresetCatalogWidget( mSplitter );

  mSplitter->addWidget( mCanvasWidget );
  mSplitter->addWidget( mPresetWidget );
  mSplitter->setStretchFactor( 0, 4 );
  mSplitter->setStretchFactor( 1, 1 );

  createToolBar();
  layout->addWidget( mToolBar );
  layout->addWidget( mSplitter, 1 );
  setWidget( mainWidget );

  connect( mPresetWidget, &PresetCatalogWidget::presetSelected, this, &PipelineEditorDock::onPresetSelected );
}

void PipelineEditorDock::createToolBar()
{
  mToolBar = new QToolBar( this );
  mToolBar->setIconSize( QSize( 16, 16 ) );
  mToolBar->setStyleSheet( QStringLiteral(
    "QToolBar { background-color: #1e293b; border-bottom: 1px solid #334155; padding: 2px; }"
    "QToolButton { color: #f8fafc; font-size: 12px; padding: 4px 8px; border-radius: 3px; }"
    "QToolButton:hover { background-color: #334155; }"
  ) );

  auto *newAct = mToolBar->addAction( tr( "➕ 新建" ) );
  newAct->setToolTip( tr( "新建工作流 (Ctrl+N)" ) );
  connect( newAct, &QAction::triggered, this, &PipelineEditorDock::onNewClicked );

  auto *openAct = mToolBar->addAction( tr( "📂 打开" ) );
  openAct->setToolTip( tr( "打开工作流 (.json) (Ctrl+O)" ) );
  connect( openAct, &QAction::triggered, this, &PipelineEditorDock::onOpenClicked );

  auto *saveAct = mToolBar->addAction( tr( "💾 保存" ) );
  saveAct->setToolTip( tr( "保存工作流 (.json) (Ctrl+S)" ) );
  connect( saveAct, &QAction::triggered, this, &PipelineEditorDock::onSaveClicked );

  mToolBar->addSeparator();

  auto *runAct = mToolBar->addAction( tr( "▶️ 运行全流程" ) );
  runAct->setToolTip( tr( "运行全流程 (按拓扑顺序调度执行)" ) );
  connect( runAct, &QAction::triggered, this, &PipelineEditorDock::onRunFullClicked );

  auto *stopAct = mToolBar->addAction( tr( "⏹️ 停止" ) );
  stopAct->setToolTip( tr( "停止正在运行的流程任务" ) );
  connect( stopAct, &QAction::triggered, this, &PipelineEditorDock::onStopClicked );

  mToolBar->addSeparator();

  auto *zoomFitAct = mToolBar->addAction( tr( "🔍 适应窗口" ) );
  zoomFitAct->setToolTip( tr( "适应窗口显示所有节点" ) );
  connect( zoomFitAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::zoomToFit );

  auto *zoomResetAct = mToolBar->addAction( tr( "🔄 100%" ) );
  zoomResetAct->setToolTip( tr( "恢复 100% 比例" ) );
  connect( zoomResetAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::resetZoom );

  auto *deleteAct = mToolBar->addAction( tr( "🗑️ 删除选中" ) );
  deleteAct->setToolTip( tr( "删除选中节点或连线 (Delete)" ) );
  connect( deleteAct, &QAction::triggered, mCanvasWidget, &PipelineCanvasWidget::deleteSelected );

  mToolBar->addSeparator();

  auto *presetAct = mToolBar->addAction( tr( "📌 预设模板" ) );
  presetAct->setToolTip( tr( "展开预设模板库" ) );
  connect( presetAct, &QAction::triggered, this, &PipelineEditorDock::onTogglePresetCatalog );
}

void PipelineEditorDock::onNewClicked()
{
  if ( mCanvasWidget && mCanvasWidget->pipelineScene() )
  {
    if ( !mCanvasWidget->pipelineScene()->nodes().empty() )
    {
      auto reply = QMessageBox::question( this,
                                          tr( "新建流程确认" ),
                                          tr( "新建流程将清空当前画布。是否继续？" ),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No );
      if ( reply != QMessageBox::Yes )
        return;
    }
    mCanvasWidget->pipelineScene()->clearWorkflow();
  }
  emit newWorkflowRequested();
}

void PipelineEditorDock::onOpenClicked()
{
  QString fileName = QFileDialog::getOpenFileName( this, tr( "打开流程定义 JSON" ), QString(), tr( "JSON Files (*.json)" ) );
  if ( fileName.isEmpty() )
    return;

  std::ifstream inFile( fileName.toStdString() );
  if ( !inFile.is_open() )
  {
    QMessageBox::warning( this, tr( "错误" ), tr( "无法打开文件: %1" ).arg( fileName ) );
    return;
  }

  Json::Value root;
  Json::CharReaderBuilder builder;
  std::string errs;
  if ( !Json::parseFromStream( builder, inFile, &root, &errs ) )
  {
    QMessageBox::warning( this, tr( "解析错误" ), QString::fromStdString( errs ) );
    return;
  }

  WorkflowDefinition def;
  std::string err;
  if ( !workflowDefinitionFromJson( root, def, err ) )
  {
    QMessageBox::warning( this, tr( "格式错误" ), QString::fromStdString( err ) );
    return;
  }

  mCanvasWidget->loadWorkflowDefinition( def );
  emit openWorkflowRequested();
}

void PipelineEditorDock::onSaveClicked()
{
  QString fileName = QFileDialog::getSaveFileName( this, tr( "保存流程定义 JSON" ), QStringLiteral( "workflow.json" ), tr( "JSON Files (*.json)" ) );
  if ( fileName.isEmpty() )
    return;

  WorkflowDefinition baseDef;
  baseDef.id = "exported_pipeline";
  baseDef.title = "Exported Pipeline";

  WorkflowDefinition exported = mCanvasWidget->exportWorkflowDefinition( baseDef );
  Json::Value root = workflowDefinitionToJson( exported );

  std::ofstream outFile( fileName.toStdString() );
  if ( !outFile.is_open() )
  {
    QMessageBox::warning( this, tr( "错误" ), tr( "无法写入文件: %1" ).arg( fileName ) );
    return;
  }

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  outFile << Json::writeString( builder, root );

  QMessageBox::information( this, tr( "成功" ), tr( "流程定义已保存至: %1" ).arg( fileName ) );
  emit saveWorkflowRequested();
}

void PipelineEditorDock::onRunFullClicked()
{
  emit runFullWorkflowRequested();
}

void PipelineEditorDock::onStopClicked()
{
  emit stopWorkflowRequested();
}

void PipelineEditorDock::onTogglePresetCatalog()
{
  if ( mPresetWidget )
  {
    mPresetWidget->setVisible( !mPresetWidget->isVisible() );
  }
}

void PipelineEditorDock::onPresetSelected( const sicnu::workflow::WorkflowDefinition &def )
{
  if ( mCanvasWidget && mCanvasWidget->pipelineScene() )
  {
    if ( !mCanvasWidget->pipelineScene()->nodes().empty() )
    {
      auto reply = QMessageBox::question( this,
                                          tr( "加载模板确认" ),
                                          tr( "加载预设模板将替换当前画布中的流程。是否继续？" ),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No );
      if ( reply != QMessageBox::Yes )
        return;
    }
    mCanvasWidget->loadWorkflowDefinition( def );
  }
}

} // namespace sicnu::workflow::gui
