// stack_registrator.h — F13 Package E: multi-scene stack registration by
// global translation adjustment.
//
// Model (DECISIONS.md D-008): each scene gets a 2-DoF offset to the
// reference frame; pairwise observations constrain offset(to) - offset(from)
// = measured translation. The reference scene is pinned at (0, 0). The
// global least-squares solution distributes measurement error across the
// graph; every edge then has a post-adjustment residual equal to its
// distributed loop-closure error. Drift metrics report max/RMS edge
// residual — a triangle whose measurements disagree cannot hide.
//
// Per-pair affine/polynomial alignment remains a pairwise product (see
// rs:register_images); a global affine/bundle adjustment with RPC parameter
// solving is explicitly out of scope (CAPABILITY_MATRIX.md).
#pragma once

#include "registration_types.h"

#include <QString>
#include <atomic>
#include <vector>

namespace sicnu::registration {

struct StackPairObservation {
    QString fromId; // moving scene id
    QString toId;   // neighbor scene id
    double tx{0.0}; // measured translation: position in `to` minus `from` (px)
    double ty{0.0};
    double confidence{1.0}; // [0, 1]; enters the adjustment as a weight
    int inlierCount{0};     // consensus support (adds to the weight)
};

struct StackSceneSolution {
    QString sceneId;
    double offsetX{0.0}; // global offset to the reference frame (px)
    double offsetY{0.0};
    int hopCount{-1};    // graph distance to the reference; -1 = disconnected
    double maxEdgeResidual{0.0}; // worst post-adjustment residual among this
                                 // scene's edges (px)
    bool connected{false};
};

struct StackOptions {
    QString referenceId;         // empty: most-connected scene wins
    double maxPairRmse{3.0};     // (informational) expected pair-level RMSE cap
    int maxScenes{256};          // hard logical cap for the dense solver
    ResourceBounds bounds{};
};

struct StackSolution {
    RegistrationStatus status{RegistrationStatus::Refused};
    QString reason; // too_few_matches / degenerate_geometry / cancelled / cap_exhausted
    QString referenceId;
    std::vector<StackSceneSolution> scenes;
    double maxEdgeResidual{0.0}; // drift: worst distributed closure error (px)
    double rmsEdgeResidual{0.0};
    int disconnectedScenes{0};
};

class StackRegistrator {
  public:
    /// Solve the global translation adjustment. Deterministic; throws
    /// std::invalid_argument for empty inputs or unknown reference id.
    /// Scenes participating in no accepted observation are reported as
    /// disconnected with hopCount -1 (the caller decides whether that is
    /// fatal; the solved subgraph is still returned).
    static StackSolution solveTranslations(const std::vector<QString>& sceneIds,
                                           const std::vector<StackPairObservation>& observations,
                                           const StackOptions& options = {},
                                           const std::atomic_bool* cancel = nullptr);

    /// Reference suggestion: the scene maximizing the total confidence weight
    /// of its incident edges (ties broken by input order). Empty input ->
    /// empty string.
    static QString suggestReference(const std::vector<QString>& sceneIds,
                                    const std::vector<StackPairObservation>& observations);
};

} // namespace sicnu::registration
