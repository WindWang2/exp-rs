#include "dataset_facts.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

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

inline constexpr double kMaxExactIntegralDouble = 9007199254740992.0; // 2^53

/// Integral JSON value without UB — the same gate goal fields apply (Slice G
/// standard): anything non-finite or outside the exact-integer double range
/// fails typed instead of casting.
sicnu::data::Result<qint64> boundedCount( double raw, const QString &key )
{
    if ( !std::isfinite( raw ) || raw < -kMaxExactIntegralDouble
         || raw > kMaxExactIntegralDouble )
    {
        return sicnu::data::Result<qint64>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.facts_invalid" ),
            QStringLiteral( "%1 is not a representable integer" ).arg( key ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    return sicnu::data::Result<qint64>::success( static_cast< qint64 >( raw ) );
}

sicnu::data::Result<QHash<QString, qint64>> hashFromJson( const QJsonObject &json,
                                                          const QString &key )
{
    QHash<QString, qint64> hash;
    for ( auto it = json.begin(); it != json.end(); ++it )
    {
        auto value = boundedCount( it.value().toDouble( -1.0 ), key );
        if ( !value.has_value() )
            return sicnu::data::Result<QHash<QString, qint64>>::failure( value.diagnostics() );
        hash.insert( it.key(), value.value() );
    }
    return sicnu::data::Result<QHash<QString, qint64>>::success( hash );
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
    auto sampleCount = boundedCount(
        json.value( QStringLiteral( "sample_count" ) ).toDouble( -1.0 ),
        QStringLiteral( "sample_count" ) );
    if ( !sampleCount.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( sampleCount.diagnostics() );
    facts.sampleCount = sampleCount.value();
    auto pseudoLabelCount = boundedCount(
        json.value( QStringLiteral( "pseudo_label_count" ) ).toDouble( -1.0 ),
        QStringLiteral( "pseudo_label_count" ) );
    if ( !pseudoLabelCount.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( pseudoLabelCount.diagnostics() );
    facts.pseudoLabelCount = pseudoLabelCount.value();
    auto missingTimeCount = boundedCount(
        json.value( QStringLiteral( "missing_time_count" ) ).toDouble( -1.0 ),
        QStringLiteral( "missing_time_count" ) );
    if ( !missingTimeCount.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( missingTimeCount.diagnostics() );
    facts.missingTimeCount = missingTimeCount.value();
    facts.hasLabelSchema = json.value( QStringLiteral( "has_label_schema" ) ).toBool( false );
    facts.labelClasses = stringArrayFromJson(
        json.value( QStringLiteral( "label_classes" ) ).toArray() );
    for ( const auto &[key, member] :
          { qMakePair( QStringLiteral( "samples_by_class" ), &facts.samplesByClass ),
            qMakePair( QStringLiteral( "samples_by_season" ), &facts.samplesBySeason ),
            qMakePair( QStringLiteral( "samples_by_year" ), &facts.samplesByYear ) } )
    {
        auto hash = hashFromJson( json.value( key ).toObject(), key );
        if ( !hash.has_value() )
            return sicnu::data::Result<DatasetFacts>::failure( hash.diagnostics() );
        *member = hash.value();
    }
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
