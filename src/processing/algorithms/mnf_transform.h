// src/processing/algorithms/mnf_transform.h — complete MNF chain (forward,
// transform model, inverse) for the Hyperspectral Platform 10.0 track.
#pragma once

#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

/// Minimum Noise Fraction with an exposed transform: the same noise
/// convention as the legacy ImageEnhancement kernel (horizontal shift
/// differences, covariance/2, ADR 0075) but in double precision, streaming
/// (two statistics passes + one application pass, O(rows·bands) memory), and
/// with the transform *model* available for inverse transforms and
/// reconstruction-error reporting.
///
/// Chain:
///   forward:  y_i = F_i · (x − μ)          (components in SNR-descending order)
///   inverse:  x  = μ + Σ_{j∈S} C[:,j] y_j  (S = selected component set)
/// with F the forward basis rows and C the inverse-basis matrix; using all B
/// components inverts the transform exactly (up to eigensolver tolerance),
/// any subset yields the optimal least-squares-free projection remainder and
/// its RMSE is reported via reconstructionRmse().
///
/// Guarded by construction: a numerically singular noise covariance or a
/// non-finite statistic refuses with a typed message (the legacy kernel
/// clamped small noise eigenvalues to 1e-9 — a silent pseudo-inverse; this
/// kernel refuses instead).
namespace MnfTransform
{
    /// Transform-model artifact band bound: the B×B bases dominate memory
    /// (~24 B × B² bytes serialized); 1024 bands ≈ 24 MiB of JSON.
    inline constexpr int kMaxBands = 1024;

    inline const QString kKind = QStringLiteral( "exp-rs:mnf-transform" );
    inline const int kFormatVersion = 1;

    struct Model
    {
        int bandCount = 0;
        std::vector<double> mean;          ///< B
        /// B×B row-major; row i is the forward axis of SNR-ranked component i.
        std::vector<double> forwardBasis;
        /// B×B row-major; COLUMN j is component j's contribution in band space.
        std::vector<double> inverseBasis;
        std::vector<double> snr;               ///< B, whitened-covariance eigenvalues, descending
        std::vector<double> noiseEigenvalues;  ///< B, diagnostic (unsorted)
        std::vector<float> wavelengthsNm;      ///< optional, copied onto inverse outputs
    };

    /// Streaming statistics feeder. Feed full image rows (BIP, width×bands)
    /// in raster order — twice, once per pass:
    ///   pass 1 (mean pass):  addRow() → finalizeMean()
    ///   pass 2 (cov passes): addRow() → finalizeCovariances()
    /// Noise differences are formed between horizontally adjacent pixels
    /// inside a row; row ends are never differenced (rowAware convention of
    /// the legacy kernel, #700).
    class RowFeeder
    {
    public:
        explicit RowFeeder( int bands );

        /// @p validMask (width entries, non-zero = usable pixel) excludes
        /// NoData/non-finite pixels from every statistic; noise differences
        /// are only formed between two adjacent valid pixels. nullptr means
        /// every pixel is valid.
        void addRow( const float *bipRow, int width, const uint8_t *validMask = nullptr );

        /// Freeze pass 1. Resets the row carry state; call before pass 2.
        void finalizeMean();

        /// Freeze pass 2 (requires finalizeMean first). After this the
        /// accessors below are valid.
        void finalizeCovariances();

        uint64_t sampleCount() const { return m_samples; }
        uint64_t noiseSampleCount() const { return m_noiseSamples; }

        const std::vector<double> &mean() const { return m_mean; }
        /// Signal covariance Σs (B×B row-major, sample variance).
        const std::vector<double> &signalCovariance() const { return m_signalCov; }
        /// Noise covariance Σn = cov(shift differences)/2 (B×B row-major).
        const std::vector<double> &noiseCovariance() const { return m_noiseCov; }

    private:
        void accumulateSecondMoments( const float *bipRow, int width, const uint8_t *validMask );

        int m_bands;
        bool m_meanFinalized = false;
        bool m_covFinalized = false;
        uint64_t m_samples = 0;
        uint64_t m_noiseSamples = 0;
        std::vector<double> m_sum;
        std::vector<double> m_mean;
        std::vector<double> m_signalCov;
        std::vector<double> m_noiseCov;
        std::vector<double> m_ddSum;   ///< Σ d dᵀ upper triangle
        std::vector<double> m_diffSum; ///< Σ d
    };

    /**
     * Fit the transform model from accumulated statistics. Fails (false +
     * message) on a singular noise covariance (eigenvalue below
     * max(eig)·1e-10), insufficient samples, or non-finite statistics.
     */
    bool fit( const RowFeeder &stats, Model *out, QString *errorMessage = nullptr );

    /// Forward one pixel spectrum (B values) to @p components outputs.
    void forward( const Model &model, const float *spectrum, double *out, int components );

    /// Inverse-transform one MNF-space spectrum using the selected component
    /// indices (ascending; empty = all components). Output is B band values.
    void inverse( const Model &model, const double *y, const std::vector<int> &components,
                  double *spectrumOut );

    /// RMSE over bands between the full inverse and the subset inverse for a
    /// pixel spectrum: ||Σ_{j∉S} C[:,j] y_j|| / sqrt(B). This is the part of
    /// the reconstruction the dropped components would have contributed.
    double reconstructionRmse( const Model &model, const double *y,
                               const std::vector<int> &components );

    /// Serialize the model into the kind/versioned JSON artifact (kind
    /// exp-rs:mnf-transform, version 1, digest over the canonical basis text,
    /// provenance block).
    void modelToJson( const Model &model, const QString &sourceInput,
                      const QString &parametersJson, qint64 createdAtMs,
                      QJsonObject &root );

    /// Parse + digest-verify an artifact JSON into a Model. False with a
    /// message on any shape/digest violation.
    bool modelFromJson( const QJsonObject &root, Model *out, QString *errorMessage );
} // namespace MnfTransform
