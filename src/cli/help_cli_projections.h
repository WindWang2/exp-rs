/***************************************************************************
 * help_cli_projections.h — Unified Help 6.0 CLI projections
 *
 * Handlers for --operator-help / --help-topic / --list-topics /
 * --export-help-docs. Kept in a dedicated TU so main_cli.cpp stays focused
 * on pipeline execution; the knowledge base composed here is the same one
 * the GUI and MCP surfaces use (embedded data/help + live operator facts).
 ***************************************************************************/
#pragma once

#include <QStringList>

namespace sicnu::cli
{

/// Returns 0 when a help projection option was handled (and the process
/// should exit with that code); returns -1 when no help option was set and
/// the caller must continue with the legacy parser flow.
int runHelpProjections( const QStringList &arguments );

} // namespace sicnu::cli
