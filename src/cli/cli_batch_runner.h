// src/cli/cli_batch_runner.h — batch manifest runner (Surface-11 WP-B)
//
// Multi-task batch execution over the CLI's operator universe
// (AtomicAlgorithmRegistry adapters): a JSON or JSONL manifest drives N
// operator runs with per-task status records, ${var} interpolation,
// fail-fast/continue policy, SIGINT-cancel, and an NDJSON result index.
//
// Manifest (object form):
//   {
//     "version": 1,
//     "variables": { "scene": "A", "year": 2024 },
//     "policy": { "on_error": "continue" | "fail-fast" },
//     "tasks": [
//       { "id": "ndvi", "operator": "rs:spectral_index",
//         "params": { … }, "params_file": "p.json", "enabled": true }
//     ]
//   }
// JSONL form: one task object per line; a first line carrying
// "variables"/"policy" (no id/operator) is a header.
//
// Task exit codes follow the exprs::ExitCode contract; the run exit code is
// 0 when every task is ok/skipped, 4 (Cancelled) when interrupted, otherwise
// the maximum task exit code (worst failure surfaces).
#pragma once

#include <json/json.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::cli::batch {

struct Callbacks
{
    /// Relayed operator progress: stepIndex = task index+1, totalSteps = task count.
    std::function<void( int stepIndex, int totalSteps, double stepProgress,
                        const std::string &message )> progress;
    std::function<void( const std::string &level, const std::string &message )> log;
    /// Cooperative cancel probe (wired to cliIsInterrupted() by the command
    /// layer; tests inject their own flag).
    std::function<bool()> isCancelled;
};

struct TaskRecord
{
    int index = 0;
    std::string id;
    std::string operatorId;
    std::string status;  // "ok" | "failed" | "skipped" | "cancelled"
    int exitCode = 0;
    std::string error;   // redacted (surface_redaction); empty when none
    long long durationMs = 0;
};

struct Outcome
{
    int exitCode = 0;
    bool cancelled = false;
    std::vector<TaskRecord> records;
};

struct Options
{
    bool failFast = false;        // overrides the manifest policy when true
    bool dryRun = false;          // validate + resolve adapters, do not execute
    std::string resultIndexPath;  // optional NDJSON index (atomic tmp+rename)
    /// --var overrides on top of the manifest's variables block.
    std::vector<std::pair<std::string, std::string>> extraVariables;
};

/// Structural validation of a manifest document (object form or an array of
/// task objects). Returns false and sets @p error on any contract violation;
/// otherwise @p normalized carries {variables, policy, tasks}.
bool parseManifestDocument( const Json::Value &document, Json::Value &normalized,
                            std::string *error );

/// Reads a manifest file: ".jsonl" parses line-by-line (blank/#-prefixed
/// lines skipped); anything else parses as one JSON document.
bool loadManifestFile( const std::string &path, Json::Value &normalized, std::string *error );

/// Executes a validated manifest. Never throws: every failure lands in a
/// TaskRecord or the outcome exit code.
Outcome runManifest( const Json::Value &normalized, const Options &options,
                     const Callbacks &callbacks );

Outcome runManifestFile( const std::string &path, const Options &options,
                         const Callbacks &callbacks, std::string *error );

} // namespace sicnu::cli::batch
