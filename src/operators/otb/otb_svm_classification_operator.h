/***************************************************************************
 * otb_svm_classification_operator.h  —  OTB SVM training RSOperator
 ***************************************************************************/
#pragma once

#include "otb_operator_base.h"

namespace sicnu::operators::otb {

/**
 * OTB SVM image classifier training operator (otbcli_TrainImagesClassifier).
 *
 * Trains a LibSVM model from a raster image and a vector layer containing
 * labelled polygons/points. An optional image statistics XML file can be
 * supplied (commonly produced by otb:compute_images_statistics).
 */
class OtbSvmClassificationOperator : public OtbOperatorBase {
public:
    std::string name() const override { return "otb:svm_classification"; }
    std::string displayName() const override { return "OTB SVM Classification (Training)"; }
    std::string group() const override { return "otb-classification"; }
    std::string description() const override {
        return "Train an OTB LibSVM classifier from raster and labelled vector data.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    QString otbApplicationName() const override { return QStringLiteral("TrainImagesClassifier"); }
    QStringList buildOtbArgs(const Json::Value& params,
                             RSOperatorContext& context) const override;
};

} // namespace sicnu::operators::otb
