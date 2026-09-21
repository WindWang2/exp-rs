// src/agent/harness/curriculum_progress.h
#pragma once

//
// RS14: the curriculum progress value model.
//
// Pure value semantics over a versioned progress document
// (`sicnu.curriculum.progress/1`):
//
//   {
//     "schema": "sicnu.curriculum.progress/1",
//     "student_note": "2026 级 2 班 张三",       // optional, self-reported
//     "completed": {
//       "lab02_spectral_analysis": {
//         "evidence": "output/labs/lab02/report.json",
//         "completed_at_iso": "2026-03-12T08:00:00Z"  // caller-injected
//       }
//     }
//   }
//
// Trust boundary: a progress document is STUDENT-SELF-REPORTED state for
// navigation and UI projection. It is never grading evidence — scores stay
// with the grading transcript (`sicnu.lab.rules/1` chain). The model exists
// so the (future) progress UI and agents share one deterministic projection.
//
// Determinism: `completed_at_iso` is always injected by the caller; the
// model never reads the clock. Serializing the same doc twice yields
// byte-identical output (jsoncpp styled writer, fixed settings).
//
// Dependency policy: jsoncpp + std only (same as curriculum_catalog).
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

struct CurriculumProgressIssue
{
    std::string code;        ///< "progress_invalid_doc" | "progress_unknown_lab" |
                             ///< "progress_conflicting_evidence" | "progress_missing_field"
    std::string path;
    std::string message_zh;

    std::string toString() const;
};

class CurriculumProgress
{
  public:
    static const char *kSchema;     ///< "sicnu.curriculum.progress/1"
    static const char *kSummarySchema; ///< "sicnu.curriculum.progress.summary/1"

    /// A fresh, empty, valid progress document.
    static Json::Value emptyDoc();

    /// Full-document validation against @p knownLabIds (order-insensitive).
    /// Empty output = valid.
    static std::vector<CurriculumProgressIssue> validateDoc(
        const Json::Value &doc, const std::vector<std::string> &knownLabIds );

    /// Records one completion. Rules:
    /// - doc must be a valid progress document (else progress_invalid_doc, false);
    /// - @p labId must be a member of @p knownLabIds when that list is
    ///   non-empty (else progress_unknown_lab, false);
    /// - @p evidence and @p completedAtIso must be non-empty
    ///   (else progress_missing_field, false);
    /// - already completed with byte-equal evidence+time → idempotent true;
    /// - already completed with a different evidence or time
    ///   → progress_conflicting_evidence, false.
    static bool markCompleted( Json::Value &doc, const std::string &labId,
                               const std::string &evidence,
                               const std::string &completedAtIso,
                               const std::vector<std::string> &knownLabIds,
                               std::vector<CurriculumProgressIssue> *issues );

    struct ModuleStat
    {
        std::string moduleId;
        int total = 0;
        int done = 0;
    };

    /// Per-module completion in manifest array order. Unknown manifest → {}.
    /// Labs referenced by several modules count once per module; the overall
    /// rate in summary() deduplicates by lab id.
    static std::vector<ModuleStat> moduleCompletion( const Json::Value &doc,
                                                     const Json::Value &manifest );

    /// `sicnu.curriculum.progress.summary/1`:
    /// { schema, ok, issues?: [...], overall_percent, completed_lab_ids: [...],
    ///   modules: [{module_id, title_zh, done, total, percent, missing_lab_ids}] }
    /// deterministic field order via jsoncpp (members sorted by key).
    /// @p knownLabIds constrains validation; empty means "no lab constraint".
    static Json::Value summary( const Json::Value &doc, const Json::Value &manifest,
                                const std::vector<std::string> &knownLabIds = {} );

    /// Byte-stable serialization helper (fixed indentation, sorted members —
    /// jsoncpp's toStyledString already sorts; this pins the settings).
    static std::string serialize( const Json::Value &doc );
};

} // namespace sicnu::agent::harness
