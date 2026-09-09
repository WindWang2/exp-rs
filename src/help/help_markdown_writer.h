/***************************************************************************
 * help_markdown_writer.h — generated Markdown reference from the registry
 *
 * Renders command/parameter/diagnostic/shortcut reference pages from live
 * descriptors. Generated pages contain only structured facts and link out to
 * hand-authored guides; hand-authored docs are never overwritten. Output is
 * deterministic (sorted by id) so drift tests can diff against committed
 * pages under docs/generated/help/.
 ***************************************************************************/
#pragma once

#include "help/help_registry.h"

#include <QString>

namespace sicnu::help
{

class HelpMarkdownWriter
{
  public:
    /// command.md — one section per command descriptor.
    static QString commandReference( const HelpRegistry &registry );

    /// operators.md + parameters.md — operator pages with schema-derived
    /// parameter tables; parameter knowledge joined where curated.
    static QString operatorReference( const HelpRegistry &registry );
    static QString parameterReference( const HelpRegistry &registry );

    /// diagnostics.md — diagnostic catalog by family.
    static QString diagnosticReference( const HelpRegistry &registry );

    /// index.md — category tree over everything registered.
    static QString index( const HelpRegistry &registry );

  private:
    static QString escape( const QString &text );
};

} // namespace sicnu::help
