// src/processing/algorithms/endmember_analysis.h — endmember set analysis
// (clustering / redundancy reduction, spectral-angle matrix, sensor
// projection) (Spectral Intelligence 11.0, work package D).
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Analysis over a set of extracted endmembers (e.g. PPI output, a spectral
/// table, or library entries). The kernels here are pure and deterministic;
/// provenance/license custody stays with the artifact layer
/// (SpectralTable): operators that consume these kernels MUST copy the
/// input provenance into the derived artifact — the operator seam enforces
/// the same digest/license rules as the 10.0 table contract.
namespace EndmemberAnalysis
{
    /// Redundancy reduction by average-link agglomerative clustering on the
    /// pairwise spectral-angle distance (DECISIONS D5): the two closest
    /// clusters merge while their average angle stays below the threshold;
    /// the surviving representative of a cluster is its member with the
    /// highest PPI count (ties: lowest source index — deterministic).
    struct ReduceConfig
    {
        double mergeAngleDegrees = 2.0; ///< cluster merge threshold (>= 0)
    };

    struct ReduceResult
    {
        std::vector<float> endmembers;  ///< representatives, endmember-major (k * bands)
        std::vector<int> representativeOf;   ///< cluster -> source endmember index
        std::vector<int> clusterOf;          ///< source endmember -> cluster id
        std::vector<int> clusterSizes;       ///< cluster -> member count
        std::vector<double> mergeAngles;     ///< merge distance at each merge step
    };

    /**
     * Reduce an endmember set to non-redundant representatives.
     *
     * @param ppiCounts optional per-endmember purity ranking (e.g. PPI
     *        counts); when absent every endmember ties and the lowest index
     *        wins the representative role. Size must equal @p nEndmembers.
     * @return false for structurally invalid arguments (null buffers,
     *         non-positive counts/bands, non-finite endmember values,
     *         zero-norm endmembers, ppiCounts size mismatch) — a degenerate
     *         input set is an error, not an empty result. With
     *         mergeAngleDegrees == 0 the set passes through unchanged
     *         (every endmember its own cluster).
     */
    bool reduceEndmembers( const float *endmembers, int nEndmembers, int bands,
                           const ReduceConfig &config,
                           const std::vector<int> *ppiCounts,
                           ReduceResult *result,
                           QString *errorMessage = nullptr );

    /**
     * Pairwise spectral-angle matrix (radians), row-major, symmetric, zero
     * diagonal. Computed with SpectralClassification::spectralAngle — the
     * single SAM source of truth. All endmembers must be finite with
     * non-zero norm (a zero-norm atom has no defined angle to anything).
     */
    bool angleMatrix( const float *endmembers, int nEndmembers, int bands,
                      std::vector<double> *matrix,
                      QString *errorMessage = nullptr );

    struct ProjectionResult
    {
        std::vector<float> spectra;       ///< k * dstBands; NaN where a target
                                          ///< band has no source coverage
        std::vector<float> wavelengthsNm; ///< copy of the target centers
        std::vector<float> fwhmNm;        ///< copy of the target FWHMs (when provided)
        std::vector<uint8_t> fullyCovered; ///< per endmember: 1 when no NaN cell
        int fullyCoveredCount = 0;
    };

    /**
     * Project endmembers onto a sensor grid via Gaussian SRF resampling
     * (SpectralResampling::resampleSpectrumGaussian; falls back to linear
     * interpolation when @p dstFwhmNm is null). Source wavelengths are
     * mandatory — projecting without wavelength metadata would silently
     * pretend the dictionary is on the sensor grid, so the refusal is
     * typed. Endmembers outside the source range produce NaN cells and are
     * flagged (not errors); with @p requireFull the call refuses unless
     * every endmember is fully covered.
     */
    bool projectToSensor( const float *endmembers, int nEndmembers, int bands,
                          const float *srcWavelengthsNm,
                          const float *dstWavelengthsNm, const float *dstFwhmNm,
                          int dstBands, bool requireFull,
                          ProjectionResult *result,
                          QString *errorMessage = nullptr );

} // namespace EndmemberAnalysis
