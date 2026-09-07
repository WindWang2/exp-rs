/***************************************************************************
 * otb_bundle_to_perfect_sensor_operator.cpp
 ***************************************************************************/
#include "otb_bundle_to_perfect_sensor_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <QFile>

namespace sicnu::operators::otb {

QStringList OtbBundleToPerfectSensorOperator::buildOtbArgs( const Json::Value& params,
                                                            RSOperatorContext& context ) const {
    Q_UNUSED( context );

    const std::string msPath = requireString( params, "ms" );
    const std::string panPath = requireString( params, "pan" );
    if ( !QFile::exists( QString::fromStdString( msPath ) ) ) {
        throw RSOperatorError( ErrorCode::FileNotFound,
                              "Multispectral raster not found: " + msPath );
    }
    if ( !QFile::exists( QString::fromStdString( panPath ) ) ) {
        throw RSOperatorError( ErrorCode::FileNotFound,
                              "Panchromatic raster not found: " + panPath );
    }
    const std::string outputPath = requireString( params, "output" );

    QStringList args;
    args << "-in" << QString::fromStdString( msPath );
    args << "-inp" << QString::fromStdString( panPath );
    args << "-out" << QString::fromStdString( outputPath );
    return args;
}

} // namespace sicnu::operators::otb
