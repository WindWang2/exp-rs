// fold_audit.h — per-fold leakage audit + fold comparability (goal 7.0 §D).
//
// A fold-based split (k-fold, spatial/group k-fold) is only as trustworthy
// as its worst fold: each materialization (fold k → Test, rest → Train) is
// its own train/test decision and gets its own audit. This module:
//
//   - materializes every fold of a manifest and runs the standard
//     LeakageAuditor per materialization (so planted cross-fold leakage is
//     found in exactly the folds where it bites);
//   - computes per-fold class balance and flags zero-ratio folds (a class
//     absent from a fold's test pool makes that fold's metric for the class
//     undefined — a comparability caveat, never hidden);
//   - verifies deterministic replay: regenerating from the stored config +
//     seed + inputs reproduces the stored fingerprint.
//
// Everything is a pure function of (manifest, inputs); the caller assembled
// those from the store (see data_platform_tools' audit assembly).
#pragma once

#include "dataset_types.h"
#include "leakage_audit.h"
#include "split.h"

#include <QHash>

namespace sicnu::dataset
{

/// One fold's audit + comparability evidence.
struct FoldAuditItem
{
    int fold = -1;
    qint64 trainCount = 0;
    qint64 testCount = 0;
    /// Fold-k test pool vs the rest (train pool) under the caller's config.
    LeakageReport report;
    /// Class balance of the materialization (from SplitInput::classCode).
    QHash<QString, qint64> trainByClass;
    QHash<QString, qint64> testByClass;
    /// Classes present in the whole input but ABSENT from one side of this
    /// fold (zero-ratio folds).
    QStringList classesMissingInTest;
    QStringList classesMissingInTrain;

    QJsonObject toJson() const;
};

/// Aggregate over all folds + the deterministic replay verdict.
struct FoldComparabilitySummary
{
    int foldCount = 0;
    QVector<FoldAuditItem> folds;
    /// Regenerating from (config, seed, inputs) reproduces the stored
    /// fingerprint. Unset (-1 folds / false) when the manifest is not fold
    /// based or inputs were insufficient.
    bool replayMatches = false;
    QString replayFingerprint;
    /// Honest gaps: what the summary could NOT check (e.g. no class evidence
    /// on any input).
    QStringList notes;
    QJsonObject toJson() const;
};

class FoldAuditor
{
  public:
    /// Audits every fold of @p manifest. Fails with
    /// `dataset.split_not_fold_based` when the manifest carries roles, not
    /// folds. Config defaults enable the bucketed checks the inputs can
    /// support (see LeakageAuditConfig). @p contentDigests is caller-supplied
    /// evidence (sampleId → digest) for the exact-duplicate check — the
    /// platform audits only digests it was given, and the report's
    /// digest-unknown count quantifies the rest.
    static sicnu::data::Result<FoldComparabilitySummary> auditFolds(
        const SplitManifest &manifest, const QVector<SplitInput> &inputs,
        const LeakageAuditConfig &config = LeakageAuditConfig(),
        const QHash<QString, QString> &contentDigests = QHash<QString, QString>() );

    /// Regenerates the split from the manifest's (config, seed) over @p
    /// inputs and compares fingerprints against the stored manifest (both
    /// sides note/leakage-cleared). Fails when the manifest is not fold
    /// based or regeneration fails.
    static sicnu::data::Result<bool> verifyDeterministicReplay(
        const SplitManifest &manifest, const QVector<SplitInput> &inputs );
};

} // namespace sicnu::dataset
