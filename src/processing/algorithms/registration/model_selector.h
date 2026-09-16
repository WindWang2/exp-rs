// model_selector.h — F13 Package C: evidence-driven transform model
// selection with overfit rejection.
//
// Ladder (ascending complexity, fixed order):
//   Translation -> Similarity -> Affine -> Projective -> Polynomial2
//   -> Polynomial3 -> Tps
//
// Selection rule (DECISIONS.md D-006): walk the ladder from the simplest
// feasible model; step up to a more complex candidate only if its k-fold
// held-out RMSE improves on the current selection by at least
// minImprovement (relative). Held-out RMSE — not fit RMSE — is the gate, so
// a polynomial that memorizes clustered control points cannot win by
// overfitting. Feasibility requires enough points for every CV training
// split, solver success, and condition number below the gate.
#pragma once

#include "registration_types.h"

#include "algorithms/geometric_transform.h"
#include "algorithms/tps_interpolator.h"

#include <QString>
#include <atomic>
#include <utility>
#include <vector>

namespace sicnu::registration {

/// Ladder entry. Tps refers to rs::algorithms::TpsInterpolator (non-
/// parametric; condition number gate does not apply, bending energy is
/// reported instead).
enum class CandidateModel {
    Translation,
    Similarity,
    Affine,
    Projective,
    Polynomial2,
    Polynomial3,
    Tps
};

[[nodiscard]] QString candidateModelName(CandidateModel model);

struct ModelSelectionOptions {
    int folds{4};                 // k for k-fold held-out evaluation (>= 2)
    double minImprovement{0.10};  // relative held-out RMSE gain to step up
    double maxConditionNumber{1e14};
    double tpsLambda{0.0};        // regularization for the Tps candidate
    ResourceBounds bounds{};
};

struct ModelCandidateEvidence {
    CandidateModel model{CandidateModel::Translation};
    double fitRmse{0.0};         // full-fit RMSE (px)
    double cvRmse{0.0};          // mean held-out RMSE over folds (px)
    double conditionNumber{0.0}; // 0 for Tps
    double bendingEnergy{0.0};   // Tps only
    bool feasible{false};
    QString rejectedReason; // empty when feasible: "" | too_few_matches |
                            // degenerate_geometry | ill_conditioned
};

struct ModelSelectionReport {
    RegistrationStatus status{RegistrationStatus::Refused};
    QString reason; // empty on Success; too_few_matches / degenerate_geometry / cancelled
    CandidateModel selected{CandidateModel::Translation};
    /// Full-data refit of the selected model. Meaningful when selected is a
    /// parametric model; for Tps use `tpsFit` instead.
    rs::algorithms::TransformResult transform;
    /// Fitted thin-plate spline when selected == Tps (isFitted() == true).
    rs::algorithms::TpsInterpolator tpsFit;
    std::vector<ModelCandidateEvidence> evidence;
};

class ModelSelector {
  public:
    /// Select a transform model from source->target correspondences. Throws
    /// std::invalid_argument on size mismatch or non-finite coordinates;
    /// data shortfalls are reported through the report, not exceptions.
    static ModelSelectionReport
    select(const std::vector<std::pair<double, double>>& sourcePts,
           const std::vector<std::pair<double, double>>& targetPts,
           const ModelSelectionOptions& options = {}, const std::atomic_bool* cancel = nullptr);
};

} // namespace sicnu::registration
