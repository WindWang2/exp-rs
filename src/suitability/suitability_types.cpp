#include "suitability_types.h"

#include <QJsonArray>

namespace sicnu::suitability
{

namespace
{

QJsonObject diagnosticToJson( const sicnu::data::Diagnostic &diagnostic )
{
    QJsonObject json;
    json.insert( QStringLiteral( "code" ), diagnostic.code );
    json.insert( QStringLiteral( "message" ), diagnostic.message );
    QString severity;
    switch ( diagnostic.severity )
    {
        case sicnu::data::DiagnosticSeverity::Error:
            severity = QStringLiteral( "error" );
            break;
        case sicnu::data::DiagnosticSeverity::Warning:
            severity = QStringLiteral( "warning" );
            break;
        case sicnu::data::DiagnosticSeverity::Info:
            severity = QStringLiteral( "info" );
            break;
    }
    json.insert( QStringLiteral( "severity" ), severity );
    return json;
}

sicnu::data::Diagnostic diagnosticFromJson( const QJsonObject &json )
{
    sicnu::data::Diagnostic diagnostic;
    diagnostic.code = json.value( QStringLiteral( "code" ) ).toString();
    diagnostic.message = json.value( QStringLiteral( "message" ) ).toString();
    const QString severity = json.value( QStringLiteral( "severity" ) ).toString();
    if ( severity == QLatin1String( "error" ) )
        diagnostic.severity = sicnu::data::DiagnosticSeverity::Error;
    else if ( severity == QLatin1String( "warning" ) )
        diagnostic.severity = sicnu::data::DiagnosticSeverity::Warning;
    else
        diagnostic.severity = sicnu::data::DiagnosticSeverity::Info;
    return diagnostic;
}

} // namespace

// --- SuitabilityGap ------------------------------------------------------------

QJsonObject SuitabilityGap::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSuitabilityGapSerializationVersion );
    json.insert( QStringLiteral( "id" ), id );
    json.insert( QStringLiteral( "criterion_id" ), criterionId );
    json.insert( QStringLiteral( "description" ), description );
    json.insert( QStringLiteral( "evidence" ), evidence );
    return json;
}

sicnu::data::Result<SuitabilityGap> SuitabilityGap::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kSuitabilityGapSerializationVersion )
    {
        return sicnu::data::Result<SuitabilityGap>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.gap_schema" ),
            QStringLiteral( "unsupported gap schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    const QString id = json.value( QStringLiteral( "id" ) ).toString();
    if ( id.isEmpty() )
    {
        return sicnu::data::Result<SuitabilityGap>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.gap_invalid" ),
            QStringLiteral( "gap is missing its id" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    SuitabilityGap gap;
    gap.id = id;
    gap.criterionId = json.value( QStringLiteral( "criterion_id" ) ).toString();
    gap.description = json.value( QStringLiteral( "description" ) ).toString();
    gap.evidence = json.value( QStringLiteral( "evidence" ) ).toObject();
    return sicnu::data::Result<SuitabilityGap>::success( gap );
}

// --- SuitabilityCriterion ------------------------------------------------------

QJsonObject SuitabilityCriterion::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSuitabilityCriterionSerializationVersion );
    json.insert( QStringLiteral( "id" ), id );
    json.insert( QStringLiteral( "applicable" ), applicable );
    json.insert( QStringLiteral( "level" ), suitabilityLevelToString( level ) );
    json.insert( QStringLiteral( "summary" ), summary );
    json.insert( QStringLiteral( "evidence" ), evidence );

    QJsonArray notesArray;
    for ( const QString &note : notes )
        notesArray.append( note );
    json.insert( QStringLiteral( "notes" ), notesArray );

    QJsonArray gapsArray;
    for ( const SuitabilityGap &gap : gaps )
        gapsArray.append( gap.toJson() );
    json.insert( QStringLiteral( "gaps" ), gapsArray );

    QJsonArray diagnosticsArray;
    for ( const sicnu::data::Diagnostic &diagnostic : diagnostics )
        diagnosticsArray.append( diagnosticToJson( diagnostic ) );
    json.insert( QStringLiteral( "diagnostics" ), diagnosticsArray );

    return json;
}

sicnu::data::Result<SuitabilityCriterion> SuitabilityCriterion::fromJson( const QJsonObject &json )
{
    const int version = json.value( QStringLiteral( "schema_version" ) ).toInt( -1 );
    if ( version != kSuitabilityCriterionSerializationVersion )
    {
        return sicnu::data::Result<SuitabilityCriterion>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.criterion_schema" ),
            QStringLiteral( "unsupported criterion schema_version %1" ).arg( version ),
            sicnu::data::DiagnosticSeverity::Error } );
    }
    const QString id = json.value( QStringLiteral( "id" ) ).toString();
    const QString levelText = json.value( QStringLiteral( "level" ) ).toString();
    const auto level = suitabilityLevelFromString( levelText );
    if ( id.isEmpty() || !level.has_value() )
    {
        return sicnu::data::Result<SuitabilityCriterion>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.criterion_invalid" ),
            QStringLiteral( "criterion is missing its id or carries an unknown level" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SuitabilityCriterion criterion;
    criterion.id = id;
    criterion.applicable = json.value( QStringLiteral( "applicable" ) ).toBool( true );
    criterion.level = *level;
    criterion.summary = json.value( QStringLiteral( "summary" ) ).toString();
    criterion.evidence = json.value( QStringLiteral( "evidence" ) ).toObject();

    const QJsonArray notesArray = json.value( QStringLiteral( "notes" ) ).toArray();
    for ( const QJsonValue &note : notesArray )
        criterion.notes.append( note.toString() );

    const QJsonArray gapsArray = json.value( QStringLiteral( "gaps" ) ).toArray();
    for ( const QJsonValue &gapValue : gapsArray )
    {
        const auto gap = SuitabilityGap::fromJson( gapValue.toObject() );
        if ( !gap.has_value() )
        {
            return sicnu::data::Result<SuitabilityCriterion>::failure( gap.diagnostics() );
        }
        criterion.gaps.append( gap.value() );
    }

    const QJsonArray diagnosticsArray = json.value( QStringLiteral( "diagnostics" ) ).toArray();
    for ( const QJsonValue &diagnosticValue : diagnosticsArray )
        criterion.diagnostics.append( diagnosticFromJson( diagnosticValue.toObject() ) );

    return sicnu::data::Result<SuitabilityCriterion>::success( criterion );
}

bool criterionIdLessThan( const SuitabilityCriterion &a, const SuitabilityCriterion &b )
{
    return a.id < b.id;
}

} // namespace sicnu::suitability
