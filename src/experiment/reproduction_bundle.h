// reproduction_bundle.h — experiment-scoped reproduction bundles
// (goal §31, ADR 0138).
//
// A bundle is a DIRECTORY capturing everything needed to judge (and where
// possible perform) a replay of one ExperimentRun:
//   manifest.json      bundle schema version + run identity + verdict
//   dataset_refs.json  dataset version pin (id + fingerprint + source refs)
//   split.json         split manifest pin
//   run_config.json    canonical parameters + identity hashes
//   environment.json   allowlisted environment (secret-filtered)
//   software.json      platform/software revisions
//   model_refs.json    model id@version + content digest
//   workflow.json      workflow definition snapshot (when applicable)
//   metrics.json       protocol + metrics
//   provenance.json    lineage slice around the run
//   README.md          human-readable cover
//   checksums.txt      SHA-256 of every bundle file
//
// Payload bytes are NOT copied by default (stable refs + digests); the
// portable mode copies capped bytes, mirroring the workspace bundle's
// policy. Export never writes secrets: the environment passes the denylist
// again before serialization.
#pragma once

#include "experiment_store.h"
#include "lineage.h"

#include <QJsonObject>

#include <functional>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

inline constexpr const char *kReproductionBundleSchemaVersion = "1";

struct ReproductionBundleOptions
{
    QString outputDir;
    enum class Mode
    {
        Reference, ///< refs + digests only (default)
        Portable,  ///< additionally copy capped payload bytes into data/
    };
    Mode mode = Mode::Reference;
    qint64 portableMaxBytes = 512LL * 1024 * 1024; // portable copy cap
    /// Software revision of the EXPORTING build (stamped into software.json).
    QString currentSoftwareRevision;
};

struct ReproductionBundleReport
{
    bool ok = false;
    QString bundlePath;
    int fileCount = 0;
    QStringList warnings;
    QJsonObject toJson() const;
};

/// External availability checks the experiment module cannot do itself
/// (model catalog, operator registry, workflow store). Every hook answers
/// "is X currently available/valid"; unwired hooks answer BestEffort, never
/// a fake Exact.
struct ReproductionHooks
{
    /// (modelId, modelDigest) → still resolvable with matching digest?
    std::function<bool( const QString &modelId, const QString &modelDigest )> modelAvailable;
    /// operator/workflow id → executable with the current install?
    std::function<bool( const QString &algorithmId )> algorithmAvailable;
    /// workflowId → definition parses against the current engine?
    std::function<bool( const QString &workflowId )> workflowValid;
    /// artifact path → file exists (and size matches when > 0)?
    std::function<bool( const QString &path, qint64 sizeBytes )> artifactAvailable;
};

struct ReproductionValidation
{
    dataset::ReproductionLevel level = dataset::ReproductionLevel::Impossible;
    QStringList reasons;   ///< evidence per check, pass or fail
    QJsonObject toJson() const;
};

class ReproductionBundleExporter
{
  public:
    ReproductionBundleExporter( const ExperimentStore &experimentStore,
                                const dataset::DatasetStore &datasetStore );

    ReproductionBundleReport exportRun( const QString &runId,
                                        const ReproductionBundleOptions &options ) const;

    /// Validates an exported bundle directory (or an in-memory record).
    ReproductionValidation validateBundle( const QString &bundleDir,
                                           const ReproductionHooks &hooks ) const;

  private:
    const ExperimentStore &m_experimentStore;
    const dataset::DatasetStore &m_datasetStore;
};

} // namespace sicnu::experiment
