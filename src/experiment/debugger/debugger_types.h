// debugger_types.h — shared constants for the Experiment Debugger
// (RS14-06, ADR 0174).
//
// The debugger is a READ-ONLY projection over already-recorded run evidence
// (experiment store reads + workflow provenance/checkpoint snapshots). It
// never executes anything, never writes a run, and never silently repairs
// or reinterprets scientific state: every "cannot decide" is a typed verdict.
#pragma once

#include <QString>

namespace sicnu::experiment::debugger
{

/// Serialization schema version of every document this module emits.
inline constexpr int kDebuggerSchemaVersion = 1;

/// Envelope kinds (closed set) — mirroring the platform's checkpoint /
/// provenance envelope discipline: refuse records this build did not write.
inline constexpr char kSnapshotKind[] = "exp.debugger.snapshot.v1";

/// Upper bound on steps normalized into one snapshot. Beyond this the
/// evidence is reported as too large (typed), never silently truncated.
inline constexpr int kMaxSnapshotSteps = 4096;

/// Upper bound on artifacts normalized into one snapshot.
inline constexpr int kMaxSnapshotArtifacts = 8192;

/// Typed error codes (closed set, `experiment.debugger.*` convention).
inline constexpr char kCodeUnknownRun[] = "experiment.debugger.unknown_run";
inline constexpr char kCodeEvidenceAbsent[] = "experiment.debugger.evidence_absent";
inline constexpr char kCodeEvidenceTooLarge[] = "experiment.debugger.evidence_too_large";
inline constexpr char kCodeMalformedEvidence[] = "experiment.debugger.malformed_evidence";
inline constexpr char kCodeMalformedSnapshot[] = "experiment.debugger.malformed_snapshot";

/// Digest provenance modes observed in recorded evidence. Comparisons across
/// different modes are typed as incomparable evidence upstream — never
/// false-equal and never silently mixed.
inline constexpr char kDigestModeSha256Hex[] = "sha256hex";    // bare hex digest (StepPlan.outputDigest / bridge evidence)
inline constexpr char kDigestModeSha256Fl[] = "sha256fl";      // workflow fast fingerprint prefix
inline constexpr char kDigestModeSha256Full[] = "sha256full";  // workflow whole-file fingerprint prefix
inline constexpr char kDigestModeUnknown[] = "";               // recorded evidence carries no digest

} // namespace sicnu::experiment::debugger
