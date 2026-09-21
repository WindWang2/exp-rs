#include "store_data_provider.h"

#include "../dataset/dataset_ids.h"
#include "../dataset/dataset_manifest.h"
#include "../dataset/dataset_store.h"
#include "../dataset/sample.h"

#include <QJsonDocument>
#include <algorithm>

namespace sicnu::suitability
{

namespace
{

sicnu::data::Diagnostic failure( const QString &code, const QString &message )
{
    return sicnu::data::Diagnostic{ code, message, sicnu::data::DiagnosticSeverity::Error };
}

/// Copies one facet distribution into @p target, skipping the store's
/// reserved "(other)" fold bucket. Returns true when the tail was folded —
/// which is exactly the truncation signal.
bool applyFacetValues( const QVector<QPair<QString, qint64>> &values,
                       QHash<QString, qint64> *target )
{
    bool truncated = false;
    for ( const auto &entry : values )
    {
        if ( entry.first == QStringLiteral( "(other)" ) )
        {
            truncated = true;
            continue;
        }
        target->insert( entry.first, entry.second );
    }
    return truncated;
}

bool truthy( const QString &value )
{
    return value == QStringLiteral( "true" ) || value == QStringLiteral( "1" ) ||
           value == QStringLiteral( "yes" );
}

} // namespace

StoreDataProvider::StoreDataProvider( const sicnu::dataset::DatasetStore *store )
    : m_store( store )
{
}

sicnu::data::Result<DatasetFacts> StoreDataProvider::datasetFacts(
    const QString &datasetVersionId, const FactsLimits &limits ) const
{
    if ( !m_store || !m_store->isOpen() )
    {
        return sicnu::data::Result<DatasetFacts>::failure(
            failure( QStringLiteral( "suitability.provider_unavailable" ),
                     QStringLiteral( "no open dataset store is available for facts projection" ) ) );
    }

    const auto versionId = sicnu::dataset::DatasetVersionId::fromString( datasetVersionId );
    if ( !versionId.has_value() )
    {
        return sicnu::data::Result<DatasetFacts>::failure(
            failure( QStringLiteral( "suitability.dataset_unknown" ),
                     QStringLiteral( "'%1' is not a dataset version id" ).arg( datasetVersionId ) ) );
    }

    // An unknown version is a missing SUBJECT, not empty evidence: refuse
    // typed instead of projecting an anonymous zero-everything dataset.
    const auto record = m_store->versionById( *versionId );
    if ( !record.has_value() )
    {
        return sicnu::data::Result<DatasetFacts>::failure(
            failure( QStringLiteral( "suitability.dataset_unknown" ),
                     QStringLiteral( "dataset version '%1' does not exist in the store" )
                         .arg( datasetVersionId ) ) );
    }

    DatasetFacts facts;
    facts.datasetVersionId = datasetVersionId;

    // Schema projection from the canonical manifest. A manifest that does
    // not parse leaves every schema-derived field unknown — never guessed.
    const QJsonDocument manifestDocument =
        QJsonDocument::fromJson( record->manifestJson().toUtf8() );
    const auto manifest = sicnu::dataset::DatasetManifest::fromJson( manifestDocument.object() );
    if ( manifest.has_value() )
    {
        facts.bandRoles = manifest->schema().bandRoles;
        facts.modality = manifest->schema().modality;
        facts.sensor = manifest->schema().sensor;
        facts.crsWkt = manifest->schema().crs;
        if ( manifest->spatialExtent().valid )
        {
            facts.hasExtent = true;
            facts.minX = manifest->spatialExtent().minimumX;
            facts.minY = manifest->spatialExtent().minimumY;
            facts.maxX = manifest->spatialExtent().maximumX;
            facts.maxY = manifest->spatialExtent().maximumY;
        }
        if ( manifest->temporalExtent().valid )
        {
            facts.hasTemporalExtent = true;
            facts.temporalStartUtc = manifest->temporalExtent().startUtc;
            facts.temporalEndUtc = manifest->temporalExtent().endUtc;
        }
        facts.hasLabelSchema = !manifest->labelSchema().isNull();
    }

    facts.sampleCount = m_store->sampleCount( *versionId );

    const auto classFacet = m_store->facetDistribution(
        *versionId, QStringLiteral( "class" ), limits.maxClassValues );
    if ( !classFacet.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( classFacet.diagnostics() );
    if ( applyFacetValues( classFacet->values, &facts.samplesByClass ) )
        facts.factsTruncated = true;

    const auto seasonFacet = m_store->facetDistribution(
        *versionId, QStringLiteral( "season" ), limits.maxClassValues );
    if ( !seasonFacet.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( seasonFacet.diagnostics() );
    if ( applyFacetValues( seasonFacet->values, &facts.samplesBySeason ) )
        facts.factsTruncated = true;

    const auto yearFacet = m_store->facetDistribution(
        *versionId, QStringLiteral( "year" ), limits.maxClassValues );
    if ( !yearFacet.has_value() )
        return sicnu::data::Result<DatasetFacts>::failure( yearFacet.diagnostics() );
    if ( applyFacetValues( yearFacet->values, &facts.samplesByYear ) )
        facts.factsTruncated = true;

    // Observed class vocabulary, sorted for deterministic serialization.
    for ( auto it = facts.samplesByClass.constBegin(); it != facts.samplesByClass.constEnd(); ++it )
        facts.labelClasses.append( it.key() );
    std::sort( facts.labelClasses.begin(), facts.labelClasses.end() );

    // Honest degradation: when the manifest declares no label schema but
    // class facets exist, a vocabulary is demonstrably in use — the schema
    // flag is then INFERRED from that evidence (source: the class facet).
    if ( !facts.hasLabelSchema && !facts.labelClasses.isEmpty() )
        facts.hasLabelSchema = true;

    // Pseudo-label evidence is facet-driven. Sample rows carry no pseudo
    // flag, so without such a facet the count stays unknown (-1) — a scan
    // of rows cannot produce it. A folded facet tail may hide the pseudo
    // value entirely: unknown, never a fabricated zero.
    const QVector<QString> facetNames = m_store->facetNames( *versionId );
    if ( facetNames.contains( QStringLiteral( "label_source" ) ) )
    {
        const auto sources = m_store->facetDistribution(
            *versionId, QStringLiteral( "label_source" ), limits.maxClassValues );
        if ( !sources.has_value() )
            return sicnu::data::Result<DatasetFacts>::failure( sources.diagnostics() );
        bool folded = false;
        qint64 pseudo = 0;
        bool sawPseudo = false;
        for ( const auto &entry : sources->values )
        {
            if ( entry.first == QStringLiteral( "(other)" ) )
            {
                folded = true;
                continue;
            }
            if ( entry.first == QStringLiteral( "pseudo" ) )
            {
                pseudo = entry.second;
                sawPseudo = true;
            }
        }
        if ( folded && !sawPseudo )
            facts.factsTruncated = true;
        else
            facts.pseudoLabelCount = pseudo;
    }
    else if ( facetNames.contains( QStringLiteral( "pseudo_label" ) ) )
    {
        const auto pseudoFacet = m_store->facetDistribution(
            *versionId, QStringLiteral( "pseudo_label" ), limits.maxClassValues );
        if ( !pseudoFacet.has_value() )
            return sicnu::data::Result<DatasetFacts>::failure( pseudoFacet.diagnostics() );
        bool folded = false;
        qint64 pseudo = 0;
        bool sawTruthy = false;
        for ( const auto &entry : pseudoFacet->values )
        {
            if ( entry.first == QStringLiteral( "(other)" ) )
            {
                folded = true;
                continue;
            }
            if ( truthy( entry.first ) )
            {
                pseudo += entry.second;
                sawTruthy = true;
            }
        }
        if ( folded && !sawTruthy )
            facts.factsTruncated = true;
        else
            facts.pseudoLabelCount = pseudo;
    }

    // Missing observation time: measured by a bounded keyset scan over the
    // sample rows. A capped scan cannot claim a total — the count stays
    // unknown and only factsTruncated carries the news.
    if ( facetNames.contains( QStringLiteral( "missing_time" ) ) )
    {
        const auto missingFacet = m_store->facetDistribution(
            *versionId, QStringLiteral( "missing_time" ), limits.maxClassValues );
        if ( !missingFacet.has_value() )
            return sicnu::data::Result<DatasetFacts>::failure( missingFacet.diagnostics() );
        qint64 missing = 0;
        bool sawTruthy = false;
        bool folded = false;
        for ( const auto &entry : missingFacet->values )
        {
            if ( entry.first == QStringLiteral( "(other)" ) )
            {
                folded = true;
                continue;
            }
            if ( truthy( entry.first ) )
            {
                missing += entry.second;
                sawTruthy = true;
            }
        }
        if ( folded && !sawTruthy )
            facts.factsTruncated = true;
        else
            facts.missingTimeCount = missing;
    }
    else
    {
        // Row-level cap: a page may carry more rows than the remaining
        // budget, so the cap is checked per row, not per page. Capped means
        // unscanned rows remained — the count then stays unknown.
        qint64 scanned = 0;
        qint64 missing = 0;
        bool capped = false;
        QString cursor;
        while ( !capped )
        {
            const auto page = m_store->samplesPageCursor(
                *versionId, cursor, sicnu::dataset::DatasetStore::kMaxPageSize );
            if ( !page.has_value() )
                return sicnu::data::Result<DatasetFacts>::failure( page.diagnostics() );
            for ( const sicnu::dataset::SampleRecord &sample : page->samples )
            {
                if ( scanned >= limits.maxSampleScan )
                {
                    capped = true;
                    break;
                }
                if ( !sample.timeUtc().isValid() )
                    ++missing;
                ++scanned;
            }
            if ( capped )
                break;
            if ( page->nextCursor.isEmpty() )
                break;
            cursor = page->nextCursor;
        }
        if ( capped )
            facts.factsTruncated = true;
        else
            facts.missingTimeCount = missing;
    }

    return sicnu::data::Result<DatasetFacts>::success( facts );
}

} // namespace sicnu::suitability
