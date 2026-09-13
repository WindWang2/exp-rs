/***************************************************************************
 * rs_library_select_operator.h — spectral library selection / projection
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:library_select — turn a validated spectral library into a pipeline
 * artifact: subset by material, clip to a wavelength window, project onto a
 * sensor band grid (data/spectral/sensors.json), and write the result as a
 * library-format JSON that downstream operators consume via libraryPath /
 * refsRef / endmembersRef.
 *
 * QA in the result payload (never silent):
 *   - selected entry count + material coverage;
 *   - near-duplicate pairs (SAM angle below the QA threshold, default 0.5
 *     degrees) among the selected entries — reported, not removed;
 *   - measured/synthetic composition of the selection.
 *
 * Provenance/license: entries keep their v2 fields; a selection mixing
 * licenses is written as-is but reported (`license: "mixed"` in the payload)
 * so the caller can gate the export.
 */
class RsLibrarySelectOperator : public RSOperator {
public:
    std::string name() const override { return "rs:library_select"; }
    std::string displayName() const override { return "Library Select"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Subset a validated spectral library by material or wavelength "
               "window and optionally project it onto a sensor band grid; "
               "writes a library-format artifact with near-duplicate QA.";
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
