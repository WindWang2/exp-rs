/***************************************************************************
  scientific_state/asset_state_resolver.cpp
  RS14-01 Scientific Data Passport — the state resolver (pure projection).
 ***************************************************************************/

#include "scientific_state/asset_state_resolver.h"

#include "scientific_state/asset_state_json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

/// NoData declarations merge on a canonical numeric text form so that "0"
/// and "0.0" agree; unparsable declarations fail with a typed note code.
struct NormalizeNoData
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        double value = 0.0;
        if ( !parseDouble( observation.value, value ) )
        {
            failCode = "validity.nodata_invalid";
            return std::nullopt;
        }
        return std::to_string( value );
    }
};

// --- Acquisition timestamps -------------------------------------------------
//
// The rest of the fact chain parses acquisition times (workflow_facts
// parseInstant, suitability's QDateTime criteria). The resolver compared
// observations as raw text, so one physical acquisition declared as
// "2026-09-23 00:00:00" by the file and "2026-09-23T00:00:00Z" by the
// catalog was reported conflicted, while "September 2026" was accepted as
// a Known time no consumer could parse. mergeObservations compares
// NORMALIZED text, so the field normalizes onto one canonical spelling:
// midnight-UTC instants collapse to the date-only form (the coarsest
// faithful representation — a date is never widened into a fabricated
// time-of-day, matching workflow_facts' coarser-wins convention), every
// other instant renders as "YYYY-MM-DDTHH:MM:SS[.frac]Z" with the zone
// offset applied; naive datetimes are UTC by the same convention
// workflow_facts documents. Deliberately std-only: this core is Qt-free by
// its CMake contract, so the harness/suitability parsers cannot be reused
// here without inverting the layering.

bool isDigitRun( const char *text, std::size_t count )
{
    for ( std::size_t i = 0; i < count; ++i )
    {
        if ( text[i] < '0' || text[i] > '9' )
            return false;
    }
    return true;
}

int digitsAsInt( const char *text, std::size_t count )
{
    int value = 0;
    for ( std::size_t i = 0; i < count; ++i )
        value = value * 10 + ( text[i] - '0' );
    return value;
}

bool isLeapYear( int year )
{
    return ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
}

int daysInMonth( int year, int month )
{
    static const int lengths[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if ( month == 2 && isLeapYear( year ) )
        return 29;
    return lengths[month - 1];
}

/// Days from 1970-01-01 to the given civil date (Howard Hinnant's
/// days_from_civil; valid for the whole proleptic range we accept).
long long daysFromCivil( long long year, unsigned month, unsigned day )
{
    year -= month <= 2;
    const long long era = ( year >= 0 ? year : year - 399 ) / 400;
    const unsigned yearOfEra = static_cast<unsigned>( year - era * 400 );
    const unsigned dayOfYear =
        ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
    const unsigned dayOfEra =
        yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<long long>( dayOfEra ) - 719468;
}

/// Inverse of daysFromCivil (civil_from_days).
void civilFromDays( long long days, int &year, unsigned &month, unsigned &day )
{
    days += 719468;
    const long long era = ( days >= 0 ? days : days - 146096 ) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>( days - era * 146097 );
    const unsigned yearOfEra =
        ( dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096 ) / 365;
    const long long y = static_cast<long long>( yearOfEra ) + era * 400;
    const unsigned dayOfYear = dayOfEra - ( 365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100 );
    const unsigned mp = ( 5 * dayOfYear + 2 ) / 153;
    day = dayOfYear - ( 153 * mp + 2 ) / 5 + 1;
    month = mp + ( mp < 10 ? 3 : -9 );
    year = static_cast<int>( y + ( month <= 2 ) );
}

std::string formatDate( int year, unsigned month, unsigned day )
{
    char buffer[11];
    std::snprintf( buffer, sizeof( buffer ), "%04d-%02u-%02u", year, month, day );
    return buffer;
}

struct ParsedTimestamp
{
    bool ok = false;
    long long epochSeconds = 0;  // UTC
    bool midnightUtc = false;    // instant == 00:00:00Z — date-equivalent
    std::string canonical;       // normalized UTC spelling
};

bool parseTimestamp( const std::string &text, ParsedTimestamp &out )
{
    // Closed ISO-8601 subset: "YYYY-MM-DD" optionally followed by
    // ('T'|' ')HH:MM[:SS[.frac]] and an optional zone (Z | ±HH[:MM] | ±HHMM).
    const char * const p = text.c_str();
    const std::size_t length = text.size();
    if ( length < 10 || !isDigitRun( p, 4 ) || p[4] != '-' || !isDigitRun( p + 5, 2 ) ||
         p[7] != '-' || !isDigitRun( p + 8, 2 ) )
        return false;

    const int year = digitsAsInt( p, 4 );
    const int month = digitsAsInt( p + 5, 2 );
    const int day = digitsAsInt( p + 8, 2 );
    if ( month < 1 || month > 12 || day < 1 || day > daysInMonth( year, month ) )
        return false;

    const long long days = daysFromCivil( year, month, day );
    if ( length == 10 )
    {
        out.epochSeconds = days * 86400;
        out.midnightUtc = true;
        out.canonical = formatDate( year, month, day );
        out.ok = true;
        return true;
    }

    if ( p[10] != 'T' && p[10] != ' ' )
        return false;
    std::size_t cursor = 11;
    // HH:MM[:SS]
    if ( length < cursor + 5 || !isDigitRun( p + cursor, 2 ) || p[cursor + 2] != ':' ||
         !isDigitRun( p + cursor + 3, 2 ) )
        return false;
    const int hour = digitsAsInt( p + cursor, 2 );
    const int minute = digitsAsInt( p + cursor + 3, 2 );
    cursor += 5;
    int second = 0;
    if ( cursor < length && p[cursor] == ':' )
    {
        if ( length < cursor + 3 || !isDigitRun( p + cursor + 1, 2 ) )
            return false;
        second = digitsAsInt( p + cursor + 1, 2 );
        cursor += 3;
    }
    if ( hour > 23 || minute > 59 || second > 59 )
        return false;

    // Optional fractional seconds; canonical form trims trailing zeros.
    std::string fraction;
    if ( cursor < length && p[cursor] == '.' )
    {
        ++cursor;
        const std::size_t start = cursor;
        while ( cursor < length && p[cursor] >= '0' && p[cursor] <= '9' )
            ++cursor;
        if ( cursor == start )
            return false;
        fraction = text.substr( start, cursor - start );
        while ( !fraction.empty() && fraction.back() == '0' )
            fraction.pop_back();
    }

    // Optional zone; naive means UTC (workflow_facts convention).
    long long offsetSeconds = 0;
    if ( cursor < length )
    {
        if ( p[cursor] == 'Z' || p[cursor] == 'z' )
        {
            ++cursor;
        }
        else if ( p[cursor] == '+' || p[cursor] == '-' )
        {
            const long long sign = ( p[cursor] == '-' ) ? -1 : 1;
            ++cursor;
            std::size_t remaining = length - cursor;
            if ( remaining == 5 && p[cursor + 2] == ':' &&
                 isDigitRun( p + cursor, 2 ) && isDigitRun( p + cursor + 3, 2 ) )
            {
                offsetSeconds = sign * ( digitsAsInt( p + cursor, 2 ) * 3600LL +
                                         digitsAsInt( p + cursor + 3, 2 ) * 60LL );
                cursor = length;
            }
            else if ( remaining == 4 && isDigitRun( p + cursor, 4 ) )
            {
                offsetSeconds = sign * ( digitsAsInt( p + cursor, 2 ) * 3600LL +
                                         digitsAsInt( p + cursor + 2, 2 ) * 60LL );
                cursor = length;
            }
            else
            {
                return false;
            }
            if ( offsetSeconds / 3600 > 23 || ( offsetSeconds % 3600 ) / 60 > 59 )
                return false;
        }
        else
        {
            return false;
        }
    }
    if ( cursor != length )
        return false;

    out.epochSeconds = days * 86400 + hour * 3600LL + minute * 60LL + second - offsetSeconds;
    const long long dayOfInstant =
        out.epochSeconds >= 0 ? out.epochSeconds / 86400
                              : ( out.epochSeconds - 86399 ) / 86400;
    const long long secondOfDay = out.epochSeconds - dayOfInstant * 86400;
    out.midnightUtc = secondOfDay == 0;

    if ( out.midnightUtc )
    {
        int utcYear = 0;
        unsigned utcMonth = 0;
        unsigned utcDay = 0;
        civilFromDays( dayOfInstant, utcYear, utcMonth, utcDay );
        out.canonical = formatDate( utcYear, utcMonth, utcDay );
        out.ok = true;
        return true;
    }

    int utcYear = 0;
    unsigned utcMonth = 0;
    unsigned utcDay = 0;
    civilFromDays( dayOfInstant, utcYear, utcMonth, utcDay );
    char buffer[32];
    std::snprintf( buffer, sizeof( buffer ), "T%02u:%02u:%02u",
                   static_cast<unsigned>( secondOfDay / 3600 ),
                   static_cast<unsigned>( ( secondOfDay % 3600 ) / 60 ),
                   static_cast<unsigned>( secondOfDay % 60 ) );
    out.canonical = formatDate( utcYear, utcMonth, utcDay ) + buffer + "Z";
    if ( !fraction.empty() )
        out.canonical.insert( out.canonical.size() - 1, "." + fraction );
    out.ok = true;
    return true;
}

/// Acquisition timestamps merge on the canonical UTC spelling; unparsable
/// text fails with a typed note code instead of travelling the chain as an
/// unparseable Known fact.
struct NormalizeTimestamp
{
    std::optional<std::string> operator()( const RawObservation &observation,
                                           std::string &failCode ) const
    {
        const std::string trimmed = trimAscii( observation.value );
        if ( trimmed.empty() )
        {
            failCode = "observation.empty";
            return std::nullopt;
        }
        ParsedTimestamp parsed;
        if ( !parseTimestamp( trimmed, parsed ) )
        {
            failCode = "acquisition.time_unparseable";
            return std::nullopt;
        }
        return parsed.canonical;
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
        mergeObservations( observations, NormalizeTimestamp{}, notes, "acquisition.time" );
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

    // Pairing the file's band i with the structure mirror's band i is only
    // founded when both sides describe the SAME structure. A mirror written
    // before the file gained or lost a band (re-registered asset, replaced
    // file) would otherwise label the wrong band — roles and NoData would
    // travel as Known facts about bands that never declared them. With a
    // count disagreement the mirror contributes nothing and the gap is
    // recorded; without a dataset the mirror is the only authority and
    // pairing is unchanged.
    const bool catalogPairable =
        catalog && !catalog->bands.empty() &&
        ( !useDataset || catalog->bands.size() == datasetBands.size() );
    if ( catalog && useDataset && !catalog->bands.empty() && !catalogPairable )
    {
        addNote( state, "bands.catalog_structure_mismatch", "bands",
                 "catalog structure mirror has " +
                     std::to_string( catalog->bands.size() ) +
                     " bands while the file has " +
                     std::to_string( datasetBands.size() ) +
                     "; catalog band facts are not merged" );
    }

    for ( std::size_t position = 0; position < projected; ++position )
    {
        BandState band;
        const BandFacts *datasetBand = useDataset ? &datasetBands[position] : nullptr;
        const CatalogBandFacts *catalogBand =
            catalogPairable && position < catalog->bands.size() ? &catalog->bands[position]
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

        // NoData: dataset band declaration first, then the catalog structure
        // mirror. Disagreement is a conflict, never a silent pick; unparsable
        // declarations stay typed unknowns.
        std::vector<RawObservation> noDataObservations;
        if ( datasetBand )
        {
            for ( const RawObservation &observation :
                  datasetBand->metadata.find( "NO_DATA_VALUE" ) )
                noDataObservations.push_back( observation );
        }
        if ( catalogBand && catalogBand->hasNoData )
            noDataObservations.push_back(
                { std::to_string( catalogBand->noDataValue ), "catalog:structure" } );
        if ( !noDataObservations.empty() )
        {
            const MergedValue noData =
                resolveBandField( noDataObservations, NormalizeNoData{}, notes,
                                  pathPrefix + ".no_data" );
            if ( noData.present && noData.kind == ClaimKind::Conflicted )
            {
                appendClaim( state, pathPrefix + ".no_data", noData,
                             "noData declarations disagree" );
                addNote( state, "validity.nodata_conflict", pathPrefix + ".no_data",
                         "noData declarations disagree; kept both alternatives" );
            }
            else if ( noData.present )
            {
                band.hasNoData = true;
                parseDouble( noData.value, band.noDataValue );
                appendClaim( state, pathPrefix + ".no_data", noData );
            }
            else
            {
                addUnknown( state, pathPrefix + ".no_data" );
            }
        }
        if ( catalogBand && band.dataType.empty() )
            band.dataType = catalogBand->dataType;

        state.bands.push_back( band );
    }
}

/// Projects CRS facts (known or typed unknown), the geotransform verbatim,
/// and — only derived, as inferred claims — pixel size and the four-corner
/// bounding-box extent. A missing geotransform or size simply leaves the
/// corresponding fields unprojected.
void resolveGeometry( RemoteSensingAssetState &state, const DatasetFacts &dataset )
{
    const GeometryFacts &geometry = dataset.geometry;

    if ( geometry.hasCrs )
    {
        state.geometry.hasCrs = true;
        state.geometry.crsWkt = geometry.crsWkt;
        state.geometry.crsAuthid = geometry.crsAuthid;
        state.geometry.crsGeographic = geometry.crsGeographic;
        state.geometry.crsProjected = geometry.crsProjected;
        MergedValue crs;
        crs.present = true;
        crs.value = geometry.crsAuthid.empty() ? geometry.crsWkt : geometry.crsAuthid;
        crs.kind = ClaimKind::Known;
        crs.sources = { "gdal:CRS" };
        appendClaim( state, "geometry.crs", crs );
    }
    else
    {
        addUnknown( state, "geometry.crs" );
    }

    const bool hasSize = geometry.width > 0 && geometry.height > 0;

    if ( geometry.hasGeoTransform )
    {
        bool geotransformFinite = true;
        for ( double term : geometry.geoTransform )
            geotransformFinite = geotransformFinite && std::isfinite( term );

        if ( !geotransformFinite )
        {
            // A corrupt header can hand us NaN/inf geotransform terms. They
            // cannot ground pixel size or extent, and carrying them further
            // would poison the passport: NaN serializes to null and
            // infinities to non-strict JSON — documents this module itself
            // would refuse to re-read. Record the gap instead (typed
            // unknowns + note), never the garbage values.
            addUnknown( state, "geometry.pixel_size" );
            if ( hasSize )
                addUnknown( state, "geometry.extent" );
            addNote( state, "geometry.geotransform_not_finite", "geometry.geo_transform",
                     "geotransform carries non-finite terms; pixel size and extent are "
                     "not derivable" );
        }
        else
        {
            state.geometry.hasGeoTransform = true;
            state.geometry.geoTransform = geometry.geoTransform;

            state.geometry.hasPixelSize = true;
            state.geometry.pixelSizeX = std::fabs( geometry.geoTransform[1] );
            state.geometry.pixelSizeY = std::fabs( geometry.geoTransform[5] );
            MergedValue pixelSize;
            pixelSize.present = true;
            pixelSize.kind = ClaimKind::Inferred;
            pixelSize.sources = { "gdal:geo_transform" };
            appendClaim( state, "geometry.pixel_size", pixelSize );
            addNote( state, "geometry.pixel_size_from_geotransform", "geometry.pixel_size",
                     "pixel size derived from the geotransform axis scales" );

            if ( hasSize )
            {
                const double gt0 = geometry.geoTransform[0];
                const double gt1 = geometry.geoTransform[1];
                const double gt2 = geometry.geoTransform[2];
                const double gt3 = geometry.geoTransform[3];
                const double gt4 = geometry.geoTransform[4];
                const double gt5 = geometry.geoTransform[5];
                const double xs[4] = { gt0,
                                       gt0 + geometry.width * gt1,
                                       gt0 + geometry.height * gt2,
                                       gt0 + geometry.width * gt1 + geometry.height * gt2 };
                const double ys[4] = { gt3,
                                       gt3 + geometry.width * gt4,
                                       gt3 + geometry.height * gt5,
                                       gt3 + geometry.width * gt4 + geometry.height * gt5 };
                state.geometry.hasExtent = true;
                state.geometry.minX = *std::min_element( xs, xs + 4 );
                state.geometry.maxX = *std::max_element( xs, xs + 4 );
                state.geometry.minY = *std::min_element( ys, ys + 4 );
                state.geometry.maxY = *std::max_element( ys, ys + 4 );

                MergedValue extent;
                extent.present = true;
                extent.kind = ClaimKind::Inferred;
                extent.sources = { "gdal:geo_transform" };
                appendClaim( state, "geometry.extent", extent );
                addNote( state, "geometry.extent_from_geotransform", "geometry.extent",
                         "extent derived as the geotransform bounding box of the four image "
                         "corners (rotation terms included)" );
            }
        }
    }

    if ( hasSize )
    {
        state.geometry.hasSize = true;
        state.geometry.width = geometry.width;
        state.geometry.height = geometry.height;
    }
}

/// Projects the validity section: the noData policy is a census over the
/// band noData declarations (dataset metadata item "NO_DATA_VALUE" and the
/// catalog structure mirror both count), cloud cover comes from the
/// "CLOUDCOVER" dataset item, and the QA vocabulary prefers the declared
/// "SICNU_QA_VOCABULARY" over the sensor profile family truth.
void resolveValidity( RemoteSensingAssetState &state, const StateResolutionInput &input,
                      const DatasetFacts *dataset )
{
    const CatalogFacts *catalog = input.catalog ? &*input.catalog : nullptr;
    const SensorProfileFacts *profile =
        input.sensorProfile ? &*input.sensorProfile : nullptr;

    // ---- noData policy (declaration census over the band universe) ----
    const bool useDatasetBands = dataset && !dataset->bands.empty();
    const std::size_t totalBands = useDatasetBands ? dataset->bands.size()
                                                   : ( catalog ? catalog->bands.size() : 0 );
    if ( totalBands > 0 )
    {
        std::size_t declaredBands = 0;
        bool anyDatasetDeclaration = false;
        bool anyCatalogDeclaration = false;
        for ( std::size_t position = 0; position < totalBands; ++position )
        {
            const BandFacts *datasetBand = useDatasetBands ? &dataset->bands[position] : nullptr;
            const CatalogBandFacts *catalogBand =
                catalog && position < catalog->bands.size() ? &catalog->bands[position]
                                                            : nullptr;
            const bool datasetDeclaration =
                datasetBand && datasetBand->metadata.contains( "NO_DATA_VALUE" );
            const bool catalogDeclaration = catalogBand && catalogBand->hasNoData;
            anyDatasetDeclaration = anyDatasetDeclaration || datasetDeclaration;
            anyCatalogDeclaration = anyCatalogDeclaration || catalogDeclaration;
            if ( datasetDeclaration || catalogDeclaration )
                ++declaredBands;
        }

        if ( declaredBands == totalBands )
            state.validity.noDataPolicy = "declared";
        else if ( declaredBands == 0 )
            state.validity.noDataPolicy = "undeclared";
        else
            state.validity.noDataPolicy = "partial";

        MergedValue policy;
        policy.present = true;
        policy.value = state.validity.noDataPolicy;
        // A fully declared policy is directly attested by every band's own
        // authoritative declaration ⇒ known. Deriving it from partial
        // coverage or from silence stays inferred.
        policy.kind = ( declaredBands == totalBands ) ? ClaimKind::Known : ClaimKind::Inferred;
        if ( anyDatasetDeclaration )
            policy.sources.push_back( "gdal:NO_DATA_VALUE" );
        if ( anyCatalogDeclaration )
            policy.sources.push_back( "catalog:structure" );
        appendClaim( state, "validity.no_data_policy", policy,
                     std::to_string( declaredBands ) + " of " + std::to_string( totalBands ) +
                         " bands declare a NoData value" );
    }

    // ---- cloud cover (dataset metadata only; absent key ⇒ not projected) ----
    if ( dataset )
    {
        const std::vector<RawObservation> cloud = dataset->metadata.find( "CLOUDCOVER" );
        if ( !cloud.empty() )
        {
            double value = 0.0;
            if ( parseDouble( cloud.front().value, value ) )
            {
                state.validity.hasCloudCover = true;
                state.validity.cloudCoverPercent = value;
                MergedValue cloudMerged;
                cloudMerged.present = true;
                cloudMerged.value = cloud.front().value;
                cloudMerged.kind = ClaimKind::Known;
                cloudMerged.sources = { cloud.front().source };
                appendClaim( state, "validity.cloud_cover", cloudMerged );
            }
            else
            {
                addUnknown( state, "validity.cloud_cover" );
                addNote( state, "validity.cloud_cover_invalid", "validity.cloud_cover",
                         "unparsable CLOUDCOVER '" + cloud.front().value + "'" );
            }
        }
    }

    // ---- QA vocabulary: declared dataset key first, then sensor profile ----
    const std::vector<RawObservation> declaredQa =
        dataset ? dataset->metadata.find( "SICNU_QA_VOCABULARY" )
                : std::vector<RawObservation>{};
    if ( !declaredQa.empty() && !trimAscii( declaredQa.front().value ).empty() )
    {
        state.validity.qualityMaskInfo = trimAscii( declaredQa.front().value );
        MergedValue qaMerged;
        qaMerged.present = true;
        qaMerged.value = state.validity.qualityMaskInfo;
        qaMerged.kind = ClaimKind::Known;
        qaMerged.sources = { declaredQa.front().source };
        appendClaim( state, "validity.quality_mask_info", qaMerged );
    }
    else if ( profile && !profile->qaVocabulary.empty() )
    {
        state.validity.qualityMaskInfo = profile->qaVocabulary;
        MergedValue qaMerged;
        qaMerged.present = true;
        qaMerged.value = profile->qaVocabulary;
        qaMerged.kind = ClaimKind::Inferred;
        qaMerged.sources = { std::string( "sensor_profile:" ) + profile->sensorKey };
        appendClaim( state, "validity.quality_mask_info", qaMerged );
        addNote( state, "validity.qa_from_sensor_profile", "validity.quality_mask_info",
                 "QA vocabulary inferred from the sensor profile" );
    }
}

} // namespace

/// Score of one key claim path in the confidence lattice (see
/// asset_state_schema.h for the full contract). Takes a prebuilt
/// path → kind map: claims are looked up once, keeping confidence
/// O(bands + claims) rather than O(bands × claims).
double claimPathScore( const std::map<std::string, ClaimKind> &kinds,
                       const std::string &path )
{
    const auto it = kinds.find( path );
    if ( it == kinds.end() )
        return 0.0;
    switch ( it->second )
    {
        case ClaimKind::Known: return 1.0;
        case ClaimKind::Inferred: return 0.75;
        case ClaimKind::Assumed: return 0.25;
        case ClaimKind::Conflicted:
        case ClaimKind::Unknown: return 0.0;
    }
    return 0.0;
}

/// Deterministic overall confidence: the lattice over the 8 key paths,
/// normalized by the count of applicable paths (paths whose source exists —
/// absent sources never dilute), rounded to 3 decimals.
double computeConfidence( const RemoteSensingAssetState &state,
                          const StateResolutionInput &input )
{
    std::map<std::string, ClaimKind> kinds;
    for ( const ClaimRecord &claim : state.claims )
        kinds.emplace( claim.path, claim.kind );

    double total = 0.0;
    std::size_t denominator = 0;
    const auto consider = [&]( double score )
    {
        ++denominator;
        total += score;
    };

    if ( input.catalog )
        consider( claimPathScore( kinds, "identity.asset_id" ) );
    consider( claimPathScore( kinds, "sensor.modality" ) );

    if ( !state.bands.empty() )
    {
        // Full credit only when every band role resolves; the worst band wins.
        double worst = 1.0;
        for ( const BandState &band : state.bands )
        {
            worst = std::min(
                worst, claimPathScore( kinds,
                                       "bands[" + std::to_string( band.index ) + "].role" ) );
        }
        consider( worst );
    }

    consider( claimPathScore( kinds, "radiometric.unit" ) );
    consider( claimPathScore( kinds, "acquisition.time" ) );

    if ( input.dataset )
        consider( claimPathScore( kinds, "geometry.crs" ) );
    if ( input.derivation )
        consider( claimPathScore( kinds, "provenance.algorithm" ) );
    if ( !state.bands.empty() )
        consider( claimPathScore( kinds, "validity.no_data_policy" ) );

    if ( denominator == 0 )
        return 0.0;
    return std::round( ( total / static_cast<double>( denominator ) ) * 1000.0 ) / 1000.0;
}

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

    // ---- Facts-level truncation is surfaced, never silent ----
    if ( dataset && dataset->droppedMetadataItems > 0 )
    {
        addNote( state, "facts.metadata_truncated", "facts",
                 "metadata observations dropped by the collection cap: " +
                     std::to_string( dataset->droppedMetadataItems ) );
    }

    // ---- Radiometric ----
    if ( dataset )
        resolveRadiometric( state, *dataset, notes );
    else
    {
        // Keep the evidence contract symmetric: a path counted in the
        // confidence lattice must also carry a typed claim.
        addUnknown( state, "radiometric.unit" );
    }

    // ---- Geometry ----
    if ( dataset )
        resolveGeometry( state, *dataset );

    // ---- Validity ----
    resolveValidity( state, input, dataset );

    // ---- Temporal (catalog collection references) ----
    if ( catalog && !catalog->temporalRefs.empty() )
    {
        state.hasTemporalRefs = true;
        state.temporalRefs = catalog->temporalRefs;
        if ( state.temporalRefs.size() > kMaxPassportTemporalRefs )
        {
            const std::size_t original = state.temporalRefs.size();
            state.temporalRefs.resize( kMaxPassportTemporalRefs );
            state.temporalRefsTruncated = true;
            addNote( state, "temporal.truncated", "temporal.refs",
                     "temporal refs truncated from " + std::to_string( original ) + " to " +
                         std::to_string( kMaxPassportTemporalRefs ) );
        }
        MergedValue refs;
        refs.present = true;
        refs.kind = ClaimKind::Known;
        refs.sources = { "catalog:collection" };
        appendClaim( state, "temporal.refs", refs );
    }

    // ---- Provenance (catalog DerivationRecord projection) ----
    if ( input.derivation )
    {
        const DerivationFacts &derivation = *input.derivation;
        state.provenance.isDerived = true;
        state.provenance.algorithmId = derivation.algorithmId;
        state.provenance.algorithmVersion = derivation.algorithmVersion;
        for ( const DerivationFacts::Input &derivationInput : derivation.inputs )
        {
            ProvenanceInputRef ref;
            ref.assetId = derivationInput.assetId;
            ref.revision = derivationInput.revision;
            ref.bandReferences = derivationInput.bandReferences;
            ref.valueDomain = derivationInput.valueDomain;
            state.provenance.inputs.push_back( ref );
        }
        state.provenance.completedAtUtc = derivation.completedAtUtc;
        state.provenance.executionFingerprint = derivation.executionFingerprint;
        state.provenance.softwareVersion = derivation.softwareVersion;
        state.provenance.workflowRef = derivation.workflowRef;
        state.provenance.cacheHit = derivation.cacheHit;

        // Claims only for the parts the record actually carries.
        if ( !derivation.algorithmId.empty() || !derivation.algorithmVersion.empty() )
        {
            MergedValue algorithm;
            algorithm.present = true;
            algorithm.value = derivation.algorithmId;
            algorithm.kind = ClaimKind::Known;
            algorithm.sources = { "catalog:DerivationRecord" };
            appendClaim( state, "provenance.algorithm", algorithm );
        }
        if ( !derivation.inputs.empty() )
        {
            MergedValue inputs;
            inputs.present = true;
            inputs.kind = ClaimKind::Known;
            inputs.sources = { "catalog:DerivationRecord" };
            appendClaim( state, "provenance.inputs", inputs );
        }
    }

    // ---- Model-derived (classifier sidecar projection) ----
    if ( input.modelSidecar )
    {
        const ModelSidecarFacts &sidecar = *input.modelSidecar;
        state.modelDerived.present = true;
        state.modelDerived.modelKind = sidecar.modelKind;
        state.modelDerived.labels = sidecar.labels;
        state.modelDerived.hasAccuracy = sidecar.hasAccuracy;
        state.modelDerived.accuracy = sidecar.accuracy;
        state.modelDerived.sidecarPath = sidecar.sidecarPath;
        state.modelDerived.featureSchema = sidecar.featureSchema;

        if ( !state.modelDerived.labels.empty() )
        {
            MergedValue labels;
            labels.present = true;
            labels.kind = ClaimKind::Known;
            labels.sources = { "sidecar:classifier-meta" };
            appendClaim( state, "model_derived.labels", labels );
        }
        else
        {
            // The sidecar declares no class labels: record the absence, never
            // an empty claim.
            addUnknown( state, "model_derived.labels" );
        }
    }

    // ---- Confidence (deterministic key-path lattice) ----
    state.confidence = computeConfidence( state, input );

    state.notes.insert( state.notes.end(), notes.begin(), notes.end() );
    normalizeState( state );
    return outcome;
}

} // namespace sicnu::state
