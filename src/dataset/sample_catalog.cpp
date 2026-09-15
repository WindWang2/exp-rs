// sample_catalog.cpp — bounded filter/page over flat sample views.
#include "sample_catalog.h"

namespace sicnu::dataset
{

namespace
{

bool matchesList( const QStringList &allowed, const QString &value )
{
    return allowed.isEmpty() || allowed.contains( value );
}

bool matchesYears( const QVector<int> &allowed, int year )
{
    return allowed.isEmpty() || allowed.contains( year );
}

bool matches( const SampleCatalogRow &row, const SampleCatalogFilter &filter )
{
    if ( filter.filterByKind && row.kind != filter.kind )
        return false;
    if ( !matchesList( filter.classCodes, row.classCode ) )
        return false;
    if ( !matchesList( filter.sensors, row.sensor ) )
        return false;
    if ( !matchesList( filter.regions, row.region ) )
        return false;
    if ( !matchesList( filter.modalities, row.modality ) )
        return false;
    if ( !matchesYears( filter.years, row.year ) )
        return false;
    if ( !matchesList( filter.splitRoles, row.splitRole ) )
        return false;
    if ( !matchesList( filter.qualities, row.quality ) )
        return false;
    if ( filter.pseudoLabelsOnly.has_value() &&
         row.hasPseudoLabel != *filter.pseudoLabelsOnly )
        return false;
    return true;
}

} // namespace

SampleCatalogPage querySampleCatalog( const QVector<SampleCatalogRow> &rows,
                                      const SampleCatalogFilter &filter, qint64 offset,
                                      qint64 limit )
{
    SampleCatalogPage page;
    if ( offset < 0 )
        offset = 0;
    if ( limit <= 0 )
        limit = 500;
    if ( limit > 500 )
        limit = 500; // hard bound aligned with DatasetStore::kMaxPageSize

    page.offset = offset;
    page.limit = limit;

    qint64 matchedIndex = 0;
    for ( const SampleCatalogRow &row : rows )
    {
        if ( !matches( row, filter ) )
            continue;
        ++page.totalMatched;
        if ( matchedIndex >= offset && page.rows.size() < limit )
            page.rows.append( row );
        ++matchedIndex;
    }
    return page;
}

SampleCatalogSummary summarizeSampleCatalog( const QVector<SampleCatalogRow> &rows,
                                             const SampleCatalogFilter &filter )
{
    SampleCatalogSummary summary;
    for ( const SampleCatalogRow &row : rows )
    {
        if ( !matches( row, filter ) )
            continue;
        ++summary.total;
        if ( row.hasPseudoLabel )
            ++summary.pseudoLabelCount;
        if ( !row.classCode.isEmpty() )
            summary.byClass[row.classCode] += 1;
        if ( !row.sensor.isEmpty() )
            summary.bySensor[row.sensor] += 1;
        if ( !row.region.isEmpty() )
            summary.byRegion[row.region] += 1;
        if ( row.year != 0 )
            summary.byYear[row.year] += 1;
        if ( !row.splitRole.isEmpty() )
            summary.bySplitRole[row.splitRole] += 1;
    }
    return summary;
}

} // namespace sicnu::dataset
