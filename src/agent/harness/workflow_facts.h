// src/agent/harness/workflow_facts.h
#pragma once

//
// Scientific Workflow Compiler & Grounding 11.0: the fact model 2.0.
//
// Normalized, provenance-carrying facts that Compiler 10.0's model left as
// raw document keys: TIME (cadence / regularity / coverage across one or many
// acquisitions), SPATIAL RESOLUTION (unit-aware pixel-size semantics + a
// closed resolution-class table), EXTENT (validated bounding box), QUALITY
// MASKS (normalized mask-band roles), PRODUCT GENERATION (processing-level
// parse), MODEL TASK (normalized task family over the model catalog's free
// strings), and per-node RESOURCE facts. Every fact keeps the harness
// fact_status discipline: "observed" (read from an authoritative document),
// "derived" (computed here by a closed, documented rule), "assumed"
// (heuristic, e.g. naive-timestamp timezone), "unknown" (absent/unparseable —
// never fabricated).
//
// Layering: pure functions over JSON documents. No file I/O, no registry
// access, no clock, no randomness. Same input -> same output bytes. The one
// bounds table (workflowFactsLimits) is enforced fail-closed: over-bounds
// inputs degrade to typed unknowns, never silent truncation.
//
// Independent truth: time parsing is anchored on QDateTime (platform
// authority); the cadence/class/level rules are closed tables documented here
// and drift-pinned by known-answer tests whose expectations are computed by
// hand, never by the implementation under test.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness::wfacts {

/// The one bounds table (mirrors IrLimits discipline). Enforced by the
/// extractors below; mirrored in docs/tests via workflowFactsLimits().
struct FactsLimits
{
    static constexpr int kMaxDates = 512;        ///< max acquisitions per fact set
    static constexpr int kMaxMasks = 32;         ///< max quality-mask bands
    static constexpr int kMaxTextChars = 256;    ///< max carried-through raw strings
    static constexpr int kMaxScenes = 512;       ///< max scenes read from a collection doc
};

/// Machine-readable mirror of FactsLimits (drift anchor for docs/tests).
Json::Value workflowFactsLimits();

// ---------------------------------------------------------------------------
// Time parsing (independent-truth anchored on QDateTime).
// ---------------------------------------------------------------------------

/// Timezone assumption for a parsed instant without an explicit offset.
enum class TzAssumption {
    Explicit,     ///< carried Z or ±HH:MM — no assumption
    NaiveAsUtc,   ///< no offset in the source; interpreted as UTC by convention
};

struct ParsedInstant
{
    long long epochSeconds = 0;   ///< Unix seconds (UTC); date-only = midnight UTC
    bool dateOnly = false;        ///< source had no time-of-day
    TzAssumption tz = TzAssumption::Explicit;
    bool ok = false;
};

/// Parses one ISO-8601-ish timestamp: "YYYY-MM-DD", "YYYY-MM-DD HH:MM:SS",
/// "YYYY-MM-DDTHH:MM:SS", each with optional fractional seconds and optional
/// "Z" / "±HH:MM" / "±HHMM" offset. Everything else is `ok=false` — never a
/// guess.
ParsedInstant parseInstant( const std::string &text );

/// Canonical wire form: "YYYY-MM-DD" for date-only instants, else
/// "YYYY-MM-DDTHH:MM:SSZ". Round-trips through parseInstant.
std::string formatInstant( long long epochSeconds, bool dateOnly );

/// The closed regularity vocabulary.
namespace regularity {
inline constexpr const char *kNone = "none";            ///< zero usable dates
inline constexpr const char *kSingle = "single";        ///< exactly one date
inline constexpr const char *kRegular = "regular";      ///< every gap identical (±60s)
inline constexpr const char *kNearRegular = "near_regular"; ///< ≤20% of gaps deviate >±25% from median
inline constexpr const char *kIrregular = "irregular";
inline constexpr const char *kUnknown = "unknown";      ///< unparseable input
bool isKnownRegularity( const std::string &value );
} // namespace regularity

/// Normalized cadence/coverage facts over a list of acquisition timestamps.
/// Deterministic: dates are sorted, deduplicated, bounded by kMaxDates (an
/// over-bounds list is `truncated=true` and the facts stay honest about it).
struct TemporalCadenceFacts
{
    int count = 0;                  ///< usable dates after parse (bounded)
    bool truncated = false;         ///< input exceeded kMaxDates
    std::string first;              ///< canonical earliest ("", when count==0)
    std::string last;               ///< canonical latest ("", when count==0)
    long long spanSeconds = 0;      ///< last - first (0 when count<=1)
    std::string regularity = regularity::kNone;
    double cadenceDays = 0.0;       ///< median gap in days (0 when count<2)
    std::string cadenceLabel;       ///< "16d" | "monthly" | "annual" | "<Nd>" | ""
    int gapDeviations = 0;          ///< gaps outside [0.75x, 1.25x] median
    std::vector<std::string> unparseable; ///< declared timestamps that failed to parse (bounded; never silently dropped)

    /// Wire shape with per-key fact_status. `status` is the provenance of the
    /// DATE LIST itself ("observed" when the caller read it from an
    /// authoritative document); cadence/regularity/span are always "derived".
    Json::Value toJson( const std::string &status ) const;
};

/// Extracts + normalizes acquisition dates from ONE understanding document
/// (single scene: 0..1 dates from `acquisition_time`; `temporal.dates` /
/// `dates` arrays are honored when a caller folded them in). Unparseable
/// timestamps count as unusable and are listed in the wire form's
/// `unparseable` array (bounded) — a malformed timestamp never silently
/// disappears.
TemporalCadenceFacts temporalCadenceFromUnderstanding( const Json::Value &understanding );

/// Cadence facts over an explicit date list (collection descriptors, scene
/// tables). Entries may be strings or objects carrying `acquisition_time` /
/// `time` / `date`. Strings that fail to parse land in `unparseable`.
TemporalCadenceFacts temporalCadenceFromDates( const Json::Value &dates );

/// Reads the acquisition-date list of a temporal collection descriptor
/// document (the sidecar `temporal:create_collection` writes:
/// {scenes: [{path, time?...}, ...]} — times may also ride a parallel
/// `times` array). Read-only, bounded by kMaxScenes/kMaxDates. Returns a
/// null Json value when the document is not a recognizable descriptor.
Json::Value temporalDatesFromCollectionDescriptor( const Json::Value &descriptor );

// ---------------------------------------------------------------------------
// Spatial resolution / extent facts.
// ---------------------------------------------------------------------------

/// The closed resolution-class table (pixel size, linear metres). Thresholds
/// are conventions, not measurements: documented here, drift-pinned in tests.
/// "unknown_meters" = pixel size present but its unit is not linear metres
/// (e.g. geographic degrees) — the honest answer without a projection.
namespace resolution_class {
inline constexpr const char *kFine = "fine";               ///< <= 10 m
inline constexpr const char *kMedium = "medium";           ///< (10 m, 30 m]
inline constexpr const char *kCoarse = "coarse";           ///< > 30 m
inline constexpr const char *kUnknownMeters = "unknown_meters";
inline constexpr const char *kUnknown = "unknown";         ///< no pixel size at all
bool isKnownResolutionClass( const std::string &value );
} // namespace resolution_class

struct ResolutionFacts
{
    double pixelSizeX = 0.0;
    double pixelSizeY = 0.0;
    bool pixelSizePresent = false;
    std::string crsUnit;          ///< "metre" | "degree" | "" (unresolvable)
    std::string resolutionClass = resolution_class::kUnknown;
    bool extentPresent = false;
    bool extentValid = false;     ///< xmin<xmax && ymin<ymax (after normalize)
    double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
    bool anisotropic = false;     ///< |x| != |y| pixel size (beyond 1e-9 relative)

    Json::Value toJson() const;   ///< includes per-key fact_status
};

/// Unit-aware spatial facts from an understanding document. CRS unit comes
/// from the WKT `UNIT[...]` when the document carries WKT, else a closed
/// geographic-authid table ("degree"), else "" (unknown — never guessed).
ResolutionFacts spatialResolutionFacts( const Json::Value &understanding );

// ---------------------------------------------------------------------------
// Quality mask facts.
// ---------------------------------------------------------------------------

/// True when `role` is a mask/quality band role (SICNU_BAND_ROLE mask
/// vocabulary). The canonical harness-side predicate — consumers must use
/// this, not private copies (spatial_contracts keeps its own copy as the
/// producer side; a unit test pins the two vocabularies together).
bool isMaskRoleName( const std::string &role );

struct QualityMaskFacts
{
    bool present = false;
    int count = 0;                       ///< bounded by kMaxMasks (truncated honest)
    bool truncated = false;
    std::vector<std::string> roles;      ///< normalized mask roles
    std::vector<int> bands;              ///< band indices (when declared)
    bool bandOutOfRange = false;         ///< a declared band index >= band_count

    Json::Value toJson() const;
};

/// Normalizes the understanding `quality_masks` array ([{band, role}]) with
/// band-index validation against `band_count` when present.
QualityMaskFacts qualityMaskFacts( const Json::Value &understanding );

// ---------------------------------------------------------------------------
// Product generation facts.
// ---------------------------------------------------------------------------

struct ProductGenerationFacts
{
    std::string productType;         ///< observed SICNU_PRODUCT_TYPE / product_type
    std::string processingLevel;     ///< observed raw level string ("L1C", "2A", ...)
    int generationLevel = -1;        ///< parsed 0..3 (-1 = unparseable/absent)
    std::string levelSuffix;         ///< trailing letters ("C" of "L1C"), lowercased
    Json::Value toJson() const;      ///< per-key fact_status (observed/derived/unknown)
};

/// Parses the processing level WITHOUT guessing semantics: "L2A" -> {2,"a"};
/// "Level-1C" -> {1,"c"}; anything else stays unknown. The radiometric
/// meaning of a level is NOT decided here (numeric-domain facts remain the
/// analysis's job).
ProductGenerationFacts productGenerationFacts( const Json::Value &understanding );

// ---------------------------------------------------------------------------
// Model task facts.
// ---------------------------------------------------------------------------

/// The closed task-family vocabulary (normalized: lowercase, '-'/' ' -> '_').
namespace model_task {
inline constexpr const char *kSegmentation = "segmentation";
inline constexpr const char *kClassification = "classification";
inline constexpr const char *kDetection = "detection";
inline constexpr const char *kChangeDetection = "change_detection";
inline constexpr const char *kRegression = "regression";
inline constexpr const char *kEmbedding = "embedding";
inline constexpr const char *kExtraction = "extraction";
inline constexpr const char *kOther = "other";
bool isKnownModelTask( const std::string &value );
} // namespace model_task

struct ModelTaskFacts
{
    std::string rawTask;             ///< observed free string ("building extraction")
    std::string taskFamily = model_task::kOther;  ///< normalized closed family
    std::string readiness;           ///< observed readiness name ("" absent)
    bool compatible = false;
    bool compatibleDeclared = false;
    long long estimatedRamMb = 0;
    bool gpu = false;
    Json::Value toJson() const;
};

/// Normalizes a model contract document — the capability candidate entry the
/// select_model tool records (task/readiness/compatible/cost) or any object
/// carrying those keys. Missing keys stay unknown in fact_status.
ModelTaskFacts modelTaskFacts( const Json::Value &modelContract );

// ---------------------------------------------------------------------------
// Per-node resource facts.
// ---------------------------------------------------------------------------

struct ResourceFacts
{
    long long nodeEstimateMb = 0;        ///< declared by the IR node (0 = absent)
    long long capabilityDemandMb = 0;    ///< derived from capability cost (0 = absent)
    long long expectationsMaxRamMb = 0;  ///< declared document budget (0 = absent)
    std::string device;                  ///< "" | "cpu" | "gpu"
    bool overBudget = false;
    bool budgetKnown = false;            ///< true when both demand and budget are > 0

    Json::Value toJson() const;
};

/// One normalization for every resource number the compiler reasons about.
/// `capabilityCost` is the capability candidate's `cost` object (as emitted
/// by the capability tools); `expectations` is the IR document's
/// expectations object.
ResourceFacts resourceFacts( long long nodeEstimateMb, const Json::Value &capabilityCost,
                             const Json::Value &expectations, const std::string &device );

/// Canonical compact serialization + SHA-256 first-16 hex of a facts object —
/// the identity a projection/provenance record cites (same discipline as
/// workflowIrFingerprint). Deterministic: jsoncpp sorts object members.
std::string workflowFactsDigest( const Json::Value &facts );

} // namespace sicnu::agent::harness::wfacts
