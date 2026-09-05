// src/processing/algorithms/sar/sar_metadata.h
// SAR metadata conventions + radiometric domain math (Platform 3.0, goal §6).
//
// Numeric-domain contract for every SAR kernel in this module:
//   * linear power values are ≥ 0 (sigma0/gamma0/beta0 in linear power);
//     nonpositive inputs to dB conversion follow the per-kernel NoData policy.
//   * dB values are 10·log10(power) (NOT 20·log10 — the convention applies to
//     power, never amplitude, and kernels never re-scale inputs silently).
// Every SAR operator writes SICNU_MODALITY / SICNU_SAR_CALIBRATION /
// SICNU_SAR_DOMAIN / SICNU_POLARIZATIONS on its outputs so products re-ingest
// with correct observation contracts (spatiotemporal_contracts.h).
#pragma once

#include <QString>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

/// Dataset metadata keys written/read by SAR operators.
inline const char *kModalityKey = "SICNU_MODALITY";
inline const char *kSensorKey = "SICNU_SENSOR";
inline const char *kPolarizationsKey = "SICNU_POLARIZATIONS";
inline const char *kCalibrationKey = "SICNU_SAR_CALIBRATION"; // sigma0|gamma0|beta0|dn
inline const char *kDomainKey = "SICNU_SAR_DOMAIN";           // linear_power|db
inline const char *kIncidenceKey = "SICNU_SAR_INCIDENCE_DEG"; // constant incidence angle
inline const char *kHeadingKey = "SICNU_SAR_HEADING_DEG";     // platform heading (look azimuth)

/// Radiometric states specific to SAR (the optical vocabulary lives in
/// satellite_products.h); stored in the shared SICNU_RADIOMETRIC_STATE key.
inline const char *kRadiometricStateKey = "SICNU_RADIOMETRIC_STATE";

bool isSarRadiometricState( const QString &state );

/// Reads a dataset-level metadata item (default domain, empty when absent).
QString datasetMeta( const GdalDatasetWrapper &ds, const char *key );

/// 10·log10(power). Callers must guarantee power > 0 (guard before calling).
double linearToDb( double power );
/// 10^(db/10).
double dbToLinear( double db );

/// Normalizes a calibration token ("SIGMA0" → "sigma0"); "" when unknown.
QString normalizeCalibration( const QString &token );

/// Writes the standard SAR output metadata block onto a GDAL dataset handle
/// (void* = GDALDatasetH). Never throws.
void writeSarDatasetMetadata( void *datasetHandle,
                              const QString &calibration,
                              const QString &domain,
                              const QString &polarizations,
                              const QString &sensor,
                              double incidenceDeg,
                              double headingDeg );

/// Same block, applied through GdalStreamingOutput's metadata seam (keys are
/// written exactly as the dataset-handle version). Never throws.
void writeSarOutputMetadata( GdalStreamingOutput &output,
                             const QString &calibration,
                             const QString &domain,
                             const QString &polarizations,
                             const QString &sensor,
                             double incidenceDeg,
                             double headingDeg );

/// Reads the SAR calibration/domain declarations from a dataset; both default
/// to "" when undeclared.
QString readCalibration( const GdalDatasetWrapper &ds );
QString readDomain( const GdalDatasetWrapper &ds );

} // namespace sicnu::sar
