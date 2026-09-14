/***************************************************************************
 * rs_cn_import_operator.h — CN import metadata stamping (ADR 0146/0147).
 *
 * Identification, resolution, role mapping, calibration and provenance moved
 * to the standardized import-plan service (rs_product_import_plan.*); this
 * header keeps the output-metadata authority: stamping declared CN
 * dataset/band metadata (sun geometry, calibration, radiometric state) onto
 * the stacked GeoTIFF, and the missing-declared-fields contract.
 *
 * Absence policy: sidecar fields that are not declared are reported as
 * explicitly missing in the operator result — never defaulted.
 ***************************************************************************/
#pragma once

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/algorithms/satellite_products.h"

#include <QString>
#include <QStringList>

#include <gdal_priv.h>

#include <string>
#include <vector>

namespace sicnu::operators::rs {

/// Stamps the CN import metadata onto the stacked GeoTIFF. Dataset level:
/// product family/kind, sensor + mode, sun geometry, radiometric state.
/// Band level: declared gain/bias (verbatim, per band) beside the role and
/// wavelength items stackToGeoTiff already wrote.
inline bool writeCnImportMetadata( const QString &outputPath,
                                   const sicnu::geo::ProductMetadata &metadata,
                                   const QStringList &stackedBands,
                                   const QString &productKindName,
                                   QString *errorMessage )
{
    GDALDatasetH dataset = GDALOpen( outputPath.toUtf8().constData(), GA_Update );
    if ( dataset == nullptr )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot reopen stacked GeoTIFF for CN metadata: %1" )
                               .arg( outputPath );
        return false;
    }

    GDALSetMetadataItem( dataset, "SICNU_PRODUCT_TYPE", productKindName.toUtf8().constData(), nullptr );
    GDALSetMetadataItem( dataset, "SICNU_PRODUCT_FAMILY", "cn", nullptr );
    if ( !metadata.sensor.empty() )
        GDALSetMetadataItem( dataset, "SICNU_SENSOR", metadata.sensor.c_str(), nullptr );
    if ( !metadata.sensorMode.empty() )
        GDALSetMetadataItem( dataset, "SICNU_SENSOR_MODE", metadata.sensorMode.c_str(), nullptr );
    if ( !metadata.orbitId.empty() )
        GDALSetMetadataItem( dataset, "SICNU_ORBIT_ID", metadata.orbitId.c_str(), nullptr );
    if ( metadata.hasSunElevation )
    {
        const QByteArray elevation = QByteArray::number( metadata.sunElevationDeg, 'f', 4 );
        GDALSetMetadataItem( dataset, "SICNU_SUN_ELEVATION_DEG", elevation.constData(), nullptr );
        // Consumer-compat key: rs:radiometric_calibration and the DOS flows
        // read the Landsat-convention "SUN_ELEVATION" (degrees above horizon).
        GDALSetMetadataItem( dataset, "SUN_ELEVATION", elevation.constData(), nullptr );
    }
    if ( metadata.hasSunAzimuth )
        GDALSetMetadataItem( dataset, "SICNU_SUN_AZIMUTH_DEG",
                             QByteArray::number( metadata.sunAzimuthDeg, 'f', 4 ).constData(),
                             nullptr );

    for ( const sicnu::geo::BandCalibration &calibration : metadata.bandCalibration )
    {
        // Index by the STACKED band order (the user may have reordered or
        // subset the declared inventory via the "bands" parameter) — keying
        // by the sidecar inventory would mislabel coefficients.
        const int bandIndex = [&] {
            for ( int i = 0; i < stackedBands.size(); ++i )
            {
                if ( stackedBands[i].compare( QString::fromStdString( calibration.band ),
                                              Qt::CaseInsensitive ) == 0 )
                    return i + 1;
            }
            return 0;
        }();
        if ( bandIndex <= 0 || bandIndex > GDALGetRasterCount( dataset ) )
            continue;
        GDALRasterBandH band = GDALGetRasterBand( dataset, bandIndex );
        if ( calibration.hasGain )
            GDALSetMetadataItem( band,
                                 QStringLiteral( "SICNU_CALIB_GAIN_%1" )
                                   .arg( QString::fromStdString( calibration.band ) )
                                   .toUtf8()
                                   .constData(),
                                 QByteArray::number( calibration.gain, 'g', 10 ).constData(),
                                 nullptr );
        if ( calibration.hasBias )
            GDALSetMetadataItem( band,
                                 QStringLiteral( "SICNU_CALIB_BIAS_%1" )
                                   .arg( QString::fromStdString( calibration.band ) )
                                   .toUtf8()
                                   .constData(),
                                 QByteArray::number( calibration.bias, 'g', 10 ).constData(),
                                 nullptr );
    }
    GDALClose( dataset );

    // L1 pixels are digital numbers; the state was decided by the parser
    // (only a declared/derived L1 level is stamped) — never labelled here
    // unconditionally (change-detection comparability checks rely on it).
    if ( metadata.radiometricState == SatelliteProducts::kRadiometricStateDigitalNumber )
    {
        QString err;
        if ( !SatelliteProducts::setRadiometricState( outputPath,
                                                      SatelliteProducts::kRadiometricStateDigitalNumber,
                                                      &err ) )
        {
            if ( errorMessage )
                *errorMessage = err;
            return false;
        }
    }
    return true;
}

/// JSON summary of sidecar fields that were NOT declared (absence is
/// absence — reported, never defaulted; ADR 0146 contract).
inline Json::Value cnMissingDeclaredFields( const sicnu::geo::ProductMetadata &metadata )
{
    Json::Value missing( Json::arrayValue );
    if ( metadata.acquisitionTime.empty() )
        missing.append( "acquisition_time" );
    if ( !metadata.hasCloudCover )
        missing.append( "cloud_cover" );
    if ( !metadata.hasResolution )
        missing.append( "resolution_m" );
    if ( !metadata.hasSunElevation )
        missing.append( "sun_elevation_deg" );
    if ( !metadata.hasSunAzimuth )
        missing.append( "sun_azimuth_deg" );
    if ( metadata.orbitId.empty() )
        missing.append( "orbit_id" );
    if ( metadata.bandCalibration.empty() )
        missing.append( "band_calibration" );
    if ( metadata.declaredBandIds.empty() )
        missing.append( "band_inventory" );
    return missing;
}

} // namespace sicnu::operators::rs
