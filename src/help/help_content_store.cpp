/***************************************************************************
 * help_content_store.cpp — JSON knowledge loader implementation
 ***************************************************************************/
#include "help/help_content_store.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>

namespace sicnu::help
{
namespace
{

QString readString( const Json::Value &value, const char *key )
{
    const Json::Value &member = value[key];
    return member.isString() ? QString::fromStdString( member.asString() ) : QString();
}

QStringList readStringList( const Json::Value &value, const char *key )
{
    QStringList out;
    const Json::Value &member = value[key];
    if ( member.isArray() ) {
        for ( const Json::Value &item : member ) {
            if ( item.isString() )
                out << QString::fromStdString( item.asString() );
        }
    }
    return out;
}

sicnu::data::DiagnosticSeverity parseSeverity( const QString &text, sicnu::data::DiagnosticSeverity fallback )
{
    if ( text == QLatin1String( "info" ) )
        return sicnu::data::DiagnosticSeverity::Info;
    if ( text == QLatin1String( "warning" ) )
        return sicnu::data::DiagnosticSeverity::Warning;
    if ( text == QLatin1String( "error" ) )
        return sicnu::data::DiagnosticSeverity::Error;
    return fallback;
}

} // namespace

void HelpContentStore::parseCommon( HelpDescriptor &d, const Json::Value &entry, QStringList &errors,
                                    const QString &context )
{
    d.title = readString( entry, "title" );
    d.summary = readString( entry, "summary" );
    d.category = readString( entry, "category" );
    d.keywords = readStringList( entry, "keywords" );
    d.relatedIds = readStringList( entry, "related" );
    d.diagnosticIds = readStringList( entry, "diagnostics" );
    d.docRefs = readStringList( entry, "docs" );
    d.deprecated = entry.isMember( "deprecated" ) && entry["deprecated"].asBool();
    d.supersededBy = readString( entry, "supersededBy" );

    if ( entry.isMember( "algorithm" ) ) {
        AlgorithmPage page;
        const Json::Value &a = entry["algorithm"];
        page.whatItDoes = readString( a, "whatItDoes" );
        page.whenToUse = readString( a, "whenToUse" );
        page.inputs = readStringList( a, "inputs" );
        page.outputs = readStringList( a, "outputs" );
        page.assumptions = readStringList( a, "assumptions" );
        page.unitsDomain = readString( a, "unitsDomain" );
        page.keyParameters = readStringList( a, "keyParameters" );
        page.limitations = readStringList( a, "limitations" );
        page.failureModes = readStringList( a, "failureModes" );
        page.relatedTools = readStringList( a, "relatedTools" );
        d.algorithm = page;
    }

    if ( entry.isMember( "guidance" ) ) {
        GuidanceDescriptor g;
        const Json::Value &jg = entry["guidance"];
        g.headline = readString( jg, "headline" );
        g.body = readString( jg, "body" );
        g.actionCommandIds = readStringList( jg, "actions" );
        g.helpTopicId = readString( jg, "helpTopic" );
        d.guidance = g;
    }
}

HelpDescriptor HelpContentStore::parseEntry( const Json::Value &entry, QStringList &errors,
                                             const QString &context )
{
    HelpDescriptor d;
    if ( !entry.isObject() ) {
        errors << QStringLiteral( "%1: entry is not an object" ).arg( context );
        return d;
    }

    QString id = readString( entry, "id" );
    const std::optional<HelpKind> kind = HelpId::kindOf( id );
    if ( !HelpId::isValid( id ) || !kind.has_value() ) {
        errors << QStringLiteral( "%1: invalid id '%2'" ).arg( context, id );
        return d;
    }
    d.id = id;
    d.kind = *kind;

    // Alias form: {"id": ..., "aliasOf": "target.id"} — nothing else required.
    const QString aliasOf = readString( entry, "aliasOf" );
    if ( !aliasOf.isEmpty() ) {
        d.supersededBy = aliasOf;
        d.deprecated = true;
        return d;
    }

    parseCommon( d, entry, errors, context );

    switch ( d.kind ) {
    case HelpKind::Command: {
        CommandHelp c;
        c.purpose = readString( entry, "purpose" );
        c.prerequisites = readStringList( entry, "prerequisites" );
        c.suggestedNextAction = readString( entry, "suggestedNextAction" );
        d.command = c;
        break;
    }
    case HelpKind::Parameter: {
        // Direct parameter entry form (operator JSON nests them as well).
        ParameterKnowledge p;
        const Json::Value &e = entry;
        p.unit = readString( e, "unit" );
        p.meaning = readString( e, "meaning" );
        p.recommended = readString( e, "recommended" );
        p.tradeOff = readString( e, "tradeOff" );
        p.performanceNote = readString( e, "performanceNote" );
        p.warnings = readStringList( e, "warnings" );
        p.dependsOn = readStringList( e, "dependsOn" );
        p.curated = true;
        d.parameter = p;
        if ( d.title.isEmpty() ) {
            // default title: last id segment (the parameter name)
            d.title = d.id.section( u'.', -1 );
        }
        break;
    }
    case HelpKind::Diagnostic: {
        DiagnosticInfo info;
        const QString familyText = readString( entry, "family" );
        const QString code = readString( entry, "code" );
        info.originFamily = familyText;
        info.originCode = code;
        if ( familyText.isEmpty() || code.isEmpty() ) {
            errors << QStringLiteral( "%1: diagnostic entry %2 missing family/code" ).arg( context, d.id );
            return d;
        }
        const std::optional<DiagnosticFamily> family = diagnosticFamilyFromName( familyText );
        if ( !family.has_value() ) {
            errors << QStringLiteral( "%1: unknown diagnostic family '%2'" ).arg( context, familyText );
            return d;
        }
        // id must agree with the derived mapping — no hand-crafted exceptions.
        const QString derived = HelpId::diagnosticId( *family, code );
        if ( derived != d.id ) {
            errors << QStringLiteral( "%1: diagnostic id %2 does not match derived '%3'" ).arg( context, d.id, derived );
            return d;
        }
        info.whatHappened = readString( entry, "whatHappened" );
        info.whyItMatters = readString( entry, "whyItMatters" );
        info.severity = parseSeverity( readString( entry, "severity" ), sicnu::data::DiagnosticSeverity::Error );
        const QString retry = readString( entry, "retry" );
        if ( retry == QLatin1String( "none" ) )
            info.retrySense = RetrySense::None;
        else if ( retry == QLatin1String( "manual" ) )
            info.retrySense = RetrySense::Manual;
        else if ( retry == QLatin1String( "transient" ) )
            info.retrySense = RetrySense::Transient;
        else
            info.retrySense = RetrySense::Derived;
        info.remediation = readStringList( entry, "remediation" );
        info.technicalNote = readString( entry, "technicalNote" );
        if ( d.title.isEmpty() )
            d.title = info.originCode;
        d.diagnostic = info;
        break;
    }
    case HelpKind::Operator:
    case HelpKind::Concept:
    case HelpKind::Template:
    case HelpKind::Workbench:
    case HelpKind::Shortcut:
        break;
    }

    return d;
}

void HelpContentStore::parseDocument( const Json::Value &document, HelpRegistry &out, QStringList &errors,
                                      const QString &context )
{
    if ( !document.isArray() ) {
        errors << QStringLiteral( "%1: document root is not an array" ).arg( context );
        return;
    }
    for ( const Json::Value &entry : document ) {
        HelpDescriptor d = parseEntry( entry, errors, context );
        if ( d.id.isEmpty() )
            continue;

        // Operator entries may nest parameter knowledge; expand each into its
        // own parameter.<operator>.<param> descriptor so lookups, coverage
        // checks and Help Center pages address them directly.
        if ( d.kind == HelpKind::Operator && entry.isMember( "parameters" ) ) {
            const Json::Value &params = entry["parameters"];
            if ( params.isArray() ) {
                // operator id for parameter-id derivation: "operator.rs.sar_speckle"
                // → "rs:sar_speckle"
                const QString operatorId = d.id.mid( QString( "operator." ).size() )
                                               .replace( u'.', u':', Qt::CaseSensitive );
                for ( const Json::Value &param : params ) {
                    const QString name = readString( param, "name" );
                    if ( name.isEmpty() ) {
                        errors << QStringLiteral( "%1: parameter entry without name under %2" ).arg( context, d.id );
                        continue;
                    }
                    HelpDescriptor p;
                    p.id = HelpId::parameterId( operatorId, name );
                    p.kind = HelpKind::Parameter;
                    ParameterKnowledge knowledge;
                    knowledge.unit = readString( param, "unit" );
                    knowledge.meaning = readString( param, "meaning" );
                    knowledge.recommended = readString( param, "recommended" );
                    knowledge.tradeOff = readString( param, "tradeOff" );
                    knowledge.performanceNote = readString( param, "performanceNote" );
                    knowledge.warnings = readStringList( param, "warnings" );
                    knowledge.dependsOn = readStringList( param, "dependsOn" );
                    knowledge.curated = true;
                    p.parameter = knowledge;
                    parseCommon( p, param, errors, context );
                    // defaults applied after parseCommon: explicit JSON fields
                    // win, but absent fields keep these sane values
                    if ( p.title.isEmpty() )
                        p.title = name;
                    if ( p.summary.isEmpty() )
                        p.summary = knowledge.meaning;
                    if ( p.category.isEmpty() )
                        p.category = d.category;
                    QString error;
                    if ( !out.registerDescriptor( std::move( p ), &error ) )
                        errors << QStringLiteral( "%1: %2" ).arg( context, error );
                }
            }
        }
        if ( d.deprecated && !d.supersededBy.isEmpty() && !d.command.has_value() && !d.diagnostic.has_value()
             && !d.parameter.has_value() && !d.algorithm.has_value() && !d.guidance.has_value() ) {
            QString error;
            out.registerAlias( d.id, d.supersededBy, &error );
            if ( !error.isEmpty() )
                errors << QStringLiteral( "%1: %2" ).arg( context, error );
            continue;
        }
        QString error;
        if ( !out.registerDescriptor( std::move( d ), &error ) )
            errors << QStringLiteral( "%1: %2" ).arg( context, error );
    }
}

void HelpContentStore::loadJsonFile( const QString &path, HelpRegistry &out, QStringList &errors )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        errors << QStringLiteral( "cannot open %1" ).arg( path );
        return;
    }
    const QByteArray bytes = file.readAll();

    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    Json::Value document;
    Json::String parseErrors;
    if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &document, &parseErrors ) ) {
        errors << QStringLiteral( "%1: %2" ).arg( path, QString::fromStdString( parseErrors ) );
        return;
    }
    parseDocument( document, out, errors, path );
}

HelpContentStore::LoadResult HelpContentStore::loadFromDirectory( const QString &directory )
{
    LoadResult result;
    const QDir dir( directory );
    if ( !dir.exists() ) {
        result.errors << QStringLiteral( "content directory missing: %1" ).arg( directory );
        return result;
    }
    const QFileInfoList entries =
        dir.entryInfoList( QStringList{ QStringLiteral( "*.json" ) }, QDir::Files, QDir::Name );
    for ( const QFileInfo &entry : entries )
        loadJsonFile( entry.absoluteFilePath(), result.registry, result.errors );
    // recursive: nested families (e.g. operators/<group>/x.json) are content
    QDirIterator subIt( directory, QStringList{ QStringLiteral( "*.json" ) }, QDir::Files,
                        QDirIterator::Subdirectories );
    while ( subIt.hasNext() )
        loadJsonFile( subIt.next(), result.registry, result.errors );
    result.descriptors = result.registry.count();
    result.aliases = result.registry.aliasCount();
    return result;
}

HelpContentStore::LoadResult HelpContentStore::loadFromResources()
{
    LoadResult result;
    QDirIterator it( QStringLiteral( ":/help" ), QStringList{ QStringLiteral( "*.json" ) }, QDir::Files,
                     QDirIterator::Subdirectories );
    while ( it.hasNext() )
        loadJsonFile( it.next(), result.registry, result.errors );
    result.descriptors = result.registry.count();
    result.aliases = result.registry.aliasCount();
    return result;
}

} // namespace sicnu::help
