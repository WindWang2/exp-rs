// dataset_manifest.h — the dataset version contract (ADR 0134).
//
// The manifest is the complete, canonical description of one dataset
// version: what sources fed it, which entries (assets/samples) it contains,
// what schema/modality/label vocabulary applies, its spatial/temporal
// extent, quality/statistics summaries and its content fingerprint.
//
// Serialization contract (mirrors the workflow-run payload policy):
//   - every payload carries `schema_version`; a reader rejects a DIFFERENT
//     version instead of guessing (corrupt/foreign payloads must be loud);
//   - unknown fields are tolerated (forward compatibility within a version);
//   - canonical bytes are produced by canonicalizeJsonRfc8785 — the ONE
//     canonical JSON implementation of the platform (execution_fingerprint.h).
//
// A dataset version with 100k samples does NOT inline samples here: the
// manifest carries counts/summaries, the store holds sample rows (bounded
// memory contract, goal §37).
#pragma once

#include "../data/data_asset.h" // sicnu::data::SpatialExtent reuse
#include "../data/data_result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

/// Serialization schema version stamped into every manifest payload.
inline constexpr int kDatasetManifestSerializationVersion = 1;

/// One upstream Data Asset a dataset version was built from, pinned to the
/// revision that was read. `assetId` is the canonical string form of a
/// `sicnu::data::AssetId` — datasets never mint asset identities.
struct SourceAssetRef
{
    QString assetId;
    quint64 revision = 0; ///< 0 = revision not pinned (external/unknown)
    QString role;         ///< e.g. "image", "label", "mask", "dem"
    QStringList bandReferences;

    friend bool operator==( const SourceAssetRef &, const SourceAssetRef & ) = default;
};

/// Temporal coverage of a dataset version.
struct TemporalExtent
{
    QDateTime startUtc;
    QDateTime endUtc;
    bool valid = false;

    friend bool operator==( const TemporalExtent &, const TemporalExtent & ) = default;
};

/// Typed description of the data schema a version carries. Kept small and
/// closed: anything richer belongs to per-entry/sample payloads, not the
/// dataset-wide contract.
struct DatasetSchema
{
    QString modality;  ///< "optical"|"sar"|"dem"|"vector"|"label"|"auxiliary"|"timeseries"|custom
    QString sensor;    ///< free-form platform/sensor, e.g. "Sentinel-2/S2MSI2A"
    QString crs;       ///< authority string ("EPSG:32650") or WKT; empty = mixed/unspecified
    QStringList bandRoles; ///< semantic band roles (ADR 0065 vocabulary strings)
    double resolutionX = 0.0; ///< 0 = unspecified
    double resolutionY = 0.0;
    QString resolutionUnit;   ///< e.g. "m"; empty = unspecified

    friend bool operator==( const DatasetSchema &, const DatasetSchema & ) = default;
};

/// Reference to the versioned label ontology governing this version
/// (ADR 0135). Null when the dataset carries no labels.
struct LabelSchemaRef
{
    QString schemaId;
    quint64 version = 0;

    bool isNull() const { return schemaId.isEmpty(); }
    friend bool operator==( const LabelSchemaRef &, const LabelSchemaRef & ) = default;
};

/// One dataset entry: an asset-level or sample-level member of the version.
/// Entries are identity + role only; sample CONTENT lives in sample rows
/// referenced by `refId`.
struct DatasetEntry
{
    QString kind;      ///< "asset" | "sample"
    QString refId;     ///< AssetId or SampleId, canonical string form
    quint64 revision = 0;
    QString role;      ///< free-form ("image", "roi", "patch", …)

    friend bool operator==( const DatasetEntry &, const DatasetEntry & ) = default;
};

class DatasetManifest
{
  public:
    DatasetManifest() = default;

    // -- identity ------------------------------------------------------------
    const QString &datasetId() const { return m_datasetId; }
    void setDatasetId( const QString &id ) { m_datasetId = id; }
    const QString &versionId() const { return m_versionId; }
    void setVersionId( const QString &id ) { m_versionId = id; }
    const QString &parentVersionId() const { return m_parentVersionId; }
    void setParentVersionId( const QString &id ) { m_parentVersionId = id; }
    bool isRootVersion() const { return m_parentVersionId.isEmpty(); }

    // -- header --------------------------------------------------------------
    const QString &name() const { return m_name; }
    void setName( const QString &name ) { m_name = name; }
    const QString &description() const { return m_description; }
    void setDescription( const QString &text ) { m_description = text; }
    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }

    // -- content -------------------------------------------------------------
    const QVector<SourceAssetRef> &sourceAssets() const { return m_sourceAssets; }
    QVector<SourceAssetRef> &sourceAssets() { return m_sourceAssets; }
    const QVector<DatasetEntry> &entries() const { return m_entries; }
    QVector<DatasetEntry> &entries() { return m_entries; }
    const DatasetSchema &schema() const { return m_schema; }
    DatasetSchema &schema() { return m_schema; }
    const LabelSchemaRef &labelSchema() const { return m_labelSchema; }
    LabelSchemaRef &labelSchema() { return m_labelSchema; }
    const sicnu::data::SpatialExtent &spatialExtent() const { return m_spatialExtent; }
    sicnu::data::SpatialExtent &spatialExtent() { return m_spatialExtent; }
    const TemporalExtent &temporalExtent() const { return m_temporalExtent; }
    TemporalExtent &temporalExtent() { return m_temporalExtent; }
    const QStringList &splitManifestIds() const { return m_splitManifestIds; }
    QStringList &splitManifestIds() { return m_splitManifestIds; }

    // -- summaries / annotations of the content ------------------------------
    const QStringList &tags() const { return m_tags; }
    QStringList &tags() { return m_tags; }
    const QString &license() const { return m_license; }
    void setLicense( const QString &license ) { m_license = license; }
    const QString &citation() const { return m_citation; }
    void setCitation( const QString &citation ) { m_citation = citation; }
    const QJsonObject &provenance() const { return m_provenance; }
    QJsonObject &provenance() { return m_provenance; }
    const QJsonObject &statistics() const { return m_statistics; }
    QJsonObject &statistics() { return m_statistics; }
    const QJsonObject &quality() const { return m_quality; }
    QJsonObject &quality() { return m_quality; }

    /// Content fingerprint (hex). Excluded from fingerprint computation;
    /// stamped by the store on commit. Empty while drafting.
    const QString &fingerprint() const { return m_fingerprint; }
    void setFingerprint( const QString &fingerprint ) { m_fingerprint = fingerprint; }

    // -- serialization -------------------------------------------------------
    QJsonObject toJson() const;

    /// Strict-version, unknown-field-tolerant parse. Fails with
    /// `dataset.manifest_version` on a foreign schema version and
    /// `dataset.manifest_invalid` when required fields are missing/malformed.
    static sicnu::data::Result<DatasetManifest> fromJson( const QJsonObject &json );

    friend bool operator==( const DatasetManifest &, const DatasetManifest & ) = default;

  private:
    QString m_datasetId;
    QString m_versionId;
    QString m_parentVersionId;
    QString m_name;
    QString m_description;
    QDateTime m_createdAtUtc;
    QVector<SourceAssetRef> m_sourceAssets;
    QVector<DatasetEntry> m_entries;
    DatasetSchema m_schema;
    LabelSchemaRef m_labelSchema;
    sicnu::data::SpatialExtent m_spatialExtent;
    TemporalExtent m_temporalExtent;
    QStringList m_splitManifestIds;
    QStringList m_tags;
    QString m_license;
    QString m_citation;
    QJsonObject m_provenance;
    QJsonObject m_statistics;
    QJsonObject m_quality;
    QString m_fingerprint;
};

} // namespace sicnu::dataset
