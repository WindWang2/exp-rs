// governance_types.cpp — string contracts for governance vocabulary.
#include "governance_types.h"

#include <QHash>
#include <QUuid>

namespace sicnu::workspace
{

namespace
{

QString makeIdText()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

std::optional<QString> normalizeIdText( const QString &text )
{
    const QUuid uuid = QUuid::fromString( text );
    if ( uuid.isNull() )
        return std::nullopt;
    return uuid.toString( QUuid::WithoutBraces );
}

} // namespace

#define SICNU_WORKSPACE_ID_IMPL( Name )                                                       \
    Name Name::generate() { return Name( makeIdText() ); }                                     \
    std::optional<Name> Name::fromString( const QString &text )                                 \
    {                                                                                          \
        const std::optional<QString> normalized = normalizeIdText( text );                      \
        if ( !normalized )                                                                     \
            return std::nullopt;                                                               \
        return Name( *normalized );                                                            \
    }                                                                                          \
    bool Name::isNull() const { return m_value.isEmpty(); }                                    \
    QString Name::toString() const { return m_value; }                                         \
    Name::Name( QString value ) : m_value( std::move( value ) ) {}

SICNU_WORKSPACE_ID_IMPL( WorkspaceId )
SICNU_WORKSPACE_ID_IMPL( DatasetId )
SICNU_WORKSPACE_ID_IMPL( ResultId )
SICNU_WORKSPACE_ID_IMPL( ExperimentId )
SICNU_WORKSPACE_ID_IMPL( SmartCollectionId )
SICNU_WORKSPACE_ID_IMPL( ExportId )

#undef SICNU_WORKSPACE_ID_IMPL

QString resultStatusToString( ResultStatus status )
{
    switch ( status )
    {
        case ResultStatus::Draft: return QStringLiteral( "draft" );
        case ResultStatus::Validated: return QStringLiteral( "validated" );
        case ResultStatus::Approved: return QStringLiteral( "approved" );
        case ResultStatus::Rejected: return QStringLiteral( "rejected" );
        case ResultStatus::Superseded: return QStringLiteral( "superseded" );
        case ResultStatus::Archived: return QStringLiteral( "archived" );
    }
    return QStringLiteral( "draft" );
}

std::optional<ResultStatus> resultStatusFromString( const QString &text )
{
    if ( text == QLatin1String( "draft" ) ) return ResultStatus::Draft;
    if ( text == QLatin1String( "validated" ) ) return ResultStatus::Validated;
    if ( text == QLatin1String( "approved" ) ) return ResultStatus::Approved;
    if ( text == QLatin1String( "rejected" ) ) return ResultStatus::Rejected;
    if ( text == QLatin1String( "superseded" ) ) return ResultStatus::Superseded;
    if ( text == QLatin1String( "archived" ) ) return ResultStatus::Archived;
    return std::nullopt;
}

bool isLegalResultTransition( ResultStatus from, ResultStatus to )
{
    if ( from == to )
        return true;
    switch ( from )
    {
        case ResultStatus::Draft:
            return to == ResultStatus::Validated || to == ResultStatus::Rejected
                   || to == ResultStatus::Archived;
        case ResultStatus::Validated:
            return to == ResultStatus::Approved || to == ResultStatus::Rejected
                   || to == ResultStatus::Superseded || to == ResultStatus::Archived;
        case ResultStatus::Approved:
            return to == ResultStatus::Superseded || to == ResultStatus::Archived
                   || to == ResultStatus::Rejected;
        case ResultStatus::Rejected:
            return to == ResultStatus::Draft || to == ResultStatus::Archived;
        case ResultStatus::Superseded:
            return to == ResultStatus::Archived;
        case ResultStatus::Archived:
            return false;
    }
    return false;
}

QString resultSemanticTypeToString( ResultSemanticType type )
{
    switch ( type )
    {
        case ResultSemanticType::Classification: return QStringLiteral( "classification" );
        case ResultSemanticType::ChangeDetection: return QStringLiteral( "change_detection" );
        case ResultSemanticType::Index: return QStringLiteral( "index" );
        case ResultSemanticType::Segmentation: return QStringLiteral( "segmentation" );
        case ResultSemanticType::VectorExtraction: return QStringLiteral( "vector_extraction" );
        case ResultSemanticType::Statistics: return QStringLiteral( "statistics" );
        case ResultSemanticType::Chart: return QStringLiteral( "chart" );
        case ResultSemanticType::Map: return QStringLiteral( "map" );
        case ResultSemanticType::Export: return QStringLiteral( "export" );
        case ResultSemanticType::Other: return QStringLiteral( "other" );
    }
    return QStringLiteral( "other" );
}

std::optional<ResultSemanticType> resultSemanticTypeFromString( const QString &text )
{
    if ( text == QLatin1String( "classification" ) ) return ResultSemanticType::Classification;
    if ( text == QLatin1String( "change_detection" ) ) return ResultSemanticType::ChangeDetection;
    if ( text == QLatin1String( "index" ) ) return ResultSemanticType::Index;
    if ( text == QLatin1String( "segmentation" ) ) return ResultSemanticType::Segmentation;
    if ( text == QLatin1String( "vector_extraction" ) ) return ResultSemanticType::VectorExtraction;
    if ( text == QLatin1String( "statistics" ) ) return ResultSemanticType::Statistics;
    if ( text == QLatin1String( "chart" ) ) return ResultSemanticType::Chart;
    if ( text == QLatin1String( "map" ) ) return ResultSemanticType::Map;
    if ( text == QLatin1String( "export" ) ) return ResultSemanticType::Export;
    if ( text == QLatin1String( "other" ) ) return ResultSemanticType::Other;
    return std::nullopt;
}

QString datasetKindToString( DatasetKind kind )
{
    switch ( kind )
    {
        case DatasetKind::SingleRaster: return QStringLiteral( "single_raster" );
        case DatasetKind::MultiBandProduct: return QStringLiteral( "multi_band_product" );
        case DatasetKind::Temporal: return QStringLiteral( "temporal" );
        case DatasetKind::Sar: return QStringLiteral( "sar" );
        case DatasetKind::Training: return QStringLiteral( "training" );
        case DatasetKind::Reference: return QStringLiteral( "reference" );
        case DatasetKind::ModelInput: return QStringLiteral( "model_input" );
        case DatasetKind::Group: return QStringLiteral( "group" );
    }
    return QStringLiteral( "group" );
}

std::optional<DatasetKind> datasetKindFromString( const QString &text )
{
    if ( text == QLatin1String( "single_raster" ) ) return DatasetKind::SingleRaster;
    if ( text == QLatin1String( "multi_band_product" ) ) return DatasetKind::MultiBandProduct;
    if ( text == QLatin1String( "temporal" ) ) return DatasetKind::Temporal;
    if ( text == QLatin1String( "sar" ) ) return DatasetKind::Sar;
    if ( text == QLatin1String( "training" ) ) return DatasetKind::Training;
    if ( text == QLatin1String( "reference" ) ) return DatasetKind::Reference;
    if ( text == QLatin1String( "model_input" ) ) return DatasetKind::ModelInput;
    if ( text == QLatin1String( "group" ) ) return DatasetKind::Group;
    return std::nullopt;
}

QString diagnosticKindToString( DiagnosticKind kind )
{
    switch ( kind )
    {
        case DiagnosticKind::MissingFile: return QStringLiteral( "missing_file" );
        case DiagnosticKind::ChangedContent: return QStringLiteral( "changed_content" );
        case DiagnosticKind::ChangedMetadata: return QStringLiteral( "changed_metadata" );
        case DiagnosticKind::BrokenUri: return QStringLiteral( "broken_uri" );
        case DiagnosticKind::UnsupportedFormat: return QStringLiteral( "unsupported_format" );
        case DiagnosticKind::CrsMissing: return QStringLiteral( "crs_missing" );
        case DiagnosticKind::BandCountChanged: return QStringLiteral( "band_count_changed" );
        case DiagnosticKind::ModelArtifactMissing: return QStringLiteral( "model_artifact_missing" );
        case DiagnosticKind::CollectionMemberMissing: return QStringLiteral( "collection_member_missing" );
        case DiagnosticKind::OrphanResult: return QStringLiteral( "orphan_result" );
        case DiagnosticKind::WorkflowReferenceMissing: return QStringLiteral( "workflow_reference_missing" );
        case DiagnosticKind::DuplicateAsset: return QStringLiteral( "duplicate_asset" );
        case DiagnosticKind::StaleCatalog: return QStringLiteral( "stale_catalog" );
        case DiagnosticKind::StoreCorruption: return QStringLiteral( "store_corruption" );
        case DiagnosticKind::PathOutsideProject: return QStringLiteral( "path_outside_project" );
        case DiagnosticKind::Other: return QStringLiteral( "other" );
    }
    return QStringLiteral( "other" );
}

DiagnosticKind diagnosticKindFromString( const QString &text, DiagnosticKind fallback )
{
    static const QHash<QString, DiagnosticKind> kMap = {
        { QStringLiteral( "missing_file" ), DiagnosticKind::MissingFile },
        { QStringLiteral( "changed_content" ), DiagnosticKind::ChangedContent },
        { QStringLiteral( "changed_metadata" ), DiagnosticKind::ChangedMetadata },
        { QStringLiteral( "broken_uri" ), DiagnosticKind::BrokenUri },
        { QStringLiteral( "unsupported_format" ), DiagnosticKind::UnsupportedFormat },
        { QStringLiteral( "crs_missing" ), DiagnosticKind::CrsMissing },
        { QStringLiteral( "band_count_changed" ), DiagnosticKind::BandCountChanged },
        { QStringLiteral( "model_artifact_missing" ), DiagnosticKind::ModelArtifactMissing },
        { QStringLiteral( "collection_member_missing" ), DiagnosticKind::CollectionMemberMissing },
        { QStringLiteral( "orphan_result" ), DiagnosticKind::OrphanResult },
        { QStringLiteral( "workflow_reference_missing" ), DiagnosticKind::WorkflowReferenceMissing },
        { QStringLiteral( "duplicate_asset" ), DiagnosticKind::DuplicateAsset },
        { QStringLiteral( "stale_catalog" ), DiagnosticKind::StaleCatalog },
        { QStringLiteral( "store_corruption" ), DiagnosticKind::StoreCorruption },
        { QStringLiteral( "path_outside_project" ), DiagnosticKind::PathOutsideProject },
        { QStringLiteral( "other" ), DiagnosticKind::Other },
    };
    return kMap.value( text, fallback );
}

} // namespace sicnu::workspace
