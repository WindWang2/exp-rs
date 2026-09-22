/***************************************************************************
  scientific_state/catalog/catalog_state_facts.cpp
  RS14-01 Scientific Data Passport — catalog adapter (Qt-typed).
 ***************************************************************************/

#include "scientific_state/catalog/catalog_state_facts.h"

#include <data/band_role.h>
#include <data/asset_types.h>

#include <QDateTime>
#include <QString>
#include <QVector>

#include <variant>

namespace sicnu::state
{

namespace
{

std::string toStd( const QString &text )
{
    return text.toStdString();
}

std::string assetKindToVocabulary( sicnu::data::AssetKind kind )
{
    using sicnu::data::AssetKind;
    switch ( kind )
    {
        case AssetKind::Raster: return "raster";
        case AssetKind::Vector: return "vector";
        case AssetKind::RemoteMap: return "remote_map";
        case AssetKind::VirtualRaster: return "virtual_raster";
    }
    return "unknown";
}

std::string assetStateToVocabulary( sicnu::data::AssetState state )
{
    using sicnu::data::AssetState;
    switch ( state )
    {
        case AssetState::Registered: return "registered";
        case AssetState::Resolving: return "resolving";
        case AssetState::Ready: return "ready";
        case AssetState::Missing: return "missing";
        case AssetState::UnavailableSource: return "unavailable_source";
        case AssetState::Offline: return "offline";
        case AssetState::AuthenticationRequired: return "authentication_required";
        case AssetState::Error: return "error";
        case AssetState::Stale: return "stale";
    }
    return "unknown";
}

std::string persistenceToVocabulary( sicnu::data::PersistencePolicy policy )
{
    using sicnu::data::PersistencePolicy;
    switch ( policy )
    {
        case PersistencePolicy::ProjectPersistent: return "project_persistent";
        case PersistencePolicy::SessionTemporary: return "session_temporary";
        case PersistencePolicy::TaskTemporary: return "task_temporary";
    }
    return "unknown";
}

} // namespace

CatalogFacts makeCatalogFacts( const sicnu::data::AssetSnapshot &snapshot )
{
    CatalogFacts facts;
    facts.assetId = toStd( snapshot.id().toString() );
    facts.revision = std::to_string( snapshot.revision().value() );
    facts.displayName = toStd( snapshot.displayName() );
    facts.kind = assetKindToVocabulary( snapshot.kind() );
    facts.lifecycle = assetStateToVocabulary( snapshot.state() );
    facts.persistence = persistenceToVocabulary( snapshot.persistence() );
    facts.sourcePath = toStd( snapshot.source().canonicalSource );

    if ( snapshot.acquisitionTime().has_value() && snapshot.acquisitionTime()->isValid() )
        facts.acquisitionTimeIso = toStd( snapshot.acquisitionTime()->toString( Qt::ISODate ) );

    const sicnu::data::RasterStructure *raster =
        std::get_if<sicnu::data::RasterStructure>( &snapshot.structure() );
    if ( raster )
    {
        facts.structureBandCount = raster->bandCount;
        for ( const sicnu::data::RasterBandStructure &band : raster->bands )
        {
            CatalogBandFacts bandFacts;
            bandFacts.index = band.number;
            bandFacts.role = toStd( sicnu::data::bandRoleToString( band.role ) );
            bandFacts.dataType = toStd( band.dataType );
            if ( band.noDataValue.has_value() )
            {
                bandFacts.hasNoData = true;
                bandFacts.noDataValue = *band.noDataValue;
            }
            facts.bands.push_back( bandFacts );
        }
    }

    return facts;
}

DerivationFacts makeDerivationFacts( const sicnu::data::DerivationRecord &record )
{
    DerivationFacts facts;
    facts.algorithmId = toStd( record.algorithmId );
    facts.algorithmVersion = toStd( record.algorithmVersion );
    for ( const sicnu::data::DerivationInput &input : record.inputs )
    {
        DerivationFacts::Input projected;
        projected.assetId = toStd( input.assetId.toString() );
        projected.revision = std::to_string( input.revision.value() );
        for ( const QString &bandReference : input.bandReferences )
            projected.bandReferences.push_back( toStd( bandReference ) );
        projected.valueDomain = toStd( input.valueDomain );
        facts.inputs.push_back( projected );
    }
    if ( record.completedAtUtc.isValid() )
        facts.completedAtUtc = toStd( record.completedAtUtc.toString( Qt::ISODate ) );
    facts.executionFingerprint = toStd( record.executionFingerprint );
    facts.softwareVersion = toStd( record.softwareVersion );

    // Composite workflow reference: the non-empty parts of
    // workflowId / workflowRunId / stepId joined by '/'.
    const QStringList workflowParts = { record.workflowId, record.workflowRunId,
                                        record.stepId };
    QStringList nonEmpty;
    for ( const QString &part : workflowParts )
    {
        if ( !part.isEmpty() )
            nonEmpty.push_back( part );
    }
    if ( !nonEmpty.isEmpty() )
        facts.workflowRef = toStd( nonEmpty.join( QStringLiteral( "/" ) ) );

    facts.cacheHit = record.cacheHit;
    return facts;
}

} // namespace sicnu::state
