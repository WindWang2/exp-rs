// feature_table.h — typed FeatureSet + safe sample-identity joins (D19 GOAL §13).
//
// Remote-sensing pipelines produce spectral / temporal / texture / SAR /
// phenology / change feature tables. This module is the dataset-level
// contract that attaches those columns to samples WITHOUT free-form CSV-by-
// path coupling. Joins are by stable sample key only; ambiguous keys and
// stale producer digests are typed refusals.
#pragma once

#include "dataset_types.h"

#include "../data/data_result.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

inline constexpr int kFeatureSetSerializationVersion = 1;

/// One typed column of a feature set.
struct FeatureColumn
{
    QString name;
    QString dtype;   ///< "float64" | "int64" | "bool" | "string" | custom
    QString unit;    ///< optional physical unit
    QString domain;  ///< optional value domain note
    bool required = true;

    QJsonObject toJson() const;
    static sicnu::data::Result<FeatureColumn> fromJson( const QJsonObject &json );

    friend bool operator==( const FeatureColumn &, const FeatureColumn & ) = default;
};

/// Schema + producer identity for one derived feature artifact.
class FeatureSet
{
  public:
    FeatureSet() = default;

    const QString &featureSetId() const { return m_featureSetId; }
    void setFeatureSetId( const QString &id ) { m_featureSetId = id; }
    quint64 schemaVersion() const { return m_schemaVersion; }
    void setSchemaVersion( quint64 version ) { m_schemaVersion = version; }

    /// Column name that carries the stable sample identity for joins.
    const QString &sampleKey() const { return m_sampleKey; }
    void setSampleKey( const QString &key ) { m_sampleKey = key; }

    QVector<FeatureColumn> &columns() { return m_columns; }
    const QVector<FeatureColumn> &columns() const { return m_columns; }

    const QString &producer() const { return m_producer; }
    void setProducer( const QString &producer ) { m_producer = producer; }
    const QString &inputDatasetVersionId() const { return m_inputDatasetVersionId; }
    void setInputDatasetVersionId( const QString &id ) { m_inputDatasetVersionId = id; }
    /// Content digest of the feature rows (or producer artifact). Empty =
    /// unknown (join still possible; freshness checks report Unknown).
    const QString &digest() const { return m_digest; }
    void setDigest( const QString &digest ) { m_digest = digest; }

    const QJsonObject &metadata() const { return m_metadata; }
    QJsonObject &metadata() { return m_metadata; }

    sicnu::data::Result<void> validate() const;
    QJsonObject toJson() const;
    static sicnu::data::Result<FeatureSet> fromJson( const QJsonObject &json );
    /// Canonical content digest of the schema document (excludes metadata
    /// volatile notes when `excludeMetadata` is true).
    QString schemaDigest( bool excludeMetadata = true ) const;

    friend bool operator==( const FeatureSet &, const FeatureSet & ) = default;

  private:
    QString m_featureSetId;
    quint64 m_schemaVersion = 1;
    QString m_sampleKey = QStringLiteral( "sample_id" );
    QVector<FeatureColumn> m_columns;
    QString m_producer;
    QString m_inputDatasetVersionId;
    QString m_digest;
    QJsonObject m_metadata;
};

/// One row of a feature table keyed by sample identity.
struct FeatureRow
{
    QString sampleId;
    QJsonObject values; ///< column name → value (numbers as double/int JSON)

    friend bool operator==( const FeatureRow &, const FeatureRow & ) = default;
};

enum class FeatureJoinStatus
{
    Ok,
    MissingKey,
    AmbiguousKey,
    ExtraColumns,
    MissingRequiredColumn,
    StaleInputVersion,
};

QString featureJoinStatusToString( FeatureJoinStatus status );

struct FeatureJoinFinding
{
    FeatureJoinStatus status = FeatureJoinStatus::Ok;
    QString sampleId;
    QString detail;
    QJsonObject evidence;
};

struct FeatureJoinResult
{
    AuditVerdict verdict = AuditVerdict::Unknown;
    qint64 matched = 0;
    qint64 missing = 0;
    qint64 ambiguous = 0;
    qint64 missingRequiredColumns = 0; ///< rows skipped for MissingRequiredColumn
    QVector<FeatureJoinFinding> findings; ///< bounded; truncated when large
    QVector<FeatureRow> joined;           ///< only successful matches
};

/// Join @p rows onto @p expectedSampleIds using @p featureSet.sampleKey().
/// Duplicate sample keys in the feature table → AmbiguousKey (refused for
/// those ids). Sample ids with no row → MissingKey. Required column gaps →
/// MissingRequiredColumn and Fail (not Unknown). When
/// @p expectedInputVersionId is non-empty and disagrees with the FeatureSet
/// input pin, the whole join fails with StaleInputVersion (Fail).
FeatureJoinResult joinFeaturesBySampleId( const FeatureSet &featureSet,
                                          const QVector<FeatureRow> &rows,
                                          const QStringList &expectedSampleIds,
                                          const QString &expectedInputVersionId = QString(),
                                          qint64 maxFindings = 200 );

} // namespace sicnu::dataset
