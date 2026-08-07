/***************************************************************************
 * otb_segmentation_operator.h  —  OTB Segmentation RSOperator
 ***************************************************************************/
#pragma once

#include "otb_operator_base.h"

namespace sicnu::operators::otb {

/**
 * OTB Segmentation operator (otbcli_Segmentation).
 *
 * Wraps OTB 10.x Segmentation application and exposes the most commonly used
 * algorithms for remote sensing teaching:
 *   - meanshift   (default)
 *   - cc          (connected components)
 *   - watershed
 *   - mprofiles
 *
 * The operator can produce either a vector file (mode=vector, default) or a
 * labeled raster image (mode=raster). For meanshift the operator exposes
 * spatialRadius, rangeRadius, minRegionSize, maxIterations and threshold
 * (mode convergence threshold).
 */
class OtbSegmentationOperator : public OtbOperatorBase {
public:
    std::string name() const override { return "otb:meanshift_segmentation"; }
    std::string displayName() const override { return "OTB MeanShift Segmentation"; }
    std::string group() const override { return "otb-segmentation"; }
    std::string description() const override {
        return "Run OTB Segmentation (MeanShift, connected components, watershed, etc.).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        // Delegates to an OTB CLI process that manages its own tiling.
        return RSOperatorMemoryPolicy::ExternalProcess;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    QString otbApplicationName() const override { return QStringLiteral("Segmentation"); }
    QStringList buildOtbArgs(const Json::Value& params,
                             RSOperatorContext& context) const override;
};

} // namespace sicnu::operators::otb
