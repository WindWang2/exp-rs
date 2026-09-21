#pragma once

// dataset_facts.h — dataset-level statistical projection for suitability
// assessment.
//
// The assessor core only sees this self-contained DTO; the store-backed
// provider that fills it arrives in a later slice. Every count of -1 means
// "unknown" — an absent measurement is never encoded as a zero.

#include "../data/data_result.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::suitability
{

inline constexpr int kDatasetFactsSerializationVersion = 1;

struct DatasetFacts
{
    QString datasetVersionId;
    /// True when the provider hit a LIMIT while collecting facts; consumers
    /// must treat every derived verdict as truncated evidence.
    bool factsTruncated = false;
    qint64 sampleCount = -1;
    qint64 pseudoLabelCount = -1;
    qint64 missingTimeCount = -1;
    bool hasLabelSchema = false;
    QStringList labelClasses;
    QHash<QString, qint64> samplesByClass;
    QHash<QString, qint64> samplesBySeason;
    QHash<QString, qint64> samplesByYear;
    QStringList bandRoles;
    QString modality;
    QString sensor;
    QString crsWkt;
    bool hasExtent = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool hasTemporalExtent = false;
    QDateTime temporalStartUtc;
    QDateTime temporalEndUtc;

    QJsonObject toJson() const;
    static sicnu::data::Result<DatasetFacts> fromJson( const QJsonObject &json );
};

} // namespace sicnu::suitability
