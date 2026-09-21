/***************************************************************************
  scientific_state/asset_state_resolver.cpp
  RS14-01 Scientific Data Passport — the state resolver (pure projection).
 ***************************************************************************/

#include "scientific_state/asset_state_resolver.h"

#include "scientific_state/asset_state_json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>

namespace sicnu::state
{

namespace
{

std::string toLowerAscii( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return text;
}

std::string trimAscii( const std::string &text )
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while ( begin < end && std::isspace( static_cast<unsigned char>( text[begin] ) ) )
        ++begin;
    while ( end > begin && std::isspace( static_cast<unsigned char>( text[end - 1] ) ) )
        --end;
    return text.substr( begin, end - begin );
}

bool parseDouble( const std::string &raw, double &out )
{
    const std::string trimmed = trimAscii( raw );
    if ( trimmed.empty() )
        return false;
    char *endPtr = nullptr;
    const double value = std::strtod( trimmed.c_str(), &endPtr );
    if ( endPtr != trimmed.c_str() + trimmed.size() )
        return false;
    if ( !std::isfinite( value ) )
        return false;
    out = value;
    return true;
}

bool isSourceInferential( const std::string &source )
{
    return source.rfind( "sensor_profile:", 0 ) == 0;
}

/// Result of the evidence lattice for one logical field.
struct MergedValue
{
    bool present = false;
    std::string value;                        // normalized; empty when conflicted
    ClaimKind kind = ClaimKind::Unknown;
    std::vector<std::string> sources;         // merged, insertion order
    std::vector<std::string> alternatives;    // sorted, unique (conflicted only)
    std::string firstRaw;                     // first raw observed token
};

using Normalizer = std::function<std::optional<std::string>( const RawObservation &,
                                                             std::string &failCode )>;

/// Runs the evidence lattice over observations:
///   no usable observation                        → not present (caller decides unknown)
///   one distinct normalized value                → Known (declarative) / Inferred (only
///                                                  inferential sources)
///   several distinct normalized values           → Conflicted, alternatives kept
///
/// Observations whose normalization fails emit a note but never silently
/// disappear: if all observations fail the field stays unresolvable.
MergedValue mergeObservations( const std::vector<RawObservation> &observations,
                               const Normalizer &normalizer,
                               std::vector<ResolutionNote> &notes, const std::string &path )
{
    MergedValue merged;
    std::vector<std::pair<std::string, std::string>> normalized;  // value → source
    std::vector<std::string> inferentialOnly;

    for ( const RawObservation &observation : observations )
    {
        if ( merged.firstRaw.empty() )
            merged.firstRaw = observation.value;
        std::string failCode;
        const std::optional<std::string> normalizedValue = normalizer( observation, failCode );
        if ( !normalizedValue )
        {
            ResolutionNote note;
            note.code = failCode.empty() ? "observation.unusable" : failCode;
            note.path = path;
            note.detail = "unusable observation '" + observation.value + "' from " +
                          observation.source;
            notes.push_back( note );
            continue;
        }
        normalized.emplace_back( *normalizedValue, observation.source );
        if ( isSourceInferential( observation.source ) )
            inferentialOnly.push_back( observation.source );
    }

    if ( normalized.empty() )
        return merged;

    merged.present = true;
    std::vector<std::string> distinct;
    for ( const auto &entry : normalized )
    {
        if ( std::find( distinct.begin(), distinct.end(), entry.first ) == distinct.end() )
            distinct.push_back( entry.first );
    }

    for ( const auto &entry : normalized )
        merged.sources.push_back( entry.second );

    if ( distinct.size() == 1 )
    {
        merged.value = distinct.front();
        // A declarative source makes the claim known; only when every usable
        // observation is inferential (sensor profile family truth) is it inferred.
        const bool anyDeclarative = merged.sources.size() > inferentialOnly.size();
        merged.kind = anyDeclarative ? ClaimKind::Known : ClaimKind::Inferred;
        return merged;
    }

    merged.kind = ClaimKind::Conflicted;
    merged.alternatives = distinct;
    std::sort( merged.alternatives.begin(), merged.alternatives.end() );
    merged.alternatives.erase( std::unique( merged.alternatives.begin(),
                                            merged.alternatives.end() ),
                               merged.alternatives.end() );
    return merged;
}

/// Partitions observations into declarative and inferential runs; declarative
/// observations win when both exist (a file's explicit declaration outranks
/// family-level truth).
std::vector<RawObservation> declarativeFirst( const std::vector<RawObservation> &observations )
{
    std::vector<RawObservation> declarative;
    std::vector<RawObservation> inferential;
    for ( const RawObservation &observation : observations )
    {
        if ( isSourceInferential( observation.source ) )
            inferential.push_back( observation );
        else
            declarative.push_back( observation );
    }
    declarative.insert( declarative.end(), inferential.begin(), inferential.end() );
    return declarative;
}

void appendClaim( RemoteSensingAssetState &state, const std::string &path, const MergedValue &merged,
                  const std::string &note = {} )
{
    if ( !merged.present )
        return;
    ClaimRecord claim;
    claim.path = path;
    claim.kind = merged.kind;
    claim.sources = merged.sources;
    claim.note = note;
    claim.alternatives = merged.alternatives;
    state.claims.push_back( claim );
}

void addNote( RemoteSensingAssetState &state, const std::string &code, const std::string &path,
              const std::string &detail )
{
    ResolutionNote note;
    note.code = code;
    note.path = path;
    note.detail = detail;
    state.notes.push_back( note );
}

void addUnknown( RemoteSensingAssetState &state, const std::string &path )
{
    state.unknowns.push_back( path );
    ClaimRecord claim;
    claim.path = path;
    claim.kind = ClaimKind::Unknown;
    state.claims.push_back( claim );
}

void addAssumption( RemoteSensingAssetState &state, const std::string &statement )
{
    state.assumptions.push_back( statement );
}

bool normalizeIdentityToken( const RawObservation &observation, std::string &failCode )
{
    const std::string trimmed = trimAscii( observation.value );
    if ( trimmed.empty() )
    {
        failCode = "observation.empty";
        return false;
    }
    return true;
}

// Wrapped normalizers that fit the Normalizer signature and keep the value.
struct NormalizeAsIs
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        if ( !normalizeIdentityToken( observation, failCode ) )
            return std::nullopt;
        return trimAscii( observation.value );
    }
};

struct NormalizeLower
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        if ( !normalizeIdentityToken( observation, failCode ) )
            return std::nullopt;
        return toLowerAscii( trimAscii( observation.value ) );
    }
};

struct NormalizeModality
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        Modality modality = Modality::Unknown;
        if ( !modalityFromString( trimAscii( observation.value ), modality ) )
        {
            failCode = "modality.unknown_token";
            return std::nullopt;
        }
        return modalityToString( modality );
    }
};

struct NormalizeRadiometric
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        std::string unit;
        if ( !normalizeRadiometricToken( observation.value, unit ) )
        {
            failCode = "radiometric.unknown_token";
            return std::nullopt;
        }
        return unit;
    }
};

} // namespace

bool normalizeRadiometricToken( const std::string &raw, std::string &unit )
{
    const std::string token = toLowerAscii( trimAscii( raw ) );
    if ( token.empty() )
        return false;
    if ( token == "digital_number" || token == "dn" )
    {
        unit = "digital_number";
        return true;
    }
    if ( token == "radiance" ) { unit = "radiance"; return true; }
    if ( token == "toa_reflectance" ) { unit = "toa_reflectance"; return true; }
    if ( token == "surface_reflectance" || token == "boa_reflectance" )
    {
        unit = "surface_reflectance";
        return true;
    }
    if ( token == "brightness_temperature" ) { unit = "brightness_temperature"; return true; }
    if ( token == "sigma0" ) { unit = "sigma0"; return true; }
    if ( token == "gamma0" ) { unit = "gamma0"; return true; }
    if ( token == "beta0" ) { unit = "beta0"; return true; }
    return false;
}

bool normalizeWavelengthNm( const std::string &rawValue, const std::string &units,
                            double &outNm, bool &unknownUnits )
{
    unknownUnits = false;
    double value = 0.0;
    if ( !parseDouble( rawValue, value ) )
        return false;

    const std::string normalizedUnits = toLowerAscii( trimAscii( units ) );
    if ( normalizedUnits.empty() || normalizedUnits == "nm" )
    {
        outNm = value;
        return true;
    }
    const bool isMicrometer = normalizedUnits == "um" || normalizedUnits == "µm" ||
                              normalizedUnits == "micrometer" || normalizedUnits == "micrometers" ||
                              normalizedUnits == "micron" || normalizedUnits == "microns";
    if ( isMicrometer )
    {
        outNm = value * 1000.0;
        return true;
    }
    unknownUnits = true;
    return false;
}

namespace
{

/// Resolves one band-indexed field with the declarative-first lattice.
MergedValue resolveBandField( const std::vector<RawObservation> &observations,
                              const Normalizer &normalizer,
                              std::vector<ResolutionNote> &notes, const std::string &path )
{
    return mergeObservations( declarativeFirst( observations ), normalizer, notes, path );
}

void resolveRadiometric( RemoteSensingAssetState &state, const DatasetFacts &dataset,
                         std::vector<ResolutionNote> &notes )
{
    const MetadataItems &metadata = dataset.metadata;

    std::vector<RawObservation> declared;
    for ( const RawObservation &observation : metadata.find( "SICNU_RADIOMETRIC_STATE" ) )
        declared.push_back( observation );
    for ( const RawObservation &observation : metadata.find( "SICNU_SAR_CALIBRATION" ) )
        declared.push_back( observation );

    const MergedValue merged =
        mergeObservations( declared, NormalizeRadiometric{}, notes, "radiometric.unit" );

    if ( merged.present )
    {
        state.radiometric.declaredRaw = merged.firstRaw;
        if ( merged.kind == ClaimKind::Conflicted )
        {
            // Mirror of readDeclaredSarState: two declarations disagree —
            // record the conflict, never resolve it silently.
            state.radiometric.unit.clear();
            ClaimRecord claim;
            claim.path = "radiometric.unit";
            claim.kind = ClaimKind::Conflicted;
            claim.sources = merged.sources;
            claim.alternatives = merged.alternatives;
            claim.note = "declared radiometric tokens disagree";
            state.claims.push_back( claim );
            addNote( state, "radiometric.conflict", "radiometric.unit",
                     "declared radiometric tokens disagree: kept both alternatives" );
        }
        else
        {
            state.radiometric.unit = merged.value;
            appendClaim( state, "radiometric.unit", merged );
        }
    }
    else
    {
        const std::vector<RawObservation> assumed = metadata.find( "SICNU_SAR_STATE_ASSUMED" );
        if ( !assumed.empty() && !trimAscii( assumed.front().value ).empty() )
        {
            // Project the SAR legacy assumption: "<unit>_legacy_undeclared"
            // (or a directly normalizable token). Never interpret it.
            const std::string raw = trimAscii( assumed.front().value );
            const std::string suffix = "_legacy_undeclared";
            std::string candidate = raw;
            if ( candidate.size() > suffix.size() &&
                 candidate.compare( candidate.size() - suffix.size(), suffix.size(), suffix ) == 0 )
                candidate = candidate.substr( 0, candidate.size() - suffix.size() );
            std::string unit;
            if ( !normalizeRadiometricToken( candidate, unit ) )
                unit = toLowerAscii( candidate );
            state.radiometric.unit = unit;
            ClaimRecord claim;
            claim.path = "radiometric.unit";
            claim.kind = ClaimKind::Assumed;
            claim.sources = { assumed.front().source };
            claim.note = "assumed from " + raw;
            state.claims.push_back( claim );
            addAssumption( state, "radiometric.unit assumed " + unit + " (" + raw + ")" );
            addNote( state, "radiometric.sar_assumed", "radiometric.unit",
                     "radiometric unit assumed from " + raw );
        }
        else
        {
            // Documented FSM default (src/core/radiometric_state.h): a missing
            // marker means DigitalNumber. Projected as an explicit assumption.
            state.radiometric.unit = "digital_number";
            ClaimRecord claim;
            claim.path = "radiometric.unit";
            claim.kind = ClaimKind::Assumed;
            claim.note = "FSM default: missing marker means digital_number";
            state.claims.push_back( claim );
            addAssumption( state, "radiometric.unit assumed digital_number (missing marker; "
                                  "FSM default)" );
            addNote( state, "radiometric.fsm_default", "radiometric.unit",
                     "no declared radiometric state; FSM default digital_number applied" );
        }
    }

    // SAR domain is recorded verbatim, never interpreted.
    const std::vector<RawObservation> domain = metadata.find( "SICNU_SAR_DOMAIN" );
    if ( !domain.empty() )
    {
        const std::string value = toLowerAscii( trimAscii( domain.front().value ) );
        if ( !value.empty() )
        {
            state.radiometric.domain = value;
            MergedValue domainMerged;
            domainMerged.present = true;
            domainMerged.value = value;
            domainMerged.kind = ClaimKind::Known;
            domainMerged.sources = { domain.front().source };
            appendClaim( state, "radiometric.domain", domainMerged );
        }
    }

    const std::vector<RawObservation> scale = metadata.find( "SICNU_NUMERIC_SCALE" );
    if ( !scale.empty() )
    {
        double value = 0.0;
        if ( parseDouble( scale.front().value, value ) )
        {
            state.radiometric.hasNumericScale = true;
            state.radiometric.numericScale = value;
            MergedValue scaleMerged;
            scaleMerged.present = true;
            scaleMerged.value = scale.front().value;
            scaleMerged.kind = ClaimKind::Known;
            scaleMerged.sources = { scale.front().source };
            appendClaim( state, "radiometric.numeric_scale", scaleMerged );
        }
        else
        {
            addUnknown( state, "radiometric.numeric_scale" );
            addNote( state, "numeric_scale.invalid", "radiometric.numeric_scale",
                     "unparsable numeric scale '" + scale.front().value + "'" );
        }
    }
}

void resolveSensor( RemoteSensingAssetState &state, const StateResolutionInput &input,
                    const DatasetFacts *dataset, std::vector<ResolutionNote> &notes )
{
    std::vector<std::pair<std::string, MetadataItems>> sources;
    if ( dataset )
        sources.emplace_back( "dataset", dataset->metadata );
    if ( input.catalog )
        sources.emplace_back( "catalog", input.catalog->metadata );

    const auto collect = []( const std::vector<std::pair<std::string, MetadataItems>> &sources,
                             const std::string &key )
    {
        std::vector<RawObservation> observations;
        for ( const auto &entry : sources )
        {
            for ( const RawObservation &observation : entry.second.find( key ) )
                observations.push_back( observation );
        }
        return observations;
    };

    const SensorProfileFacts *profile =
        input.sensorProfile ? &*input.sensorProfile : nullptr;

    const auto resolveTextField = [&]( const std::string &key, const std::string &path,
                                       const std::optional<std::string> &profileValue,
                                       std::string &out )
    {
        MergedValue merged = mergeObservations( declarativeFirst( collect( sources, key ) ),
                                                NormalizeAsIs{}, notes, path );
        if ( !merged.present && profileValue && !profileValue->empty() )
        {
            merged.present = true;
            merged.value = *profileValue;
            merged.kind = ClaimKind::Inferred;
            merged.sources.push_back( std::string( "sensor_profile:" ) +
                                      ( profile ? profile->sensorKey : std::string() ) );
        }
        if ( merged.present )
        {
            out = merged.value;
            appendClaim( state, path, merged );
        }
        else
        {
            addUnknown( state, path );
        }
    };

    resolveTextField( "SICNU_PLATFORM", "sensor.platform",
                      profile ? std::optional<std::string>( profile->platform ) : std::nullopt,
                      state.sensor.platform );
    resolveTextField( "SICNU_INSTRUMENT", "sensor.instrument",
                      profile ? std::optional<std::string>( profile->instrument ) : std::nullopt,
                      state.sensor.instrument );
    resolveTextField( "SICNU_SENSOR", "sensor.sensor_key",
                      profile ? std::optional<std::string>( profile->sensorKey ) : std::nullopt,
                      state.sensor.sensorKey );
    resolveTextField( "SICNU_PRODUCT_FAMILY", "sensor.product_family",
                      profile ? std::optional<std::string>( profile->productFamily )
                              : std::nullopt,
                      state.sensor.productFamily );
    resolveTextField( "SICNU_PRODUCT_ID", "sensor.product_id", std::nullopt,
                      state.sensor.productId );
    resolveTextField( "SICNU_PROCESSING_LEVEL", "sensor.processing_level", std::nullopt,
                      state.sensor.processingLevel );

    // Modality: declared → profile → inferred from SAR keys.
    MergedValue modality = mergeObservations(
        declarativeFirst( collect( sources, "SICNU_MODALITY" ) ), NormalizeModality{}, notes,
        "sensor.modality" );
    if ( !modality.present && profile && profile->modality != Modality::Unknown )
    {
        modality.present = true;
        modality.value = modalityToString( profile->modality );
        modality.kind = ClaimKind::Inferred;
        modality.sources.push_back( std::string( "sensor_profile:" ) + profile->sensorKey );
    }
    if ( !modality.present && dataset )
    {
        const bool hasSarKeys = dataset->metadata.contains( "SICNU_SAR_CALIBRATION" ) ||
                                dataset->metadata.contains( "SICNU_SAR_DOMAIN" ) ||
                                dataset->metadata.contains( "SICNU_SAR_STATE_ASSUMED" );
        if ( hasSarKeys )
        {
            modality.present = true;
            modality.value = "sar";
            modality.kind = ClaimKind::Inferred;
            modality.sources.push_back( "gdal:SICNU_SAR_CALIBRATION" );
            addNote( state, "sensor.modality_from_sar_keys", "sensor.modality",
                     "modality inferred from SAR metadata keys" );
        }
    }
    if ( modality.present )
    {
        Modality parsed = Modality::Unknown;
        modalityFromString( modality.value, parsed );
        state.sensor.modality = parsed;
        appendClaim( state, "sensor.modality", modality );
    }
    else
    {
        addUnknown( state, "sensor.modality" );
    }
}

void resolveAcquisition( RemoteSensingAssetState &state, const StateResolutionInput &input,
                         const DatasetFacts *dataset, std::vector<ResolutionNote> &notes )
{
    std::vector<RawObservation> observations;
    if ( dataset )
    {
        for ( const RawObservation &observation :
              dataset->metadata.find( "SICNU_ACQUISITION_DATE" ) )
            observations.push_back( observation );
    }
    if ( input.catalog && !input.catalog->acquisitionTimeIso.empty() )
    {
        observations.push_back(
            { input.catalog->acquisitionTimeIso, "catalog:AssetSnapshot" } );
    }

    const MergedValue merged =
        mergeObservations( observations, NormalizeAsIs{}, notes, "acquisition.time" );
    if ( !merged.present )
    {
        addUnknown( state, "acquisition.time" );
        return;
    }

    if ( merged.kind == ClaimKind::Conflicted )
    {
        ClaimRecord claim;
        claim.path = "acquisition.time";
        claim.kind = ClaimKind::Conflicted;
        claim.sources = merged.sources;
        claim.alternatives = merged.alternatives;
        state.claims.push_back( claim );
        addNote( state, "acquisition.conflict", "acquisition.time",
                 "acquisition timestamps disagree across sources" );
        return;
    }

    state.acquisition.valid = true;
    state.acquisition.timeIso = merged.value;
    // The time-source tag mirrors TemporalSceneRef.timeSource conventions.
    if ( !merged.sources.empty() && merged.sources.front() == "catalog:AssetSnapshot" )
        state.acquisition.timeSource = "catalog";
    else
        state.acquisition.timeSource = "metadata";
    if ( input.catalog && !input.catalog->acquisitionTimePrecision.empty() &&
         merged.sources.front() == "catalog:AssetSnapshot" )
        state.acquisition.precision = input.catalog->acquisitionTimePrecision;
    appendClaim( state, "acquisition.time", merged );
}

void resolveBands( RemoteSensingAssetState &state, const StateResolutionInput &input,
                   const DatasetFacts *dataset, std::vector<ResolutionNote> &notes )
{
    const SensorProfileFacts *profile =
        input.sensorProfile ? &*input.sensorProfile : nullptr;
    const CatalogFacts *catalog = input.catalog ? &*input.catalog : nullptr;

    // Iterate over the dataset band list when present; fall back to the
    // catalog structure mirror.
    std::vector<BandFacts> emptyBands;
    const std::vector<BandFacts> &datasetBands = dataset ? dataset->bands : emptyBands;
    const bool useDataset = dataset && !dataset->bands.empty();

    std::size_t totalBands = useDataset ? datasetBands.size() : 0;
    if ( !useDataset && catalog )
        totalBands = catalog->bands.size();

    const std::size_t projected = std::min( totalBands, kMaxPassportBands );
    if ( totalBands > kMaxPassportBands )
    {
        addNote( state, "bands.truncated", "bands",
                 "band facts truncated from " + std::to_string( totalBands ) + " to " +
                     std::to_string( kMaxPassportBands ) );
    }

    for ( std::size_t position = 0; position < projected; ++position )
    {
        BandState band;
        const BandFacts *datasetBand = useDataset ? &datasetBands[position] : nullptr;
        const CatalogBandFacts *catalogBand = catalog && position < catalog->bands.size()
                                                  ? &catalog->bands[position]
                                                  : nullptr;
        band.index = datasetBand ? datasetBand->index
                                 : ( catalogBand ? catalogBand->index : static_cast<int>( position + 1 ) );
        if ( datasetBand )
        {
            band.name = datasetBand->name;
            band.dataType = datasetBand->dataType;
        }
        const std::string pathPrefix =
            "bands[" + std::to_string( band.index ) + "]";

        // Role: declarative first (file metadata, catalog structure), then
        // family truth from the sensor profile.
        std::vector<RawObservation> roleObservations;
        if ( datasetBand )
        {
            for ( const RawObservation &observation :
                  datasetBand->metadata.find( "SICNU_BAND_ROLE" ) )
                roleObservations.push_back( observation );
        }
        if ( catalogBand && !catalogBand->role.empty() )
            roleObservations.push_back( { catalogBand->role, "catalog:structure" } );

        MergedValue role = resolveBandField( roleObservations, NormalizeLower{}, notes,
                                             pathPrefix + ".role" );
        if ( !role.present && profile )
        {
            for ( const ProfileBand &profileBand : profile->bands )
            {
                if ( profileBand.index != band.index || profileBand.role.empty() )
                    continue;
                role.present = true;
                role.value = toLowerAscii( profileBand.role );
                role.kind = ClaimKind::Inferred;
                role.sources.push_back( std::string( "sensor_profile:" ) + profile->sensorKey );
                addNote( state, "band_role.from_sensor_profile", pathPrefix + ".role",
                         "band role inferred from sensor profile band axis" );
                break;
            }
        }
        if ( role.present )
        {
            band.role = role.value;
            appendClaim( state, pathPrefix + ".role", role );
        }
        else
        {
            addUnknown( state, pathPrefix + ".role" );
        }

        // Wavelength: file metadata (with unit normalization), then profile.
        if ( datasetBand )
        {
            const std::vector<RawObservation> wavelength =
                datasetBand->metadata.find( "WAVELENGTH" );
            const std::vector<RawObservation> units =
                datasetBand->metadata.find( "WAVELENGTH_UNITS" );
            if ( !wavelength.empty() )
            {
                double nm = 0.0;
                bool unknownUnits = false;
                const std::string unitsText = units.empty() ? std::string() : units.front().value;
                if ( normalizeWavelengthNm( wavelength.front().value, unitsText, nm,
                                            unknownUnits ) )
                {
                    band.hasWavelengthNm = true;
                    band.wavelengthNm = nm;
                    MergedValue wavelengthMerged;
                    wavelengthMerged.present = true;
                    wavelengthMerged.value = wavelength.front().value;
                    wavelengthMerged.kind = ClaimKind::Known;
                    wavelengthMerged.sources = { wavelength.front().source };
                    appendClaim( state, pathPrefix + ".wavelength_nm", wavelengthMerged );
                }
                else if ( unknownUnits )
                {
                    addUnknown( state, pathPrefix + ".wavelength_nm" );
                    addNote( state, "wavelength.unknown_units", pathPrefix + ".wavelength_nm",
                             "unsupported WAVELENGTH_UNITS '" + unitsText + "'" );
                }
                else
                {
                    addUnknown( state, pathPrefix + ".wavelength_nm" );
                    addNote( state, "wavelength.unparsable", pathPrefix + ".wavelength_nm",
                             "unparsable WAVELENGTH '" + wavelength.front().value + "'" );
                }
            }
            else if ( profile )
            {
                for ( const ProfileBand &profileBand : profile->bands )
                {
                    if ( profileBand.index != band.index || !profileBand.hasWavelengthNm )
                        continue;
                    band.hasWavelengthNm = true;
                    band.wavelengthNm = profileBand.wavelengthNm;
                    ClaimRecord claim;
                    claim.path = pathPrefix + ".wavelength_nm";
                    claim.kind = ClaimKind::Inferred;
                    claim.sources = { std::string( "sensor_profile:" ) + profile->sensorKey };
                    state.claims.push_back( claim );
                    break;
                }
            }
        }

        if ( catalogBand && catalogBand->hasNoData )
        {
            band.hasNoData = true;
            band.noDataValue = catalogBand->noDataValue;
        }
        if ( catalogBand && band.dataType.empty() )
            band.dataType = catalogBand->dataType;

        state.bands.push_back( band );
    }
}

} // namespace

ResolveOutcome resolveAssetState( const StateResolutionInput &input )
{
    ResolveOutcome outcome;
    RemoteSensingAssetState &state = outcome.state;
    std::vector<ResolutionNote> notes;  // merged into state at the end

    const DatasetFacts *dataset = input.dataset ? &*input.dataset : nullptr;
    const CatalogFacts *catalog = input.catalog ? &*input.catalog : nullptr;

    // ---- Identity (catalog only) ----
    if ( catalog )
    {
        const auto projectText = [&]( const std::string &value, const std::string &path,
                                      std::string &field )
        {
            if ( value.empty() )
            {
                addUnknown( state, path );
                return;
            }
            field = value;
            MergedValue merged;
            merged.present = true;
            merged.value = value;
            merged.kind = ClaimKind::Known;
            merged.sources = { "catalog:AssetSnapshot" };
            appendClaim( state, path, merged );
        };

        projectText( catalog->assetId, "identity.asset_id", state.assetId );
        projectText( catalog->revision, "identity.revision", state.revision );
        projectText( catalog->displayName, "identity.display_name", state.displayName );
        projectText( catalog->persistence, "identity.persistence", state.persistence );

        if ( !catalog->kind.empty() )
        {
            if ( assetKindFromString( catalog->kind, state.kind ) )
            {
                MergedValue merged;
                merged.present = true;
                merged.value = catalog->kind;
                merged.kind = ClaimKind::Known;
                merged.sources = { "catalog:AssetSnapshot" };
                appendClaim( state, "identity.kind", merged );
            }
            else
            {
                addUnknown( state, "identity.kind" );
                addNote( state, "identity.unknown_kind", "identity.kind",
                         "catalog kind '" + catalog->kind + "' is not a known asset kind" );
            }
        }
        else
            addUnknown( state, "identity.kind" );

        if ( !catalog->lifecycle.empty() )
        {
            if ( assetLifecycleFromString( catalog->lifecycle, state.lifecycle ) )
            {
                MergedValue merged;
                merged.present = true;
                merged.value = catalog->lifecycle;
                merged.kind = ClaimKind::Known;
                merged.sources = { "catalog:AssetSnapshot" };
                appendClaim( state, "identity.lifecycle", merged );
            }
            else
            {
                addUnknown( state, "identity.lifecycle" );
                addNote( state, "identity.unknown_lifecycle", "identity.lifecycle",
                         "catalog lifecycle '" + catalog->lifecycle + "' is not known" );
            }
        }
        else
            addUnknown( state, "identity.lifecycle" );
    }

    // ---- Source path: catalog > dataset > input ----
    if ( catalog && !catalog->sourcePath.empty() )
        state.sourcePath = catalog->sourcePath;
    else if ( dataset && !dataset->sourcePath.empty() )
        state.sourcePath = dataset->sourcePath;
    else
        state.sourcePath = input.sourcePath;

    // ---- Sensor ----
    resolveSensor( state, input, dataset, notes );

    // ---- Acquisition ----
    resolveAcquisition( state, input, dataset, notes );

    // ---- Bands ----
    resolveBands( state, input, dataset, notes );

    // ---- Radiometric ----
    if ( dataset )
        resolveRadiometric( state, *dataset, notes );

    state.notes.insert( state.notes.end(), notes.begin(), notes.end() );
    normalizeState( state );
    return outcome;
}

} // namespace sicnu::state
