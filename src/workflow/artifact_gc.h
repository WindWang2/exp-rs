#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "workflow_run.h"

namespace sicnu::workflow {

struct GCSweepReport {
  QStringList reapedFiles;   // successfully deleted files (including sidecars)
  QStringList retainedFiles; // kept outputs (final outputs, cache-shared, outside workspace)
  QStringList errors;        // "path: reason" for every failed removal
  int reapedCount = 0;
};

class ArtifactGC {
public:
  ArtifactGC() = default;

  /// Files the execution cache still holds and can serve again (#726).
  /// Installed once by the host seam (TaskCenter's constructor); queried by
  /// every sweep. Returning an empty list (no provider installed) keeps the
  /// legacy behavior — sweeps are never BLOCKED by the seam's absence, and a
  /// provider failure cannot nominate files for deletion.
  using ProtectedArtifactProvider = std::function<QStringList()>;
  static void installProtectedArtifactProvider( ProtectedArtifactProvider provider );
  /// The union of every provider-reported path (empty when none installed).
  static QStringList protectedArtifacts();

  /// The platform's sidecar vocabulary (".tfw", ".aux.xml", …), shared with
  /// the cache-serve materialization so ONE definition decides which files
  /// accompany a raster/vector artifact.
  static const QStringList &sidecarSuffixes();

  /// Inspect files from a workflow run that are eligible for reaping.
  ///
  /// Gating rules (all must hold):
  /// - the run is in the Completed state (Running/Interrupted/Failed/Canceled
  ///   runs keep everything: resume and retry depend on their intermediates);
  /// - the step's status is "Completed" (only reap what was actually produced
  ///   and consumed downstream);
  /// - the step was NOT served from the result cache (checkpoint-era flag)
  ///   AND the output path is not currently claimed by the execution result
  ///   cache (cache-served artifacts are what a future identical execution
  ///   reuses — #726 lifecycle contract);
  /// - the output is neither a DAG leaf (nothing consumes it) nor a declared
  ///   workflow artifact, when retainFinalOutputs is true;
  /// - the output path lies in the same directory as (or below) a retained
  ///   final output, so persisted paths from a tampered or corrupt checkpoint
  ///   can never nominate files outside the run workspace for deletion
  ///   (this containment guarantee is scoped to retainFinalOutputs=true;
  ///   with the flag false every Completed output anchors a root and finals
  ///   reap too — the explicit full-cleanup opt-in, #697).
  QStringList inspectReapable( const WorkflowRun &run,
                               bool retainFinalOutputs = true ) const;

  /// Delete intermediate temporary outputs from a workflow run (see
  /// inspectReapable for the gating rules). Deletion failures are collected
  /// in GCSweepReport::errors instead of being silently dropped.
  GCSweepReport sweepRun( const WorkflowRun &run,
                          bool retainFinalOutputs = true );

  /// Remove files and their sidecars. Each file is first renamed to a
  /// ".gctrash" staging name and then deleted, so a concurrent reader either
  /// sees the file or a clean miss - never a half-deleted sidecar set.
  /// @param errors when non-null, receives a "path: reason" entry for every
  /// failed removal.
  static QStringList removeFilesWithSidecars( const QStringList &filePaths, QStringList *errors = nullptr );
};

} // namespace sicnu::workflow
