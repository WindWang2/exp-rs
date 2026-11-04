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

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

struct ReproductionBundleImportOptions
{
    QString bundleDir;
    /// Experiment the imported run attaches to; must exist in the target
    /// store (a missing experiment is reported as a failed import with an
    /// "does not exist" warning).
    QString targetExperimentId;
    /// Keep the ORIGINAL run id. When it already exists in the target store
    /// with a different identity, the import is refused with an
    /// "already exists with a different identity" warning.
    bool keepOriginalRunId = false;
    /// When the bundle is Portable (carries data/), its payload files are
    /// copied here (created as needed). Empty leaves the files in the bundle
    /// with a warning.
    QString relocateDataDir;
    /// #1804 (WE-30): relocation never overwrites by default. An existing
    /// destination file is reported in ReproductionBundleImportReport::
    /// conflicts and skipped; an explicit opt-in overwrites.
    bool overwriteExistingData = false;
    /// #1804 (WE-29): per-member byte budget for relocation. Members larger
    /// than this are refused with a warning instead of being slurped into
    /// memory. Default mirrors the exporter's portable-copy cap
    /// (governance repro_bundle.h portableMaxBytes).
    qint64 maxRelocateMemberBytes = 20LL * 1024 * 1024 * 1024;
    /// WP07 dry-run: run EVERY gate (integrity, schema, identity, duplicate
    /// check, metrics validation, relocation conflict scan) but write
    /// nothing — no store rows, no relocated files. The report carries the
    /// would-be run id and the full conflict/warning summary.
    bool dryRun = false;
};

struct ReproductionBundleImportReport
{
    bool ok = false;
    bool dryRun = false;         ///< set when the import was a check-only pass
    QString runId;               ///< the run id the imported evidence lives under
    QString originalRunId;       ///< the id recorded in the bundle
    QString executionFingerprint;
    QString resultFingerprint;
    bool alreadyPresent = false; ///< idempotent re-import (no rows changed)
    int relocatedFiles = 0;
    /// R24-B (#1581 residual): direction-preserving run_edges items that were
    /// re-attached into the target store(s), with the run endpoint rewritten
    /// to the installed run id.
    int lineageEdgesReattached = 0;
    /// #1804 (WE-30): relocation destinations that already exist and were
    /// NOT overwritten (user-consumable conflict preview; empty when
    /// overwriteExistingData cleared them or no target was set).
    QStringList conflicts;
    QStringList warnings;
    QJsonObject toJson() const;
};

class ReproductionBundleImporter
{
  public:
    /// @p experimentStore must outlive this importer (a bare pointer is
    /// held).
    explicit ReproductionBundleImporter( ExperimentStore &experimentStore );
    /// R24-B: with a @p datasetStore, dataset-side lineage edges
    /// (asset|dataset|dataset_version|sample|annotation|split endpoints) are
    /// re-attached there too; without one they keep the named-gap warning
    /// (the default constructor preserves the historical behavior).
    ReproductionBundleImporter( ExperimentStore &experimentStore,
                                sicnu::dataset::DatasetStore &datasetStore );

    /// Imports one bundle directory. Integrity (checksums) and schema
    /// version are hard gates; availability hooks are consulted best-effort
    /// and never block an offline import (unavailable model/algorithm only
    /// warns — the record is still the recorded truth).
    ReproductionBundleImportReport importRun(
        const ReproductionBundleImportOptions &options,
        const ReproductionHooks &hooks = {} ) const;

  private:
    ExperimentStore *m_experimentStore = nullptr;
    sicnu::dataset::DatasetStore *m_datasetStore = nullptr;
};

} // namespace sicnu::experiment
