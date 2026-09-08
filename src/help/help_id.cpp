/***************************************************************************
 * help_id.cpp — Help ID grammar implementation
 ***************************************************************************/
#include "help/help_id.h"

#include <QChar>
#include <QRegularExpression>

namespace sicnu::help
{
namespace
{

bool isSegmentValid( const QString &segment )
{
    if ( segment.isEmpty() )
        return false;
    for ( const QChar &ch : segment ) {
        const char16_t c = ch.unicode();
        const bool ok = ( c >= u'a' && c <= u'z' ) || ( c >= u'A' && c <= u'Z' )
                        || ( c >= u'0' && c <= u'9' ) || c == u'_';
        if ( !ok )
            return false;
    }
    return true;
}

} // anonymous namespace

QString diagnosticFamilyName( DiagnosticFamily family )
{
    switch ( family ) {
    case DiagnosticFamily::Harness:
        return QStringLiteral( "harness" );
    case DiagnosticFamily::Operator:
        return QStringLiteral( "operator" );
    case DiagnosticFamily::GeoSpatial:
        return QStringLiteral( "geospatial" );
    case DiagnosticFamily::Dataset:
        return QStringLiteral( "dataset" );
    case DiagnosticFamily::Preflight:
        return QStringLiteral( "preflight" );
    case DiagnosticFamily::Rs:
        return QStringLiteral( "rs" );
    }
    return QStringLiteral( "rs" );
}

std::optional<DiagnosticFamily> diagnosticFamilyFromName( const QString &name )
{
    if ( name == QLatin1String( "harness" ) )
        return DiagnosticFamily::Harness;
    if ( name == QLatin1String( "operator" ) )
        return DiagnosticFamily::Operator;
    if ( name == QLatin1String( "geospatial" ) )
        return DiagnosticFamily::GeoSpatial;
    if ( name == QLatin1String( "dataset" ) )
        return DiagnosticFamily::Dataset;
    if ( name == QLatin1String( "preflight" ) )
        return DiagnosticFamily::Preflight;
    if ( name == QLatin1String( "rs" ) )
        return DiagnosticFamily::Rs;
    return std::nullopt;
}

namespace
{

/// camelCase / kebab-case / SCREAMING_CASE → snake_case.
QString toSnakeCase( const QString &text )
{
    QString out;
    out.reserve( text.size() + 8 );
    bool previousWasLower = false;
    for ( const QChar &ch : text ) {
        if ( ch.isUpper() ) {
            if ( previousWasLower )
                out += u'_';
            out += ch.toLower();
            previousWasLower = false;
        } else if ( ch == u'-' || ch == u' ' ) {
            out += u'_';
            previousWasLower = false;
        } else {
            out += ch;
            previousWasLower = ch.isLower() || ch.isDigit();
        }
    }
    return out;
}

} // namespace

std::optional<HelpKind> helpKindFromName( const QString &name )
{
    if ( name == QLatin1String( "command" ) )
        return HelpKind::Command;
    if ( name == QLatin1String( "operator" ) )
        return HelpKind::Operator;
    if ( name == QLatin1String( "parameter" ) )
        return HelpKind::Parameter;
    if ( name == QLatin1String( "workbench" ) )
        return HelpKind::Workbench;
    if ( name == QLatin1String( "diagnostic" ) )
        return HelpKind::Diagnostic;
    if ( name == QLatin1String( "concept" ) )
        return HelpKind::Concept;
    if ( name == QLatin1String( "template" ) )
        return HelpKind::Template;
    if ( name == QLatin1String( "shortcut" ) )
        return HelpKind::Shortcut;
    return std::nullopt;
}

QStringList HelpId::splitSegments( const QString &id )
{
    return id.split( u'.', Qt::KeepEmptyParts );
}

bool HelpId::isValid( const QString &id )
{
    if ( id.isEmpty() || id.startsWith( u'.' ) || id.endsWith( u'.' ) )
        return false;
    // Minimum 2 segments: kind + name (e.g. workbench.classification).
    const QStringList segments = splitSegments( id );
    if ( segments.size() < 2 )
        return false;
    if ( !helpKindFromName( segments.first() ).has_value() )
        return false;
    for ( const QString &segment : segments ) {
        if ( !isSegmentValid( segment ) )
            return false;
    }
    return true;
}

std::optional<HelpKind> HelpId::kindOf( const QString &id )
{
    const QStringList segments = splitSegments( id );
    if ( segments.isEmpty() )
        return std::nullopt;
    return helpKindFromName( segments.first() );
}

QString HelpId::domainForOperatorId( const QString &operatorId )
{
    QString domain = operatorId;
    domain.replace( u':', u'.' );
    return domain;
}

QString HelpId::parameterId( const QString &operatorId, const QString &paramName )
{
    // Parameter names preserved verbatim from the schema (identity is
    // case-sensitive); only the operator id's ':' becomes '.'.
    return QStringLiteral( "parameter.%1.%2" ).arg( domainForOperatorId( operatorId ), paramName );
}

QString HelpId::normalizeCode( const QString &code )
{
    QString snake = toSnakeCase( code );
    // collapse repeated underscores (e.g. from mixed separators)
    static const QRegularExpression repeats( QStringLiteral( "_{2,}" ) );
    QString collapsed = snake;
    collapsed.replace( repeats, QStringLiteral( "_" ) );
    while ( collapsed.startsWith( u'_' ) )
        collapsed.remove( 0, 1 );
    while ( collapsed.endsWith( u'_' ) )
        collapsed.chop( 1 );
    return collapsed;
}

QString HelpId::diagnosticId( DiagnosticFamily family, const QString &code )
{
    const QString normalized = normalizeCode( code );
    if ( normalized.isEmpty() )
        return QString();
    return QStringLiteral( "diagnostic.%1.%2" ).arg( diagnosticFamilyName( family ), normalized );
}

} // namespace sicnu::help
