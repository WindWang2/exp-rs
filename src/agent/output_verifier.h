#pragma once

#include "sicnu_agent_export.h"

#include <json/json.h>

#include <QString>
#include <QStringList>

#include <cstddef>
#include <vector>

namespace sicnu::data
{
enum class AssetKind;
}

namespace sicnu::agent
{

/// Lightweight structured report for a committed output.
struct OutputVerification
{
  bool ok = false;
  QString kind;
  Json::Value summary;
  QStringList issues;
  QStringList warnings;
};

/// Verifies that a committed GIS output is structurally healthy without
/// scanning the whole dataset.  Used by the agent run coordinator and wired
/// into the ToolCallDispatcher result payload builder.
class SICNU_AGENT_EXPORT OutputVerifier
{
  public:
    OutputVerifier() = default;

    /// Verify @a path using the hint @a kindHint ("raster" or "vector").
    /// When the hint is empty the verifier tries raster first, then vector.
    OutputVerification verify( const QString &path, const QString &kindHint = QString() ) const;

    /// Verify a raster dataset.
    static OutputVerification verifyRaster( const QString &path );

    /// Verify a vector dataset.
    static OutputVerification verifyVector( const QString &path );

    // --------------------------------------------------------------------
    // Teaching grade mode (D4, ADR 0146).  Binary PASS/FAIL above is
    // unchanged for non-teaching callers; the methods below never touch it.
    // --------------------------------------------------------------------

    /// One graded assertion.  A deduction always carries evidence:
    /// {assertion_id, observed, expected, delta} — a score without evidence
    /// is a defect.
    struct SICNU_AGENT_EXPORT LabDeduction
    {
        QString assertionId;
        QString kind;
        QString severity;      ///< "normal" | "blocking"
        double weight = 0.0;   ///< points deducted (= assertion weight)
        double delta = 0.0;    ///< observed - expected (NaN -> JSON null)
        QString message;
        Json::Value observed;  ///< structured, rounded (12 significant digits)
        Json::Value expected;
        Json::Value toJson() const;
    };

    /// Evidence record for every evaluated assertion (passed or failed).
    struct SICNU_AGENT_EXPORT LabEvidence
    {
        QString assertionId;
        QString kind;
        bool passed = false;
        Json::Value observed;
        Json::Value expected;
        Json::Value toJson() const;
    };

    /// Result of grading one artifact against one lab's rules.
    ///
    /// D7 `--batch` extension point: batch grading / CSV export should wrap
    /// gradeForTeaching()/gradeArtifact() and serialize LabGradeResult —
    /// do not grow ad-hoc side entries; extend THIS result instead.
    struct SICNU_AGENT_EXPORT LabGradeResult
    {
        QString labId;
        QString artifactPath;   ///< as given to the grader
        QString rulesPath;
        QString verdict;        ///< "pass" | "fail" | "unverifiable"
        double score = 0.0;     ///< 0..100, 100 - sum(failed weights)
        double passingScore = 60.0;
        bool cappedByBlocking = false; ///< a blocking failure capped the score below the pass line
        bool graded = false;    ///< false => unverifiable; see @a error
        QString error;          ///< human-readable reason when unverifiable
        QString errorClass;     ///< "usage" (exit 2) | "artifact" (exit 3) | "" graded
        std::vector<LabDeduction> deductions;
        std::vector<LabEvidence> evidence;
        Json::Value summary;    ///< grid info, valid/nodata counts, budget used
        QString digest;         ///< sha256 over the canonical body (no timestamp)

        /// Deterministic body object (the "report" member of the emitted
        /// document; also the digest input).  Byte-identical for identical
        /// inputs — no wall-clock values inside.
        Json::Value toBodyJson() const;
        /// Full report document: {schema, digest, generated_utc, report}.
        /// @a generatedUtc is header-only metadata, excluded from @a digest.
        Json::Value toJson( const QString &generatedUtc ) const;
    };

    /// Options for the teaching grader.
    struct SICNU_AGENT_EXPORT LabGradeOptions
    {
        /// Byte budget for windowed reads (#808 contract).  Grading streams
        /// the raster in tiles that never exceed this budget.
        std::size_t maxBytes = 64ull * 1024ull * 1024ull;
        /// Search directory for `<lab_id>.rules.json`.  Empty resolves via
        /// SICNU_LAB_RULES_DIR, then the source/build-tree data/labs/grading.
        QString rulesDir;
    };

    /// Grade @a artifactPath against the lab rules named by
    /// @a labIdOrRulesPath (a `<lab_id>.rules.json` path or a lab id).
    /// Never throws for gradeable input: grading failures come back as a
    /// result with verdict "unverifiable" (or usage-level errors reported in
    /// @a error with graded == false).
    LabGradeResult gradeForTeaching( const QString &labIdOrRulesPath, const QString &artifactPath,
                                     const LabGradeOptions &options = {} ) const;

    /// Stable seam for batch callers (D7): identical to gradeForTeaching.
    LabGradeResult gradeArtifact( const QString &labIdOrRulesPath, const QString &artifactPath,
                                  const LabGradeOptions &options = {} ) const
    {
        return gradeForTeaching( labIdOrRulesPath, artifactPath, options );
    }

  private:
    static QString kindHintFromPath( const QString &path );
};

} // namespace sicnu::agent
