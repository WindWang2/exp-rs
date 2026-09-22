// src/recipes/lab_document.h
#pragma once

//
// RS14-20 normalized LabSpec view.
//
// Two authored lab contracts exist on master and both are inputs:
//   * D2 "canonical" lab: data/labs/<id>.lab.json, spec_version 1|2
//     (data/schemas/labspec.schema.json, strict loader lab_spec_loader.cpp)
//   * D3 rich lab: data/labs/<id>.labspec.json, "schema":"sicnu.labspec.v1"
//     (data/labs/labspec.schema.json)
// lab12–14 ship as step-less v2 wrappers whose executable content lives in a
// D3 source resolved through data/labs/lab-registry.json.
//
// LabDocument collapses both into one struct the compiler consumes. It keeps
// raw fields verbatim (params are NOT rewritten — data/…, outputs/… strings
// resolve under the future executor's policy exactly like the lab runtime).
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::recipes {

/// Which authored contract a lab document was parsed from.
enum class LabFormat { D2, D3 };

/// One lab step, normalized across both contracts.
struct LabStepDoc
{
  std::string id;            ///< D3 step id ("s1"); empty for D2 (compiler generates).
  std::string title;         ///< D2 title | D3 title.
  std::string titleZh;       ///< D2 title_zh (D3 titles are already zh).
  std::string descriptionZh; ///< D2 description_zh | D3 detail.
  std::string operatorId;    ///< e.g. "rs:spectral_index"; empty → non-operator step.
  Json::Value params{Json::objectValue}; ///< verbatim operator params (D2 only in practice).
  std::string action;        ///< D2 UI-verb slot name; empty otherwise.
  std::string teachingNote;  ///< D2 teaching_note.
  std::string completionHint;///< D2 completion_hint.
  std::string headlessNote;  ///< D3 headless_note (pipeline step mapping hint).
  int index = 0;             ///< 0-based position in the source steps array.

  bool hasOperator() const { return !operatorId.empty(); }
  bool isUiAction() const { return operatorId.empty() && !action.empty(); }
  bool isManual() const { return operatorId.empty() && action.empty(); }
};

/// Data prerequisite (D2 prerequisites[] {path,note} / D3 data.spec_ref).
struct LabAssetRef
{
  std::string path;
  std::string note;
};

/// D2 expected_artifacts[] entry.
struct LabArtifact
{
  std::string path;
  std::string kind;   ///< "raster"|"vector"|"file"|"" (unspecified)
  std::string noteZh;
};

/// D3 expected_results[] entry.
struct LabExpectedResult
{
  std::string claim;
  std::string artifact;
  std::string toleranceNote;
};

/// A reflection question (D2 thinking_questions → {prompt}; D3 questions → {prompt,hint}).
struct LabQuestion
{
  std::string prompt;
  std::string hint;
};

/// Normalized lab document. `raw` keeps the source JSON for fields with no
/// normalized slot (e.g. D3 notes/dependencies) so the compiler can still
/// carry them into teaching-origin metadata without re-parsing.
struct LabDocument
{
  LabFormat format = LabFormat::D2;
  int specVersion = 0;           ///< D2 spec_version (1|2); 0 for D3.
  std::string schemaTag;         ///< "sicnu.labspec.v1" for D3; empty for D2.

  std::string id;
  std::string title;
  std::string titleZh;
  std::string objective;
  std::string objectiveZh;
  std::string theme;             ///< D3 only.
  std::string audience;          ///< D3 only.
  int durationMinutes = 0;       ///< D3 only; 0 = unspecified.

  std::vector<LabAssetRef> prerequisites;        ///< data refs (D2) — D3 data.spec_ref folds in as one ref.
  std::vector<std::string> prerequisiteKnowledge;
  std::vector<LabStepDoc> steps;

  Json::Value paramRanges{Json::objectValue};    ///< op → param → {min?,max?,values?,note_zh?}
  std::vector<LabArtifact> expectedArtifacts;    ///< D2 expected_artifacts
  std::vector<LabExpectedResult> expectedResults;///< D3 expected_results

  std::string gradingRules;      ///< D2 grading_rules path (sicnu.lab.rules/1 file)
  std::string gradingPipeline;   ///< D2 grading_ref.pipeline | D3 pipeline.ref
  std::string gradingIntentRef;  ///< D3 grading_ref.intent_ref

  std::vector<LabQuestion> questions;            ///< reflection prompts
  std::vector<std::string> glossaryTerms;        ///< term names only (definitions stay in the lab doc)
  std::vector<std::string> principleHeadings;    ///< principles[].heading
  std::vector<std::string> operatorRoles;        ///< D3 operators[] "id — role" strings
  std::string dataSpecRef;                       ///< D3 data.spec_ref
  std::string pipelineRunner;                    ///< D3 pipeline.runner (documentary)

  std::string sourcePath;        ///< file the document was loaded from (may be empty)
  std::string sourceFingerprint; ///< fnv1a64 hex of source bytes (non-cryptographic drift check)
  std::string wrapperPath;       ///< v2 wrapper merged over this doc (registry-resolved), else empty
};

/// Typed parse/normalize failure.
struct LabDocumentError
{
  std::string path;
  std::string reason;
  std::string toString() const;
};

/// Parse raw JSON text into a LabDocument. Format is sniffed: integer
/// `spec_version` → D2; string `schema` == "sicnu.labspec.v1" → D3; anything
/// else fails typed. `path`/`bytes` feed provenance (sourcePath/fingerprint).
/// On failure returns false and fills `error` (when non-null).
bool parseLabDocument( const std::string &jsonText, const std::string &path,
                       LabDocument &out, LabDocumentError *error = nullptr );

/// Load + parse one lab file (.lab.json or .labspec.json).
bool loadLabDocumentFile( const std::string &path, LabDocument &out,
                          LabDocumentError *error = nullptr );

} // namespace sicnu::recipes
