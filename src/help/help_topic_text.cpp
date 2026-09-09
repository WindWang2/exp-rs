/***************************************************************************
 * help_topic_text.cpp — plain-text topic renderer
 ***************************************************************************/
#include "help/help_topic_text.h"

#include <QIODevice>
#include <QTextStream>

namespace sicnu::help
{

QString HelpTopicText::render( const HelpDescriptor &d )
{
    QString out;
    QTextStream stream( &out, QIODevice::WriteOnly );

    stream << d.title << "\n" << QString( qMax( 1, d.title.size() ), u'=' ) << "\n";
    stream << "id: " << d.id << "\n";
    if ( !d.summary.isEmpty() )
        stream << "\n" << d.summary << "\n";

    if ( d.command.has_value() ) {
        const CommandHelp &c = *d.command;
        if ( !c.purpose.isEmpty() )
            stream << "\n用途: " << c.purpose << "\n";
        for ( const QString &prerequisite : c.prerequisites )
            stream << "前提: " << prerequisite << "\n";
        if ( !c.suggestedNextAction.isEmpty() )
            stream << "建议下一步: " << c.suggestedNextAction << "\n";
    }
    if ( d.parameter.has_value() ) {
        const ParameterKnowledge &p = *d.parameter;
        if ( !p.meaning.isEmpty() )
            stream << "\n含义: " << p.meaning << "\n";
        if ( !p.unit.isEmpty() )
            stream << "单位: " << p.unit << "\n";
        if ( !p.recommended.isEmpty() )
            stream << "推荐值: " << p.recommended << "\n";
        if ( !p.tradeOff.isEmpty() )
            stream << "权衡: " << p.tradeOff << "\n";
        if ( !p.performanceNote.isEmpty() )
            stream << "性能: " << p.performanceNote << "\n";
        for ( const QString &warning : p.warnings )
            stream << "! " << warning << "\n";
    }
    if ( d.algorithm.has_value() ) {
        const AlgorithmPage &a = *d.algorithm;
        if ( !a.whatItDoes.isEmpty() )
            stream << "\n原理: " << a.whatItDoes << "\n";
        if ( !a.whenToUse.isEmpty() )
            stream << "适用: " << a.whenToUse << "\n";
        for ( const QString &item : a.inputs )
            stream << "输入: " << item << "\n";
        for ( const QString &item : a.outputs )
            stream << "输出: " << item << "\n";
        for ( const QString &item : a.assumptions )
            stream << "假设: " << item << "\n";
        if ( !a.unitsDomain.isEmpty() )
            stream << "数值域: " << a.unitsDomain << "\n";
        for ( const QString &item : a.limitations )
            stream << "局限: " << item << "\n";
        for ( const QString &item : a.failureModes )
            stream << "失败模式: " << item << "\n";
    }
    if ( d.guidance.has_value() ) {
        const GuidanceDescriptor &g = *d.guidance;
        if ( !g.body.isEmpty() )
            stream << "\n" << g.body << "\n";
        for ( const QString &action : g.actionCommandIds )
            stream << "建议操作: " << action << "\n";
    }
    if ( d.diagnostic.has_value() ) {
        const DiagnosticInfo &info = *d.diagnostic;
        stream << "\n原始代码: " << info.originCode << " (family: " << info.originFamily << ")\n";
        if ( !info.whatHappened.isEmpty() )
            stream << "发生了什么: " << info.whatHappened << "\n";
        if ( !info.whyItMatters.isEmpty() )
            stream << "为什么重要: " << info.whyItMatters << "\n";
        if ( !info.remediation.isEmpty() ) {
            stream << "如何解决:\n";
            for ( const QString &step : info.remediation )
                stream << "  - " << step << "\n";
        }
        if ( !info.technicalNote.isEmpty() )
            stream << "技术细节: " << info.technicalNote << "\n";
    }

    if ( !d.relatedIds.isEmpty() ) {
        stream << "\n相关主题:";
        for ( const QString &related : d.relatedIds )
            stream << " " << related;
        stream << "\n";
    }
    if ( !d.docRefs.isEmpty() ) {
        stream << "文档:";
        for ( const QString &doc : d.docRefs )
            stream << " " << doc;
        stream << "\n";
    }
    return out;
}

} // namespace sicnu::help
