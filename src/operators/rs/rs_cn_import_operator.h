/***************************************************************************
 * rs_cn_import_operator.h — shared machinery behind rs:gaofen_import /
 * rs:zy3_import / rs:hj_import (ADR 0146).
 *
 * Pipeline per operator: identify (diagnosable refusal for CN names outside
 * the adapted set) → read the CRESDA sidecar metadata → map declared bands
 * onto ADR 0065 roles via data/products/band_roles → build a
 * SatelliteProducts::ProductInfo → stack through the existing
 * stackToGeoTiff (windowed, fail-closed) → stamp CN dataset/band metadata
 * (sun geometry, calibration, radiometric state) onto the output GeoTIFF.
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

/// Resolved CN product for import.
struct CnImportProduct
{
    sicnu::geo::CnProductIdentity identity;
    sicnu::geo::ProductMetadata metadata;
    sicnu::geo::CnBandRoleTable bandTable;
    std::string sensorKey;
    QString xmlPath;
    QString tiffPath;
    /// Declared band ids in sidecar order = TIFF band order (1-based index is
    /// the stacking sourceBand).
    QStringList bandNames;
};

/// Identify + read a CN product from any accepted input shape (sidecar XML,
/// image TIFF, or product directory). Throws RSOperatorError(InvalidInputData)
/// with the identity diagnosis for unsupported CN families and
/// FileNotFound/InvalidInputData when the sidecar/image cannot be resolved.
inline CnImportProduct resolveCnImportProduct( const std::string &input,
                                               sicnu::geo::ProductKind expectedKind,
                                               const char *expectedKindName )
{
    using namespace sicnu::geo;
    CnImportProduct product;

    product.identity = cnIdentifyProduct( input );
    if ( !product.identity.supported )
    {
        Json::Value details;
        details["input"] = input;
        if ( !product.identity.reason.empty() )
            details["reason"] = product.identity.reason;
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               product.identity.reason.empty()
                                 ? "Input does not name a supported Chinese satellite product"
                                 : product.identity.reason,
                               details );
    }
    if ( expectedKindName && product.identity.kindName != expectedKindName )
    {
        Json::Value details;
        details["input"] = input;
        details["expectedFamily"] = expectedKindName;
        details["actualFamily"] = product.identity.kindName;
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "Input belongs to a different CN product family: " ) +
                                 product.identity.kindName,
                               details );
    }
    ( void )expectedKind;

    try
    {
        product.metadata = readCnProductMetadata( input, product.identity );
    }
    catch ( const GeoError &error )
    {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "CN product metadata unavailable: " ) + error.what() );
    }

    product.sensorKey = cnSensorKey( product.identity, product.metadata );
    try
    {
        product.bandTable = cnBandRoleTable( product.sensorKey );
    }
    catch ( const GeoError &error )
    {
        // Fail-closed: no band-role table, no promised role semantics.
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "Band-role table unavailable: " ) + error.what() );
    }

    const std::string xml = cnLocateSidecarXml( input );
    const std::string tiff = cnLocateImageTiff( input, xml );
    if ( xml.empty() )
        throw RSOperatorError( ErrorCode::FileNotFound, "No L1A sidecar XML found for input" );
    if ( tiff.empty() )
        throw RSOperatorError( ErrorCode::FileNotFound, "No measurement TIFF found for input" );
    product.xmlPath = QString::fromStdString( xml );
    product.tiffPath = QString::fromStdString( tiff );

    if ( product.metadata.declaredBandIds.empty() )
    {
        // No declared inventory: use the table's band list (the sensor layout
        // is documented even when the sidecar omits BandID entries) and say
        // so in the result.
        for ( const sicnu::geo::CnBandSpec &spec : product.bandTable.bands )
            product.bandNames << QString::fromStdString( spec.band );
    }
    else
    {
        for ( const std::string &band : product.metadata.declaredBandIds )
            product.bandNames << QString::fromStdString( band );
    }
    return product;
}

/// Builds the stacking input from the resolved product. Wavelengths/roles
/// come from the band-role table; sourceBand follows the declared band order.
inline SatelliteProducts::ProductInfo buildCnProductInfo( const CnImportProduct &product )
{
    SatelliteProducts::ProductInfo info;
    info.productId = QString::fromStdString( product.metadata.productId );
    info.spacecraft = QString::fromStdString( product.metadata.platform );
    info.processingLevel = QString::fromStdString( product.metadata.processingLevel );
    info.acquisitionDate = QString::fromStdString( product.metadata.acquisitionTime );
    info.attributes[QStringLiteral( "SICNU_SENSOR" )] =
      QString::fromStdString( product.metadata.sensor );
    if ( product.metadata.hasSunElevation )
        info.attributes[QStringLiteral( "SICNU_SUN_ELEVATION_DEG" )] =
          QString::number( product.metadata.sunElevationDeg, 'f', 4 );
    if ( product.metadata.hasSunAzimuth )
        info.attributes[QStringLiteral( "SICNU_SUN_AZIMUTH_DEG" )] =
          QString::number( product.metadata.sunAzimuthDeg, 'f', 4 );

    for ( int i = 0; i < product.bandNames.size(); ++i )
    {
        SatelliteProducts::BandFile band;
        band.path = product.tiffPath;
        band.name = product.bandNames[i];
        band.sourceBand = i + 1;
        const std::string bandStd = band.name.toStdString();
        for ( const sicnu::geo::CnBandSpec &spec : product.bandTable.bands )
        {
            if ( QString::fromStdString( spec.band ).compare( band.name, Qt::CaseInsensitive ) == 0 )
            {
                if ( spec.hasWavelength )
                    band.wavelengthNm = static_cast<int>( spec.wavelengthNm + 0.5 );
                band.role = sicnu::data::bandRoleFromString( QString::fromStdString( spec.role ) );
                break;
            }
        }
        info.bands.append( band );
    }
    return info;
}

/// Stamps the CN import metadata onto the stacked GeoTIFF. Dataset level:
/// product family/kind, sensor + mode, sun geometry, radiometric state.
/// Band level: declared gain/bias (verbatim, per band) beside the role and
/// wavelength items stackToGeoTiff already wrote.
inline bool writeCnImportMetadata( const QString &outputPath,
                                   const sicnu::geo::ProductMetadata &metadata,
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
        GDALSetMetadataItem( dataset, "SICNU_SUN_ELEVATION_DEG",
                             QByteArray::number( metadata.sunElevationDeg, 'f', 4 ).constData(),
                             nullptr );
    if ( metadata.hasSunAzimuth )
        GDALSetMetadataItem( dataset, "SICNU_SUN_AZIMUTH_DEG",
                             QByteArray::number( metadata.sunAzimuthDeg, 'f', 4 ).constData(),
                             nullptr );

    for ( const sicnu::geo::BandCalibration &calibration : metadata.bandCalibration )
    {
        const int bandIndex = [&] {
            for ( int i = 0; i < metadata.declaredBandIds.size(); ++i )
            {
                if ( QString::fromStdString( metadata.declaredBandIds[i] )
                       .compare( QString::fromStdString( calibration.band ), Qt::CaseInsensitive ) == 0 )
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

    // L1A pixels are digital numbers; declared-state consumers rely on this
    // label (change detection comparability checks).
    QString err;
    if ( !SatelliteProducts::setRadiometricState( outputPath,
                                                  SatelliteProducts::kRadiometricStateDigitalNumber,
                                                  &err ) )
    {
        if ( errorMessage )
            *errorMessage = err;
        return false;
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
    return missing;
}

/// Shared result body for the three CN import operators.
inline Json::Value cnImportResult( const CnImportProduct &product,
                                   const std::string &outputPath,
                                   const QStringList &stackedBands )
{
    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["productId"] = product.metadata.productId;
    result["productKind"] = product.identity.kindName;
    result["satellite"] = product.metadata.platform.empty() ? product.identity.satellite
                                                            : product.metadata.platform;
    result["sensor"] = product.metadata.sensor;
    result["sensorMode"] = product.metadata.sensorMode.empty() ? product.identity.sensorMode
                                                               : product.metadata.sensorMode;
    result["sensorKey"] = product.sensorKey;
    result["processingLevel"] = product.metadata.processingLevel;
    result["acquisitionTime"] = product.metadata.acquisitionTime;
    result["radiometricState"] = product.metadata.radiometricState;
    result["bandCount"] = static_cast<Json::Int64>( stackedBands.size() );
    Json::Value bands( Json::arrayValue );
    Json::Value roles( Json::arrayValue );
    for ( const QString &band : stackedBands )
    {
        bands.append( band.toStdString() );
        std::string role;
        for ( const sicnu::geo::CnBandSpec &spec : product.bandTable.bands )
        {
            if ( QString::fromStdString( spec.band ).compare( band, Qt::CaseInsensitive ) == 0 )
            {
                role = spec.role;
                break;
            }
        }
        roles.append( role );
    }
    result["bands"] = bands;
    result["bandRoles"] = roles;
    Json::Value declared( Json::objectValue );
    declared["sunElevationDeg"] = product.metadata.hasSunElevation;
    declared["sunAzimuthDeg"] = product.metadata.hasSunAzimuth;
    declared["calibration"] = !product.metadata.bandCalibration.empty();
    declared["cloudCover"] = product.metadata.hasCloudCover;
    declared["orbitId"] = !product.metadata.orbitId.empty();
    result["declared"] = declared;
    result["missingDeclaredFields"] = cnMissingDeclaredFields( product.metadata );
    return result;
}

} // namespace sicnu::operators::rs
