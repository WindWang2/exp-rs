/***************************************************************************
 * help_presenter.cpp — surface projection implementations
 ***************************************************************************/
#include "help/help_presenter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace sicnu::help
{
namespace
{

QString elide( const QString &text, int maxLength )
{
    if ( maxLength <= 0 || text.size() <= maxLength )
        return text;
    return text.left( qMax( 1, maxLength - 1 ) ) + QStringLiteral( "…" );
}

} // namespace

QString HelpPresenter::tooltip( const HelpDescriptor &descriptor, int maxLength )
{
    if ( descriptor.title.isEmpty() )
        return elide( descriptor.summary, maxLength );
    if ( descriptor.summary.isEmpty() )
        return descriptor.title;
    return elide( QStringLiteral( "%1 — %2" ).arg( descriptor.title, descriptor.summary ), maxLength );
}

QString HelpPresenter::whatsThis( const HelpDescriptor &descriptor )
{
    QString text = descriptor.title;
    if ( !descriptor.summary.isEmpty() )
        text += QStringLiteral( "\n\n%1" ).arg( descriptor.summary );

    if ( descriptor.command.has_value() ) {
        const CommandHelp &c = *descriptor.command;
        if ( !c.purpose.isEmpty() )
            text += QStringLiteral( "\n\n用途：%1" ).arg( c.purpose );
        if ( !c.prerequisites.isEmpty() )
            text += QStringLiteral( "\n前提：%1" ).arg( c.prerequisites.join( QStringLiteral( "；" ) ) );
        if ( !c.suggestedNextAction.isEmpty() )
            text += QStringLiteral( "\n建议下一步：%1" ).arg( c.suggestedNextAction );
    }
    if ( descriptor.algorithm.has_value() ) {
        const AlgorithmPage &a = *descriptor.algorithm;
        if ( !a.whatItDoes.isEmpty() )
            text += QStringLiteral( "\n\n原理：%1" ).arg( a.whatItDoes );
        if ( !a.whenToUse.isEmpty() )
            text += QStringLiteral( "\n适用：%1" ).arg( a.whenToUse );
        if ( !a.limitations.isEmpty() )
            text += QStringLiteral( "\n局限：%1" ).arg( a.limitations.join( QStringLiteral( "；" ) ) );
    }
    if ( !descriptor.relatedIds.isEmpty() )
        text += QStringLiteral( "\n相关主题：%1" ).arg( descriptor.relatedIds.join( QStringLiteral( "、" ) ) );
    text += moreHelpHint( descriptor.id );
    return text;
}

QString HelpPresenter::statusTip( const HelpDescriptor &descriptor )
{
    if ( !descriptor.summary.isEmpty() )
        return elide( descriptor.summary, 80 );
    return descriptor.title;
}

QString HelpPresenter::disabledExplanation( const AvailabilityExplanation &explanation )
{
    return explanation.toText();
}

QString HelpPresenter::moreHelpHint( const QString &helpId )
{
    if ( helpId.isEmpty() )
        return QStringLiteral( "\n更多帮助：按 F1 打开帮助中心" );
    return QStringLiteral( "\n更多帮助：按 F1（主题 %1）" ).arg( helpId );
}

QString HelpCompact::summary( const HelpDescriptor &descriptor, int budgetChars )
{
    QString params;
    if ( descriptor.algorithm.has_value() && !descriptor.algorithm->keyParameters.isEmpty() )
        params = QStringLiteral( " params: %1" ).arg( descriptor.algorithm->keyParameters.join( u',' ) );
    const QString line = QStringLiteral( "%1 | %2 | %3%4" )
                             .arg( descriptor.id,
                                   descriptor.title,
                                   descriptor.summary,
                                   params );
    return elide( line.simplified(), budgetChars );
}

QString HelpCompact::toJsonText( const HelpDescriptor &descriptor, int budgetChars )
{
    // Built with QJsonObject so output is always valid JSON regardless of
    // authored text; budget pressure drops optional fields (diagnostics →
    // keywords → category), then elides the summary, then the id only.
    const auto makeJson = []( const QJsonObject &object ) {
        return QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
    };
    const auto withSummary = [&]( const QJsonObject &base, const QString &summary ) {
        QJsonObject copy = base;
        copy.insert( QStringLiteral( "summary" ), summary );
        return makeJson( copy );
    };

    QJsonObject base;
    base.insert( QStringLiteral( "id" ), descriptor.id );
    base.insert( QStringLiteral( "title" ), descriptor.title );

    QJsonObject full = base;
    full.insert( QStringLiteral( "summary" ), descriptor.summary );
    if ( !descriptor.category.isEmpty() )
        full.insert( QStringLiteral( "category" ), descriptor.category );
    if ( !descriptor.keywords.isEmpty() ) {
        QJsonArray keywords;
        for ( const QString &keyword : descriptor.keywords )
            keywords.append( keyword );
        full.insert( QStringLiteral( "keywords" ), keywords );
    }
    if ( !descriptor.diagnosticIds.isEmpty() ) {
        QJsonArray diagnostics;
        for ( const QString &id : descriptor.diagnosticIds )
            diagnostics.append( id );
        full.insert( QStringLiteral( "diagnostics" ), diagnostics );
    }
    if ( makeJson( full ).size() <= budgetChars )
        return makeJson( full );

    // drop optional fields progressively
    full.remove( QStringLiteral( "diagnostics" ) );
    if ( makeJson( full ).size() <= budgetChars )
        return makeJson( full );
    full.remove( QStringLiteral( "keywords" ) );
    if ( makeJson( full ).size() <= budgetChars )
        return makeJson( full );
    full.remove( QStringLiteral( "category" ) );
    if ( makeJson( full ).size() <= budgetChars )
        return makeJson( full );

    // elide the summary until it fits
    const int overhead = makeJson( base ).size() + 14; // {"summary":""} shell + slack
    const int allowed = qMax( 0, budgetChars - overhead );
    QString summary = descriptor.summary;
    if ( summary.size() > allowed )
        summary = summary.left( allowed ) + QStringLiteral( "…" );
    const QString trimmed = withSummary( base, summary );
    if ( trimmed.size() <= budgetChars )
        return trimmed;

    const QString idOnly = makeJson( base );
    if ( idOnly.size() <= budgetChars )
        return idOnly;
    return idOnly.left( qMax( 2, budgetChars ) );
}

} // namespace sicnu::help
