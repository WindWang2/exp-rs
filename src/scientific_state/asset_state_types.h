/***************************************************************************
  scientific_state/asset_state_types.h
  RS14-01 Scientific Data Passport — core value types.

  Pure C++ (std::string / vectors), no Qt, no GDAL, jsoncpp only. Everything
  here is a value object: equality-comparable, deterministically serializable
  (see asset_state_json.h), and free of platform/state mutation logic.

  Evidence contract (the claim lattice):
    known      — declared by an authoritative source (GDAL key, catalog record,
                 sidecar field).
    inferred   — derived by the resolver from another declared fact.
    assumed    — a documented system default applied without a declaration.
    unknown    — no source and no default; absence is meaningful.
    conflicted — two sources disagree; both values are kept in `alternatives`
                 and never auto-resolved.
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_ASSET_STATE_TYPES_H
#define SICNU_SCIENTIFIC_STATE_ASSET_STATE_TYPES_H

#include "scientific_state/asset_state_schema.h"

#include <array>
#include <string>
#include <vector>

namespace sicnu::state
{

/// Evidence kind of a projected field.
enum class ClaimKind
{
    Known,
    Inferred,
    Assumed,
    Unknown,
    Conflicted
};

std::string claimKindToString( ClaimKind kind );
bool claimKindFromString( const std::string &text, ClaimKind &out );

/// Machine-readable typed error codes for state parsing/validation.
enum class StateErrorCode
{
    None,
    SchemaMismatch,
    MalformedJson,
    InvalidField
};

struct AssetStateError
{
    StateErrorCode code = StateErrorCode::None;
    std::string message;

    bool ok() const { return code == StateErrorCode::None; }
};

/// Mirrors sicnu::data::AssetKind (the catalog adapter maps between them).
enum class AssetKind
{
    Unknown,
    Raster,
    Vector,
    RemoteMap,
    VirtualRaster
};

std::string assetKindToString( AssetKind kind );
bool assetKindFromString( const std::string &text, AssetKind &out );

/// Mirrors sicnu::data::AssetState (lifecycle, not scientific state).
enum class AssetLifecycle
{
    Unknown,
    Registered,
    Resolving,
    Ready,
    Missing,
    UnavailableSource,
    Offline,
    AuthenticationRequired,
    Error,
    Stale
};

std::string assetLifecycleToString( AssetLifecycle lifecycle );
bool assetLifecycleFromString( const std::string &text, AssetLifecycle &out );

/// Sensor modality family of the asset.
enum class Modality
{
    Unknown,
    Optical,
    Sar,
    Thermal,
    Hyperspectral
};

std::string modalityToString( Modality modality );
bool modalityFromString( const std::string &text, Modality &out );

/// One evidence record for a dotted field path (e.g. "radiometric.unit").
/// Stored sorted by path; serialized verbatim (sorted) for byte determinism.
struct ClaimRecord
{
    std::string path;
    ClaimKind kind = ClaimKind::Unknown;
    std::vector<std::string> sources;       ///< ordered, deduplicated source tags
    std::string note;                       ///< why this kind, for student and agent
    std::vector<std::string> alternatives;  ///< populated iff Conflicted (sorted, unique)

    bool operator==( const ClaimRecord &other ) const
    {
        return path == other.path && kind == other.kind && sources == other.sources &&
               note == other.note && alternatives == other.alternatives;
    }
    bool operator!=( const ClaimRecord &other ) const { return !( *this == other ); }
};

/// Returns the explicit claim for @p path, or a synthesized {Unknown} record
/// when no explicit claim exists. Synthesis happens at query time only and
/// never enters the serialized document.
ClaimRecord claimFor( const struct RemoteSensingAssetState &state, const std::string &path );

/// Machine-readable explanation of one resolver inference/assumption.
struct ResolutionNote
{
    std::string code;    ///< stable machine-readable code, e.g. "radiometric.fsm_default"
    std::string path;    ///< dotted field path the note explains
    std::string detail;  ///< human/agent-readable sentence

    bool operator==( const ResolutionNote &other ) const
    {
        return code == other.code && path == other.path && detail == other.detail;
    }
    bool operator!=( const ResolutionNote &other ) const { return !( *this == other ); }
};

struct SensorStateSection
{
    std::string platform;
    std::string instrument;
    std::string sensorKey;
    Modality modality = Modality::Unknown;
    std::string productFamily;
    std::string productId;
    std::string processingLevel;

    bool operator==( const SensorStateSection &other ) const
    {
        return platform == other.platform && instrument == other.instrument &&
               sensorKey == other.sensorKey && modality == other.modality &&
               productFamily == other.productFamily && productId == other.productId &&
               processingLevel == other.processingLevel;
    }
    bool operator!=( const SensorStateSection &other ) const { return !( *this == other ); }
};

struct AcquisitionStateSection
{
    std::string timeIso;      ///< empty when unknown (never guessed)
    std::string timeSource;   ///< e.g. "metadata"|"explicit"|"filename"|"descriptor"|"catalog"
    std::string precision;    ///< e.g. "second"|"day"|"" (unknown)
    bool valid = false;

    bool operator==( const AcquisitionStateSection &other ) const
    {
        return timeIso == other.timeIso && timeSource == other.timeSource &&
               precision == other.precision && valid == other.valid;
    }
    bool operator!=( const AcquisitionStateSection &other ) const { return !( *this == other ); }
};

/// One projected band. Per-band radiometric fields are projected verbatim;
/// interpretation belongs to the dataset-level radiometric section.
struct BandState
{
    int index = 0;  // 1-based GDAL band index
    std::string name;
    std::string role;  // band-role vocabulary ("nir", "red_edge", ...); "" = unknown

    bool hasWavelengthNm = false;
    double wavelengthNm = 0.0;
    bool hasFwhmNm = false;
    double fwhmNm = 0.0;

    std::string dataType;
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool hasScale = false;
    double scale = 0.0;
    bool hasOffset = false;
    double offset = 0.0;

    std::string radiometricUnit;  // per-band override, "" when absent
    bool maskBand = false;

    bool operator==( const BandState &other ) const
    {
        return index == other.index && name == other.name && role == other.role &&
               hasWavelengthNm == other.hasWavelengthNm && wavelengthNm == other.wavelengthNm &&
               hasFwhmNm == other.hasFwhmNm && fwhmNm == other.fwhmNm &&
               dataType == other.dataType && hasNoData == other.hasNoData &&
               noDataValue == other.noDataValue && hasScale == other.hasScale &&
               scale == other.scale && hasOffset == other.hasOffset && offset == other.offset &&
               radiometricUnit == other.radiometricUnit && maskBand == other.maskBand;
    }
    bool operator!=( const BandState &other ) const { return !( *this == other ); }
};

struct GeometryStateSection
{
    bool hasCrs = false;
    std::string crsWkt;
    std::string crsAuthid;
    bool crsGeographic = false;
    bool crsProjected = false;

    bool hasGeoTransform = false;
    std::array<double, 6> geoTransform {};  // traditional GIS order, GDAL convention

    bool hasPixelSize = false;
    double pixelSizeX = 0.0;
    double pixelSizeY = 0.0;

    bool hasSize = false;
    int width = 0;
    int height = 0;

    bool hasExtent = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    bool operator==( const GeometryStateSection &other ) const
    {
        return hasCrs == other.hasCrs && crsWkt == other.crsWkt && crsAuthid == other.crsAuthid &&
               crsGeographic == other.crsGeographic && crsProjected == other.crsProjected &&
               hasGeoTransform == other.hasGeoTransform && geoTransform == other.geoTransform &&
               hasPixelSize == other.hasPixelSize && pixelSizeX == other.pixelSizeX &&
               pixelSizeY == other.pixelSizeY && hasSize == other.hasSize &&
               width == other.width && height == other.height && hasExtent == other.hasExtent &&
               minX == other.minX && minY == other.minY && maxX == other.maxX &&
               maxY == other.maxY;
    }
    bool operator!=( const GeometryStateSection &other ) const { return !( *this == other ); }
};

struct ValidityStateSection
{
    /// "declared" | "undeclared" | "partial" | "" (unknown)
    std::string noDataPolicy;
    bool hasCloudCover = false;
    double cloudCoverPercent = 0.0;
    /// Declared QA vocabulary reference (e.g. "landsat_qa_pixel", "s2_scl"), "" when unknown.
    std::string qualityMaskInfo;

    bool operator==( const ValidityStateSection &other ) const
    {
        return noDataPolicy == other.noDataPolicy && hasCloudCover == other.hasCloudCover &&
               cloudCoverPercent == other.cloudCoverPercent &&
               qualityMaskInfo == other.qualityMaskInfo;
    }
    bool operator!=( const ValidityStateSection &other ) const { return !( *this == other ); }
};

/// Dataset-level radiometric projection. `unit` uses the normalized passport
/// vocabulary ("digital_number", "radiance", "toa_reflectance",
/// "surface_reflectance", "brightness_temperature", "sigma0", "gamma0",
/// "beta0", "unknown"); `declaredRaw` keeps the first raw declared token.
/// SAR domain is recorded verbatim — never interpreted here.
struct RadiometricStateSection
{
    std::string unit;
    std::string declaredRaw;
    std::string domain;  // "linear_power"|"db"|"" 
    bool hasNumericScale = false;
    double numericScale = 0.0;

    bool operator==( const RadiometricStateSection &other ) const
    {
        return unit == other.unit && declaredRaw == other.declaredRaw &&
               domain == other.domain && hasNumericScale == other.hasNumericScale &&
               numericScale == other.numericScale;
    }
    bool operator!=( const RadiometricStateSection &other ) const { return !( *this == other ); }
};

struct TemporalStateRef
{
    std::string collectionId;
    std::string role;

    bool operator==( const TemporalStateRef &other ) const
    {
        return collectionId == other.collectionId && role == other.role;
    }
    bool operator!=( const TemporalStateRef &other ) const { return !( *this == other ); }
};

struct ProvenanceInputRef
{
    std::string assetId;
    std::string revision;
    std::vector<std::string> bandReferences;  // sorted, unique
    std::string valueDomain;

    bool operator==( const ProvenanceInputRef &other ) const
    {
        return assetId == other.assetId && revision == other.revision &&
               bandReferences == other.bandReferences && valueDomain == other.valueDomain;
    }
    bool operator!=( const ProvenanceInputRef &other ) const { return !( *this == other ); }
};

/// Projection of a DerivationRecord summary. Kept small on purpose — the
/// full provenance stays where it lives today.
struct ProvenanceSection
{
    bool isDerived = false;
    std::string algorithmId;
    std::string algorithmVersion;
    std::vector<ProvenanceInputRef> inputs;  // sorted by assetId, then revision
    std::string completedAtUtc;
    std::string executionFingerprint;
    std::string softwareVersion;
    std::string workflowRef;
    bool cacheHit = false;

    bool operator==( const ProvenanceSection &other ) const
    {
        return isDerived == other.isDerived && algorithmId == other.algorithmId &&
               algorithmVersion == other.algorithmVersion && inputs == other.inputs &&
               completedAtUtc == other.completedAtUtc &&
               executionFingerprint == other.executionFingerprint &&
               softwareVersion == other.softwareVersion && workflowRef == other.workflowRef &&
               cacheHit == other.cacheHit;
    }
    bool operator!=( const ProvenanceSection &other ) const { return !( *this == other ); }
};

/// Projection of a model sidecar (e.g. trained classifier `.meta.json`).
struct ModelDerivedSection
{
    bool present = false;
    std::string modelKind;
    std::vector<std::string> labels;  // sorted, unique
    bool hasAccuracy = false;
    double accuracy = 0.0;
    std::string sidecarPath;
    std::string featureSchema;  // presence summary, "" when absent

    bool operator==( const ModelDerivedSection &other ) const
    {
        return present == other.present && modelKind == other.modelKind &&
               labels == other.labels && hasAccuracy == other.hasAccuracy &&
               accuracy == other.accuracy && sidecarPath == other.sidecarPath &&
               featureSchema == other.featureSchema;
    }
    bool operator!=( const ModelDerivedSection &other ) const { return !( *this == other ); }
};

/// The unified scientific state of one remote-sensing asset. A versioned,
/// read-only projection — never a second registry.
struct RemoteSensingAssetState
{
    std::string schemaId = kAssetStateSchemaId;

    // Identity
    std::string assetId;
    std::string revision;
    std::string sourcePath;
    std::string displayName;
    AssetKind kind = AssetKind::Unknown;
    AssetLifecycle lifecycle = AssetLifecycle::Unknown;
    std::string persistence;

    SensorStateSection sensor;
    AcquisitionStateSection acquisition;

    std::vector<BandState> bands;  // sorted by 1-based index

    GeometryStateSection geometry;
    ValidityStateSection validity;
    RadiometricStateSection radiometric;

    bool hasTemporalRefs = false;
    std::vector<TemporalStateRef> temporalRefs;  // sorted by collectionId
    bool temporalRefsTruncated = false;

    ProvenanceSection provenance;
    ModelDerivedSection modelDerived;

    /// Overall resolvability confidence in [0,1]; 0 when nothing is known.
    double confidence = 0.0;

    std::vector<std::string> assumptions;  // sorted, unique
    std::vector<std::string> unknowns;     // sorted, unique field paths
    std::vector<ClaimRecord> claims;       // sorted by path
    std::vector<ResolutionNote> notes;     // sorted by (code, path, detail)

    bool operator==( const RemoteSensingAssetState &other ) const
    {
        return schemaId == other.schemaId && assetId == other.assetId &&
               revision == other.revision && sourcePath == other.sourcePath &&
               displayName == other.displayName && kind == other.kind &&
               lifecycle == other.lifecycle && persistence == other.persistence &&
               sensor == other.sensor && acquisition == other.acquisition &&
               bands == other.bands && geometry == other.geometry &&
               validity == other.validity && radiometric == other.radiometric &&
               hasTemporalRefs == other.hasTemporalRefs && temporalRefs == other.temporalRefs &&
               temporalRefsTruncated == other.temporalRefsTruncated &&
               provenance == other.provenance && modelDerived == other.modelDerived &&
               confidence == other.confidence && assumptions == other.assumptions &&
               unknowns == other.unknowns && claims == other.claims && notes == other.notes;
    }
    bool operator!=( const RemoteSensingAssetState &other ) const { return !( *this == other ); }
};

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_TYPES_H
