// src/agent/cartography/solution_registry.h
#pragma once

//
// SolutionTemplate registry (Platform 5.0, Milestones A/B).
//
// A SolutionTemplate is the task-level knowledge package binding an analysis
// recipe, a map template, optional report template and StyleSpec references
// into one validated, searchable, instantiable unit (see
// .planning/solution-template-platform-5/SOLUTION_SCHEMA.md). Documents live
// under data/agent/solutions/*.json (kind "solution_template").
//
// The registry owns loading, intrinsic validation, `extends` resolution with
// cycle detection, deterministic id ordering, bounded facet search and
// compact (token-budget) summaries. Cross-domain reference validation
// (recipe/template/style ids) is pluggable via RefResolvers so this module
// stays inside the cartography layer; the harness tools wire the real
// resolvers. Instantiation itself lives with the harness tooling — solutions
// never execute operators themselves.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

namespace sicnu::agent::cartography {

/// Resolvers used to validate cross-domain references. Empty functions mean
/// "cannot check here" — the reference check is then skipped (recorded as a
/// deferred problem by callers that need full validation).
struct RefResolvers
{
    std::function<bool( const std::string & )> recipe;    ///< harness recipe id known?
    std::function<bool( const std::string & )> mapTemplate;  ///< template id known?
    std::function<bool( const std::string & )> reportTemplate;
    std::function<bool( const std::string & )> style;     ///< style spec id known?
};

/// Closed modality facet vocabulary.
bool isSolutionModality( const std::string &modality );
/// Closed quality-grade vocabulary: experimental|reviewed|certified.
bool isQualityGrade( const std::string &grade );

/// Structural validation of a solution document. `resolvers` may be null:
/// with null resolvers references are shape-checked but not resolved.
/// Empty returned vector = valid.
std::vector<std::string> validateSolutionTemplate( const Json::Value &doc,
                                                   const RefResolvers *resolvers = nullptr );

/// Resolves `extends` chains against `solutionsById` (id -> document).
/// Deep-merges parent under child (child fields win; objects merge, arrays
/// replace). Detects cycles and unknown parents; problems are appended to
/// *problems. Returns the resolved document, or Null when unresolvable.
Json::Value resolveSolutionInheritance( const Json::Value &doc,
                                        const QMap<QString, Json::Value> &solutionsById,
                                        QStringList *problems );

/// Compact, bounded summary for search results:
/// {id, version, title, tasks, modalities, sensors, quality_grade,
///  analysis_recipe, map_template, input_summary}.
Json::Value compactSolutionSummary( const Json::Value &solution );

/// Faceted search query. Empty strings are ignored; `page`/`pageSize` clamp
/// to bounded values. Deterministic: hits are id-ordered.
struct SolutionQuery
{
    std::string task;        ///< substring match over `tasks`
    std::string modality;    ///< exact match over `modalities`
    std::string sensor;      ///< substring match over `sensors`
    std::string keyword;     ///< substring over title/description/keywords
    std::string quality;     ///< exact quality_grade
    std::string family;      ///< exact `family` facet
    int page = 0;
    int pageSize = 20;       ///< clamped to [1, 50]
};

/// Result envelope: {items: [compact summaries], total, page, page_size,
/// next_page|null}.
Json::Value searchSolutions( const QMap<QString, Json::Value> &solutions,
                             const SolutionQuery &query );

class SolutionRegistry
{
  public:
    static SolutionRegistry &instance();

    /// Directory override (scans *.json directly). Reloads lazily.
    void setDirectory( const QString &dir );
    QString directory() const;

    /// All resolved solution documents (id-ordered).
    Json::Value solutions() const;

    Json::Value find( const QString &id ) const;   ///< Resolved document; null when unknown.
    /// True when `id` or any declared alias resolves to a solution.
    bool knowsId( const std::string &id ) const;

    bool registerSolution( Json::Value doc, QString *error = nullptr );

    /// Problems recorded while loading (parse failures, invalid documents,
    /// inheritance cycles, duplicate aliases).
    QStringList loadProblems() const;

    void reload();

  private:
    SolutionRegistry() = default;
    void ensureLoadedLocked() const;
    void loadEmbeddedDefaults();

    mutable QMutex mMutex;
    mutable bool mLoaded = false;
    QString mDirectory;
    mutable QMap<QString, Json::Value> mSolutions;      // id -> resolved document
    mutable QMap<QString, QString> mAliases;            // alias -> canonical id
    mutable QStringList mLoadProblems;
};

} // namespace sicnu::agent::cartography
