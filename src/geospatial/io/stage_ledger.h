/***************************************************************************
  geospatial/io/stage_ledger.h
  Geospatial I/O, COG & Interchange 11.0 — dataset-level staging ledger,
  attach-existing and orphan sweep.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The atomic writer contract (Geospatial I/O Foundation 4.0) guarantees that
  a crash or cancel never corrupts a published dataset — but a crash also
  leaves the half-written STAGED dataset unnamed and unknown: the next
  process cannot tell a staged transaction from garbage, and the resume seam
  the execution runtime (G01) needs did not exist. This module is that seam,
  at the dataset level, without becoming a scheduler:

  * recordStaged() journals a staged transaction next to its TARGET as
    "<name>.sicnu-stage-ledger.json": runId, producer, staged path, declared
    shape/driver, state.
  * attachExisting() validates an interrupted transaction after a crash: the
    ledger must say "staged", the staged dataset must still exist and open
    in GDAL, and the opened driver/shape must match the declaration. The
    producer may then continue writing to the staged path and call
    finalizeAttached() to run the normal digest → manifest → group-publish
    → mark-finalized pipeline.
  * finalizeAttached() / discardAttached() close the transaction; both
    update the ledger so state is always explicit.
  * sweepOrphans() enumerates a directory's staging leftovers (".tmp"
    staged files and their sidecars, plus ledger entries whose staged file
    is gone) and reports them with redacted display paths; removal is
    explicit opt-in, never implicit.

  Chunk/compute-level resume stays in src/runtime/chunk (G01 domain); this
  module only ever reasons about dataset files.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_STAGE_LEDGER_H
#define SICNU_GEOSPATIAL_IO_STAGE_LEDGER_H

#include "geospatial/common.h"
#include "geospatial/io/finalize_manifest.h"

#include <string>
#include <vector>

namespace sicnu::geo::io
{

/// Ledger sidecar suffix appended to the TARGET main file.
extern const char *const kStageLedgerSuffix;

/// Ledger states: "staged" (transaction open), "finalized" (published),
/// "discarded" (staging removed by cancel/rollback).
struct StageRecord
{
    std::string runId;      ///< caller-chosen unique id (required)
    std::string producer;   ///< free text ("RasterWriter", "io:translate", ...)
    std::string finalPath;  ///< canonical target main file
    std::string stagedPath; ///< staged main file (canonical)
    std::string driver;     ///< declared GDAL short name ("" = unchecked)
    int width = 0;          ///< declared raster shape (0 = vector/unchecked)
    int height = 0;
    int bandCount = 0;
    std::string state = "staged";
    std::string updatedAtUtc;
};

Json::Value stageRecordToJson( const StageRecord &record );
StageRecord stageRecordFromJson( const Json::Value &json ); ///< GeoError(InvalidMetadata) on malformed input

/// Path of the ledger sidecar for a target main file.
std::string stageLedgerPath( const std::string &finalPath );

/// Creates/overwrites the ledger of `finalPath` with `record` (state forced
/// to "staged" — a record is only ever journaled for an open transaction;
/// state transitions below are explicit calls). Atomic write. Throws
/// GeoError(InvalidArgument) when runId or finalPath/stagedPath is empty.
void recordStaged( const StageRecord &record );

/// Reads the ledger. GeoError(NotFound) when absent, GeoError(InvalidMetadata)
/// when unparseable or of a foreign schema version.
StageRecord readStageLedger( const std::string &finalPath );

void removeStageLedger( const std::string &finalPath ); ///< quiet when absent

/// Attach-validation issue codes:
///   ledger_missing, ledger_invalid, state_not_staged, staged_missing,
///   staged_unopenable, driver_mismatch, shape_mismatch
struct AttachIssue
{
    std::string code;
    std::string message;
};

struct AttachCheck
{
    bool attachable = false;
    StageRecord record;
    std::vector<AttachIssue> issues;
    Json::Value toJson() const;
};

/// Validates that the transaction behind `finalPath` can be resumed: ledger
/// present + state "staged" + staged file exists + staged dataset opens in
/// GDAL + declared driver/shape match the opened dataset. Read-only.
AttachCheck attachExisting( const std::string &finalPath );

/// Closes an attached transaction successfully: re-runs attach validation,
/// then (optionally) computes the dataset digest and writes the staged
/// finalize manifest sidecar, fsyncs, publishes the staged group
/// (sidecars-first/main-last), and marks the ledger "finalized". When
/// `manifest` is nullptr no manifest sidecar is written (plain publish).
/// Throws GeoError (AttachCheck codes arrive as InvalidState-style
/// InvalidArgument with details) — the staged group stays untouched on
/// failure.
void finalizeAttached( const std::string &finalPath, const FinalizeManifestFields *manifest = nullptr );

/// Cancels an attached transaction: staged group discarded, ledger removed.
/// Quiet on an already-absent staged file; refuses when the ledger says the
/// transaction already finalized (state_not_staged).
void discardAttached( const std::string &finalPath );

/// One sweep finding. `path` is the on-disk name; `display` its redacted
/// form; kinds: "staged_file", "staged_sidecar", "stale_ledger",
/// "stale_manifest".
struct StrayStaging
{
    std::string path;
    std::string display;
    std::string kind;
    bool removed = false;
};

/// Enumerates staging leftovers in `directory` (non-recursive). A file is a
/// leftover when its name contains a ".tmp" component in the staged-name
/// shape ("<stem>.<n>.<n>.tmp<ext>") or it is a finalize manifest / stage
/// ledger whose dataset main file no longer exists. With `remove` the
/// strays and their sidecars are deleted (ledger files only when their
/// staged dataset is gone). Throws GeoError(IoError) when `directory` is
/// not a readable directory.
std::vector<StrayStaging> sweepOrphans( const std::string &directory, bool remove = false );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_STAGE_LEDGER_H
