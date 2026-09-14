// src/processing/algorithms/spectral_table.h — typed spectral table artifact
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

/// Typed serialized artifact for named spectra ("spectral table"): the
/// machine-readable form in which operator outputs (endmember sets, extracted
/// targets, projected library subsets) travel through workflows.
///
/// The workflow placeholder contract substitutes *string* ports, so artifacts
/// flow as file paths: a producer (e.g. rs:endmember_extraction) writes the
/// table and returns its path in the result payload; a consumer
/// (rs:sam_classify `refsRef`, rs:spectral_unmixing `endmembersRef`,
/// rs:matched_filter/rs:ace `targetRef`) loads and validates it through the
/// shared operator reference seam. The path contract is untouched — this
/// module gives those paths a typed, digest-verified payload.
///
/// Contract (format v1, kind "exp-rs:spectral-table"):
///   - non-empty spectra, all rows share @p bandCount values;
///   - all values finite (NaN is never a valid table cell);
///   - optional wavelength/FWHM grids sized to bandCount, strictly increasing
///     / positive-finite respectively;
///   - a SHA-256 digest over the canonical spectra block (see digestHex());
///   - provenance: source operator, input reference, parameters, timestamp,
///     synthetic flag. Measured tables (synthetic == false) MUST carry a
///     non-empty license and citation — the machine-checkable
///     provenance/license rule; validation fails closed otherwise.
///   - size bound: count * bandCount <= kMaxCells (typed refusal above the
///     bound — the bound is anti-abuse, not a typical-case constraint).
///
/// Validation never truncates to the first error (mirrors
/// SpectralLibrary::validateLibrary). Loading is all-or-nothing.
namespace SpectralTable
{
    /// Format identity written into every table JSON.
    inline const QString kKind = QStringLiteral( "exp-rs:spectral-table" );
    inline const int kFormatVersion = 1;

    /// Anti-abuse cell bound: 4 Mi spectral cells (e.g. 4096 spectra x 1024
    /// bands). Tables are k<=O(100) spectra in practice.
    inline constexpr long long kMaxCells = 4LL * 1024LL * 1024LL;

    /// Where the table's spectra came from and how they may be reused.
    struct Provenance
    {
        QString sourceOperator; ///< producing operator id, e.g. "rs:endmember_extraction"
        QString sourceInput;    ///< producing input reference (raster path, library id, ...)
        QString parameters;     ///< compact JSON of the producer parameters
        qint64 createdAtMs = 0; ///< epoch milliseconds
        bool synthetic = false; ///< true = physics-model derived; false = measured
        /// true = derived artifact (operator output over sourceInput): the
        /// license story follows the source, so license/citation are not
        /// mandatory. Field/measured imports (derived == false,
        /// synthetic == false) MUST carry license + citation.
        bool derived = false;

        bool operator==( const Provenance &other ) const
        {
            return sourceOperator == other.sourceOperator && sourceInput == other.sourceInput
                   && parameters == other.parameters && createdAtMs == other.createdAtMs
                   && synthetic == other.synthetic && derived == other.derived;
        }
    };

    /// An ordered set of same-width spectra with optional grids and labels.
    struct Table
    {
        QString id;                          ///< stable artifact identity (required)
        int bandCount = 0;                   ///< shared spectral width (required > 0)
        std::vector<float> wavelengthsNm;    ///< optional band centers (nm), strictly increasing
        std::vector<float> fwhmNm;           ///< optional band FWHM (nm), positive finite
        std::vector<std::vector<float>> spectra; ///< rows of @p bandCount values
        QStringList labels;                  ///< optional per-spectrum labels
        QStringList materials;               ///< optional per-spectrum material labels
        Provenance provenance;
        QString license;                     ///< required when measured (synthetic == false)
        QString citation;                    ///< required when measured (synthetic == false)

        /// SHA-256 over the canonical spectra block (lowercase hex). Computed
        /// by toJson()/save()/validateDigest — the stored field compared on load.
        QString digestHex;

        int count() const { return static_cast<int>( spectra.size() ); }
    };

    /// Canonical digest input: "exp-rs:spectral-table/v1/<count>x<bands>\n"
    /// followed by one "%.9g"-formatted value per line, row-major. %.9g is
    /// round-trip exact for IEEE float, so the digest is stable across
    /// platforms for identical values.
    QString digestHex( const std::vector<std::vector<float>> &spectra, int bandCount );

    /**
     * Full structural + provenance validation. Checks: non-empty id / spectra
     * / bandCount; row width consistency; finite values; grid sizes,
     * monotonicity and positivity; labels/materials sizing when present;
     * cell-count bound; digest consistency (when digestHex non-empty);
     * measured field tables (synthetic == false && derived == false) carry
     * non-empty license+citation. Every violation appends one human-readable
     * issue; returns true when @p errors ends up empty.
     */
    bool validate( const Table &table, QStringList *errors );

    /// Serialize to the kind/versioned JSON object. Computes and stamps the
    /// digest (the stored digestHex is recomputed, never trusted).
    QJsonObject toJson( const Table &table );

    /**
     * Parse from a JSON object: shape checks + digest verification. Returns
     * false with a message on any violation — nothing loads partially.
     * Structural provenance/license rules are applied by validate(); this
     * function enforces the shape subset needed to parse (kind, version,
     * spectra widths, finite values, grid sizes, digest match).
     */
    bool fromJson( const QJsonObject &json, Table *out, QString *errorMessage );

    /// Write the table to @p path as JSON. False with a message on failure.
    bool save( const Table &table, const QString &path, QString *errorMessage = nullptr );

    /// Load a table from a JSON file (fromJson contract). False with a message
    /// when the file is missing, unparseable, or violates the shape contract.
    bool load( const QString &path, Table *out, QString *errorMessage = nullptr );

    /// Convenience: load + validate() in one step (the consumer-side entry
    /// point — a table that loads but does not validate is refused).
    bool loadValidated( const QString &path, Table *out, QString *errorMessage = nullptr );
} // namespace SpectralTable
