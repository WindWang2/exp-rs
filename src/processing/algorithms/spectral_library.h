// src/processing/algorithms/spectral_library.h — spectral library domain
#pragma once

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

        bool operator==( const Entry &other ) const
        {
            return name == other.name && material == other.material
                   && source == other.source && spectrum == other.spectrum
                   && wavelengths == other.wavelengths;
        }
    };

    /// An ordered collection of spectra.
    struct Library
    {
        QVector<Entry> entries;

        /// Serialize to a JSON object (band count / wavelengths stored once,
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
    };
} // namespace SpectralLibrary
