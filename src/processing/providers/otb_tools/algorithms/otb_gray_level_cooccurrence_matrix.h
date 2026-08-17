// src/processing/providers/otb_tools/algorithms/otb_gray_level_cooccurrence_matrix.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbGrayLevelCooccurrenceMatrixAlgorithm : public OtbToolWrapper
{
public:
    OtbGrayLevelCooccurrenceMatrixAlgorithm() = default;

    QString name() const override { return "otb_gray_level_cooccurrence_matrix"; }
    QString displayName() const override { return "Gray Level Co-occurrence Matrix"; }
    QString group() const override { return "Feature"; }
    QString groupId() const override { return "feature"; }
    QStringList tags() const override { return { QObject::tr( "glcm" ), QObject::tr( "texture" ), QObject::tr( "co-occurrence" ), QObject::tr( "feature" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "HaralickTextureExtraction"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbGrayLevelCooccurrenceMatrixAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};