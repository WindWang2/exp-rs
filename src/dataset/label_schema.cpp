// label_schema.cpp — ontology implementation.
#include "label_schema.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <QUuid>

namespace sicnu::dataset
{

namespace
{

bool validIdText( const QString &text )
{
    return !text.isEmpty() && !QUuid::fromString( text ).isNull();
}

} // namespace

// --- LabelClass ----------------------------------------------------------------

QJsonObject LabelClass::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "stable_id" ), m_stableId );
    json.insert( QStringLiteral( "code" ), m_code );
    if ( !m_displayName.isEmpty() )
        json.insert( QStringLiteral( "display_name" ), m_displayName );
    if ( !m_description.isEmpty() )
        json.insert( QStringLiteral( "description" ), m_description );
    if ( !m_parentCode.isEmpty() )
        json.insert( QStringLiteral( "parent_code" ), m_parentCode );
    if ( !m_colorHex.isEmpty() )
        json.insert( QStringLiteral( "color" ), m_colorHex );
    if ( m_background )
        json.insert( QStringLiteral( "background" ), true );
    if ( m_ignore )
        json.insert( QStringLiteral( "ignore" ), true );
    if ( m_unknown )
        json.insert( QStringLiteral( "unknown" ), true );
    if ( !m_aliases.isEmpty() )
        json.insert( QStringLiteral( "aliases" ), QJsonArray::fromStringList( m_aliases ) );
    if ( m_legacyIntId >= 0 )
        json.insert( QStringLiteral( "legacy_int_id" ), m_legacyIntId );
    if ( !m_metadata.isEmpty() )
        json.insert( QStringLiteral( "metadata" ), m_metadata );
    return json;
}

sicnu::data::Result<LabelClass> LabelClass::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LabelClass>;
    LabelClass labelClass;
    labelClass.m_stableId = json.value( QStringLiteral( "stable_id" ) ).toString();
    labelClass.m_code = json.value( QStringLiteral( "code" ) ).toString();
    if ( labelClass.m_code.isEmpty() )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.label_schema_invalid" ),
                                            QStringLiteral( "class without code" ),
                                            DiagnosticSeverity::Error } );
    }
    if ( !validIdText( labelClass.m_stableId ) )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.label_schema_invalid" ),
            QStringLiteral( "class '%1' requires a valid stable_id" ).arg( labelClass.m_code ),
            DiagnosticSeverity::Error } );
    }
    labelClass.m_stableId =
        QUuid::fromString( labelClass.m_stableId ).toString( QUuid::WithoutBraces );
    labelClass.m_displayName = json.value( QStringLiteral( "display_name" ) ).toString();
    labelClass.m_description = json.value( QStringLiteral( "description" ) ).toString();
    labelClass.m_parentCode = json.value( QStringLiteral( "parent_code" ) ).toString();
    labelClass.m_colorHex = json.value( QStringLiteral( "color" ) ).toString();
    labelClass.m_background = json.value( QStringLiteral( "background" ) ).toBool( false );
    labelClass.m_ignore = json.value( QStringLiteral( "ignore" ) ).toBool( false );
    labelClass.m_unknown = json.value( QStringLiteral( "unknown" ) ).toBool( false );
    labelClass.m_aliases = json.value( QStringLiteral( "aliases" ) ).toVariant().toStringList();
    labelClass.m_metadata = json.value( QStringLiteral( "metadata" ) ).toObject();
    labelClass.m_legacyIntId = json.value( QStringLiteral( "legacy_int_id" ) ).toInt( -1 );
    return Result::success( labelClass );
}

// --- LabelSchema ----------------------------------------------------------------

const LabelClass *LabelSchema::classByCode( const QString &code ) const
{
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( labelClass.code() == code )
            return &labelClass;
    }
    return nullptr;
}

const LabelClass *LabelSchema::classByStableId( const QString &stableId ) const
{
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( labelClass.stableId() == stableId )
            return &labelClass;
    }
    return nullptr;
}

QStringList LabelSchema::ancestorsOf( const QString &code ) const
{
    QStringList ancestors;
    QSet<QString> seen;
    QString current = code;
    while ( true )
    {
        const LabelClass *labelClass = classByCode( current );
        if ( !labelClass || labelClass->parentCode().isEmpty() )
            break;
        const QString &parent = labelClass->parentCode();
        if ( seen.contains( parent ) )
            break; // cycle guarded here too; validate() reports it
        seen.insert( parent );
        ancestors.prepend( parent );
        current = parent;
    }
    return ancestors;
}

QStringList LabelSchema::descendantsOf( const QString &code ) const
{
    QStringList descendants;
    QVector<QString> queue{ code };
    QSet<QString> visited{ code };
    while ( !queue.isEmpty() )
    {
        const QString current = queue.takeFirst();
        for ( const LabelClass &labelClass : m_classes )
        {
            if ( labelClass.parentCode() == current && !visited.contains( labelClass.code() ) )
            {
                visited.insert( labelClass.code() );
                descendants.append( labelClass.code() );
                queue.append( labelClass.code() );
            }
        }
    }
    return descendants;
}

QStringList LabelSchema::leafCodes() const
{
    QSet<QString> parents;
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( !labelClass.parentCode().isEmpty() )
            parents.insert( labelClass.parentCode() );
    }
    QStringList leaves;
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( !parents.contains( labelClass.code() ) )
            leaves.append( labelClass.code() );
    }
    return leaves;
}

sicnu::data::Result<void> LabelSchema::validate() const
{
    using Result = sicnu::data::Result<void>;
    auto fail = []( const QString &message ) {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.label_schema_invalid" ),
                                            message, DiagnosticSeverity::Error } );
    };
    if ( !validIdText( m_schemaId ) )
        return fail( QStringLiteral( "schema requires a valid schema_id" ) );
    if ( m_version == 0 )
        return fail( QStringLiteral( "schema version must be >= 1" ) );
    if ( m_classes.isEmpty() )
        return fail( QStringLiteral( "schema must declare at least one class" ) );

    QHash<QString, int> codeCount;
    QHash<QString, int> stableIdCount;
    for ( const LabelClass &labelClass : m_classes )
    {
        codeCount[labelClass.code()]++;
        stableIdCount[labelClass.stableId()]++;
    }
    for ( auto it = codeCount.constBegin(); it != codeCount.constEnd(); ++it )
    {
        if ( it.value() > 1 )
            return fail( QStringLiteral( "duplicate class code '%1'" ).arg( it.key() ) );
    }
    for ( auto it = stableIdCount.constBegin(); it != stableIdCount.constEnd(); ++it )
    {
        if ( it.value() > 1 )
            return fail( QStringLiteral( "duplicate class stable_id '%1'" ).arg( it.key() ) );
    }
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( !labelClass.parentCode().isEmpty() && !classByCode( labelClass.parentCode() ) )
            return fail( QStringLiteral( "class '%1' references unknown parent '%2'" )
                             .arg( labelClass.code(), labelClass.parentCode() ) );
    }

    // Acyclicity: walk up from every class; a schema-sized walk must
    // terminate within class count steps.
    for ( const LabelClass &labelClass : m_classes )
    {
        QString current = labelClass.code();
        for ( int step = 0; step <= m_classes.size(); ++step )
        {
            const LabelClass *parent = classByCode( current );
            if ( !parent || parent->parentCode().isEmpty() )
                break;
            current = parent->parentCode();
            if ( current == labelClass.code() )
                return fail( QStringLiteral( "hierarchy cycle through '%1'" )
                                 .arg( labelClass.code() ) );
        }
    }

    // At most one explicit background per schema keeps rasterization
    // unambiguous.
    int backgroundCount = 0;
    for ( const LabelClass &labelClass : m_classes )
    {
        if ( labelClass.isBackground() )
            ++backgroundCount;
    }
    if ( backgroundCount > 1 )
        return fail( QStringLiteral( "at most one background class allowed" ) );
    return Result::success();
}

QJsonObject LabelSchema::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kLabelSchemaSerializationVersion );
    json.insert( QStringLiteral( "schema_id" ), m_schemaId );
    json.insert( QStringLiteral( "version" ), qint64( m_version ) );
    if ( !m_parentSchemaId.isEmpty() )
        json.insert( QStringLiteral( "parent_schema_id" ), m_parentSchemaId );
    if ( !m_name.isEmpty() )
        json.insert( QStringLiteral( "name" ), m_name );
    if ( !m_description.isEmpty() )
        json.insert( QStringLiteral( "description" ), m_description );
    QJsonArray classArray;
    for ( const LabelClass &labelClass : m_classes )
        classArray.append( labelClass.toJson() );
    json.insert( QStringLiteral( "classes" ), classArray );
    return json;
}

sicnu::data::Result<LabelSchema> LabelSchema::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LabelSchema>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kLabelSchemaSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.label_schema_version" ),
            QStringLiteral( "label schema version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kLabelSchemaSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }
    LabelSchema schema;
    schema.m_schemaId = json.value( QStringLiteral( "schema_id" ) ).toString();
    schema.m_version = quint64(
        qMax<qint64>( 1, json.value( QStringLiteral( "version" ) ).toInteger( 1 ) ) );
    schema.m_parentSchemaId = json.value( QStringLiteral( "parent_schema_id" ) ).toString();
    schema.m_name = json.value( QStringLiteral( "name" ) ).toString();
    schema.m_description = json.value( QStringLiteral( "description" ) ).toString();
    for ( const QJsonValue &value : json.value( QStringLiteral( "classes" ) ).toArray() )
    {
        auto labelClass = LabelClass::fromJson( value.toObject() );
        if ( !labelClass )
            return Result::failure( labelClass.diagnostics() );
        schema.m_classes.append( labelClass.value() );
    }
    const auto validated = schema.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( schema );
}

// --- LabelMapping ----------------------------------------------------------------

std::optional<QString> LabelMapping::map( const QString &code ) const
{
    for ( const LabelMappingRule &rule : m_rules )
    {
        if ( rule.fromCode == code )
            return rule.toCode;
    }
    return std::nullopt;
}

sicnu::data::Result<QStringList> LabelMapping::covers( const LabelSchema &from ) const
{
    using Result = sicnu::data::Result<QStringList>;
    if ( m_fromSchemaId != from.schemaId() || m_fromSchemaVersion != from.version() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.label_mapping_mismatch" ),
            QStringLiteral( "mapping targets %1@%2, schema is %3@%4" )
                .arg( m_fromSchemaId )
                .arg( m_fromSchemaVersion )
                .arg( from.schemaId() )
                .arg( from.version() ),
            DiagnosticSeverity::Error,
        } );
    }
    QStringList unmapped;
    for ( const LabelClass &labelClass : from.classes() )
    {
        // Background/ignore/unknown classes carry evaluation semantics, not
        // training targets; they are allowed to be unmapped.
        if ( labelClass.isBackground() || labelClass.isIgnore() || labelClass.isUnknown() )
            continue;
        // Leaf classes must be mapped; mapping every leaf covers the tree.
        if ( !from.descendantsOf( labelClass.code() ).isEmpty() )
            continue;
        if ( !map( labelClass.code() ).has_value() )
            unmapped.append( labelClass.code() );
    }
    return Result::success( unmapped );
}

QJsonObject LabelMapping::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kLabelSchemaSerializationVersion );
    json.insert( QStringLiteral( "name" ), m_name );
    json.insert( QStringLiteral( "from_schema_id" ), m_fromSchemaId );
    json.insert( QStringLiteral( "from_schema_version" ), qint64( m_fromSchemaVersion ) );
    json.insert( QStringLiteral( "to_schema_id" ), m_toSchemaId );
    json.insert( QStringLiteral( "to_schema_version" ), qint64( m_toSchemaVersion ) );
    QJsonArray ruleArray;
    for ( const LabelMappingRule &rule : m_rules )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "from" ), rule.fromCode );
        item.insert( QStringLiteral( "to" ), rule.toCode );
        ruleArray.append( item );
    }
    json.insert( QStringLiteral( "rules" ), ruleArray );
    return json;
}

sicnu::data::Result<LabelMapping> LabelMapping::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LabelMapping>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kLabelSchemaSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.label_mapping_version" ),
            QStringLiteral( "label mapping version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kLabelSchemaSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }
    LabelMapping mapping;
    mapping.m_name = json.value( QStringLiteral( "name" ) ).toString();
    mapping.m_fromSchemaId = json.value( QStringLiteral( "from_schema_id" ) ).toString();
    mapping.m_fromSchemaVersion = quint64( qMax<qint64>(
        1, json.value( QStringLiteral( "from_schema_version" ) ).toInteger( 1 ) ) );
    mapping.m_toSchemaId = json.value( QStringLiteral( "to_schema_id" ) ).toString();
    mapping.m_toSchemaVersion = quint64( qMax<qint64>(
        1, json.value( QStringLiteral( "to_schema_version" ) ).toInteger( 1 ) ) );
    if ( mapping.m_fromSchemaId.isEmpty() || mapping.m_toSchemaId.isEmpty() )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.label_mapping_invalid" ),
                                            QStringLiteral( "mapping requires from/to schema ids" ),
                                            DiagnosticSeverity::Error } );
    }
    for ( const QJsonValue &value : json.value( QStringLiteral( "rules" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        LabelMappingRule rule;
        rule.fromCode = item.value( QStringLiteral( "from" ) ).toString();
        rule.toCode = item.value( QStringLiteral( "to" ) ).toString();
        if ( rule.fromCode.isEmpty() || rule.toCode.isEmpty() )
        {
            return Result::failure( Diagnostic{ QStringLiteral( "dataset.label_mapping_invalid" ),
                                                QStringLiteral( "mapping rules require from + to codes" ),
                                                DiagnosticSeverity::Error } );
        }
        mapping.m_rules.append( rule );
    }
    return Result::success( mapping );
}

} // namespace sicnu::dataset
