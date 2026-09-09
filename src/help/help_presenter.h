/***************************************************************************
 * help_presenter.h — surface projection helpers (tooltip / What's This / …)
 *
 * UI code never assembles help strings by hand: it asks here. Layers are
 * deliberately different sizes — tooltips stay concise (~1–2 lines), What's
 * This carries structure, the Help Center renders everything — so the shell
 * stays clean while depth stays reachable. Pure QString assembly; no widgets
 * in this layer (Qt-action adapters live in src/app).
 ***************************************************************************/
#pragma once

#include "help/availability_facts.h"
#include "help/help_descriptor.h"
#include "help/help_registry.h"

#include <QString>

namespace sicnu::help
{

class HelpPresenter
{
  public:
    /// Concise tooltip body: "标题 — 摘要" (truncated to maxLength, default 90).
    static QString tooltip( const HelpDescriptor &descriptor, int maxLength = 90 );

    /// Richer What's This text: title, summary, purpose/algorithm gist,
    /// prerequisites, related-topic hint. Plain text (Qt allows rich text but
    /// plain keeps it readable everywhere).
    static QString whatsThis( const HelpDescriptor &descriptor );

    /// Status-bar hint (one short line; falls back to summary).
    static QString statusTip( const HelpDescriptor &descriptor );

    /// Disabled-control explanation assembled from availability facts.
    static QString disabledExplanation( const AvailabilityExplanation &explanation );

    /// "更多帮助：帮助中心（F1）" hint line appended by WhatsThis surfaces.
    static QString moreHelpHint( const QString &helpId );
};

/// Token-bounded compact summaries for agent surfaces (MCP/Pi) and CLI.
class HelpCompact
{
  public:
    /// One-line summary: "id | title | summary | params: a, b, c" truncated to
    /// @p budgetChars. Deterministic; params listed in schema order (commands:
    /// keywords instead of params).
    static QString summary( const HelpDescriptor &descriptor, int budgetChars = 220 );

    /// JSON object {id,title,summary,category,keywords,diagnostics?} bounded
    /// by dropping optional fields first (agent budget contracts).
    static QString toJsonText( const HelpDescriptor &descriptor, int budgetChars = 400 );
};

} // namespace sicnu::help
