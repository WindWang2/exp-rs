// evidence.h — Automatic Scientific Evidence projection (goal M4).
//
// A recorded run becomes citable science only when its evidence is complete
// and honestly labeled. This module PROJECTS recorded truth — it never
// computes, estimates or invents evidence: a missing dimension is listed as
// missing, never filled in.
//
// Every summary carries a metrics-schema version so consumers can evolve
// the documents without silently reinterpreting old records.
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"

#include <QJsonObject>
#include <QStringList>

#include <optional>

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

/// Version of the projected evidence document (goal M4). Bump on any
/// breaking change of the summary layout; readers refuse foreign versions
/// instead of guessing.
inline constexpr int kEvidenceSchemaVersion = 1;

/// Which evidence dimensions a run carries, per dimension — the honest
/// answer to "can this run be cited/replayed/compared?".
struct EvidenceCompleteness
{
    struct Dimension
    {
        QString name;    ///< "identity", "environment", "artifacts", "metrics",
                         ///< "steps", "timing", "protocol"
        bool present = false;
        QString detail;  ///< what exactly is missing, when absent
    };

    QVector<Dimension> dimensions;

    /// True only when EVERY dimension reports present.
    bool complete() const;
    /// Names of the missing dimensions (empty when complete()).
    QStringList missing() const;

    QJsonObject toJson() const;
};

/// Typed projection of a recorded run's scientific evidence (goal M4).
/// Pure function of (run, metricRecord?, datasetFingerprint?): the caller
/// supplies what the stores hold; nothing is looked up lazily and nothing
/// is fabricated.
class EvidenceProjector
{
  public:
    struct Input
    {
        ExperimentRun run;
        /// The run's primary metric record, when one exists (std::nullopt =
        /// no metrics evidence — reported as missing, never faked).
        std::optional<MetricRecord> metricRecord;
    };

    /// Projects @p input into a schema-versioned summary document.
    /// Layout (v1):
    ///   schema_version, run_id, experiment_id, status,
    ///   identity { dataset_version, dataset_fingerprint, split_manifest,
    ///              split_fingerprint, model, model_digest, seed,
    ///              software_revision, algorithm },
    ///   environment { fields, platform },
    ///   artifacts [ {path, role, digest, size_bytes} ],
    ///   metrics { hash, protocol, schema_version, document } | missing,
    ///   steps { count, completed, failed } | missing,
    ///   completeness { dimensions, complete }
    static sicnu::data::Result<QJsonObject> summarize( const Input &input );
};

} // namespace sicnu::experiment
