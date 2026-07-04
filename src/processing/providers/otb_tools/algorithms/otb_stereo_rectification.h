// src/processing/providers/otb_tools/algorithms/otb_stereo_rectification.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbStereoRectificationAlgorithm : public OtbToolWrapper
{
public:
    OtbStereoRectificationAlgorithm() = default;

    QString name() const override { return "otb_stereo_rectification"; }
    QString displayName() const override { return "Stereo Rectification"; }
    QString group() const override { return "Geometry"; }
    QString groupId() const override { return "geometry"; }
    QStringList tags() const override { return { QObject::tr( "stereo" ), QObject::tr( "rectification" ), QObject::tr( "epipolar" ), QObject::tr( "geometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "StereoRectificationGridGenerator"; }
    QString shortHelpString() const override
    {
        return QObject::tr( "Generates deformation grids to stereo-rectify a pair of images into epipolar geometry. "
                            "Output grids can be used with GridBasedImageResampling for final resampling." );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbStereoRectificationAlgorithm(); }

    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;

protected:
    QStringList buildArgs( const QVariantMap &parameters,
                           QgsProcessingContext &context,
                           QgsProcessingFeedback *feedback ) override;
};