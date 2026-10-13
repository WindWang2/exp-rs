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
  /// Workbench 10.0: run workflow:preflight on the canvas DAG and project
  /// error findings onto node badges (shared agent engine, no second check).
  void runPreflightProjection();
  // Convenience for ribbon / programmatic open — delegates to onOpenClicked's file-dialog flow.
  void openPipelineDialog() { onOpenClicked(); }

  /// Atomic write: writes to temporary file and renames to target, ensuring
  /// zero corruption or partial files on failure.
  static bool writePipelineDefinitionFile( const QString &filePath,
                                           const Json::Value &root,
                                           QString *errorOut = nullptr );

  static bool readPipelineDefinitionFile( const QString &filePath,
                                          Json::Value &rootOut,
                                          QString *errorOut = nullptr );
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
