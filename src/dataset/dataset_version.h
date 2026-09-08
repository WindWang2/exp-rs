// dataset_version.h — version lifecycle + diff (ADR 0134).
//
// A DatasetVersionRecord is the store-side state of one version: identity,
// lifecycle status, parent link, quality level, and the canonical manifest
// document. Committing is a single atomic store transaction; committed and
// deprecated versions are immutable — every mutation path must go through a
// draft child version.
#pragma once

#include "dataset_manifest.h"
#include "dataset_types.h"

#include <QDateTime>
#include <QString>

namespace sicnu::dataset
{

class DatasetVersionRecord
{
  public:
    DatasetVersionRecord() = default;

    const QString &versionId() const { return m_versionId; }
    void setVersionId( const QString &id ) { m_versionId = id; }
    const QString &datasetId() const { return m_datasetId; }
    void setDatasetId( const QString &id ) { m_datasetId = id; }
    const QString &parentVersionId() const { return m_parentVersionId; }
    void setParentVersionId( const QString &id ) { m_parentVersionId = id; }

    DatasetVersionStatus status() const { return m_status; }
    void setStatus( DatasetVersionStatus status ) { m_status = status; }
    DatasetQualityLevel qualityLevel() const { return m_qualityLevel; }
    void setQualityLevel( DatasetQualityLevel level ) { m_qualityLevel = level; }

    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }
    const QDateTime &committedAtUtc() const { return m_committedAtUtc; }
    void setCommittedAtUtc( const QDateTime &time ) { m_committedAtUtc = time; }

    /// Version note (what changed and why, goal §10).
    const QString &note() const { return m_note; }
    void setNote( const QString &note ) { m_note = note; }

    /// Canonical manifest JSON (the fingerprinted document).
    const QString &manifestJson() const { return m_manifestJson; }
    void setManifestJson( const QString &json ) { m_manifestJson = json; }

    const QString &fingerprint() const { return m_fingerprint; }
    void setFingerprint( const QString &fingerprint ) { m_fingerprint = fingerprint; }

    /// True when the version may be mutated (draft only, by contract).
    bool isMutable() const { return m_status == DatasetVersionStatus::Draft; }

    friend bool operator==( const DatasetVersionRecord &, const DatasetVersionRecord & ) = default;

  private:
    QString m_versionId;
    QString m_datasetId;
    QString m_parentVersionId;
    DatasetVersionStatus m_status = DatasetVersionStatus::Draft;
    DatasetQualityLevel m_qualityLevel = DatasetQualityLevel::Unassessed;
    QDateTime m_createdAtUtc;
    QDateTime m_committedAtUtc;
    QString m_note;
    QString m_manifestJson;
    QString m_fingerprint;
};

/// Semantic diff between two manifests of one dataset (goal §10). Entry
/// identity is (kind, refId); label/split changes are detected structurally.
struct DatasetVersionDiff
{
    QVector<DatasetEntry> addedEntries;
    QVector<DatasetEntry> removedEntries;
    QVector<DatasetEntry> changedEntries; ///< same identity, different role/revision
    bool sourceAssetsChanged = false;
    bool schemaChanged = false;
    bool labelSchemaChanged = false;
    bool splitChanged = false;
    bool extentChanged = false;
    bool metadataChanged = false; ///< name/description/tags/license/citation

    bool isEmpty() const
    {
        return addedEntries.isEmpty() && removedEntries.isEmpty() &&
               changedEntries.isEmpty() && !sourceAssetsChanged && !schemaChanged &&
               !labelSchemaChanged && !splitChanged && !extentChanged && !metadataChanged;
    }

    QJsonObject toJson() const;
};

/// Diff @p from → @p to. Both manifests must belong to the same dataset;
/// a mismatch fails with `dataset.diff_dataset_mismatch`.
sicnu::data::Result<DatasetVersionDiff> diffManifests( const DatasetManifest &from,
                                                       const DatasetManifest &to );

} // namespace sicnu::dataset
