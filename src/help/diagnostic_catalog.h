/***************************************************************************
 * diagnostic_catalog.h — unified lookup across the error vocabularies
 *
 * Maps every origin failure family (HarnessError, RSOperatorError, GeoError,
 * dataset findings, preflight issues) onto stable diagnostic.* Help IDs and
 * resolves them against a HelpRegistry populated with descriptors
 * (data/help/diagnostics.json). The original machine code is preserved
 * byte-identical; unknown codes fall back to a generated descriptor so no
 * failure is ever swallowed or renamed.
 *
 * The catalog owns NO business rules: severity/retry facts in content are
 * drift-checked against the origin taxonomies (harness_error.*), not decided
 * here.
 ***************************************************************************/
#pragma once

#include "help/help_descriptor.h"
#include "help/help_id.h"
#include "help/help_registry.h"

#include <QString>

namespace sicnu::help
{

class DiagnosticCatalog
{
  public:
    explicit DiagnosticCatalog( const HelpRegistry &registry );

    /// Stable diagnostic id for an origin failure (see HelpId::diagnosticId).
    static QString idFor( DiagnosticFamily family, const QString &originCode );

    /// Registered descriptor for @p family + @p originCode, or nullptr when
    /// the code has no curated page (use fallback() to still present it).
    const HelpDescriptor *find( DiagnosticFamily family, const QString &originCode ) const;

    /// Generated descriptor for codes without a curated page: carries the
    /// original code verbatim plus generic (honest) remediation guidance.
    /// Not registered — returned by value so the registry stays curated-only.
    HelpDescriptor fallback( DiagnosticFamily family, const QString &originCode ) const;

    /// One-stop presentation: curated descriptor if present, else fallback.
    HelpDescriptor resolve( DiagnosticFamily family, const QString &originCode ) const;

    /// Maps HarnessError RetryClass vocabulary (string form as produced by
    /// retryClassToString) onto this layer's RetrySense for drift checks.
    static RetrySense retrySenseFromClass( const QString &retryClass );

  private:
    const HelpRegistry &m_registry;
};

} // namespace sicnu::help
