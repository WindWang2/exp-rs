/***************************************************************************
 * availability_facts.h — structured "why is this disabled" view model
 *
 * The help layer defines the fact *shape* and its presentation; the actual
 * rules stay in ContextRules (pure functions over SelectionContextSnapshot).
 * A small fact provider callback is injected by the app adapter, so no
 * dependency from sicnu_help onto app/ ever appears:
 *
 *   Available: false
 *   ✓ 已选中图层
 *   ✗ 编辑会话未开启
 *   建议下一步 → 切换编辑
 *
 * A disabled command must never be merely gray: surfaces call these helpers
 * to explain the state through shared metadata.
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace sicnu::help
{

/// One checkable fact about the current context.
struct AvailabilityFact
{
    QString label;      ///< 中文事实描述, e.g. "已选中栅格图层"
    bool satisfied = false;
};

/// Structured explanation for a (possibly) unavailable command.
struct AvailabilityExplanation
{
    QString commandId;
    bool available = true;
    QList<AvailabilityFact> facts;
    QString suggestedCommandTitle; ///< 建议下一步的命令标题（可空）
    QString suggestedCommandId;    ///< its command id (for the action link)
    /// Legacy flat reason (ContextRules::unavailabilityReason) for status bars.
    QString flatReason;

    /// "✓/✗ 事实" lines (no header).
    QStringList factLines() const;
    /// Full plain-text block used by "why unavailable?" popups.
    QString toText() const;
    /// Concise single-line form for tooltips under disabled actions.
    QString toConciseLine() const;
};

/// Computes facts for a command id against the current snapshot. Implemented
/// in the app adapter with ContextRules; the help layer only consumes it.
using AvailabilityFactProvider =
    std::function<AvailabilityExplanation( const QString &commandId )>;

} // namespace sicnu::help
