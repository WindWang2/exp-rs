// src/app/workflow/pipeline_editor_dock.h
#pragma once

#include <QDockWidget>
#include <QToolBar>
#include <QSplitter>

#include "pipeline_canvas_widget.h"
#include "preset_catalog_widget.h"

namespace sicnu::workflow::gui {

class PipelineEditorDock : public QDockWidget
{
  Q_OBJECT

public:
  explicit PipelineEditorDock( QWidget *parent = nullptr );
  ~PipelineEditorDock() override = default;

  PipelineCanvasWidget *pipelineCanvas() const { return mCanvasWidget; }
  PresetCatalogWidget *presetCatalog() const { return mPresetWidget; }

signals:
  void newWorkflowRequested();
  void openWorkflowRequested();
  void saveWorkflowRequested();
  void runFullWorkflowRequested();
  void stopWorkflowRequested();

public slots:
  void onNewClicked();
  void onOpenClicked();
  void onSaveClicked();
public:
  // Convenience for ribbon / programmatic open — delegates to onOpenClicked's file-dialog flow.
  void openPipelineDialog() { onOpenClicked(); }
private slots:
  void onRunFullClicked();
  void onStopClicked();
  void onTogglePresetCatalog();
  void onPresetSelected( const sicnu::workflow::WorkflowDefinition &def );

private:
  void createToolBar();

  PipelineCanvasWidget *mCanvasWidget = nullptr;
  PresetCatalogWidget *mPresetWidget = nullptr;
  QSplitter *mSplitter = nullptr;
  QToolBar *mToolBar = nullptr;
};

} // namespace sicnu::workflow::gui
