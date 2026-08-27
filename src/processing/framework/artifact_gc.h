#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <vector>
#include <memory>

#include "sicnu_processing_export.h"
#include "workflow/workflow_run.h"

namespace sicnu::data {
class DataManager;
}

namespace sicnu::processing {

struct GCSweepReport {
  QStringList reapedFiles;
  QStringList retainedFiles;
  QStringList errors;
  int reapedCount = 0;
};

class SICNU_PROCESSING_EXPORT ArtifactGC {
public:
  explicit ArtifactGC( sicnu::data::DataManager *dataManager = nullptr );

  /// Inspect files from a workflow run that are eligible for reaping.
  /// If retainFinalOutputs is true, the final step's output (and any step producing an explicit workflow artifact) is retained.
  QStringList inspectReapable( const sicnu::workflow::WorkflowRun &run,
                               bool retainFinalOutputs = true ) const;

  /// Delete intermediate temporary outputs from a workflow run.
  GCSweepReport sweepRun( const sicnu::workflow::WorkflowRun &run,
                          bool retainFinalOutputs = true );

  /// Clean temporary files directly for a given list of paths (and their sidecars).
  static QStringList removeFilesWithSidecars( const QStringList &filePaths );

private:
  sicnu::data::DataManager *m_dataManager = nullptr;
};

} // namespace sicnu::processing
