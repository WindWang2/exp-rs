/***************************************************************************
 * help_presenter.cpp — surface projection implementations
 ***************************************************************************/
#include "help/help_presenter.h"

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
    // Guaranteed bound: try the extended form (category+keywords), then the
    // plain shell, then an elided summary, then id-only. Each step only when
    // the previous one exceeds the budget.
    const QString shell = QStringLiteral( "{\"id\":\"%1\",\"title\":\"%2\",\"summary\":\"%3\"}" );

    QStringList keywordList;
    for ( const QString &keyword : descriptor.keywords )
        keywordList << QStringLiteral( "\"%1\"" ).arg( keyword );
    QString extended = QStringLiteral( "{\"id\":\"%1\",\"title\":\"%2\",\"summary\":\"%3\"" )
                           .arg( descriptor.id, descriptor.title, descriptor.summary );
    if ( !descriptor.category.isEmpty() )
        extended += QStringLiteral( ",\"category\":\"%1\"" ).arg( descriptor.category );
    if ( !keywordList.isEmpty() )
        extended += QStringLiteral( ",\"keywords\":[%1]" ).arg( keywordList.join( u',' ) );
    extended += u'}';
    if ( extended.size() <= budgetChars )
        return extended;

    QString plain = shell.arg( descriptor.id, descriptor.title, descriptor.summary );
    if ( plain.size() <= budgetChars )
        return plain;

    // shell = format string; the three %n placeholders (~2 chars each) are
    // replaced by dynamic content, so fixed characters = shell.size() - 6.
    const int fixedOverhead = shell.size() - 6;
    const int allowed = qMax( 0, budgetChars - descriptor.id.size() - descriptor.title.size() - fixedOverhead );
    QString elidedSummary = descriptor.summary;
    if ( elidedSummary.size() > allowed )
        elidedSummary = elidedSummary.left( allowed ) + QStringLiteral( "…" );
    QString trimmed = shell.arg( descriptor.id, descriptor.title, elidedSummary );
    if ( trimmed.size() <= budgetChars )
        return trimmed;

    const QString idOnly = QStringLiteral( "{\"id\":\"%1\"}" ).arg( descriptor.id );
    if ( idOnly.size() <= budgetChars )
        return idOnly;
    return idOnly.left( qMax( 2, budgetChars ) );
}

} // namespace sicnu::help
