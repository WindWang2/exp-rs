// src/cli/cli_tool_commands.h — Surface-11 CLI discovery & batch commands
//
// `tools`   — discovery over the union surface projection
//             (agent/tool_catalog/surface_registry.h): the SAME source MCP
//             tools/list renders, so CLI and MCP never disagree.
//             Subcommands: list | search <query> | schema <tool-id>
// `batch`   — multi-task batch execution over JSON/JSONL manifests
//             (cli_batch_runner.h). Subcommands: run | validate.
#pragma once

#include <QStringList>

#include "cli_commands.h"

namespace sicnu::cli {

int commandTools( QStringList args, const CliIO &io );
int commandBatch( QStringList args, const CliIO &io );

} // namespace sicnu::cli
