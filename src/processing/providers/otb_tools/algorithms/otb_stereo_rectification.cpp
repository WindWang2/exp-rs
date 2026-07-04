// src/processing/providers/otb_tools/algorithms/otb_stereo_rectification.cpp
#include "otb_stereo_rectification.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbStereoRectificationAlgorithm::initAlgorithm( const QVariantMap &configuration )
{
    Q_UNUSED( configuration );

    addParameter( new QgsProcessingParameterRasterLayer( "LEFT", QObject::tr( "Left stereo image" ) ) );
    addParameter( new QgsProcessingParameterRasterLayer( "RIGHT", QObject::tr( "Right stereo image" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination( "OUTPUT_LEFT", QObject::tr( "Left deformation grid" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination( "OUTPUT_RIGHT", QObject::tr( "Right deformation grid" ) ) );
    addParameter( new QgsProcessingParameterNumber(
        "ELEVATION", QObject::tr( "Mean elevation (meters)" ),
        Qgis::ProcessingNumberParameterType::Double, 0.0, false, -500.0, 9000.0 ) );
    addParameter( new QgsProcessingParameterNumber(
        "GRID_STEP", QObject::tr( "Deformation grid step (pixels)" ),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 1, 64 ) );
}

QStringList OtbStereoRectificationAlgorithm::buildArgs( const QVariantMap &parameters,
                                                        QgsProcessingContext &context,
                                                        QgsProcessingFeedback *feedback )
{
    Q_UNUSED( context );
    Q_UNUSED( feedback );

    QStringList args;
    args << "-io.inleft" << rasterLayerSource( parameters.value( "LEFT" ) );
    args << "-io.inright" << rasterLayerSource( parameters.value( "RIGHT" ) );
    args << "-io.outleft" << parameters.value( "OUTPUT_LEFT" ).toString();
    args << "-io.outright" << parameters.value( "OUTPUT_RIGHT" ).toString();
    args << "-epi.elevation.default" << QString::number( parameters.value( "ELEVATION" ).toDouble() );
    args << "-epi.step" << QString::number( parameters.value( "GRID_STEP" ).toInt() );
    return args;
}