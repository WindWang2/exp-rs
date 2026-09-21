// src/agent/harness/curriculum_catalog.h
#pragma once

//
// RS14: the curriculum grounding seam.
//
// The curriculum manifest (`data/curriculum/*.curriculum.json`,
// `sicnu.curriculum/1`) is the teaching-organization layer OVER the lab
// contract chain — it references LabSpecs, lab-registry identities, packs and
// capabilities; it never restates lab content. This catalog loads and
// validates the manifest, projects machine-readable availability questions
// and feeds the progress model.
//
// Dependency policy (deliberate, like lab_spec_loader): jsoncpp + std only.
// No Qt, no Processing Registry, no CapabilityKnowledge — so the whole
// behavior surface is unit-testable as a small pure-C++ target. Operator
// availability lives in curriculum_availability.h (probe-injected); the
// real-registry wiring lives in curriculum_registry_probe.h and is compiled
// only into sicnu_agent.
//
// Failure policy: a manifest that fails parse or ANY validation issue loads
// as typed `unavailable` with the full issue list attached. There is no
// partial manifest and no silent fallback — same contract as LabSpecCatalog.
//
// Single-source-of-truth policy: lab content stays in LabSpec documents (the
// strict loader in src/app/widgets/lab_spec_loader.{h,cpp} is the authority
// for full LabSpec validity). The catalog's labspec probe here is a shallow
// resolvability check (id match + spec_version + steps shape) used only to
// answer "does this curriculum reference resolve" — never to judge labspec
// correctness.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

/// One typed, machine-readable validation problem.
/// `code` comes from the closed `sicnu.curriculum.error/1` vocabulary.
struct CurriculumIssue
{
    std::string code;        ///< e.g. "unknown_lab_reference"
    std::string path;        ///< JSON-pointer-ish location, e.g. "modules[0].labs[1].lab_id"
    std::string message_zh;  ///< human-readable Chinese explanation

    std::string toString() const;
};

/// Filesystem roots the reference resolver probes. Empty members keep the
/// repo defaults (<cwd>/data/labs, … — same search policy as LabSpecCatalog,
/// plus SICNU_SOURCE_DIR fallback). Tests inject temp dirs.
struct CurriculumPaths
{
    std::string labsDir;      ///< data/labs (lab documents + lab-registry.json)
    std::string packsDir;     ///< data/labs/packs
};

class CurriculumCatalog
{
  public:
    static CurriculumCatalog &instance();

    /// Directory override for MANIFESTS (tests, embedders). Empty restores the
    /// default search: $SICNU_CURRICULUM_DIR, <cwd>/data/curriculum,
    /// SICNU_SOURCE_DIR/data/curriculum.
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// Roots for the reference resolver (labs / packs). Empty keeps defaults.
    void setPaths( const CurriculumPaths &paths );
    CurriculumPaths paths() const;

    /// (Re)loads the manifest and validates it. Returns the module count
    /// (0 when unavailable). All issues survive in issues()/loadProblems().
    int reload();

    bool loaded() const;
    /// "ok" | "unavailable" (missing/empty directory, parse failure, or any
    /// validation issue).
    std::string status() const;
    const std::vector<CurriculumIssue> &issues() const;
    /// Human-readable projection of issues() (parity with LabSpecCatalog).
    std::vector<std::string> loadProblems() const;

    /// The raw manifest document. Null when unavailable.
    Json::Value manifest() const;

    /// Module ids ordered by declared index (empty when unavailable).
    std::vector<std::string> moduleIds() const;
    /// Module document by id, or null when unknown.
    Json::Value module( const std::string &moduleId ) const;
    /// One lab reference document, or null when the module/lab is unknown.
    Json::Value labRef( const std::string &moduleId, const std::string &labId ) const;

    /// All referenced lab ids in manifest order, deduplicated.
    std::vector<std::string> labIds() const;

    /// How a lab reference resolves (after a successful reload):
    /// "labspec"   — data/labs/<id>.lab.json exists and passes the shallow probe
    /// "registry"  — resolved through lab-registry.json canonical/alias source
    /// "external"  — declared external and present in lab-registry out_of_scope
    /// "unknown"   — unresolvable (cannot happen for a loaded "ok" manifest)
    std::string labResolution( const std::string &labId ) const;

    /// Progress projection: validates @p progressDoc against this manifest's
    /// lab ids and returns the completion summary document
    /// (`sicnu.curriculum.progress.summary/1`). On invalid progress docs the
    /// summary carries ok=false plus the typed issue list — never throws,
    /// never silently drops entries. See CurriculumProgress for the doc rules.
    Json::Value progressFor( const Json::Value &progressDoc ) const;

  private:
    CurriculumCatalog() = default;
    std::string defaultDirectory() const;
    CurriculumPaths effectivePaths() const;

    std::string mDirectory;
    CurriculumPaths mPaths;
    bool mLoaded = false;
    Json::Value mManifest{ Json::nullValue };
    std::vector<CurriculumIssue> mIssues;
};

/// Free-function validation so tests can exercise the full rule set without
/// touching the singleton. Validates structure, references, packs, DAG and
/// effort/role/outcome contracts; appends typed issues to @p issues.
/// @p manifest must already be a parsed JSON object.
void validateCurriculumManifest( const Json::Value &manifest,
                                 const CurriculumPaths &paths,
                                 std::vector<CurriculumIssue> &issues );

/// Shallow resolvability probe for one lab id (see class comment). Returns
/// "labspec", "registry", "external" or "unknown".
std::string resolveLabReference( const std::string &labId, const CurriculumPaths &paths );

} // namespace sicnu::agent::harness
