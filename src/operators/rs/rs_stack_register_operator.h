/***************************************************************************
 * rs_stack_register_operator.h — F13 multi-scene stack registration
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Global translation adjustment over a multi-scene stack (F13 Package E):
 * pairwise measured translations (e.g. from rs:register_images consensus
 * inliers) are combined into one least-squares solution against a reference
 * scene, with loop-closure drift reported per edge.
 *
 * Parameters:
 *   scenes       (array of string, required)  Scene ids
 *   observations (array of object, required)  {fromId, toId, tx, ty,
 *                                             [confidence, inlierCount]}
 *   reference    (string, optional)           Reference scene id
 *                                             (default: most connected)
 *   reportPath   (string, optional)           JSON sidecar for the solution
 *
 * Returns: reference, status, reason, maxEdgeResidualPx, rmsEdgeResidualPx,
 *          disconnectedScenes, scenes[].
 */
class RsStackRegisterOperator : public RSOperator {
public:
    std::string name() const override { return "rs:stack_register"; }
    std::string displayName() const override { return "Stack Registration"; }
    std::string group() const override { return "geometric"; }
    std::string description() const override
    {
        return "Multi-scene stack registration: global translation least squares over "
               "pairwise observations with loop-closure drift metrics.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
