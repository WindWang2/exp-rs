// src/lab_pack/lab_pack.h — `sicnu.lab-pack/1`: the lab data pack contract.
//
// Qt-free shared leaf (same discipline as src/grader, ADR 0174): pure C++20 +
// jsoncpp. This is the ONE parser/validator for the pack contract — both the
// agent side (sicnu::agent::LabDataPack, a Qt façade) and the teacher console
// (sicnu::teaching_admin::validatePackDocument) delegate here, so a mutation
// to the pack semantics is killed by the agent and admin oracles at once.
//
// A pack is ONE lab's machine-checkable data contract: which files the lab
// needs, where they come from, and how far a classroom machine can trust
// them. It answers the teacher's 23:00 question — "will lab 4 grade on this
// machine?" — with typed evidence instead of a run-time surprise.
//
// Provenance tiers (verification strength follows the tier):
//   committed-fixture  in-repo, deterministic (tests/fixtures/lab/...) —
//                      sha256 REQUIRED; missing/corrupt input FAILS the pack.
//   generated-samples  produced on the target by sicnu_generate_samples
//                      (fixed seed) — presence checked; declared byte size is
//                      informative (GDAL-version drift), a mismatch WARNS.
//   generated-tmp      produced by scripts/gen_lab_fixtures.py for headless
//                      pipeline verification — same policy as samples.
//
// The pack NEVER writes; verification is read-only, streams hashes in bounded
// chunks (no whole-file loads), and tolerates Unicode paths (paths are
// std::filesystem, built from UTF-8 — wide-native on Windows). Sibling of the
// grading rules (sicnu.lab.rules/1) and the data-spec sheets
// (sicnu.lab-data-spec.v1): rules grade artifacts, data-specs declare
// requirements, packs verify deployment.
#pragma once

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sicnu::labpack
{

inline constexpr const char *kLabPackSchemaId = "sicnu.lab-pack/1";

/// Where an input comes from; determines verification strength.
enum class Provenance
{
  CommittedFixture,   ///< in-repo deterministic file; sha256 required, hard-fail
  GeneratedSamples,   ///< sicnu_generate_samples output; presence + soft size
  GeneratedTmp,       ///< gen_lab_fixtures.py output; presence + soft size
};

Provenance provenanceFromString( const std::string &provenance );
std::string provenanceToString( Provenance provenance );

/// One required file.
struct PackInput
{
  std::string path;         ///< repo/bundle-relative forward-slash path
  std::string role;         ///< "sample" | "aux" | "truth" | "fixture"
  Provenance provenance = Provenance::GeneratedSamples;
  std::string sha256;       ///< lowercase hex; required iff CommittedFixture
  std::int64_t declaredBytes = -1; ///< informative unless CommittedFixture (hard check)
  std::string sensorTruth;  ///< band roles / CRS / dtype statement (human-auditable)
  std::string notes;

  Json::Value toJson() const;
};

/// One lab's parsed pack document.
struct PackDocument
{
  std::string labId;
  std::string packVersion;
  std::string license;
  std::string generator;    ///< canonical command that (re)produces the pack's data
  std::vector<PackInput> inputs;
  std::int64_t declaredOfflineBytes = -1; ///< sum the teacher can quote for disks
  std::string notes;
};

/// Typed load outcome for one pack.
///   lab.pack_schema      not parseable / wrong schema_version
///   lab.pack_field       required field missing or ill-typed
///   lab.pack_input       an input entry is invalid (bad sha256, duplicate
///                        path, sha256 missing on a committed fixture, …)
///   lab.pack_unreadable  the file cannot be opened
struct PackLoadResult
{
  bool ok = false;
  PackDocument pack;
  std::string errorCode;
  std::string errorMessage;

  static PackLoadResult success( PackDocument pack )
  {
    PackLoadResult r;
    r.ok = true;
    r.pack = std::move( pack );
    return r;
  }
  static PackLoadResult failure( std::string code, std::string message )
  {
    PackLoadResult r;
    r.ok = false;
    r.errorCode = std::move( code );
    r.errorMessage = std::move( message );
    return r;
  }
};

/// Deterministic, teacher-quotable summary of one verification.
struct PackVerification
{
  /// "verified"  — every input present, fixtures checksum-clean
  /// "degraded"  — verified except missing/mis-sized regenerable inputs
  /// "failed"    — at least one committed fixture missing or corrupt
  std::string overall;
  std::vector<Json::Value> issues;  ///< typed {code, path?, detail} objects, sorted
  std::int64_t verifiedBytes = 0;     ///< bytes confirmed on disk
  Json::Value toJson() const;   ///< {overall, verified_bytes, input_count, issues[]}
};

/// Builds a std::filesystem::path from a UTF-8 string without losing
/// non-ASCII characters on Windows (char8_t keeps the encoding explicit).
std::filesystem::path pathFromUtf8( const std::string &utf8 );

/// UTF-8 rendering of a filesystem path (inverse of pathFromUtf8).
std::string utf8FromPath( const std::filesystem::path &path );

class PackVerifier
{
  public:
    /// Buffer size for streaming sha256 (bounded memory, never whole-file).
    static constexpr std::int64_t kHashChunkBytes = 1024 * 1024;

    /// Parse + validate the pack document at @p path.
    static PackLoadResult load( const std::filesystem::path &path );

    /// Parse + validate a pack document from in-memory bytes (the admin
    /// console re-serializes documents; the parser truth stays here).
    /// @p errorContext names the source in messages ("<path>: …").
    static PackLoadResult loadFromBytes( const std::string &bytes,
                                         const std::string &errorContext );

    /// Verify @p pack's inputs against @p root. Read-only; never throws for
    /// pack content problems — everything lands in the typed result.
    static PackVerification verify( const PackDocument &pack,
                                    const std::filesystem::path &root );

    /// Load every `*.pack.json` in @p dir (sorted by filename bytes — the
    /// deterministic order, stable across locales/platforms). Returns packs
    /// that loaded; @p problems receives one entry per unloadable file
    /// ("<file>: <code> <message>", load order).
    static std::vector<PackDocument> loadPacksFromDir(
      const std::filesystem::path &dir, std::vector<PackLoadResult> *problems = nullptr );
};

} // namespace sicnu::labpack
