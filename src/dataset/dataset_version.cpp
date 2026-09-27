// dataset_version.cpp — manifest diff.
#include "dataset_version.h"

#include <QJsonArray>
#include <QHash>

#include <algorithm>

namespace sicnu::dataset
{

namespace
{

QString entryKey( const DatasetEntry &entry )
{
    return entry.kind + QLatin1Char( ':' ) + entry.refId;
}

} // namespace

QJsonObject DatasetVersionDiff::toJson() const
{
    QJsonObject json;
    auto encodeList = []( const QVector<DatasetEntry> &list ) {
        QJsonArray array;
        for ( const DatasetEntry &entry : list )
        {
            QJsonObject item;
            item.insert( QStringLiteral( "kind" ), entry.kind );
            item.insert( QStringLiteral( "ref_id" ), entry.refId );
            item.insert( QStringLiteral( "revision" ), qint64( entry.revision ) );
            if ( !entry.role.isEmpty() )
                item.insert( QStringLiteral( "role" ), entry.role );
            array.append( item );
        }
        return array;
    };
    if ( !addedEntries.isEmpty() )
        json.insert( QStringLiteral( "added_entries" ), encodeList( addedEntries ) );
    if ( !removedEntries.isEmpty() )
        json.insert( QStringLiteral( "removed_entries" ), encodeList( removedEntries ) );
    if ( !changedEntries.isEmpty() )
        json.insert( QStringLiteral( "changed_entries" ), encodeList( changedEntries ) );
    if ( sourceAssetsChanged )
        json.insert( QStringLiteral( "source_assets_changed" ), true );
    if ( schemaChanged )
        json.insert( QStringLiteral( "schema_changed" ), true );
    if ( labelSchemaChanged )
        json.insert( QStringLiteral( "label_schema_changed" ), true );
    if ( splitChanged )
        json.insert( QStringLiteral( "split_changed" ), true );
    if ( extentChanged )
        json.insert( QStringLiteral( "extent_changed" ), true );
    if ( metadataChanged )
        json.insert( QStringLiteral( "metadata_changed" ), true );
    return json;
}

sicnu::data::Result<DatasetVersionDiff> diffManifests( const DatasetManifest &from,
                                                       const DatasetManifest &to )
{
    using Result = sicnu::data::Result<DatasetVersionDiff>;
    if ( from.datasetId() != to.datasetId() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.diff_dataset_mismatch" ),
            QStringLiteral( "cannot diff manifests of datasets %1 and %2" )
                .arg( from.datasetId(), to.datasetId() ),
            DiagnosticSeverity::Error,
        } );
    }

    DatasetVersionDiff diff;

    QHash<QString, DatasetEntry> fromEntries;
    for ( const DatasetEntry &entry : from.entries() )
        fromEntries.insert( entryKey( entry ), entry );
    QHash<QString, DatasetEntry> toEntries;
    for ( const DatasetEntry &entry : to.entries() )
        toEntries.insert( entryKey( entry ), entry );

    for ( auto it = toEntries.constBegin(); it != toEntries.constEnd(); ++it )
    {
        const auto fromIt = fromEntries.constFind( it.key() );
        if ( fromIt == fromEntries.constEnd() )
            diff.addedEntries.append( it.value() );
        else if ( !( *fromIt == it.value() ) )
            diff.changedEntries.append( it.value() );
    }
    for ( auto it = fromEntries.constBegin(); it != fromEntries.constEnd(); ++it )
    {
        if ( !toEntries.contains( it.key() ) )
            diff.removedEntries.append( it.value() );
    }

    // Entry vectors are ordered by (kind, refId): QHash iteration order is
    // unspecified (and re-seeded per process in Qt6), so an unsorted diff
    // serializes to different bytes for identical inputs. The entry key is
    // the natural total order — same key the diff itself is computed on.
    const auto byEntryKey = []( const DatasetEntry &a, const DatasetEntry &b ) {
        return entryKey( a ) < entryKey( b );
    };
    std::sort( diff.addedEntries.begin(), diff.addedEntries.end(), byEntryKey );
    std::sort( diff.removedEntries.begin(), diff.removedEntries.end(), byEntryKey );
    std::sort( diff.changedEntries.begin(), diff.changedEntries.end(), byEntryKey );

    diff.sourceAssetsChanged = !( from.sourceAssets() == to.sourceAssets() );
    diff.schemaChanged = !( from.schema() == to.schema() );
    diff.labelSchemaChanged = !( from.labelSchema() == to.labelSchema() );
    diff.splitChanged = !( from.splitManifestIds() == to.splitManifestIds() );
    diff.extentChanged = !( from.spatialExtent() == to.spatialExtent() ) ||
                         !( from.temporalExtent() == to.temporalExtent() );
    diff.metadataChanged = from.name() != to.name() || from.description() != to.description() ||
                           from.tags() != to.tags() || from.license() != to.license() ||
                           from.citation() != to.citation();

    return Result::success( diff );
}

} // namespace sicnu::dataset
