/***************************************************************************
 * contextual_help_resolver.h — one composition of contextual guidance
 *
 * Work package B (F20): given the current selection snapshot and an optional
 * focus command, resolve the three things every surface needs — a detail
 * help topic, a short tip, and (when the focus command is disabled) the
 * machine-readable reason plus the suggested next step. Pure: no widgets,
 * no registry mutation, safe to call from GUI/CLI/agent/tests alike.
 *
 * The resolver owns no facts: availability truth stays in ContextRules
 * (requirementFacts), help topics stay in the global help registry, and the
 * enabled state stays in CommandRegistry. It only composes projections.
 ***************************************************************************/
#pragma once

#include "help/availability_facts.h"

#include <QString>

namespace sicnu::app
{

class CommandRegistry;

struct SelectionContextSnapshot;

/// Fully composed guidance for one (surface, focus command) query.
struct ContextualGuidance
{
    QString detailHelpId;       ///< topic to open via F1 / --help-topic (may be empty)
    QString shortTip;           ///< 1–2 line surfaced hint (surface language)
    /// Stable machine token for the first unsatisfied requirement
    /// ("raster.selected" …), empty when available or no predicate applies.
    QString unavailableReasonCode;
    /// Human reason the focus command is disabled (empty when available).
    QString unavailableReason;
    QString suggestedCommandId;   ///< next-step command id (empty = none)
    QString suggestedCommandText; ///< next-step user text
    bool available = true;
};

class ContextualHelpResolver
{
  public:
    /// Guidance for a command in context: availability facts + detail topic
    /// ("command.<id>") + next step. @p registry may be null (suggested
    /// titles then resolve empty; ids are still returned).
    static ContextualGuidance resolve( const SelectionContextSnapshot &snapshot,
                                       const CommandRegistry *registry,
                                       const QString &focusCommandId );

    /// Guidance for a surface (workbench/panel) rather than a command: the
    /// context's suggested next action plus the surface's own help topic.
    static ContextualGuidance resolveForSurface( const SelectionContextSnapshot &snapshot,
                                                 const QString &surfaceHelpId );
};

} // namespace sicnu::app
