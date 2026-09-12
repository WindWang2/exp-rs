// promotion.h — Model Evaluation / Promotion Seam (goal M8).
//
// The platform owns NO model registry. This seam turns a recorded,
// completed run into PROMOTION EVIDENCE for a model candidate keyed by the
// model catalog's existing ids: typed criteria, benchmark-set membership,
// and approval metadata. A model catalog integrates by READING the store's
// promotion records through the stable store API — nothing here writes into
// the catalog, and nothing here approves anything by itself: the verdict is
// evidence, the approval is separate recorded metadata.
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::experiment
{

/// One promotion criterion: the named metric (dotted path into the metric
/// document) of the evidence run must be >= @p minValue.
struct PromotionCriterion
{
    QString metric;
    double minValue = 0.0;

    QJsonObject toJson() const;
    static Result<PromotionCriterion> fromJson( const QJsonObject &json );
};

struct PromotionRequest
{
    QString runId;                 ///< the evidence run (must be Completed)
    QString modelId;               ///< model catalog id ("" = no catalog entry)
    QString modelDigest;           ///< content digest the evidence ran with
    QVector<PromotionCriterion> criteria;
    /// Benchmark sets: when non-empty, the evidence run's dataset version
    /// must be one of these (cross-region / cross-year stability is asserted
    /// through benchmark membership, never guessed).
    QVector<QString> benchmarkDatasetVersions;

    QJsonObject toJson() const;
    static Result<PromotionRequest> fromJson( const QJsonObject &json );
};

/// Typed evaluation outcome: per-criterion results + the honest missing-
/// evidence list. `eligible` is true only when EVERY criterion passed AND
/// no evidence is missing.
struct PromotionEvaluation
{
    struct CriterionResult
    {
        QString metric;
        double value = 0.0;
        bool passed = false;
        QString detail;
        QJsonObject toJson() const;
    };

    bool eligible = false;
    QVector<CriterionResult> results;
    QStringList missingEvidence;

    QJsonObject toJson() const;
};

class PromotionEvaluator
{
  public:
    explicit PromotionEvaluator( ExperimentStore &store );

    /// Evaluates @p request against the recorded truth in the store.
    /// Reads only; never mutates.
    Result<PromotionEvaluation> evaluate( const PromotionRequest &request ) const;

    /// Re-derives the evaluation from the store and persists it as
    /// promotion evidence with approval metadata (@p decision
    /// "approved"/"rejected"/"pending", @p decidedBy = who or what decided).
    /// The verdict stored is ALWAYS the freshly derived one — a caller
    /// cannot persist an eligibility claim the recorded metrics do not
    /// support (review round 1). Each record gets a fresh id; immutability
    /// is enforced at the store layer for a SAME id (content conflict).
    Result<QString> record( const PromotionRequest &request,
                            const QString &decision, const QString &decidedBy ) const;

  private:
    ExperimentStore *m_store = nullptr;
};

} // namespace sicnu::experiment
