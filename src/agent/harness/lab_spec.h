// src/agent/harness/lab_spec.h
#pragma once

//
// D9: the LabSpec grounding seam.
//
// The copilot must read the SAME lab specs the UI uses (`data/labs/*.lab.json`,
// the D2 schema: spec_version / id / title_zh / steps[] with title_zh,
// description_zh, operator_id, params, teaching_note, completion_hint /
// thinking_questions). On branches where D2 has not landed the directory is
// absent — the catalog degrades with a typed `unavailable` status and the
// copilot says so honestly instead of inventing steps.
//
// Answers are anchored to the student's CURRENT step, never to the whole
// lab: stepDoc() returns exactly one step's teaching facts.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

class LabSpecCatalog
{
  public:
    static LabSpecCatalog &instance();

    /// Directory override (tests, embedders). Empty restores the default
    /// search: $SICNU_LAB_SPEC_DIR, <cwd>/data/labs, <app>/../data/labs,
    /// SICNU_SOURCE_DIR/data/labs — same policy as the capability knowledge.
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// (Re)scans the directory for *.lab.json. Returns the number of valid
    /// specs loaded.
    int reload();

    /// True after a completed scan (auto-loaded on first query — same
    /// contract as CapabilityKnowledge).
    bool loaded() const { return mLoaded; }

    /// "ok" | "unavailable" (missing/empty directory or parse failures only).
    std::string status() const;
    std::vector<std::string> loadProblems() const;

    /// Sorted lab ids (empty when unavailable).
    std::vector<std::string> labIds() const;

    /// The raw spec document for `labId`, or null when unknown.
    Json::Value lab( const std::string &labId ) const;

    /// One step's teaching facts for the CURRENT-step anchor:
    /// {index (0-based), title_zh, description_zh, teaching_note?,
    ///  completion_hint?, operator_id?, param_names[] (names only — values
    ///  are the SOLUTION and never leave this module for students)}.
    /// Null when the lab/step does not exist.
    Json::Value stepDoc( const std::string &labId, int stepIndex ) const;

    /// "我不会做第3步" / "i am stuck on step 2" → 0-based step index.
    /// -1 when the message does not name a step. Deterministic.
    static int stepIndexFromMessage( const std::string &message, int stepCount );

  private:
    LabSpecCatalog() = default;
    std::string defaultDirectory() const;

    std::string mDirectory;
    bool mLoaded = false;
    Json::Value mSpecs{Json::objectValue}; ///< lab id -> spec document
    std::vector<std::string> mOrder;       ///< lab ids in load order
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness
