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
///
/// Format v2 (D12) additions are parsed leniently by fromJson so v1 files keep
/// loading: per-entry `id`, `subclass`, `license`, `citation`, `synthetic`,
/// `derivation`, `tags`, per-entry `wavelengths` / `fwhm` grids, a `reflectance`
/// alias for `spectrum`, and a root `id` naming the library itself. Strict
/// curation rules live in validateLibrary() / Library::loadValidated(); the
/// schema of record for curated data is data/spectral/library.schema.json.
namespace SpectralLibrary
{
    /// Material taxonomy of the built-in material-prior library
    /// (data/spectral/library.json). The schema of record carries the same
    /// enum; material knowledge itself lives in the data, never in C++.
    inline const QStringList kKnownMaterials = {
        QStringLiteral( "bare_rock" ),       QStringLiteral( "burned_area" ),
        QStringLiteral( "cloud" ),           QStringLiteral( "cropland" ),
        QStringLiteral( "ice" ),             QStringLiteral( "impervious_surface" ),
        QStringLiteral( "sand" ),            QStringLiteral( "shadow" ),
        QStringLiteral( "snow" ),            QStringLiteral( "soil" ),
        QStringLiteral( "vegetation" ),      QStringLiteral( "water" )
    };

    /// One named spectrum of a library.
    struct Entry
    {
        QString name;                 ///< unique-ish display name
        QString material;             ///< material / class label (optional)
        QString source;               ///< provenance label (optional)
        std::vector<float> spectrum;  ///< band values (all entries in one library share a band count)
        std::vector<float> wavelengths; ///< band center wavelengths (nm), optional
        std::vector<float> fwhm;      ///< band FWHM (nm), optional

        // Format v2 fields (all optional; curated libraries require them —
        // see validateLibrary()).
        QString id;                   ///< stable slug identity (falls back to name for v1 files)
        QString subclass;             ///< fine-grained label, e.g. "healthy canopy"
        QString license;              ///< license of THIS spectrum, e.g. "CC0-1.0"
        QString citation;             ///< how to credit the spectrum
        bool synthetic = false;       ///< true when the spectrum is physics-model derived
        QString derivation;           ///< required when synthetic: how it was computed
        QStringList tags;             ///< free-form markers, e.g. "resampled", "sensor:sentinel-2-msi"

        bool operator==( const Entry &other ) const
        {
            return name == other.name && material == other.material
                   && source == other.source && spectrum == other.spectrum
                   && wavelengths == other.wavelengths
                   && fwhm == other.fwhm
                   && id == other.id && subclass == other.subclass
                   && license == other.license && citation == other.citation
                   && synthetic == other.synthetic && derivation == other.derivation
                   && tags == other.tags;
        }
    };

    /// One sensor band: nominal center wavelength and FWHM (nm).
    struct SensorBand
    {
        QString name;        ///< band name, e.g. "B4" / "red"
        float wavelengthNm = 0.0f;
        float fwhmNm = 0.0f;
    };

    /// A sensor band grid used as a resampling target (see Library::resampleTo).
    /// Band tables are data (data/spectral/sensors.json), not compiled-in
    /// knowledge; nominal centers/FWHM approximate the official RSRs with the
    /// Gaussian SRF model of ADR 0079.
    struct SensorProfile
    {
        QString id;          ///< stable slug, e.g. "sentinel-2-msi"
        QString name;        ///< display name
        QVector<SensorBand> bands;

        bool isValid() const; ///< non-empty id/name, bands with positive finite centers and FWHM

        static bool fromJson( const QJsonObject &json, SensorProfile *out, QString *errorMessage );
        /// Loads every sensor from a registry file ({ "sensors": [...] }).
        static bool loadSensors( const QString &path, QVector<SensorProfile> *out,
                                 QString *errorMessage = nullptr );
        /// Loads one sensor by id from a registry file.
        static bool loadSensor( const QString &path, const QString &sensorId,
                                SensorProfile *out, QString *errorMessage = nullptr );
    };

    /// An ordered collection of spectra.
    struct Library
    {
        QVector<Entry> entries;
        QString id; ///< stable library identity (root "id"), e.g. for lab "library_id" references

        /// Serialize to a JSON object (band count / wavelengths / fwhm stored once,
        /// spectra as arrays). V2 fields are written when present.
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

        /**
         * Load a library from a JSON file and apply the strict curation rules
         * (validateLibrary). On any rule violation the load fails and
         * @p errorMessage names the offending entries — curated libraries
         * never load partially or with silently dropped entries.
         */
        static bool loadValidated( const QString &path, Library *out, QString *errorMessage = nullptr );

        /// Stable library identity accessor (empty for v1 files).
        QString libraryId() const { return id; }

        /// Shared band count across entries, or 0 when empty or inconsistent.
        int bandCount() const;

        /// Shared wavelength grid, or empty when absent / inconsistent.
        std::vector<float> wavelengths() const;

        /// Shared FWHM grid, or empty when absent / inconsistent.
        std::vector<float> fwhm() const;

        /// Distinct non-empty material labels, sorted; O(entries).
        QStringList materials() const;

        /// All entries labelled @p material, in library order; O(entries).
        QVector<Entry> byMaterial( const QString &material ) const;

        /**
         * Entries whose wavelength grid overlaps [minNm, maxNm], in library
         * order. Entries without a wavelength grid cannot be assessed and are
         * skipped; an inverted range yields an empty result.
         */
        QVector<Entry> byWavelengthRange( float minNm, float maxNm ) const;

        /**
         * Material prior summary as stable JSON for the knowledge/teaching
         * layers: material, entryCount, subclasses, entries, syntheticOnly,
         * wavelengthRangeNm and per-window reflectance statistics
         * (blue/green/red/redEdge/nir/swir1/swir2 — sensor-agnostic wavelength
         * windows; statistics are computed from the data, never hard-coded).
         * Unknown materials yield a well-formed summary with entryCount 0.
         */
        QJsonObject priorsFor( const QString &material ) const;

        /**
         * Resample every entry onto @p sensor's band grid using the Gaussian
         * SRF kernel of ADR 0079 (linear fallback for entries without FWHM).
         * Output entries keep identity and provenance, carry the sensor's
         * wavelength/FWHM grid and are tagged "resampled" and
         * "sensor:<id>"; the source library is untouched. Fails (naming the
         * entry) when an entry lacks a wavelength grid or a sensor band falls
         * outside the entry's source coverage (NaN output is an error, never
         * a silent fill).
         */
        bool resampleTo( const SensorProfile &sensor, Library *out,
                         QString *errorMessage = nullptr ) const;
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

    /**
     * Strict curation validation for spectral libraries (D12). Checks, per
     * entry: unique non-empty slug id, name, non-empty spectrum with all
     * values finite and in [0, 1], wavelength grid sized to the spectrum and
     * strictly increasing / finite, FWHM sized to the spectrum and finite
     * > 0, and complete provenance (`source` / `license` / `citation`
     * non-empty; `synthetic: true` requires a non-empty `derivation`).
     *
     * Every violation produces one human-readable issue that names the entry
     * (id, falling back to "#<index>" when the id itself is invalid);
     * validation never truncates to the first error. Returns true when
     * @p errors ends up empty. O(entries x bands), no library-wide expansion.
     */
    bool validateLibrary( const Library &library, QStringList *errors );
} // namespace SpectralLibrary
