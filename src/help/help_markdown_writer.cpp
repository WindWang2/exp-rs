/***************************************************************************
 * help_markdown_writer.cpp — deterministic Markdown generation
 ***************************************************************************/
#include "help/help_markdown_writer.h"

#include <QIODevice>
#include <QTextStream>

namespace sicnu::help
{
namespace
{

QString header( int level, const QString &title )
{
    return QStringLiteral( "%1 %2\n\n" ).arg( QString( level, u'#' ), title );
}

/// Headless composition (CLI) has no CommandRegistry titles; fall back to id.
QString displayTitle( const HelpDescriptor *d )
{
    return d->title.isEmpty() ? d->id : d->title;
}

} // namespace

QString HelpMarkdownWriter::escape( const QString &text )
{
    QString out = text;
    out.replace( u'|', QStringLiteral( "\\|" ) );
    out.replace( u'\n', QStringLiteral( "<br>" ) );
    return out;
}

QString HelpMarkdownWriter::commandReference( const HelpRegistry &registry )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );
    stream << header( 1, QStringLiteral( "命令参考（自动生成）" ) );
    stream << QStringLiteral( "> 本页由统一帮助系统 6.0 从命令注册表生成；请勿手工编辑。\n"
                              "> 权威来源：CommandRegistry / data/help/commands.json。\n\n" );
    for ( const HelpDescriptor *d : registry.byKind( HelpKind::Command ) ) {
        stream << header( 2, QStringLiteral( "%1（%2）" ).arg( displayTitle( d ), d->id ) );
        if ( !d->summary.isEmpty() )
            stream << d->summary << "\n\n";
        if ( d->command.has_value() ) {
            const CommandHelp &c = *d->command;
            if ( !c.purpose.isEmpty() )
                stream << QStringLiteral( "- 用途：%1\n" ).arg( escape( c.purpose ) );
            if ( !c.prerequisites.isEmpty() )
                stream << QStringLiteral( "- 前提：%1\n" ).arg( escape( c.prerequisites.join( QStringLiteral( "；" ) ) ) );
            if ( !c.suggestedNextAction.isEmpty() )
                stream << QStringLiteral( "- 建议下一步：%1\n" ).arg( escape( c.suggestedNextAction ) );
        }
        if ( !d->relatedIds.isEmpty() )
            stream << QStringLiteral( "- 相关主题：%1\n" ).arg( d->relatedIds.join( QStringLiteral( "、" ) ) );
        if ( !d->docRefs.isEmpty() )
            stream << QStringLiteral( "- 文档：%1\n" ).arg( d->docRefs.join( QStringLiteral( "、" ) ) );
        stream << u'\n';
    }
    return out;
}

QString HelpMarkdownWriter::operatorReference( const HelpRegistry &registry )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );
    stream << header( 1, QStringLiteral( "遥感算子参考（自动生成）" ) );
    stream << QStringLiteral( "> 本页由统一帮助系统 6.0 生成；参数类型/范围/默认值以算子 JSON Schema "
                              "为权威来源，此处仅呈现。\n\n" );
    for ( const HelpDescriptor *d : registry.byKind( HelpKind::Operator ) ) {
        stream << header( 2, QStringLiteral( "%1（%2）" ).arg( displayTitle( d ), d->id ) );
        if ( !d->summary.isEmpty() )
            stream << d->summary << "\n\n";
        if ( d->algorithm.has_value() ) {
            const AlgorithmPage &a = *d->algorithm;
            if ( !a.whatItDoes.isEmpty() )
                stream << QStringLiteral( "**原理**：%1\n\n" ).arg( escape( a.whatItDoes ) );
            if ( !a.whenToUse.isEmpty() )
                stream << QStringLiteral( "**适用**：%1\n\n" ).arg( escape( a.whenToUse ) );
            if ( !a.assumptions.isEmpty() )
                stream << QStringLiteral( "**假设**：%1\n\n" )
                            .arg( escape( a.assumptions.join( QStringLiteral( "；" ) ) ) );
            if ( !a.limitations.isEmpty() )
                stream << QStringLiteral( "**局限**：%1\n\n" )
                            .arg( escape( a.limitations.join( QStringLiteral( "；" ) ) ) );
        }
        if ( !d->docRefs.isEmpty() )
            stream << QStringLiteral( "深入阅读：%1\n\n" ).arg( d->docRefs.join( QStringLiteral( "、" ) ) );
    }
    return out;
}

QString HelpMarkdownWriter::parameterReference( const HelpRegistry &registry )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );
    stream << header( 1, QStringLiteral( "参数知识参考（自动生成）" ) );
    stream << QStringLiteral( "> 类型/范围/默认值由算子 Schema 推导；单位/推荐值/权衡为人工知识层。\n\n" );
    for ( const HelpDescriptor *d : registry.byKind( HelpKind::Parameter ) ) {
        if ( !d->parameter.has_value() )
            continue;
        const ParameterKnowledge &p = *d->parameter;
        stream << header( 3, d->id );
        if ( !p.meaning.isEmpty() )
            stream << QStringLiteral( "- 含义：%1\n" ).arg( escape( p.meaning ) );
        if ( !p.unit.isEmpty() )
            stream << QStringLiteral( "- 单位：%1\n" ).arg( escape( p.unit ) );
        if ( !p.recommended.isEmpty() )
            stream << QStringLiteral( "- 推荐值：%1\n" ).arg( escape( p.recommended ) );
        if ( !p.tradeOff.isEmpty() )
            stream << QStringLiteral( "- 权衡：%1\n" ).arg( escape( p.tradeOff ) );
        if ( !p.performanceNote.isEmpty() )
            stream << QStringLiteral( "- 性能：%1\n" ).arg( escape( p.performanceNote ) );
        for ( const QString &warning : p.warnings )
            stream << QStringLiteral( "- ⚠ %1\n" ).arg( escape( warning ) );
        stream << u'\n';
    }
    return out;
}

QString HelpMarkdownWriter::diagnosticReference( const HelpRegistry &registry )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );
    stream << header( 1, QStringLiteral( "诊断目录（自动生成）" ) );
    stream << QStringLiteral( "> 原始机器代码保留不变；severity/retry 与源错误体系 drift 校验。\n\n" );
    QString currentFamily;
    for ( const HelpDescriptor *d : registry.byKind( HelpKind::Diagnostic ) ) {
        if ( !d->diagnostic.has_value() )
            continue;
        const DiagnosticInfo &info = *d->diagnostic;
        if ( info.originFamily != currentFamily ) {
            currentFamily = info.originFamily;
            stream << header( 2, currentFamily );
        }
        stream << header( 3, QStringLiteral( "%1（%2）" ).arg( d->title, info.originCode ) );
        if ( !info.whatHappened.isEmpty() )
            stream << QStringLiteral( "**发生了什么**：%1\n\n" ).arg( escape( info.whatHappened ) );
        if ( !info.whyItMatters.isEmpty() )
            stream << QStringLiteral( "**为什么重要**：%1\n\n" ).arg( escape( info.whyItMatters ) );
        if ( !info.remediation.isEmpty() ) {
            stream << QStringLiteral( "**如何解决**：\n\n" );
            for ( const QString &step : info.remediation )
                stream << QStringLiteral( "- %1\n" ).arg( escape( step ) );
            stream << u'\n';
        }
    }
    return out;
}

QString HelpMarkdownWriter::index( const HelpRegistry &registry )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );
    stream << header( 1, QStringLiteral( "帮助索引（自动生成）" ) );
    stream << QStringLiteral( "> 共 %1 个主题。\n\n" ).arg( registry.count() );

    // group by category, preserving a stable category order (first-seen, sorted ids)
    QStringList categories;
    QHash<QString, QStringList> byCategory;
    for ( const HelpDescriptor *d : registry.all() ) {
        const QString category = d->category.isEmpty() ? QStringLiteral( "未分类" ) : d->category;
        if ( !byCategory.contains( category ) )
            categories << category;
        byCategory[category] << d->id;
    }
    for ( const QString &category : categories ) {
        stream << header( 2, category );
        for ( const QString &id : byCategory.value( category ) ) {
            const HelpDescriptor *d = registry.find( id );
            stream << QStringLiteral( "- `%1` — %2\n" ).arg( id, d ? displayTitle( d ) : QString() );
        }
        stream << u'\n';
    }
    return out;
}

} // namespace sicnu::help
