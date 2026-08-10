// src/processing/algorithms/spectral_library.h — spectral library domain
#pragma once

#include "spectral_classification.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

/// Spectral library domain objects: named spectra with optional wavelength
/// grids and provenance labels, persisted as JSON. The `spectrum` shape
/// matches the `refs` / `endmembers` arrays consumed by rs:sam_classify and
/// rs:spectral_unmixing, so a library entry can feed those operators directly.
namespace SpectralLibrary
{
    /// One named spectrum of a library.
    struct Entry
    {
        QString name;                 ///< unique-ish display name
        QString material;             ///< material / class label (optional)
        QString source;               ///< provenance label (optional)
        std::vector<float> spectrum;  ///< band values (all entries in one library share a band count)
        std::vector<float> wavelengths; ///< band center wavelengths (nm), optional
        std::vector<float> fwhm;      ///< band FWHM (nm), optional

        bool operator==( const Entry &other ) const
        {
            return name == other.name && material == other.material
                   && source == other.source && spectrum == other.spectrum
                   && wavelengths == other.wavelengths
                   && fwhm == other.fwhm;
        }
    };

    /// An ordered collection of spectra.
    struct Library
    {
        QVector<Entry> entries;

        /// Serialize to a JSON object (band count / wavelengths / fwhm stored once,
        /// spectra as arrays).
        QJsonObject toJson() const;

        /// Parse from a JSON object; returns false with a message on invalid
        /// shape (missing name, non-array spectrum, inconsistent band counts).
        static bool fromJson( const QJsonObject &json, Library *out, QString *errorMessage );

        /// Write the library to @p path as JSON. Returns false with an error
        /// message when the file cannot be written.
        bool save( const QString &path, QString *errorMessage = nullptr ) const;

        /// Load a library from a JSON file. Returns false with an error
        /// message when the file is missing or malformed.
        static bool load( const QString &path, Library *out, QString *errorMessage = nullptr );

        /// Shared band count across entries, or 0 when empty or inconsistent.
        int bandCount() const;

        /// Shared wavelength grid, or empty when absent / inconsistent.
        std::vector<float> wavelengths() const;

        /// Shared FWHM grid, or empty when absent / inconsistent.
        std::vector<float> fwhm() const;
    };

    /// Result of matching a test spectrum against one library entry.
    struct MatchScore
    {
        int entryIndex = -1; ///< index into Library::entries
        QString name;        ///< entry name
        QString material;    ///< material / class label (may be empty)
        /// SAM angle in degrees; NaN when the angle is undefined (zero-norm or
        /// nodata-carrying spectrum).
        double angleDegrees = std::numeric_limits<double>::quiet_NaN();
        /// Spectral Information Divergence; NaN when undefined.
        double divergence = std::numeric_limits<double>::quiet_NaN();
        /// True when the test spectrum was wavelength-resampled onto this
        /// entry's grid before scoring (band counts differed but both sides
        /// carried wavelength metadata).
        bool resampled = false;
    };

    /**
     * Match @p spectrum against every library entry using the SAM angle and
     * SID kernels (spectral_classification.h), ranked by ascending SAM angle
     * (undefined angles sort last). Entries whose band count differs from the
     * spectrum are skipped — they cannot be compared. @p nodata is the
     * spectral nodata sentinel passed to the kernels.
     */
    std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                           const Library &library,
                                           float nodata = SpectralClassification::kNoDataSentinel );

    /**
     * Wavelength-aware variant: when an entry's band count differs from the
     * spectrum's, the spectrum is linearly resampled onto the entry's
     * wavelength grid (SpectralResampling::resampleSpectrum) before scoring,
     * provided BOTH sides carry wavelength metadata (strictly increasing,
     * non-empty). Entries that still cannot be compared (no wavelengths, or a
     * resample landing outside the source range) are skipped, as in the
     * non-wavelength variant. Resampled matches are marked MatchScore::resampled.
     */
    std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                           const std::vector<float> &spectrumWavelengths,
                                           const Library &library,
                                           float nodata = SpectralClassification::kNoDataSentinel );
} // namespace SpectralLibrary
