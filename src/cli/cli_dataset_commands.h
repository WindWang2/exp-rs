/***************************************************************************
 * src/cli/cli_dataset_commands.h — Foundation 5.0 CLI command groups
 *
 * dataset | experiment | reproduce — thin wrappers over the scientific
 * dataset/experiment stores (ADR 0134–0138). Read-mostly: the CLI exposes
 * the SAME store contracts the libraries enforce, no second logic layer.
 *
 *   dataset create   --dataset-db <db> --name <n> [--description <d>]
 *   dataset inspect  --dataset-db <db> [--dataset <id>] [--version <id>]
 *   dataset validate --dataset-db <db> --version <id>
 *   dataset diff     --dataset-db <db> --from <vid> --to <vid>
 *   dataset stats    --dataset-db <db> --version <id>
 *   experiment create --experiment-db <db> --name <n> [--objective <o>]
 *   experiment inspect --experiment-db <db> --experiment <id>
 *   experiment compare --experiment-db <db> --a <runId> --b <runId>
 *   reproduce export  --experiment-db <db> --dataset-db <db> --run <id>
 *                     --out <dir>
 *   reproduce validate --experiment-db <db> --dataset-db <db> --bundle <dir>
 ***************************************************************************/
#pragma once

#include <QStringList>

namespace sicnu::cli
{

struct CliIO;

int commandDataset( QStringList args, const CliIO &io );
int commandExperiment( QStringList args, const CliIO &io );
int commandReproduce( QStringList args, const CliIO &io );

} // namespace sicnu::cli
