// reproduction_bundle_import.h — offline import of reproduction bundles
// into an ExperimentStore (12.0, ADR 0138).
//
// The exporter (reproduction_bundle.h) writes a checksummed directory; the
// importer is its counterpart: verify integrity FIRST (checksums.txt against
// bundle contents — a tampered bundle never reaches the store), check the
// schema version, reconstruct the recorded run and install it as EVIDENCE:
//
//   - the imported run keeps every identity pin, so the repeat-execution
//     classifier recognizes re-ingested bundles (duplicate ingest guard);
//   - the run lands as status Created — a store that never observed the
//     execution never fabricates a terminal lifecycle; metrics.json is
//     installed beside it as the recorded evidence;
//   - the ORIGINAL run/experiment ids are preserved in the report; the
//     store-side record is either the original id (keepOriginalRunId, when
//     free) or a fresh id with the original recorded in the report and
//     dedupable through the identical execution fingerprint.
#pragma once

#include "reproduction_bundle.h"

namespace sicnu::experiment
{

struct ReproductionBundleImportOptions
{
    QString bundleDir;
    /// Experiment the imported run attaches to; must exist in the target
    /// store (`experiment.not_found` otherwise).
    QString targetExperimentId;
    /// Keep the ORIGINAL run id. When it already exists in the target store
    /// the import is idempotent if the recorded execution fingerprint
    /// matches, and fails `experiment.run_exists` otherwise.
    bool keepOriginalRunId = false;
    /// When the bundle is Portable (carries data/), its payload files are
    /// copied here (created as needed). Empty leaves the files in the bundle
    /// with a warning.
    QString relocateDataDir;
};

struct ReproductionBundleImportReport
{
    bool ok = false;
    QString runId;               ///< the run id the imported evidence lives under
    QString originalRunId;       ///< the id recorded in the bundle
    QString executionFingerprint;
    QString resultFingerprint;
    bool alreadyPresent = false; ///< idempotent re-import (no rows changed)
    int relocatedFiles = 0;
    QStringList warnings;
    QJsonObject toJson() const;
};

class ReproductionBundleImporter
{
  public:
    explicit ReproductionBundleImporter( ExperimentStore &experimentStore );

    /// Imports one bundle directory. Integrity (checksums) and schema
    /// version are hard gates; availability hooks are consulted best-effort
    /// and never block an offline import (unavailable model/algorithm only
    /// warns — the record is still the recorded truth).
    ReproductionBundleImportReport importRun(
        const ReproductionBundleImportOptions &options,
        const ReproductionHooks &hooks = {} ) const;

  private:
    ExperimentStore *m_experimentStore = nullptr;
};

} // namespace sicnu::experiment
