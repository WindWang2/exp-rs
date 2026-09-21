#include "dataset_facts.h"

#include <QJsonArray>

#include <algorithm>

namespace sicnu::suitability
{

namespace
{

// QHash iteration order is unspecified; every hash is serialized as a JSON
// object built over sorted keys so identical content has exactly one
// canonical form (digests and golden files depend on it).
QJsonObject sortedHashToJson( const QHash<QString, qint64> &hash )
{
    QList<QString> keys = hash.keys();
    std::sort( keys.begin(), keys.end() );
    QJsonObject json;
    for ( const QString &key : keys )
        json.insert( key, static_cast< qint64 >( hash.value( key ) ) );
    return json;
}

QHash<QString, qint64> hashFromJson( const QJsonObject &json )
{
    QHash<QString, qint64> hash;
    for ( auto it = json.begin(); it != json.end(); ++it )
        hash.insert( it.key(), static_cast< qint64 >( it.value().toDouble( -1.0 ) ) );
    return hash;
}

QStringList stringArrayFromJson( const QJsonArray &array )
{
    QStringList values;
    for ( const QJsonValue &value : array )
        values.append( value.toString() );
    return values;
}

} // namespace

QJsonObject DatasetFacts::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kDatasetFactsSerializationVersion );
    json.insert( QStringLiteral( "dataset_version_id" ), datasetVersionId );
    json.insert( QStringLiteral( "facts_truncated" ), factsTruncated );
    json.insert( QStringLiteral( "sample_count" ), static_cast< qint64 >( sampleCount ) );
    json.insert( QStringLiteral( "pseudo_label_count" ), static_cast< qint64 >( pseudoLabelCount ) );
    json.insert( QStringLiteral( "missing_time_count" ), static_cast< qint64 >( missingTimeCount ) );
    json.insert( QStringLiteral( "has_label_schema" ), hasLabelSchema );

    QJsonArray classesArray;
    for ( const QString &className : labelClasses )
        classesArray.append( className );
    json.insert( QStringLiteral( "label_classes" ), classesArray );

    json.insert( QStringLiteral( "samples_by_class" ), sortedHashToJson( samplesByClass ) );
    json.insert( QStringLiteral( "samples_by_season" ), sortedHashToJson( samplesBySeason ) );
    json.insert( QStringLiteral( "samples_by_year" ), sortedHashToJson( samplesByYear ) );

    QJsonArray bandRolesArray;
    for ( const QString &role : bandRoles )
        bandRolesArray.append( role );
    json.insert( QStringLiteral( "band_roles" ), bandRolesArray );

    json.insert( QStringLiteral( "modality" ), modality );
    json.insert( QStringLiteral( "sensor" ), sensor );
    json.insert( QStringLiteral( "crs_wkt" ), crsWkt );

    json.insert( QStringLiteral( "has_extent" ), hasExtent );
    if ( hasExtent )
    {
        json.insert( QStringLiteral( "min_x" ), minX );
        json.insert( QStringLiteral( "min_y" ), minY );
        json.insert( QStringLiteral( "max_x" ), maxX );
        json.insert( QStringLiteral( "max_y" ), maxY );
    }

    json.insert( QStringLiteral( "has_temporal_extent" ), hasTemporalExtent );
    if ( hasTemporalExtent )
    {
        json.insert( QStringLiteral( "temporal_start_utc" ), temporalStartUtc.toString( Qt::ISODate ) );
        json.insert( QStringLiteral( "temporal_end_utc" ), temporalEndUtc.toString( Qt::ISODate ) );
    }
    return json;
}

sicnu::data::Result<DatasetFacts> DatasetFacts::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kDatasetFactsSerializationVersion )
    {
        return sicnu::data::Result<DatasetFacts>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.facts_schema" ),
            QStringLiteral( "unsupported dataset facts schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    DatasetFacts facts;
    facts.datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    if ( facts.datasetVersionId.isEmpty() )
    {
        return sicnu::data::Result<DatasetFacts>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.facts_invalid" ),
            QStringLiteral( "dataset facts are missing their dataset_version_id" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    facts.factsTruncated = json.value( QStringLiteral( "facts_truncated" ) ).toBool( false );
    facts.sampleCount = static_cast< qint64 >(
        json.value( QStringLiteral( "sample_count" ) ).toDouble( -1.0 ) );
    facts.pseudoLabelCount = static_cast< qint64 >(
        json.value( QStringLiteral( "pseudo_label_count" ) ).toDouble( -1.0 ) );
    facts.missingTimeCount = static_cast< qint64 >(
        json.value( QStringLiteral( "missing_time_count" ) ).toDouble( -1.0 ) );
    facts.hasLabelSchema = json.value( QStringLiteral( "has_label_schema" ) ).toBool( false );
    facts.labelClasses = stringArrayFromJson(
        json.value( QStringLiteral( "label_classes" ) ).toArray() );
    facts.samplesByClass = hashFromJson(
        json.value( QStringLiteral( "samples_by_class" ) ).toObject() );
    facts.samplesBySeason = hashFromJson(
        json.value( QStringLiteral( "samples_by_season" ) ).toObject() );
    facts.samplesByYear = hashFromJson(
        json.value( QStringLiteral( "samples_by_year" ) ).toObject() );
    facts.bandRoles = stringArrayFromJson(
        json.value( QStringLiteral( "band_roles" ) ).toArray() );
    facts.modality = json.value( QStringLiteral( "modality" ) ).toString();
    facts.sensor = json.value( QStringLiteral( "sensor" ) ).toString();
    facts.crsWkt = json.value( QStringLiteral( "crs_wkt" ) ).toString();

    facts.hasExtent = json.value( QStringLiteral( "has_extent" ) ).toBool( false );
    if ( facts.hasExtent )
    {
        facts.minX = json.value( QStringLiteral( "min_x" ) ).toDouble();
        facts.minY = json.value( QStringLiteral( "min_y" ) ).toDouble();
        facts.maxX = json.value( QStringLiteral( "max_x" ) ).toDouble();
        facts.maxY = json.value( QStringLiteral( "max_y" ) ).toDouble();
    }

    facts.hasTemporalExtent = json.value( QStringLiteral( "has_temporal_extent" ) ).toBool( false );
    if ( facts.hasTemporalExtent )
    {
        facts.temporalStartUtc = QDateTime::fromString(
            json.value( QStringLiteral( "temporal_start_utc" ) ).toString(), Qt::ISODate );
        facts.temporalEndUtc = QDateTime::fromString(
            json.value( QStringLiteral( "temporal_end_utc" ) ).toString(), Qt::ISODate );
    }
    return sicnu::data::Result<DatasetFacts>::success( facts );
}

} // namespace sicnu::suitability
