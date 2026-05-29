// src/processing/providers/otb_tools/algorithms/otb_band_math.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbBandMathAlgorithm : public OtbToolWrapper
{
public:
    OtbBandMathAlgorithm() = default;

    QString name() const override { return "otb_band_math"; }
    QString displayName() const override { return "Band Math (Mathematical Expression)"; }
    QString group() const override { return "Radiometry"; }
    QString applicationName() const override { return "BandMath"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbBandMathAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
