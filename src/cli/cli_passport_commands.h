// src/cli/cli_passport_commands.h — RS14-01 Scientific Data Passport command
//
// `passport` — read-only scientific state projection of one raster asset
//              (sicnu.asset_state.v1). Subcommand-free:
//                passport --path <file> [--json] [--teaching]
//                         [--diff <passport.json>]
//              --json      machine-readable envelope (default when --quiet)
//              --teaching  human-readable evidence buckets on stdout
//              --diff      compare against a previously exported passport
#pragma once

#include <QStringList>

#include "cli_commands.h"

namespace sicnu::cli {

int commandPassport( QStringList args, const CliIO &io );

} // namespace sicnu::cli
