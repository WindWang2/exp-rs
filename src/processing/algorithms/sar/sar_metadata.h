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

#include <vector>

class GdalDatasetWrapper;
class GdalStreamingOutput;

namespace sicnu::sar
{

/// Dataset metadata keys written/read by SAR operators.
inline const char *kModalityKey = "SICNU_MODALITY";
inline const char *kSensorKey = "SICNU_SENSOR";
inline const char *kPolarizationsKey = "SICNU_POLARIZATIONS";
inline const char *kCalibrationKey = "SICNU_SAR_CALIBRATION"; // sigma0|gamma0|beta0|dn
// Accepted input spellings (normalizeCalibration also maps sigma_naught/sigma,
// gamma, beta, digital_number onto the canonical tokens above); only the
// canonical tokens are written.
inline const char *kDomainKey = "SICNU_SAR_DOMAIN";           // linear_power|db
/// #1165: assumption provenance written by the terrain/geocode family when
/// a legacy undeclared input was processed under the documented
/// sigma0/linear assumption — consumed by downstream family guards.
inline const char *kRadiometricStateAssumedKey = "SICNU_SAR_STATE_ASSUMED";
inline const char *kDomainAssumedKey = "SICNU_SAR_DOMAIN_ASSUMED";
inline const char *kIncidenceKey = "SICNU_SAR_INCIDENCE_DEG"; // constant incidence angle
inline const char *kHeadingKey = "SICNU_SAR_HEADING_DEG";     // platform flight heading
// #785: the antenna look azimuth (boresight ground azimuth) is orthogonal to
// the heading; it is the geometric parameter the terrain kernels consume.
inline const char *kLookAzimuthKey = "SICNU_SAR_LOOK_AZIMUTH_DEG";

/// Radiometric states specific to SAR (the optical vocabulary lives in
/// satellite_products.h); stored in the shared SICNU_RADIOMETRIC_STATE key.
inline const char *kRadiometricStateKey = "SICNU_RADIOMETRIC_STATE";

/// Declared per-row calibration LUT sidecar (Radiometric State 13.0): a
/// plain-text file with exactly one finite, positive calibration constant per
/// input row, resolved relative to the declaring raster. Consumed by
/// rs:sar_calibrate; no interpolation is applied.
inline const char *kCalibrationLutKey = "SICNU_SAR_CALIBRATION_LUT";

/// Derived (non-backscatter) SAR product states. They are written like every
/// other SAR state token so the fail-closed guards reject them, but they are
/// NOT calibration states: normalizeCalibration() maps them to "".
/// rs:sar_ratio writes kDerivedPairMetricState, rs:sar_texture writes
/// kDerivedTextureState — derived products never claim a backscatter
/// calibration.
inline const char *kDerivedPairMetricState = "sar_pair_metric";
inline const char *kDerivedTextureState = "sar_texture";

bool isSarRadiometricState( const QString &state );

/// True for the derived SAR product states (pair metric / texture). Derived
/// states are declared outputs of derived-product operators; they are never
/// lawful inputs to calibration or backscatter conversion.
bool isSarDerivedState( const QString &state );

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

/// Raw declared SICNU_SAR_CALIBRATION token (trimmed, lowercased); "" when
/// undeclared. Unlike readCalibration(), unrecognized tokens are returned
/// verbatim so operator seams can fail closed on them instead of silently
/// treating an unreadable declared contract as DN.
QString declaredCalibrationToken( const GdalDatasetWrapper &ds );

/// Declared SAR state read with conflict detection (Radiometric State 13.0).
/// @a token is the effective declared token (SICNU_SAR_CALIBRATION, falling
/// back to SICNU_RADIOMETRIC_STATE); @a conflict is true when both keys are
/// present and disagree — a conflicted declaration is never interpreted.
/// NOTE: the shared SICNU_RADIOMETRIC_STATE key also carries the optical
/// vocabulary (exp_radiometric::RadiometricState, uppercase). SAR products
/// never mix the two; the conflict rule assumes a SAR-only vocabulary on both
/// keys and refuses anything that disagrees.
struct SarStateRead
{
    QString calibration; ///< raw SICNU_SAR_CALIBRATION token (trimmed/lowered)
    QString state;       ///< raw SICNU_RADIOMETRIC_STATE token (trimmed/lowered)
    QString token;       ///< effective declared token; "" when undeclared
    bool conflict = false;
};
SarStateRead readDeclaredSarState( const GdalDatasetWrapper &ds );

/// The declared token when it is a recognized SAR state — a canonical
/// calibration token (normalized) or a derived state; "" for undeclared,
/// conflicting or unrecognized declarations.
QString recognizedSarState( const GdalDatasetWrapper &ds );

/// Outcome of checkDeclaredState().
enum class SarStateCheck
{
    Ok,            ///< declared (and normalized to) the required state
    OkUndeclared,  ///< nothing declared: legacy path, caller warns
    Refused        ///< declared conflicting/derived/unknown/wrong: @a reason set
};

/// Validates that a dataset declares @p required (or nothing) before an
/// algorithm that is only lawful for that state runs. Conflicting, derived,
/// unrecognized and mismatched declarations are refusals with a reason.
SarStateCheck checkDeclaredState( const GdalDatasetWrapper &ds, const QString &required,
                                  QString *reason );

/// Parses a per-row calibration LUT sidecar: one finite, positive ASCII
/// number per line; @a expectedRows must equal the line count exactly (no
/// interpolation, no resampling — a mismatch is an error, not a guess).
/// Returns false with @a error set on any violation.
bool parseCalibrationLut( const QString &path, int expectedRows,
                          std::vector<double> *values, QString *error );

} // namespace sicnu::sar
