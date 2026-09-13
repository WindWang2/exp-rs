// src/operators/rs/rs_spectral_reference_input.h — shared spectral reference seam
#pragma once

#include "processing/algorithms/spectral_wavelength.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <json/json.h>

#include <QString>
#include <QStringList>

#include <vector>

/// The single resolution path for spectral reference inputs across the
/// spectral operators: inline arrays (legacy), spectral-table artifacts and
/// spectral libraries all land here, so wavelength reconciliation, provenance
/// echo and typed refusals can never drift between operators.
///
/// Parameter contract per operator (exactly one source may be given):
///   - <inlineKey>  ("refs" / "endmembers" / "target"): legacy inline array,
///     width must equal the selected input band count;
///   - <refKey>     ("refsRef" / "endmembersRef" / "targetRef"): path to an
///     `exp-rs:spectral-table` artifact or a spectral library JSON;
///   - "libraryPath" (+ optional "libraryMaterials" string array): a library
///     JSON with a material filter applied before use.
/// Supplying zero or more than one source is a typed InvalidParameter.
///
/// Wavelength reconciliation (both sides = the input raster's per-band
/// WAVELENGTH metadata and the reference's wavelength grid):
///   - both sides present -> reference rows are resampled onto the input grid
///     (Gaussian SRF when both carry FWHM, linear otherwise); disjoint ranges
///     or out-of-range target bands are typed refusals;
///   - either side missing wavelengths -> row width must equal the input band
///     count; mismatches refuse naming both widths and which side lacks the
///     wavelength axis.
namespace sicnu::operators::rs {

/// A validated, nanometer-normalized wavelength grid plus presence flag.
struct RasterWavelengthGrid
{
    bool present = false;
    SpectralWavelength::Grid grid;

    /// Reads per-band WAVELENGTH (+ optional WAVELENGTH_UNITS, FWHM,
    /// FWHM_UNITS) metadata for @p bands. All bands must agree on presence —
    /// a partial grid is a typed refusal (returned via @p error).
    static RasterWavelengthGrid read( const GdalDatasetWrapper &ds,
                                      const std::vector<int> &bands,
                                      QString *error );
};

/// The resolved, input-matched reference spectra.
struct ResolvedSpectralReference
{
    std::vector<float> flat;      ///< row-major count x width
    int count = 0;                ///< number of spectra
    int width = 0;                ///< spectral width == selected input band count
    QStringList labels;           ///< per-spectrum labels (table labels / library names)
    QStringList materials;        ///< per-spectrum materials when known
    QString sourceDescription;    ///< provenance echo for result payloads/logs
    QString license;              ///< when the source declares one (uniform)
    QString citation;
    bool synthetic = false;       ///< true when every source spectrum is synthetic
    bool resampled = false;       ///< true when wavelength reconciliation resampled

    /// Strict single-spectrum view for target-style consumers.
    const float *single() const;
};

/**
 * Resolve @p inlineKey / @p refKey / libraryPath params against the input
 * raster. @p inputGrid must come from RasterWavelengthGrid::read for the same
 * @p bands subset. Throws RSOperatorError (typed) on every refusal.
 */
ResolvedSpectralReference resolveSpectralReference(
    const Json::Value &params,
    const char *inlineKey,
    const char *refKey,
    const GdalDatasetWrapper &input,
    const std::vector<int> &bands,
    const RasterWavelengthGrid &inputGrid );

/// Schema fragment describing the reference-input parameter family, shared by
/// every consumer operator so the documented contract stays identical.
/// Returns the properties to merge into the operator schema (inlineKey/refKey
/// descriptions + libraryPath/libraryMaterials).
Json::Value referenceInputSchemaProps( const char *inlineKey, const char *refKey,
                                       const char *inlineDescription );

} // namespace sicnu::operators::rs
