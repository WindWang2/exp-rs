/***************************************************************************
 * rs_sar_pair_network_operator.h — multi-temporal pair network
 * (Advanced InSAR 11.0, package E; DECISIONS D-005)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:sar_pair_network — build the time/space-baseline-constrained
 * interferometric pair graph of a scene stack (truth declared inline as
 * JSON: acquisition UTC, wavelength µm, orbit states, optional absolute
 * orbit anchor), with connectivity analysis, reference selection, and the
 * fail-closed semantics of sar_pair_network.h (Oracle 2): invalid
 * metadata and wavelength disagreement are typed refusals; a disconnected
 * graph refuses unless allowDisconnected is set explicitly.
 *
 * The optional JSON sidecar (outputFile) carries the pair table and
 * component map for pipeline consumption; it is written atomically
 * (same-directory .tmp~ + rename, the repo convention). The scene list is
 * sorted by acquisition UTC before pairing (the network's contract is
 * calendar order, not file order).
 */
class RsSarPairNetworkOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sar_pair_network"; }
    std::string displayName() const override { return "InSAR Pair Network"; }
    std::string group() const override { return "sar"; }
    std::string description() const override {
        return "Build a time/space-baseline-constrained pair network from a "
               "scene truth stack (orbits, wavelengths, acquisition dates) "
               "with connectivity QA and typed fail-closed semantics.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
