/***************************************************************************
 * otb_compute_images_statistics_operator.h  —  OTB image statistics RSOperator
 ***************************************************************************/
#pragma once

#include "otb_operator_base.h"

namespace sicnu::operators::otb {

/**
 * OTB ComputeImagesStatistics operator (otbcli_ComputeImagesStatistics).
 *
 * Computes mean and standard-deviation statistics for one or more input
 * raster images and writes an XML file suitable for consumption by
 * otb:svm_classification and other OTB learning applications.
 */
class OtbComputeImagesStatisticsOperator : public OtbOperatorBase {
public:
    std::string name() const override { return "otb:compute_images_statistics"; }
    std::string displayName() const override { return "OTB Compute Images Statistics"; }
    std::string group() const override { return "otb-classification"; }
    std::string description() const override {
        return "Compute mean/std-dev statistics for input images using OTB.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    QString otbApplicationName() const override { return QStringLiteral("ComputeImagesStatistics"); }
    QStringList buildOtbArgs(const Json::Value& params,
                             RSOperatorContext& context) const override;
};

} // namespace sicnu::operators::otb
