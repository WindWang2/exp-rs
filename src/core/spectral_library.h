// src/core/spectral_library.h — D13 high-precision spectral library retriever
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstddef>
#include <vector>

namespace exp_spectral
{
    /// One reference spectrum of the library with its wavelength grid.
    struct SpectralLibraryEntry
    {
        QString id;
        QString name;
        QString materialClass;
        QString source;
        std::vector<float> spectrum;
        std::vector<float> wavelengthsNm;
        std::vector<float> fwhmNm;
    };

    /// One match candidate: identity plus three independent similarity
    /// measures against the query spectrum.
    struct SpectralMatchCandidate
    {
        QString id;
        QString name;
        QString materialClass;
        double spectralAngleRad = 0.0;  ///< Spectral Angle Mapper theta in [0, pi/2]
        double correlation = 0.0;       ///< Pearson correlation coefficient [-1, 1]
        double euclideanDistance = 0.0; ///< L2 Euclidean distance
    };

    /// Standard spectral library: JSON-persisted reference spectra with SAM
    /// retrieval and Gaussian-SRF sensor resampling.
    ///
    /// SAM measures spectral SHAPE (robust to multiplicative illumination);
    /// zero-norm spectra are scored at the maximum angle pi/2 (deterministic
    /// worst match, never NaN-poisoned ordering).
    class SpectralLibrary
    {
      public:
        /// Parses a library object:
        /// { "id": str, "entries": [ { "id", "name", "materialClass",
        ///   "source", "spectrum": [float...], "wavelengthsNm": [...],
        ///   "fwhmNm": [...] } ] }. All entry arrays must share one length.
        /// Returns an empty library and leaves @p errorMessage set on a
        /// malformed shape.
        static SpectralLibrary fromJson( const QJsonObject &root, QString *errorMessage = nullptr );
        QJsonObject toJson() const;

        bool loadFromFile( const QString &filePath, QString *errorMsg = nullptr );
        bool saveToFile( const QString &filePath, QString *errorMsg = nullptr ) const;

        /// Ranks entries whose band count equals @p bandCount by SAM angle,
        /// ascending, keeping only theta <= @p maxAngleRad, truncated to
        /// @p topK. Ties break by correlation (descending), then id.
        std::vector<SpectralMatchCandidate> matchSpectrum( const float *querySpectrum, size_t bandCount,
                                                           size_t topK = 5, double maxAngleRad = 0.5 ) const;

        /// Resamples every entry onto @p targetWavelengths with Gaussian SRF
        /// of width @p targetFwhm (sigma = FWHM / 2.35482), integrating over
        /// the ±3σ window (bisected via std::lower_bound). Entries without a
        /// wavelength grid, and target bands whose window falls outside an
        /// entry's coverage, fail the call (never NaN fills). Constant
        /// spectra are preserved exactly (partition-of-unity weights).
        bool resampleToSensor( const std::vector<float> &targetWavelengths,
                               const std::vector<float> &targetFwhm,
                               SpectralLibrary *outResampled ) const;

        const std::vector<SpectralLibraryEntry> &entries() const noexcept { return m_entries; }
        size_t size() const noexcept { return m_entries.size(); }

        /// Test/fixture seam: appends one entry after validating its shape.
        bool addEntry( const SpectralLibraryEntry &entry, QString *errorMessage = nullptr );

      private:
        std::vector<SpectralLibraryEntry> m_entries;
    };

} // namespace exp_spectral
