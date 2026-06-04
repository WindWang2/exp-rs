// src/processing/providers/otb_tools/algorithms/otb_band_math_x.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbBandMathXAlgorithm : public OtbToolWrapper
{
public:
    OtbBandMathXAlgorithm() = default;

    QString name() const override { return "otb_band_math_x"; }
    QString displayName() const override { return "Band Math X (Multi-Input Expression)"; }
    QString group() const override { return "Radiometry"; }
    QString groupId() const override { return "radiometry"; }
    QStringList tags() const override { return { QObject::tr( "band math" ), QObject::tr( "expression" ), QObject::tr( "multi-input" ), QObject::tr( "radiometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "BandMathX"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbBandMathXAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
