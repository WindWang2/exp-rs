// src/agent/harness/lab_glossary.h
#pragma once

//
// D9: the Chinese glossary seam (D6 dependency).
//
// Loads `data/terms/rs_glossary.json` — the D6 schema (a list of
// {en, zh, alias?, definition_zh, category?, related?} entries) — so the
// copilot's answers use the SAME Chinese term the UI and help panels use,
// verbatim. When D6 has not landed (the file is absent) the seam degrades
// with a typed `unavailable` and the copilot says so honestly; it never
// fabricates definitions and never vendors a private copy of the data.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

class LabGlossary
{
  public:
    static LabGlossary &instance();

    /// File override (tests, embedders). Empty restores the default search:
    /// $SICNU_RS_GLOSSARY, <cwd>/data/terms/rs_glossary.json,
    /// <app>/../data/terms/rs_glossary.json,
    /// SICNU_SOURCE_DIR/data/terms/rs_glossary.json.
    void setFilePath( const std::string &path );
    std::string filePath() const;

    /// Returns the number of entries loaded; records a typed problem when
    /// the file is absent or invalid.
    int reload();

    /// "ok" | "unavailable".
    std::string status() const;
    std::vector<std::string> loadProblems() const;

    /// Case-insensitive lookup by English term, zh term, or alias.
    /// Entry document {en, zh, definition_zh, category?, related[]} or null.
    Json::Value term( const std::string &word ) const;

  private:
    LabGlossary() = default;
    std::string defaultFilePath() const;

    std::string mFilePath;
    bool mLoaded = false;
    Json::Value mByLower{Json::objectValue}; ///< lowered key -> entry
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness
