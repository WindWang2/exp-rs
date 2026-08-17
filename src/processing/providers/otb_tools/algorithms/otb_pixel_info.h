// src/processing/providers/otb_tools/algorithms/otb_pixel_info.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbPixelInfoAlgorithm : public OtbToolWrapper
{
public:
    OtbPixelInfoAlgorithm() = default;

    QString name() const override { return "otb_pixel_info"; }
    QString displayName() const override { return "Pixel Info"; }
    QString group() const override { return "Utilities"; }
    QString groupId() const override { return "utilities"; }
    QStringList tags() const override { return { QObject::tr( "pixel" ), QObject::tr( "info" ), QObject::tr( "value" ), QObject::tr( "utilities" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "PixelValue"; }
    QString shortHelpString() const override
    {
        return QObject::tr( "Returns the spectral values of a raster pixel at the given image coordinates." );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbPixelInfoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;

    // Override to avoid requiring OUTPUT parameter
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;
};
