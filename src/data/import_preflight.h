// src/data/import_preflight.h — import preflight explanation report
// (R25-B feature 1, schema sicnu.import.preflight/1).
//
// One read-only question: "what would happen if this source were imported,
// and why?" The answer is a machine-readable report assembled from the SAME
// production pieces the import path uses — the product adapter registry,
// canonical metadata inspection, local identity tokens and the DataManager's
// read-only path lookup — without registering anything and without any
// network activity. Local sources only: remote spellings are reported
// OutOfScope rather than probed (offline doctrine; no packets leave).
//
// Consumers: the CLI / agent surfaces print the JSON; the D-track import UI
// renders it before a commit. It never gates registerSource by itself —
// verdicts are explanations, and registration keeps its own typed refusals.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::data
{

class DataManager;

/// One typed finding: what was observed, how severe it is for an import
/// decision, and — when an action exists — what the user can do about it.
struct ImportPreflightFinding
{
    QString code;        ///< stable machine code, "import.*" vocabulary
    QString severity;    ///< "error" | "warning" | "info"
    QString message;     ///< human-readable observation (no secrets, paths only)
    QString remediation; ///< actionable next step; empty when none exists

    QJsonObject toJson() const;
};

/// Per-band facts reported for raster sources (mirrors the canonical band
/// vocabulary; absence flags are explicit — undeclared is not zero).
struct ImportPreflightBandFact
{
    int index = 0;           ///< 1-based
    QString dtype;           ///< "Byte", "Float32", ...
    QString role;            ///< canonical band role; "" unknown
    QString unit;
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool hasWavelength = false;
    double wavelengthNm = 0.0;

    QJsonObject toJson() const;
};

/// Verdict of a preflight inspection. Refused means "registerSource will
/// refuse this or the source is unreadable"; NeedsUserChoice means the
/// registration would proceed but a material decision is left implicit
/// (container subdatasets, ambiguous adapter claims); OutOfScope means the
/// preflight refuses to act (remote spelling — no network by doctrine).
enum class ImportPreflightVerdict
{
    Importable,
    NeedsUserChoice,
    Refused,
    OutOfScope
};

enum class ImportPreflightResourceKind
{
    Raster,
    Vector,
    Product, ///< recognized directory product (non-generic adapter claimed it)
    Unknown
};

struct ImportPreflightReport
{
    /// Schema stamp serialized as "schema": "sicnu.import.preflight/1".
    inline static constexpr int kSchemaVersion = 1;

    QString path;          ///< as given by the caller
    QString canonicalPath; ///< symlink-resolved ("" when unresolvable)

    ImportPreflightVerdict verdict = ImportPreflightVerdict::Refused;
    ImportPreflightResourceKind resourceKind = ImportPreflightResourceKind::Unknown;

    // --- proven local identity (fail-closed: empty token = unprovable) ------
    QString identityToken;     ///< "li1:v1:<hex>" or ""
    QString identityStrength;  ///< "content" | "metadata" | ""
    quint64 identitySizeBytes = 0;
    quint64 identityHashedBytes = 0;

    // --- dedup prediction (read-only lookup; no registration happens) ------
    bool alreadyRegistered = false;
    QString existingAssetId;       ///< AssetId::toString() of the catalog hit
    quint64 existingAssetRevision = 0;

    // --- recognition ---------------------------------------------------------
    QString productKind;     ///< ProductKind name ("Sentinel2Safe", "GenericRaster", ...)
    QString adapterId;       ///< winning adapter id (first-match-wins policy)
    QString productId;
    QStringList candidateAdapterIds; ///< EVERY adapter whose accepts() fired
    QString completeness;    ///< product completeness verdict ("" for plain files)
    QStringList missingConstituents;

    // --- raster facts (empty for pure vector sources) ------------------------
    QString driver;
    int width = 0;
    int height = 0;
    int bandCount = 0;
    bool crsValid = false;
    QString crsAuthid;
    QString crsWkt;
    bool hasGeotransform = false;
    bool hasExtent = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    QString sensor;
    QString platform;
    QString processingLevel;
    QString radiometricState;
    QString acquisitionTime; ///< ISO-8601 when declared
    bool hasCloudCover = false;
    double cloudCover = 0;   ///< percent
    bool hasGsd = false;
    double gsd = 0;          ///< meters
    QVector<ImportPreflightBandFact> bands;
    int subdatasetCount = 0;

    // --- vector facts --------------------------------------------------------
    int layerCount = 0;
    QStringList layerNames;

    QVector<ImportPreflightFinding> findings;

    QString verdictString() const;
    QString resourceKindString() const;
    QJsonObject toJson() const;

    /// Convenience: first finding with this code, or nullptr.
    const ImportPreflightFinding *finding( const QString &code ) const;
};

/// Read-only import preflight over one local source.
class ImportPreflight
{
  public:
    struct Options
    {
        /// Compute the local identity token (bounded prefix hash). When false
        /// the identity fields stay empty and no file bytes are read.
        bool probeIdentity = true;
        /// Query the DataManager for an existing registration of this path
        /// (read-only findByPath; requires @p manager). When false the dedup
        /// fields stay empty.
        bool probeDedup = true;
        /// Hash budget for the identity token (mirror of the geospatial
        /// foundation default; 0 = metadata-only identity).
        quint64 identityHashBytes = 8ull * 1024 * 1024;
    };

    /// Inspect one source. Never throws: every failure lands in the findings
    /// list with an "error" severity and the verdict reflects it.
    static ImportPreflightReport inspect( const QString &path,
                                          const DataManager *manager,
                                          const Options &options );
    /// Convenience overload with default options (identity + dedup probing).
    static ImportPreflightReport inspect( const QString &path,
                                          const DataManager *manager = nullptr )
    {
        return inspect( path, manager, Options{} );
    }
};

} // namespace sicnu::data
