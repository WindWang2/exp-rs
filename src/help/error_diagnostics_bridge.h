/***************************************************************************
 * error_diagnostics_bridge.h — error text → diagnostic help topic
 *
 * Work package D (F20): surfaces receive failure text from many origins
 * (task center, workflow runner, harness envelopes). This bridge connects
 * such text to the diagnostics catalog WITHOUT guessing: a token in the
 * message only matches when it resolves to a *curated* diagnostic page in
 * the live registry (harness codes like CRS_MISMATCH, operator codes like
 * FileNotFound). Unknown text yields an empty topic — never a fabricated
 * diagnosis (the catalog's own fallback stays available to callers that
 * want an honest "uncatalogued code" page).
 ***************************************************************************/
#pragma once

#include "help/diagnostic_catalog.h"
#include "help/help_registry.h"

#include <QString>

namespace sicnu::help
{

class ErrorDiagnosticsBridge
{
  public:
    explicit ErrorDiagnosticsBridge( const HelpRegistry &registry );

    /// The registry the bridge resolves against.
    const HelpRegistry &registry() const { return m_registry; }

    /// helpId of the curated diagnostic page for @p family + @p code, or
    /// empty when no curated page exists (no fallback fabrication here).
    QString helpIdForCode( DiagnosticFamily family, const QString &code ) const;

    /// Scans a raw failure message for any token that resolves to a curated
    /// page. Word-boundary matching, longest token wins; returns the helpId
    /// or empty. Only whole-token curated codes ever match.
    QString helpIdFromMessage( const QString &message ) const;

  private:
    const HelpRegistry &m_registry;
    DiagnosticCatalog m_catalog;
};

} // namespace sicnu::help
