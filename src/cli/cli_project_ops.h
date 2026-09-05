/***************************************************************************
 * cli_project_ops.h — headless governance subcommands for `project` (CLI
 * surface of the Workspace Governance 3.0 platform; Phase U).
 *
 *   project info <file>                       (legacy, unchanged)
 *   project validate <file> [--fingerprints]  machine-readable health report
 *   project health    <file>                  alias of validate
 *   project search    <file> [text] [--set=assets|results|runs|datasets]
 *                     [--kind=] [--state=] [--sensor=] [--modality=] [--crs=]
 *                     [--tag=] [--run-id=] [--offset=N] [--limit=N]
 *                     [--facet=kind|state|sensor|modality|crs|tag]
 *   project migrate   <file> [--write]        v1→v3 migration report (+persist)
 *   project relink    <file> (--from=ROOT --to=ROOT | --asset=ID --path=NEW)
 *                     [--verify-fingerprints]
 *   project lineage   <file> --asset=ID [--direction=upstream|downstream]
 *                     [--depth=N]
 *   project export-manifest <file> --out=DIR [--mode=reference_only|
 *                     metadata_only|portable]   (reproducibility bundle)
 *   project import    <file> --remote=URL[,URL…]   (COG/STAC hrefs)
 *   project audit     <file> [--limit=N]
 *
 * All subcommands drive the SAME service layer as the GUI and the agent
 * tools (ProjectContext + WorkspaceService); none re-implements project
 * parsing or domain logic.
 ***************************************************************************/
#pragma once

#include <QStringList>

#include <json/json.h>

#include "cli_commands.h"

namespace sicnu::cli
{
  /// Dispatches `project <governance-sub>`; returns the process exit code.
  int runProjectGovernanceCommand( const QString &sub, QStringList args, const CliIO &io );
} // namespace sicnu::cli
