// src/app/workflow/pipeline_editor_dock.h
#pragma once

#include <QDockWidget>
#include <QToolBar>
#include <QSplitter>

#include "pipeline_canvas_widget.h"
#include "preset_catalog_widget.h"

namespace sicnu::workflow::gui {

/// Serialize @p def as pipeline JSON and write it VERIFIED to @p path
/// (QFile — Unicode-path safe, unlike std::ofstream). Returns false with a
/// human-readable @p error when the file cannot be opened or the bytes do
/// not fully land (disk full, target vanished mid-write). A save that
/// reports success without checking the write is worse than a failed save
/// (R10-B P0-1: onSaveClicked used to toast "Succeeded" on an unchecked
/// ofstream).
bool writePipelineDefinitionFile( const QString &path,
                                  const sicnu::workflow::WorkflowDefinition &def,
                                  QString *error = nullptr );

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

public:
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
