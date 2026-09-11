// split.h — deterministic split engine + persisted split manifests
// (goal §18, ADR 0136).
//
// The engine is a PURE function: (config, seed, inputs) → SplitManifest.
// Identical inputs replay byte-identically on every platform (the PRNG is
// fixed in deterministic_random.h). Manifests are persisted content — a
// split that was never stored is not a split the platform reasons about.
//
// Input model: SplitInput is the flat, engine-facing view of one sample
// (identity + grouping + label + time + spatial bounds). Building it from
// SampleRecords + annotation tips is the caller's adapter job — the engine
// stays decoupled from payload shapes and remains trivially testable.
#pragma once

#include "dataset_ids.h"
#include "dataset_types.h"

#include "../data/data_result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

/// Serialization schema version of split manifests.
inline constexpr int kSplitManifestSerializationVersion = 1;

/// Upper bound of distinct class codes materialized into one role's class
/// distribution of the split summary; beyond it the distribution is replaced
/// by a truncation flag (bounded metadata, honestly marked).
inline constexpr int kSplitSummaryMaxClasses = 256;

/// The engine-facing view of one sample.
struct SplitInput
{
    QString sampleId;
    QString groupId;   ///< envelope group (object/event/scene granularity)
    QString classCode; ///< tip annotation class ("" = unlabeled)
    qint64 timeMs = 0; ///< envelope observation time (0 = none)
    // Ground-space bounds for spatial methods; validBounds=false keeps the
    // sample out of TEST pools of spatial methods (it cannot be located).
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool validBounds = false;
    QString sceneId;   ///< leave-one-scene-out key ("" falls back to groupId)
    qint64 year = 0;   ///< leave-one-year-out key (0 = unknown)
    QString eventGroup; ///< same-event key (pre/post pairs share it)

    friend bool operator==( const SplitInput &, const SplitInput & ) = default;
};

/// Split configuration (validated). Ratios: train + validation + test must
/// sum to ~1 when used; k-fold family uses foldCount instead. Every double
/// field must be finite: NaN/Inf are validation failures, never silent
/// degenerate behavior (#875 class).
struct SplitConfig
{
    SplitMethod method = SplitMethod::Random;
    double trainRatio = 0.7;
    double validationRatio = 0.15;
    double testRatio = 0.15;
    quint64 seed = 0;      ///< required; 0 is a legal seed, absence is not
    qint64 foldCount = 5;  ///< k-fold family
    double blockSizeX = 0.0; ///< spatial grid size (CRS units; spatial_block,
                             ///  spatial_k_fold, spatiotemporal_block)
    double blockSizeY = 0.0;
    double bufferDistance = 0.0; ///< spatial_buffer exclusion (CRS units)
    qint64 temporalWindowMs = 0; ///< spatiotemporal_block time window length
    QString regionKey;  ///< leave-one-region-out key field name (documentation)
    QJsonObject extra;  ///< method-specific extras (validated per method)

    QJsonObject toJson() const;
    static sicnu::data::Result<SplitConfig> fromJson( const QJsonObject &json );
    sicnu::data::Result<void> validate() const;

    friend bool operator==( const SplitConfig &, const SplitConfig & ) = default;
};

/// One sample's outcome in a manifest. For plain methods role is set and
/// fold is -1; for k-fold/leave-one-out families fold is set and role is
/// Unassigned until a fold is materialized.
struct SplitAssignment
{
    QString sampleId;
    SplitRole role = SplitRole::Unassigned;
    int fold = -1;

    friend bool operator==( const SplitAssignment &, const SplitAssignment & ) = default;
};

class SplitManifest
{
  public:
    SplitManifest() = default;

    const QString &manifestId() const { return m_manifestId; }
    void setManifestId( const QString &id ) { m_manifestId = id; }
    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const SplitConfig &config() const { return m_config; }
    void setConfig( const SplitConfig &config ) { m_config = config; }
    DeterminismGrade determinism() const { return m_determinism; }
    void setDeterminism( DeterminismGrade grade ) { m_determinism = grade; }
    /// Free-text reason REQUIRED when determinism != Strict.
    const QString &determinismNote() const { return m_determinismNote; }
    void setDeterminismNote( const QString &note ) { m_determinismNote = note; }

    QVector<SplitAssignment> &assignments() { return m_assignments; }
    const QVector<SplitAssignment> &assignments() const { return m_assignments; }

    /// Leakage audit summary JSON (findings counts by kind/severity) —
    /// embedded at audit time; the full report lives in the store (goal §19).
    const QJsonObject &leakageSummary() const { return m_leakageSummary; }
    void setLeakageSummary( const QJsonObject &summary ) { m_leakageSummary = summary; }

    /// Role/fold/class summary computed by the engine at generation time:
    /// total samples, per-role and per-fold counts, per-role class
    /// distribution (plain methods). Class keys are capped (see
    /// kSplitSummaryMaxClasses) with an explicit truncation flag — the
    /// summary is bounded metadata, and truncation is visible, never silent.
    const QJsonObject &summary() const { return m_summary; }
    void setSummary( const QJsonObject &summary ) { m_summary = summary; }

    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }
    const QString &note() const { return m_note; }
    void setNote( const QString &note ) { m_note = note; }
    const QString &fingerprint() const { return m_fingerprint; }
    void setFingerprint( const QString &fingerprint ) { m_fingerprint = fingerprint; }

    /// Sample ids of one role (order: manifest assignment order).
    QStringList sampleIdsOfRole( SplitRole role ) const;
    /// Materializes fold @p foldIndex: fold → Test, everything else → Train.
    /// Returns nullopt when this manifest carries no fold assignments.
    std::optional<QVector<SplitAssignment>> materializeFold( int foldIndex ) const;
    /// Linear scan by contract - batch consumers (splits over 100k rows)
    /// should build their own hash from assignments() instead.
    std::optional<SplitAssignment> assignmentOf( const QString &sampleId ) const;

    QJsonObject toJson() const;
    static sicnu::data::Result<SplitManifest> fromJson( const QJsonObject &json );

    friend bool operator==( const SplitManifest &, const SplitManifest & ) = default;

  private:
    QString m_manifestId;
    QString m_datasetVersionId;
    SplitConfig m_config;
    DeterminismGrade m_determinism = DeterminismGrade::Strict;
    QString m_determinismNote;
    QVector<SplitAssignment> m_assignments;
    QJsonObject m_leakageSummary;
    QJsonObject m_summary;
    QDateTime m_createdAtUtc;
    QString m_note;
    QString m_fingerprint;
};

/// Content fingerprint of a manifest (canonical JSON minus the fingerprint
/// field). Equal fingerprint = identical split definition + assignment.
QString splitManifestFingerprint( const SplitManifest &manifest );

/// The split engine. Every method is deterministic under the seed; failures
/// are typed (`dataset.split_*`) and pre-validate the config so a bad ratio
/// or missing key never produces a half-assignment.
class SplitEngine
{
  public:
    /// Runs @p config over @p inputs. Inputs must carry unique sample ids;
    /// method-specific requirements (bounds, groups, times, years) are
    /// enforced per method.
    static sicnu::data::Result<SplitManifest> generate( const SplitConfig &config,
                                                        const QString &datasetVersionId,
                                                        const QVector<SplitInput> &inputs );

    /// The methods that persist fold ids rather than final roles.
    static bool methodUsesFolds( SplitMethod method );

  private:
    static sicnu::data::Result<SplitManifest> generatePlain( SplitManifest manifest,
                                                             const QVector<SplitInput> &inputs );
    static sicnu::data::Result<SplitManifest> generateFolds( SplitManifest manifest,
                                                             const QVector<SplitInput> &inputs );
};

} // namespace sicnu::dataset
