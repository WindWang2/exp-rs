/***************************************************************************
  platform/durable_sidecar.h
  The single sidecar write authority (R6, ADR 0166 / issue #1394 direction).
  ---------------------------
  Copyright            : (C) 2026 SICNU GEO RS

  Contract — prepare → validate → write temp → durability boundary → publish
  → last-good rotation → verify:

  * every sidecar-class durable write goes through write(); no sidecar writer
    keeps its own temp naming, fsync gate, atomic replace, last-good rotation
    or error mapping (dataset GROUP publishing stays in
    geospatial/util/atomic_fs.h — the two layers are documented at
    "Two sanctioned durability layers" below).

  * staging happens in the TARGET directory under an O_EXCL-claimed unique
    name "<stem>.<pid>.<counter>.<rng>.tmp<ext>" (same shape as atomic_fs,
    #1097), so the publish rename never crosses a volume.

  * the durability boundary is fsync(2) / FlushFileBuffers (portable.h) on
    the STAGED bytes — a failed sync refuses the publish, so a crash can
    never commit a directory entry for bytes that did not reach the disk.

  * publish is atomic-or-failed: POSIX rename(2); Windows ReplaceFileW when
    the target exists (transactional, READONLY attr cleared first) else
    MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH). At every
    instant the target holds old-or-new COMPLETE bytes; readers never see a
    torn sidecar. A failed publish leaves the target untouched and discards
    the staged file.

  * last-good: after a successful publish the SAME payload is published to
    "<target><suffix>" through the identical path (default ".last-good",
    empty suffix disables). The rotation is best-effort — the main artifact
    is already committed — and its failure is reported, never fatal. The
    last-good copy may lag main by one generation; it never leads main.

  * verify: after publish the main file is read back (size-bounded) and
    byte-compared with the payload. A mismatch (silent corruption between
    write and rename) is reported typed as VerifyFailed; main is NOT rolled
    back (the old generation is already gone) — the caller decides how to
    surface a suspect artifact.

  * crash windows (process killed at any point):
      before temp        → target untouched (old-or-absent)
      mid temp write     → target untouched; inert ".tmp" residue (O_EXCL
                           pid-tagged name, never read by ANY reader)
      post temp, pre fsync → target untouched; temp bytes possibly torn on
                           power loss — still inert residue
      post fsync, pre publish → target untouched
      publish            → rename atomic: target old-or-new
      post publish, pre last-good → target = new; last-good = previous
                           generation (one behind, still decodable)
      last-good rotation → main already committed; rotation failure only
                           means last-good stays one further behind
    Residue sweep policy: ".tmp" staging residue is inert by construction
    (readers resolve main → last-good only); call sites that already sweep
    (scratch_registry, workflow_checkpoint tmp sweep) keep doing it.

  * read side (read()): resolves main → last-good with a typed source:
      Main       main exists and was read fully
      LastGood   main missing/unreadable/oversize, last-good read fully —
                 callers treat this as RECOVERED content, not business data
      Missing    neither exists (fresh state, not an error)
      Corrupt    at least one exists but neither could be read fully
    Schema/version gates (old schema, newer schema, partial legacy state)
    stay with the CALLER's decoder — this layer is content-agnostic; the
    contract is that a source of Main or LastGood hands the decoder complete
    bytes. Newer-schema refusal must be fail-closed (decode refused, never
    saved over) per the mission-runtime pattern.

  * deterministic fault seams (test-only): WriteHooks/ReadHooks inject a
    failure at a NAMED PHASE — every hook routes through the real failure
    branch (a fault never fabricates success). No sleeps, no randomness.
    Deliberately registry-free (unlike SICNU_FAULT_POINT) so sicnu_platform
    stays linkable from the plugin SDK without a runtime-DLL dependency
    (DECISIONS D2); a static lib copy has no global state to fork.

  * two sanctioned durability layers:
      platform::sidecar   — one-file metadata/sidecar artifacts (this API)
      geo::atomic_fs      — dataset groups (main + GDAL sidecars) and
                            GDAL-created staged datasets
    Nothing else may implement temp naming, durability gating, atomic
    replace or last-good rotation for new writers.

  Qt-free, dependency-free (std + platform/portable.h only). Exceptions are
  not used for flow control; failures are typed in the result structs.
 ***************************************************************************/

#ifndef SICNU_PLATFORM_DURABLE_SIDECAR_H
#define SICNU_PLATFORM_DURABLE_SIDECAR_H

#include <cstdint>
#include <functional>
#include <string>

namespace sicnu::platform::sidecar
{

/// Payload ceiling. Sidecars are metadata, not data; a writer pushing more
/// than this through one atomic publish is a design smell and the oversize
/// refusal doubles as the disk-full guard (a truncating filesystem never
/// gets that far).
inline constexpr std::size_t kDefaultMaxSidecarBytes = 32u * 1024u * 1024u;

/// Default last-known-good suffix ("<target>.last-good").
inline constexpr const char *kDefaultLastGoodSuffix = ".last-good";

/// Phases of write(), in execution order. Deterministic fault injection
/// targets these names (prepare/validate refusals are covered directly by
/// the EmptyPath/TooLarge statuses — they have no filesystem effect to
/// inject around).
enum class WritePhase : std::uint8_t
{
  WriteTemp,   ///< write the staged temp file
  Durability,  ///< fsync / FlushFileBuffers of the staged bytes
  Publish,     ///< atomic rename/replace onto the target
  LastGood,    ///< rotation of the same payload to <target><suffix>
  Verify       ///< read-back byte comparison of the published main file
};

enum class WriteStatus : std::uint8_t
{
  Ok = 0,
  EmptyPath,           ///< refused before any filesystem effect
  TooLarge,            ///< payload above maxBytes; refused before any effect
  ClaimFailed,         ///< no unique staged name could be claimed
  WriteFailed,         ///< open/short-write/stream error on the temp file
  DurabilityFailed,    ///< fsync refused — publish deliberately withheld
  PublishFailed,       ///< rename/replace failed — target untouched
  VerifyFailed,        ///< published bytes differ from the payload (typed)
  LastGoodFailed,      ///< main committed; last-good rotation failed
  StagedCleanupFailed  ///< a failed path could not remove its temp (inert)
};

/// Stable name of a status — the shared error-mapping vocabulary for every
/// migrated caller (callers append their own context, never re-invent codes).
const char *writeStatusName( WriteStatus status );

struct WriteResult
{
  WriteStatus status = WriteStatus::Ok;
  std::string error;          ///< human-readable; carries the OS error code
  std::string stagedPath;     ///< diagnosis; non-empty only on cleanup failure
  bool lastGoodWritten = false;
  WriteStatus lastGoodStatus = WriteStatus::Ok;

  operator bool() const noexcept { return status == WriteStatus::Ok; }
};

/// Test-only deterministic seams. `failAt(phase)` routes the named phase
/// through its REAL failure branch (cleanup included). `payloadOverride`
/// replaces the bytes written AT a phase — e.g. a truncated payload at
/// WriteTemp models a torn write and drives the verify gate. Never armed by
/// production code.
struct WriteHooks
{
  std::function<bool( WritePhase )> failAt;
  std::function<std::string( WritePhase )> payloadOverride;
};

struct WriteRequest
{
  std::string targetPath;         ///< final artifact path (UTF-8)
  std::string bytes;              ///< complete payload to publish
  std::string lastGoodSuffix = kDefaultLastGoodSuffix; ///< "" disables rotation
  std::size_t maxBytes = kDefaultMaxSidecarBytes;
  bool verifyReadBack = true;     ///< read-back byte comparison after publish
  WriteHooks hooks;               ///< test-only
};

/// The sidecar write authority. See the file-header contract for the state
/// machine; on any failure before publish the target is untouched and the
/// staged file is discarded (removal failure is reported, residue stays
/// inert).
WriteResult write( const WriteRequest &request );

/// Read phases for read()'s fault seams.
enum class ReadPhase : std::uint8_t
{
  ReadMain,     ///< reading the main artifact
  ReadLastGood  ///< reading the last-good snapshot
};

enum class ReadSource : std::uint8_t
{
  Main = 0,     ///< main artifact read fully
  LastGood,     ///< main missing/unreadable — recovered from last-good
  Missing,      ///< neither artifact exists (fresh state)
  Corrupt       ///< at least one exists, neither read fully
};

struct ReadResult
{
  ReadSource source = ReadSource::Missing;
  std::string bytes;   ///< complete bytes of the resolved source
  std::string error;   ///< why the rejected source(s) were rejected
};

struct ReadHooks
{
  std::function<bool( ReadPhase )> failAt;
};

/// Sidecar read resolution: main → last-good → Missing/Corrupt, with the
/// rejection reason of every rejected source in @p error. Oversize files
/// count as unreadable (Corrupt, or the last-good is resolved instead).
/// Content decoding stays with the caller.
ReadResult read( const std::string &targetPath,
                 const std::string &lastGoodSuffix = kDefaultLastGoodSuffix,
                 std::size_t maxBytes = kDefaultMaxSidecarBytes,
                 const ReadHooks &hooks = {} );

} // namespace sicnu::platform::sidecar

#endif // SICNU_PLATFORM_DURABLE_SIDECAR_H
