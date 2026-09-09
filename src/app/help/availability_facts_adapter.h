/***************************************************************************
 * availability_facts_adapter.h — ContextRules → availability fact lists
 *
 * The help layer defines the AvailabilityExplanation shape; ContextRules owns
 * the actual predicates. This adapter composes the existing pure predicates
 * into per-command fact lists — no new availability logic is introduced, and
 * the flat ContextRules::unavailabilityReason stays the single flat-string
 * source for status-bar text.
 ***************************************************************************/
#pragma once

#include "help/availability_facts.h"
#include "workbench/selection_context.h"

#include <QString>

namespace sicnu::app
{

class AvailabilityFactsAdapter
{
  public:
    /// Composes the fact list for @p commandId against @p snapshot.
    static sicnu::help::AvailabilityExplanation explain( const SelectionContextSnapshot &snapshot,
                                                         const QString &commandId );

    /// One requirement row: display label + the ContextRules predicate that
    /// decides it. Exposed because the fact tables are authored in the .cpp
    /// (anonymous-namespace free functions own the per-command tables).
    struct Requirement
    {
        const char *label;                  ///< 中文事实描述
        bool ( *predicate )( const SelectionContextSnapshot & );
    };
};

} // namespace sicnu::app
