/***************************************************************************
 * operator_help_provider.cpp — schema-derived operator/parameter composition
 ***************************************************************************/
#include "help/operator_help_provider.h"

#include "help/help_catalog_source.h"
#include "help/help_id.h"

#include <QSet>

namespace sicnu::help
{
namespace
{

/// Applies additive operator knowledge onto the derived descriptor.
void applyKnowledge( HelpDescriptor &derived, const HelpDescriptor &knowledge )
{
    if ( derived.title.isEmpty() )
        derived.title = knowledge.title;
    if ( derived.summary.isEmpty() )
        derived.summary = knowledge.summary;
    if ( derived.category.isEmpty() )
        derived.category = knowledge.category;
    for ( const QString &keyword : knowledge.keywords )
        if ( !derived.keywords.contains( keyword ) )
            derived.keywords << keyword;
    for ( const QString &related : knowledge.relatedIds )
        if ( !derived.relatedIds.contains( related ) )
            derived.relatedIds << related;
    for ( const QString &diag : knowledge.diagnosticIds )
        if ( !derived.diagnosticIds.contains( diag ) )
            derived.diagnosticIds << diag;
    for ( const QString &doc : knowledge.docRefs )
        if ( !derived.docRefs.contains( doc ) )
            derived.docRefs << doc;
    if ( knowledge.algorithm.has_value() )
        derived.algorithm = knowledge.algorithm;
    // deprecation is knowledge-level state: an entry marked deprecated in
    // content stays deprecated after the derived upsert
    if ( knowledge.deprecated ) {
        derived.deprecated = true;
        if ( !knowledge.supersededBy.isEmpty() )
            derived.supersededBy = knowledge.supersededBy;
    }
}

QString jsonToDisplayText( const Json::Value &value )
{
    if ( value.isNull() )
        return QString();
    if ( value.isString() )
        return QString::fromStdString( value.asString() );
    if ( value.isBool() )
        return value.asBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
    if ( value.isIntegral() )
        return QString::number( value.asInt64() );
    if ( value.isNumeric() )
        return QString::number( value.asDouble() );
    if ( value.isArray() ) {
        QStringList parts;
        for ( const Json::Value &item : value )
            parts << jsonToDisplayText( item );
        return parts.join( QStringLiteral( ", " ) );
    }
    return QString();
}

} // namespace

QVector<ParameterFact> OperatorHelpProvider::parameterFacts( const OperatorFact &fact )
{
    QVector<ParameterFact> facts;
    if ( !fact.schema.isObject() || !fact.schema["properties"].isObject() )
        return facts;

    QSet<QString> requiredNames;
    if ( fact.schema["required"].isArray() ) {
        for ( const Json::Value &name : fact.schema["required"] ) {
            if ( name.isString() )
                requiredNames.insert( QString::fromStdString( name.asString() ) );
        }
    }

    const Json::Value &properties = fact.schema["properties"];
    for ( auto it = properties.begin(); it != properties.end(); ++it ) {
        // iterator key() yields a Json::Value temp in this jsoncpp version
        const QString paramName = QString::fromStdString( it.key().asString() );
        const Json::Value &prop = *it;

        ParameterFact pf;
        pf.name = paramName;
        pf.helpId = HelpId::parameterId( fact.id, paramName );
        pf.description = QString::fromStdString( prop.get( "description", "" ).asString() );
        pf.required = requiredNames.contains( paramName ) || prop.get( "required", false ).asBool();

        QString type = QString::fromStdString( prop.get( "type", "" ).asString() );
        const QString format = QString::fromStdString( prop.get( "format", "" ).asString() );
        if ( prop.isMember( "enum" ) ) {
            pf.type = QStringLiteral( "enum" );
            for ( const Json::Value &value : prop["enum"] )
                pf.enumValues << QString::fromStdString( value.asString() );
        } else if ( !format.isEmpty() ) {
            pf.type = format; // raster | vector | tif | ...
        } else {
            pf.type = type;
        }
        if ( prop.isMember( "items" ) )
            pf.type = QStringLiteral( "array<%1>" ).arg(
                QString::fromStdString( prop["items"].get( "type", "" ).asString() ) );

        if ( prop.isMember( "default" ) && !prop["default"].isNull() ) {
            pf.hasDefault = true;
            pf.defaultText = jsonToDisplayText( prop["default"] );
        }
        if ( prop.isMember( "minimum" ) && prop.isMember( "maximum" ) ) {
            pf.hasRange = true;
            pf.minimum = prop["minimum"].asDouble();
            pf.maximum = prop["maximum"].asDouble();
        }
        facts.push_back( pf );
    }
    return facts;
}

ParameterHelpEntry OperatorHelpProvider::parameterHelp( const OperatorFact &fact, const QString &paramName,
                                                        const HelpRegistry &knowledge )
{
    ParameterHelpEntry entry;
    entry.operatorId = fact.id;
    entry.operatorHelpId = QStringLiteral( "operator.%1" ).arg( HelpId::domainForOperatorId( fact.id ) );
    for ( const ParameterFact &candidate : parameterFacts( fact ) ) {
        if ( candidate.name == paramName ) {
            entry.fact = candidate;
            break;
        }
    }
    if ( const HelpDescriptor *k = knowledge.find( entry.fact.helpId ) )
        entry.knowledge = k->parameter;
    return entry;
}

void OperatorHelpProvider::compose( const OperatorCatalogSource &source, const HelpRegistry &knowledge,
                                    HelpRegistry &out, QStringList *errors )
{
    auto report = [errors]( const QString &message ) {
        if ( errors )
            *errors << message;
    };

    QSet<QString> covered;
    for ( const OperatorFact &fact : source.operators() ) {
        HelpDescriptor d;
        d.id = QStringLiteral( "operator.%1" ).arg( HelpId::domainForOperatorId( fact.id ) );
        if ( !HelpId::isValid( d.id ) ) {
            report( QStringLiteral( "operator id not help-grammar compatible: %1" ).arg( d.id ) );
            continue;
        }
        d.kind = HelpKind::Operator;
        d.title = fact.displayName.isEmpty() ? fact.id : fact.displayName;
        d.summary = fact.description;
        d.category = fact.group;

        if ( const HelpDescriptor *k = knowledge.find( d.id ) )
            applyKnowledge( d, *k );
        covered.insert( d.id );

        // Copy values needed after d is moved into the registry.
        const QString parameterPrefix =
            QStringLiteral( "parameter.%1." ).arg( HelpId::domainForOperatorId( fact.id ) );
        const QString category = d.category;

        // Register a descriptor for every schema-visible parameter: base tier
        // from the schema (type/range/default/description), curated knowledge
        // merged in where present. Knowledge pointing at params the schema
        // does not declare is a drift error.
        const QVector<ParameterFact> facts = parameterFacts( fact );
        QSet<QString> schemaParamIds;
        for ( const ParameterFact &pf : facts ) {
            schemaParamIds.insert( pf.helpId );
            HelpDescriptor p;
            p.id = pf.helpId;
            p.kind = HelpKind::Parameter;
            p.title = pf.name;
            p.summary = pf.description; // authoritative schema description
            p.category = category;
            ParameterKnowledge paramKnowledge;
            paramKnowledge.curated = false;
            if ( const HelpDescriptor *k = knowledge.find( pf.helpId ) ) {
                if ( k->parameter.has_value() )
                    paramKnowledge = *k->parameter;
                p.keywords = k->keywords;
                p.relatedIds = k->relatedIds;
                p.docRefs = k->docRefs;
                if ( !k->title.isEmpty() )
                    p.title = k->title;
            }
            p.parameter = paramKnowledge;
            QString error;
            if ( !out.upsertDescriptor( std::move( p ), &error ) )
                report( error );
        }
        for ( const HelpDescriptor *k : knowledge.byKind( HelpKind::Parameter ) ) {
            if ( !k->id.startsWith( parameterPrefix ) )
                continue;
            if ( !schemaParamIds.contains( k->id ) ) {
                report( QStringLiteral( "parameter knowledge %1 does not exist in schema of %2" )
                            .arg( k->id, d.id ) );
            }
        }

        QString error;
        if ( !out.upsertDescriptor( std::move( d ), &error ) )
            report( error );
    }

    for ( const HelpDescriptor *k : knowledge.byKind( HelpKind::Operator ) ) {
        if ( !covered.contains( k->id ) )
            report( QStringLiteral( "operator knowledge without registered operator: %1" ).arg( k->id ) );
    }
}

} // namespace sicnu::help
