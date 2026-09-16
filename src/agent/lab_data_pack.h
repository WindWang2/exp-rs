// src/agent/lab_data_pack.h — `sicnu.lab-pack/1`: the lab data pack contract.
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
// chunks (no whole-file loads), and tolerates Unicode paths (QString/QFile).
// Sibling of the grading rules (sicnu.lab.rules/1) and the data-spec sheets
// (sicnu.lab-data-spec.v1): rules grade artifacts, data-specs declare
// requirements, packs verify deployment.
#pragma once

#include <json/json.h>

#include <QString>
#include <QVector>

namespace sicnu::agent {

inline constexpr const char *kLabPackSchemaId = "sicnu.lab-pack/1";

class LabDataPackResult; // defined after LabDataPack (Qt containers need
                         // copyable payloads)

/// Where an input comes from; determines verification strength.
enum class LabPackProvenance
{
  CommittedFixture,   ///< in-repo deterministic file; sha256 required, hard-fail
  GeneratedSamples,   ///< sicnu_generate_samples output; presence + soft size
  GeneratedTmp,       ///< gen_lab_fixtures.py output; presence + soft size
};

LabPackProvenance labPackProvenanceFromString( const QString &provenance );
QString labPackProvenanceToString( LabPackProvenance provenance );

/// One required file.
struct LabPackInput
{
  QString path;         ///< repo/bundle-relative forward-slash path
  QString role;         ///< "sample" | "aux" | "truth" | "fixture"
  LabPackProvenance provenance = LabPackProvenance::GeneratedSamples;
  QString sha256;       ///< lowercase hex; required iff CommittedFixture
  qint64 declaredBytes = -1; ///< informative unless CommittedFixture (hard check)
  QString sensorTruth;  ///< band roles / CRS / dtype statement (human-auditable)
  QString notes;

  Json::Value toJson() const;
};

/// One lab's parsed pack document.
struct LabDataPack
{
  QString labId;
  QString packVersion;
  QString license;
  QString generator;    ///< canonical command that (re)produces the pack's data
  QVector<LabPackInput> inputs;
  qint64 declaredOfflineBytes = -1; ///< sum the teacher can quote for disks
  QString notes;

  /// Parse + validate the pack document at @p path.
  static LabDataPackResult load( const QString &path );
};

/// Typed load outcome for one pack (alias `LabDataPack::Result`).
///   lab.pack_schema      not parseable / wrong schema_version
///   lab.pack_field       required field missing or ill-typed
///   lab.pack_input       an input entry is invalid (bad sha256, duplicate
///                        path, sha256 missing on a committed fixture, …)
///   lab.pack_unreadable  the file cannot be opened
class LabDataPackResult
{
  public:
    static LabDataPackResult ok( LabDataPack pack )
    {
      LabDataPackResult r;
      r.m_ok = true;
      r.m_pack = std::move( pack );
      return r;
    }
    static LabDataPackResult fail( const QString &code, const QString &message )
    {
      LabDataPackResult r;
      r.m_code = code;
      r.m_message = message;
      return r;
    }

    explicit operator bool() const { return m_ok; }
    bool has_value() const { return m_ok; }
    const LabDataPack &pack() const { return m_pack; }
    const QString &errorCode() const { return m_code; }
    const QString &errorMessage() const { return m_message; }

  private:
    bool m_ok = false;
    LabDataPack m_pack;
    QString m_code;
    QString m_message;
};

/// Deterministic, teacher-quotable summary of one verification.
struct LabPackVerification
{
  /// "verified"  — every input present, fixtures checksum-clean
  /// "degraded"  — verified except missing/mis-sized regenerable inputs
  /// "failed"    — at least one committed fixture missing or corrupt
  QString overall;
  QVector<Json::Value> issues;  ///< typed {code, path?, detail} objects, sorted
  qint64 verifiedBytes = 0;     ///< bytes confirmed on disk
  Json::Value toJson() const;   ///< {overall, verified_bytes, input_count, issues[]}
};

class LabPackVerifier
{
  public:
    /// Buffer size for streaming sha256 (bounded memory, never whole-file).
    static constexpr qint64 kHashChunkBytes = 1024 * 1024;

    /// Verify @p pack's inputs against @p root. Read-only; never throws for
    /// pack content problems — everything lands in the typed result.
    static LabPackVerification verify( const LabDataPack &pack, const QString &root );

    /// Load every `*.pack.json` in @p dir (sorted by filename, same policy as
    /// LabSpecCatalog). Returns packs that loaded; @p problems receives one
    /// entry per unloadable file ("<file>: <code> <message>", load order).
    static QVector<LabDataPack> loadPacksFromDir(
      const QString &dir, QVector<LabDataPackResult> *problems = nullptr );
};

} // namespace sicnu::agent
