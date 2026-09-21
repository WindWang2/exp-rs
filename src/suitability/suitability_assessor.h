#pragma once

// suitability_assessor.h — the assess entry point: goal + subject in,
// versioned report out.
//
// The assessor is a pure function over its inputs: it opens nothing and
// injects nothing. Failure modes are typed (never silent truncation or a
// guessed subject).

#include "../data/data_result.h"
#include "scene_candidate.h"
#include "suitability_goal.h"
#include "suitability_report.h"

#include <QString>
#include <QVector>

namespace sicnu::suitability
{

class SuitabilityAssessor
{
    public:
        /// Upper bound on scenes per assessment; the rectangle-union and
        /// sweep budgets are sized for this cap. Callers pre-filter or sample.
        static inline constexpr int kMaxScenes = 1000;

        struct Inputs
        {
            SuitabilityGoal goal;
            QVector<SceneCandidate> scenes;
            /// Optional subject identifier when assessing a dataset version.
            QString datasetVersionId;
        };

        /// Typed failures: goal resolution failures pass through unchanged
        /// ("suitability.goal_invalid"/"suitability.profile_unknown"),
        /// "suitability.too_many_scenes" above kMaxScenes, and
        /// "suitability.empty_subject" when neither scenes nor a
        /// datasetVersionId nor an AOI identifies anything to assess.
        static sicnu::data::Result<SuitabilityReport> assess( const Inputs &inputs );
};

} // namespace sicnu::suitability
