#include "preflight/rules.h"

#include "preflight/engine.h"  // full PreflightRequest definition

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::preflight {
namespace {

/// Function-pointer rule adapter: builtin families are pure functions, the
/// versioned identity rides the adapter.
class Rule : public IPreflightRule
{
  public:
    using EvaluateFn = RuleResult ( * )( const PreflightRequest &, const RuleFacts & );

    Rule( std::string id, int revision, EvaluateFn fn )
        : id_( std::move( id ) ), revision_( revision ), fn_( fn )
    {
    }

    std::string id() const override { return id_; }
    int revision() const override { return revision_; }
    RuleResult evaluate( const PreflightRequest &request, const RuleFacts &facts ) const override
    {
        return fn_( request, facts );
    }

  private:
    std::string id_;
    int revision_;
    EvaluateFn fn_;
};

// Deterministic thresholds (documented in the slice-B rule table; boundary
// semantics are pinned by tests: strictly-greater comparisons for the ack
// thresholds, at-or-above for the cloud block threshold).
constexpr double kResolutionAckRatio = 2.0;    ///< mean-pixel ratio above -> require_ack
constexpr double kResolutionBlockRatio = 10.0; ///< mean-pixel ratio above -> block
constexpr double kCloudAckPercent = 30.0;      ///< cloud cover strictly above -> require_ack
constexpr double kCloudBlockPercent = 70.0;    ///< cloud cover at or above -> block
constexpr std::size_t kMaxTemporalDates = 512; ///< dates evaluated per slot

Json::Value stringArray( const std::vector<std::string> &items )
{
    Json::Value arr( Json::arrayValue );
    for ( const auto &item : items )
        arr.append( item );
    return arr;
}

PreflightFinding makeFinding( const std::string &code, PreflightSeverity severity,
                              const std::string &domain, const std::string &subject,
                              const std::string &basis, const std::string &human,
                              const std::string &expectation, const std::string &actual )
{
    PreflightFinding f;
    f.code = code;
    f.severity = severity;
    f.domain = domain;
    f.subject = subject;
    f.basis = basis;
    f.humanExplanation = human;
    f.machineExplanation["expectation"] = expectation;
    f.machineExplanation["actual"] = actual;
    f.machineExplanation["remediation"] = Json::Value( Json::arrayValue );
    return f;
}

PreflightFinding unknownFinding( const std::string &code, const std::string &domain,
                                 const std::string &subject, const std::string &detail )
{
    return makeFinding( code, PreflightSeverity::RequireAck, domain, subject, "unknown",
                        "Cannot verify " + domain + " for " + subject + ": " + detail +
                            ". The check degrades to an explicit unknown, not a pass.",
                        "verifiable facts present", "facts missing: " + detail );
}

bool stringInArray( const Json::Value &array, const std::string &value )
{
    if ( !array.isArray() )
        return false;
    for ( const auto &item : array )
        if ( item.isString() && item.asString() == value )
            return true;
    return false;
}

/// Rule-side view of one raster slot with its resolved facts.
struct RasterSlot
{
    const RuleFacts::Slot *slot = nullptr;
    const SlotFacts *facts = nullptr;
};

std::vector<RasterSlot> rasterSlots( const RuleFacts &facts )
{
    std::vector<RasterSlot> out;
    for ( const auto *slot : facts.slotsOfKind( "raster" ) )
        out.push_back( RasterSlot{ slot, &slot->facts.facts } );
    return out;
}

int countRole( const SlotFacts &facts, const std::string &role )
{
    int count = 0;
    for ( const auto &band : facts.bands )
        if ( band.role == role )
            ++count;
    return count;
}

/// Outcome from the emitted findings: unknowns-only means the rule could not
/// judge (insufficient_facts); any concrete violation is a finding.
std::string outcomeFor( const std::vector<PreflightFinding> &findings, std::size_t unknownCount )
{
    if ( !findings.empty() && findings.size() > unknownCount )
        return "finding";
    if ( !findings.empty() ) // only unknowns
        return "insufficient_facts";
    return "pass";
}

/// Rules that read the merged entry skip silently (with a detail note) when
/// the capability authority could not answer: preflight.operator_known owns
/// that finding so a mirror outage is reported exactly once.
bool entryReadable( const CapabilityEntryResult &capability, std::string &detail )
{
    if ( capability.status != FactStatus::Available )
    {
        detail = "capability entry unavailable";
        return false;
    }
    return true;
}

// ---- ISO date handling (deterministic, locale-free) --------------------------

/// Days since 1970-01-01 for "YYYY-MM-DD..." text; false when unparseable.
bool isoDayNumber( const std::string &iso, long long &days )
{
    if ( iso.size() < 10 )
        return false;
    for ( std::size_t i = 0; i < 10; ++i )
    {
        const char c = iso[i];
        if ( i == 4 || i == 7 )
        {
            if ( c != '-' )
                return false;
        }
        else if ( c < '0' || c > '9' )
        {
            return false;
        }
    }
    const long long y = std::stoll( iso.substr( 0, 4 ) );
    const long long m = std::stoll( iso.substr( 5, 2 ) );
    const long long d = std::stoll( iso.substr( 8, 2 ) );
    if ( m < 1 || m > 12 || d < 1 || d > 31 )
        return false;
    // Days-from-civil (proleptic Gregorian).
    const long long yy = m <= 2 ? y - 1 : y;
    const long long era = ( yy >= 0 ? yy : yy - 399 ) / 400;
    const long long yoe = yy - era * 400;
    const long long mp = ( m + 9 ) % 12;
    const long long doy = ( 153 * mp + 2 ) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + doe - 719468;
    return true;
}

// ---- preflight.band_role -----------------------------------------------------

RuleResult bandRoleEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    std::string gateDetail;
    if ( !entryReadable( facts.capability, gateDetail ) )
    {
        result.detail = gateDetail;
        result.outcome = "pass";
        return result;
    }
    const Json::Value &roles = facts.capability.entry["band_roles"];
    if ( !roles.isObject() || roles.empty() )
    {
        result.detail = "no band_roles declared";
        result.outcome = "pass";
        return result;
    }

    std::size_t unknownCount = 0;
    for ( const auto &slot : facts.slots )
    {
        if ( slot.facts.status != FactStatus::Available )
        {
            result.findings.push_back( unknownFinding(
                "SPF_BAND_ROLE_UNKNOWN", "spectral", slot.slot,
                slot.facts.detail.empty() ? "facts unresolved" : slot.facts.detail ) );
            ++unknownCount;
            continue;
        }
        if ( slot.facts.facts.kind != "raster" )
        {
            result.detail = "non-raster slot skipped: " + slot.slot;
            continue;
        }
        for ( const auto &role : roles.getMemberNames() )
        {
            if ( !roles[role].isInt() )
                continue;
            const int required = roles[role].asInt();
            const int found = countRole( slot.facts.facts, role );
            if ( found >= required )
                continue;
            auto f = makeFinding(
                "SPF_BAND_ROLE_MISSING", PreflightSeverity::Block, "spectral", slot.slot,
                "observed",
                "Input " + slot.slot + " lacks the '" + role + "' band role this operator "
                    "requires (" + std::to_string( found ) + " of " + std::to_string( required ) +
                    " declared).",
                role + " >= " + std::to_string( required ), role + " = " + std::to_string( found ) );
            f.affectedInputs = { slot.assetRef };
            f.evidence["role"] = role;
            f.evidence["required"] = required;
            f.evidence["found"] = found;
            result.findings.push_back( std::move( f ) );
        }
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "all band-role minimums satisfied";
    return result;
}

// ---- preflight.pair_crs ------------------------------------------------------

RuleResult pairCrsEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    const auto rasters = rasterSlots( facts );
    if ( rasters.size() < 2 )
    {
        result.detail = "fewer than two raster inputs; no pair to compare";
        result.outcome = "pass";
        return result;
    }

    std::size_t unknownCount = 0;
    for ( std::size_t i = 0; i < rasters.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < rasters.size(); ++j )
        {
            const auto &a = rasters[i];
            const auto &b = rasters[j];
            const std::string subject = a.slot->slot + " + " + b.slot->slot;
            auto pairFinding = [&]( PreflightSeverity severity, const std::string &basis,
                                    const std::string &human, const std::string &expectation,
                                    const std::string &actual, const std::string &code ) {
                auto f = makeFinding( code, severity, "grid", subject, basis, human, expectation,
                                      actual );
                f.affectedInputs = { a.slot->assetRef, b.slot->assetRef };
                return f;
            };

            if ( !a.facts->hasCrs || !b.facts->hasCrs )
            {
                result.findings.push_back( pairFinding(
                    PreflightSeverity::RequireAck, "unknown",
                    "Cannot compare coordinate systems: " +
                        ( a.facts->hasCrs ? b.slot->slot : a.slot->slot ) +
                        " declares no CRS.",
                    "both inputs georeferenced", "missing CRS on one side", "SPF_CRS_UNKNOWN" ) );
                ++unknownCount;
                continue;
            }
            bool equal = false;
            bool comparable = false;
            if ( !a.facts->crsAuthid.empty() && !b.facts->crsAuthid.empty() )
            {
                equal = a.facts->crsAuthid == b.facts->crsAuthid;
                comparable = true;
            }
            else if ( !a.facts->crsWkt.empty() && !b.facts->crsWkt.empty() )
            {
                equal = a.facts->crsWkt == b.facts->crsWkt;
                comparable = true;
            }
            if ( !comparable )
            {
                result.findings.push_back( pairFinding(
                    PreflightSeverity::RequireAck, "unknown",
                    "Coordinate systems are declared but not comparable (no authid or WKT).",
                    "comparable CRS declarations", "unnamed CRS without WKT",
                    "SPF_CRS_UNKNOWN" ) );
                ++unknownCount;
                continue;
            }
            if ( equal )
                continue;
            auto f = pairFinding(
                PreflightSeverity::Block, "observed",
                "The pair mixes coordinate systems (" + a.facts->crsAuthid + " vs " +
                    b.facts->crsAuthid +
                    "); comparing or fusing them would produce a silently wrong product.",
                "identical CRS across the pair", a.facts->crsAuthid + " vs " + b.facts->crsAuthid,
                "SPF_CRS_MISMATCH" );
            f.evidence["crs_a"] = a.facts->crsAuthid;
            f.evidence["crs_b"] = b.facts->crsAuthid;
            result.findings.push_back( std::move( f ) );
        }
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "pair CRS consistent";
    return result;
}

// ---- preflight.pair_resolution_ratio -----------------------------------------

double meanPixelSize( const SlotFacts &f )
{
    return ( f.pixelSizeX + f.pixelSizeY ) / 2.0;
}

RuleResult resolutionRatioEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    const auto rasters = rasterSlots( facts );
    if ( rasters.size() < 2 )
    {
        result.detail = "fewer than two raster inputs; no pair to compare";
        result.outcome = "pass";
        return result;
    }

    std::size_t unknownCount = 0;
    for ( std::size_t i = 0; i < rasters.size(); ++i )
    {
        for ( std::size_t j = i + 1; j < rasters.size(); ++j )
        {
            const auto &a = rasters[i];
            const auto &b = rasters[j];
            const std::string subject = a.slot->slot + " + " + b.slot->slot;
            if ( !a.facts->hasPixelSize || !b.facts->hasPixelSize || a.facts->pixelSizeX <= 0 ||
                 a.facts->pixelSizeY <= 0 || b.facts->pixelSizeX <= 0 || b.facts->pixelSizeY <= 0 )
            {
                result.findings.push_back( unknownFinding(
                    "SPF_RESOLUTION_UNKNOWN", "grid", subject,
                    "pixel sizes missing or non-positive on one side" ) );
                ++unknownCount;
                continue;
            }
            const double pa = meanPixelSize( *a.facts );
            const double pb = meanPixelSize( *b.facts );
            const double ratio = std::max( pa, pb ) / std::min( pa, pb );
            if ( ratio <= kResolutionAckRatio )
                continue;
            const bool blocking = ratio > kResolutionBlockRatio;
            auto f = makeFinding(
                "SPF_GRID_RESOLUTION_MISMATCH",
                blocking ? PreflightSeverity::Block : PreflightSeverity::RequireAck, "grid",
                subject, "observed",
                "Resolution ratio " + std::to_string( ratio ) + " between " + a.slot->slot +
                    " and " + b.slot->slot +
                    ( blocking ? " is too large for a defensible product."
                               : " implies resampling; accept the risk or align the grids first." ),
                "mean pixel-size ratio <= " + std::to_string( kResolutionAckRatio ),
                "ratio = " + std::to_string( ratio ) );
            f.affectedInputs = { a.slot->assetRef, b.slot->assetRef };
            f.evidence["resolution_a"] = pa;
            f.evidence["resolution_b"] = pb;
            f.evidence["ratio"] = ratio;
            result.findings.push_back( std::move( f ) );
        }
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "pair resolutions compatible";
    return result;
}

// ---- preflight.radiometric_state_policy --------------------------------------

RuleResult radiometricStateEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    std::string gateDetail;
    if ( !entryReadable( facts.capability, gateDetail ) )
    {
        result.detail = gateDetail;
        result.outcome = "pass";
        return result;
    }
    const Json::Value &radiometric = facts.capability.entry["radiometric"];
    if ( !radiometric.isObject() ||
         ( !radiometric.isMember( "acceptable" ) && !radiometric.isMember( "warn" ) ) )
    {
        result.detail = "no radiometric policy declared";
        result.outcome = "pass";
        return result;
    }
    const Json::Value acceptable = radiometric.isMember( "acceptable" )
                                     ? radiometric["acceptable"]
                                     : Json::Value();
    const Json::Value warn =
        radiometric.isMember( "warn" ) ? radiometric["warn"] : Json::Value();

    std::size_t unknownCount = 0;
    for ( const auto &entry : facts.slots )
    {
        if ( entry.facts.status != FactStatus::Available )
        {
            result.findings.push_back( unknownFinding( "SPF_RADIOMETRIC_STATE_UNKNOWN",
                                                       "radiometric", entry.slot,
                                                       "facts unresolved" ) );
            ++unknownCount;
            continue;
        }
        const SlotFacts &f = entry.facts.facts;
        if ( f.kind != "raster" )
            continue;
        if ( f.radiometricUnit.empty() )
        {
            result.findings.push_back( unknownFinding( "SPF_RADIOMETRIC_STATE_UNKNOWN",
                                                       "radiometric", entry.slot,
                                                       "radiometric unit undeclared" ) );
            ++unknownCount;
            continue;
        }
        if ( stringInArray( acceptable, f.radiometricUnit ) )
            continue;
        const bool warnOnly = stringInArray( warn, f.radiometricUnit );
        auto finding = makeFinding(
            "SPF_RADIOMETRIC_STATE_MISMATCH",
            warnOnly ? PreflightSeverity::RequireAck : PreflightSeverity::Block, "radiometric",
            entry.slot, "declared",
            warnOnly ? "Input " + entry.slot + " declares '" + f.radiometricUnit +
                           "', a state this operator only accepts with explicit risk "
                           "acknowledgement."
                     : "Input " + entry.slot + " declares '" + f.radiometricUnit +
                           "', which is scientifically wrong for this operator.",
            "radiometric unit in the acceptable list", "unit = " + f.radiometricUnit );
        finding.affectedInputs = { entry.assetRef };
        finding.evidence["unit"] = f.radiometricUnit;
        finding.evidence["policy_class"] = warnOnly ? "warn" : "unacceptable";
        if ( acceptable.isArray() )
            finding.evidence["acceptable"] = acceptable;
        result.findings.push_back( std::move( finding ) );
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "radiometric states within policy";
    return result;
}

// ---- preflight.modality_policy ------------------------------------------------

RuleResult modalityEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    std::string gateDetail;
    if ( !entryReadable( facts.capability, gateDetail ) )
    {
        result.detail = gateDetail;
        result.outcome = "pass";
        return result;
    }
    const Json::Value &modalities = facts.capability.entry["modality"];
    if ( !modalities.isArray() || modalities.empty() )
    {
        result.detail = "no modality expectation declared";
        result.outcome = "pass";
        return result;
    }

    std::size_t unknownCount = 0;
    for ( const auto &entry : facts.slots )
    {
        if ( entry.facts.status != FactStatus::Available )
        {
            result.findings.push_back( unknownFinding( "SPF_MODALITY_UNKNOWN", "modality",
                                                       entry.slot, "facts unresolved" ) );
            ++unknownCount;
            continue;
        }
        const SlotFacts &f = entry.facts.facts;
        if ( f.kind != "raster" )
            continue;
        if ( f.modality.empty() || f.modality == "unknown" )
        {
            result.findings.push_back( unknownFinding( "SPF_MODALITY_UNKNOWN", "modality",
                                                       entry.slot, "modality undeclared" ) );
            ++unknownCount;
            continue;
        }
        if ( stringInArray( modalities, f.modality ) )
            continue;
        auto finding = makeFinding(
            "SPF_MODALITY_MISMATCH", PreflightSeverity::Block, "modality", entry.slot,
            "observed",
            "Input " + entry.slot + " is " + f.modality + " data, but the operator is defined "
                "for a different modality; running it would be scientifically meaningless.",
            "modality declared by the capability entry", "modality = " + f.modality );
        finding.affectedInputs = { entry.assetRef };
        finding.evidence["modality"] = f.modality;
        finding.evidence["expected"] = modalities;
        result.findings.push_back( std::move( finding ) );
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "modalities within expectation";
    return result;
}

// ---- preflight.quality_mask ---------------------------------------------------

RuleResult qualityMaskEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    for ( const auto *slot : facts.slotsOfKind( "raster" ) )
    {
        const SlotFacts &f = slot->facts.facts;
        if ( !f.hasCloudCover )
            continue;
        if ( f.cloudCoverPercent <= kCloudAckPercent )
            continue;
        const bool blocking = f.cloudCoverPercent >= kCloudBlockPercent;
        auto finding = makeFinding(
            "SPF_CLOUD_COVER_HIGH",
            blocking ? PreflightSeverity::Block : PreflightSeverity::RequireAck, "quality",
            slot->slot, "observed",
            blocking ? "Cloud cover " + std::to_string( f.cloudCoverPercent ) +
                           "% on " + slot->slot + " makes the scene unusable for this run."
                     : "Cloud cover " + std::to_string( f.cloudCoverPercent ) +
                           "% on " + slot->slot +
                           "; proceed only with an explicit quality-mask plan.",
            "cloud cover <= " + std::to_string( kCloudAckPercent ) + "%",
            "cloud cover = " + std::to_string( f.cloudCoverPercent ) + "%" );
        finding.affectedInputs = { slot->assetRef };
        finding.evidence["cloud_cover_percent"] = f.cloudCoverPercent;
        finding.evidence["quality_mask"] = f.qualityMaskInfo;
        result.findings.push_back( std::move( finding ) );
    }
    result.outcome = result.findings.empty() ? "pass" : "finding";
    if ( result.outcome == "pass" )
        result.detail = "no declared cloud cover over threshold";
    return result;
}

// ---- preflight.temporal_policy -------------------------------------------------

RuleResult temporalEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    std::string gateDetail;
    if ( !entryReadable( facts.capability, gateDetail ) )
    {
        result.detail = gateDetail;
        result.outcome = "pass";
        return result;
    }
    const Json::Value &temporal = facts.capability.entry["temporal"];
    if ( !temporal.isObject() )
    {
        result.detail = "no temporal policy declared";
        result.outcome = "pass";
        return result;
    }
    const int minScenes = temporal.isMember( "min_scenes" ) && temporal["min_scenes"].isInt()
                            ? temporal["min_scenes"].asInt()
                            : 0;
    const bool requiresTime = temporal.isMember( "requires_acquisition_time" ) &&
                              temporal["requires_acquisition_time"].asBool();
    const int maxGapDays = temporal.isMember( "max_gap_days" ) && temporal["max_gap_days"].isInt()
                             ? temporal["max_gap_days"].asInt()
                             : 0;

    std::size_t unknownCount = 0;
    for ( const auto *slot : facts.slotsOfKind( "raster" ) )
    {
        const SlotFacts &f = slot->facts.facts;
        const bool hasDates = !f.temporalDates.empty();
        const int sceneCount = f.temporalSceneCount > 0
                                 ? f.temporalSceneCount
                                 : static_cast<int>( f.temporalDates.size() );

        if ( sceneCount == 0 )
        {
            const bool timeUnknown = requiresTime && !f.hasAcquisitionTime;
            result.findings.push_back( unknownFinding(
                timeUnknown ? "SPF_TEMPORAL_TIME_UNKNOWN" : "SPF_TEMPORAL_UNKNOWN", "temporal",
                slot->slot, timeUnknown ? "acquisition time required but undeclared"
                                        : "no temporal facts declared" ) );
            ++unknownCount;
            continue;
        }
        if ( requiresTime && !f.hasAcquisitionTime && !hasDates )
        {
            result.findings.push_back( unknownFinding( "SPF_TEMPORAL_TIME_UNKNOWN", "temporal",
                                                       slot->slot,
                                                       "acquisition time required but undeclared" ) );
            ++unknownCount;
            continue;
        }
        if ( minScenes > 0 && sceneCount < minScenes )
        {
            auto finding = makeFinding(
                "SPF_TEMPORAL_SCENES_INSUFFICIENT", PreflightSeverity::Block, "temporal",
                slot->slot, "observed",
                "Temporal analysis needs " + std::to_string( minScenes ) + " scenes but only " +
                    std::to_string( sceneCount ) + " are declared for " + slot->slot + ".",
                "scene count >= " + std::to_string( minScenes ),
                "scene count = " + std::to_string( sceneCount ) );
            finding.affectedInputs = { slot->assetRef };
            finding.evidence["scene_count"] = sceneCount;
            finding.evidence["min_scenes"] = minScenes;
            result.findings.push_back( std::move( finding ) );
        }

        if ( f.temporalTruncated || f.temporalDates.size() > kMaxTemporalDates )
        {
            auto finding = makeFinding(
                "SPF_TEMPORAL_DATES_TRUNCATED", PreflightSeverity::RequireAck, "temporal",
                slot->slot, "observed",
                "Temporal facts for " + slot->slot +
                    " were truncated by a bounded provider; gap and order checks are partial.",
                "complete date list", "truncated date list" );
            finding.affectedInputs = { slot->assetRef };
            finding.evidence["declared_scenes"] = f.temporalSceneCount;
            result.findings.push_back( std::move( finding ) );
            continue; // order/gap judgment would be made on partial facts
        }

        if ( hasDates )
        {
            std::vector<long long> days;
            bool parseable = true;
            for ( const auto &date : f.temporalDates )
            {
                long long day = 0;
                if ( !isoDayNumber( date, day ) )
                {
                    parseable = false;
                    break;
                }
                days.push_back( day );
            }
            if ( !parseable )
            {
                result.findings.push_back( unknownFinding( "SPF_TEMPORAL_UNKNOWN", "temporal",
                                                           slot->slot,
                                                           "unparseable declared date" ) );
                ++unknownCount;
                continue;
            }
            bool ascending = true;
            for ( std::size_t i = 1; i < days.size(); ++i )
                if ( days[i] < days[i - 1] )
                    ascending = false;
            if ( !ascending )
            {
                auto finding = makeFinding(
                    "SPF_TEMPORAL_ORDER_INVALID", PreflightSeverity::Block, "temporal",
                    slot->slot, "observed",
                    "Declared acquisition dates for " + slot->slot +
                        " are not in ascending order; the series cannot be judged as declared.",
                    "ascending acquisition dates", "non-ascending dates" );
                finding.affectedInputs = { slot->assetRef };
                finding.evidence["dates"] = stringArray( f.temporalDates );
                result.findings.push_back( std::move( finding ) );
            }
            else if ( maxGapDays > 0 && days.size() >= 2 )
            {
                long long maxGap = 0;
                for ( std::size_t i = 1; i < days.size(); ++i )
                    maxGap = std::max( maxGap, days[i] - days[i - 1] );
                if ( maxGap > maxGapDays )
                {
                    auto finding = makeFinding(
                        "SPF_TEMPORAL_GAP_EXCEEDED", PreflightSeverity::RequireAck, "temporal",
                        slot->slot, "observed",
                        "Largest acquisition gap (" + std::to_string( maxGap ) +
                            " days) exceeds the declared maximum (" +
                            std::to_string( maxGapDays ) + " days) for " + slot->slot + ".",
                        "gap <= " + std::to_string( maxGapDays ) + " days",
                        "gap = " + std::to_string( maxGap ) + " days" );
                    finding.affectedInputs = { slot->assetRef };
                    finding.evidence["max_gap_days_found"] = static_cast<int>( maxGap );
                    finding.evidence["max_gap_days"] = maxGapDays;
                    result.findings.push_back( std::move( finding ) );
                }
            }
        }
    }
    result.outcome = outcomeFor( result.findings, unknownCount );
    if ( result.outcome == "pass" )
        result.detail = "temporal policy satisfied";
    return result;
}

// ---- preflight.train_eval_leakage ---------------------------------------------

RuleResult leakageEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    const RuleFacts::Slot *training = nullptr;
    const RuleFacts::Slot *evaluation = nullptr;
    for ( const auto &slot : facts.slots )
    {
        if ( slot.slot == "training" )
            training = &slot;
        else if ( slot.slot == "eval" || slot.slot == "validation" )
            evaluation = &slot;
    }
    if ( !training || !evaluation )
    {
        result.detail = "no train/eval pair in the request";
        result.outcome = "pass";
        return result;
    }
    if ( training->facts.status != FactStatus::Available ||
         evaluation->facts.status != FactStatus::Available )
    {
        result.findings.push_back( unknownFinding(
            "SPF_LEAKAGE_UNKNOWN", "ml", training->slot + " + " + evaluation->slot,
            "cannot verify split identity without resolved facts" ) );
        result.outcome = "insufficient_facts";
        return result;
    }

    auto identity = []( const RuleFacts::Slot &slot ) {
        return slot.facts.facts.assetId.empty() ? slot.assetRef : slot.facts.facts.assetId;
    };
    const std::string trainId = identity( *training );
    const std::string evalId = identity( *evaluation );
    const bool sameAsset = !trainId.empty() && trainId == evalId;
    bool derivedReuse = false;
    if ( !sameAsset && !trainId.empty() )
    {
        for ( const auto &origin : evaluation->facts.facts.derivedFromAssetIds )
            if ( origin == trainId )
                derivedReuse = true;
    }
    if ( !sameAsset && !derivedReuse )
    {
        result.detail = "training and eval are disjoint";
        result.outcome = "pass";
        return result;
    }

    auto finding = makeFinding(
        "SPF_TRAIN_EVAL_LEAKAGE", PreflightSeverity::Block, "ml",
        training->slot + " + " + evaluation->slot, derivedReuse ? "derived" : "observed",
        derivedReuse ? "The eval input was derived from the training asset; accuracy metrics "
                       "would be silently optimistic."
                     : "Training and eval resolve to the same asset; accuracy metrics would "
                       "be silently optimistic.",
        "disjoint training and eval sources",
        derivedReuse ? "eval derived from " + trainId : "identity " + trainId );
    finding.affectedInputs = { training->assetRef, evaluation->assetRef };
    finding.evidence["training"] = trainId;
    finding.evidence["eval"] = evalId;
    finding.evidence["relation"] = derivedReuse ? "derived" : "identical";
    result.findings.push_back( std::move( finding ) );
    result.outcome = "finding";
    return result;
}

// ---- preflight.model_compatibility ---------------------------------------------

RuleResult modelCompatEvaluate( const PreflightRequest &, const RuleFacts &facts )
{
    RuleResult result;
    std::string gateDetail;
    if ( !entryReadable( facts.capability, gateDetail ) )
    {
        result.detail = gateDetail;
        result.outcome = "pass";
        return result;
    }
    const Json::Value &model = facts.capability.entry["model_compatibility"];
    if ( !model.isObject() )
    {
        result.detail = "no model_compatibility declared";
        result.outcome = "pass";
        return result;
    }

    // The model manifest may ride any slot (conventionally "model").
    const RuleFacts::Slot *manifest = nullptr;
    for ( const auto &slot : facts.slots )
    {
        if ( slot.facts.status == FactStatus::Available && slot.facts.facts.hasModelManifest )
        {
            manifest = &slot;
            break;
        }
    }
    if ( !manifest || manifest->facts.facts.modelKind.empty() )
    {
        result.findings.push_back( unknownFinding( "SPF_MODEL_UNKNOWN", "model", "model",
                                                   "no model manifest resolved" ) );
        result.outcome = "insufficient_facts";
        return result;
    }

    const Json::Value &families = model["families"];
    if ( families.isArray() && !families.empty() &&
         !stringInArray( families, manifest->facts.facts.modelKind ) )
    {
        auto finding = makeFinding(
            "SPF_MODEL_INCOMPATIBLE", PreflightSeverity::Block, "model", manifest->slot,
            "declared",
            "Model family '" + manifest->facts.facts.modelKind +
                "' is not supported by this operator.",
            "model family in the supported list", "family = " + manifest->facts.facts.modelKind );
        finding.affectedInputs = { manifest->assetRef };
        finding.evidence["model_kind"] = manifest->facts.facts.modelKind;
        finding.evidence["supported"] = families;
        result.findings.push_back( std::move( finding ) );
    }

    const Json::Value &roles = model["input_band_roles"];
    if ( roles.isObject() && !roles.empty() )
    {
        for ( const auto *slot : facts.slotsOfKind( "raster" ) )
        {
            if ( slot == manifest )
                continue;
            const SlotFacts &f = slot->facts.facts;
            for ( const auto &role : roles.getMemberNames() )
            {
                if ( !roles[role].isInt() )
                    continue;
                const int required = roles[role].asInt();
                const int found = countRole( f, role );
                if ( found >= required )
                    continue;
                auto finding = makeFinding(
                    "SPF_MODEL_INCOMPATIBLE", PreflightSeverity::Block, "model", slot->slot,
                    "observed",
                    "The model manifest requires the '" + role + "' band role on the inference "
                        "input " + slot->slot + " (" + std::to_string( found ) + " of " +
                        std::to_string( required ) + " declared).",
                    role + " >= " + std::to_string( required ),
                    role + " = " + std::to_string( found ) );
                finding.affectedInputs = { slot->assetRef };
                finding.evidence["role"] = role;
                finding.evidence["required"] = required;
                finding.evidence["found"] = found;
                result.findings.push_back( std::move( finding ) );
            }
        }
    }
    result.outcome = result.findings.empty() ? "pass" : "finding";
    if ( result.outcome == "pass" )
        result.detail = "model compatible with the operator";
    return result;
}

// ---- preflight.operator_known ---------------------------------------------------

RuleResult operatorKnownEvaluate( const PreflightRequest &request, const RuleFacts &facts )
{
    RuleResult result;
    if ( facts.capability.status == FactStatus::Available )
    {
        result.detail = "operator declared by the capability authority";
        result.outcome = "pass";
        return result;
    }
    const bool unavailable = facts.capability.status == FactStatus::Unavailable;
    auto finding = makeFinding(
        unavailable ? "SPF_CAPABILITY_MIRROR_UNAVAILABLE" : "SPF_OPERATOR_UNKNOWN",
        PreflightSeverity::RequireAck, "operator", request.operatorId.empty() ? "(none)"
                                                                              : request.operatorId,
        "unknown",
        unavailable ? "The capability mirror could not be consulted (" +
                          facts.capability.detail + "); requirements cannot be verified."
                    : "Operator '" + request.operatorId +
                          "' is not declared by the capability mirror; its requirements are "
                          "unknown and the run proceeds only with explicit acceptance.",
        "operator declared by the capability authority",
        unavailable ? "authority unavailable" : "operator undeclared" );
    finding.evidence["operator"] = request.operatorId;
    if ( !facts.capability.detail.empty() )
        finding.evidence["detail"] = facts.capability.detail;
    result.findings.push_back( std::move( finding ) );
    result.outcome = "insufficient_facts";
    result.detail = facts.capability.detail;
    return result;
}

} // namespace

// ---- factories -----------------------------------------------------------------

PreflightRulePtr makeBandRoleRule()
{
    return std::make_unique<Rule>( rule_id::BandRole, 1, &bandRoleEvaluate );
}

PreflightRulePtr makePairCrsRule()
{
    return std::make_unique<Rule>( rule_id::PairCrs, 1, &pairCrsEvaluate );
}

PreflightRulePtr makeResolutionRatioRule()
{
    return std::make_unique<Rule>( rule_id::ResolutionRatio, 1, &resolutionRatioEvaluate );
}

PreflightRulePtr makeRadiometricStateRule()
{
    return std::make_unique<Rule>( rule_id::RadiometricState, 1, &radiometricStateEvaluate );
}

PreflightRulePtr makeModalityRule()
{
    return std::make_unique<Rule>( rule_id::Modality, 1, &modalityEvaluate );
}

PreflightRulePtr makeQualityMaskRule()
{
    return std::make_unique<Rule>( rule_id::QualityMask, 1, &qualityMaskEvaluate );
}

PreflightRulePtr makeTemporalRule()
{
    return std::make_unique<Rule>( rule_id::Temporal, 1, &temporalEvaluate );
}

PreflightRulePtr makeLeakageRule()
{
    return std::make_unique<Rule>( rule_id::Leakage, 1, &leakageEvaluate );
}

PreflightRulePtr makeModelCompatRule()
{
    return std::make_unique<Rule>( rule_id::ModelCompat, 1, &modelCompatEvaluate );
}

PreflightRulePtr makeOperatorKnownRule()
{
    return std::make_unique<Rule>( rule_id::OperatorKnown, 1, &operatorKnownEvaluate );
}

std::vector<PreflightRulePtr> builtinRules()
{
    std::vector<PreflightRulePtr> rules;
    rules.reserve( 10 );
    rules.push_back( makeBandRoleRule() );
    rules.push_back( makePairCrsRule() );
    rules.push_back( makeResolutionRatioRule() );
    rules.push_back( makeRadiometricStateRule() );
    rules.push_back( makeModalityRule() );
    rules.push_back( makeQualityMaskRule() );
    rules.push_back( makeTemporalRule() );
    rules.push_back( makeLeakageRule() );
    rules.push_back( makeModelCompatRule() );
    rules.push_back( makeOperatorKnownRule() );
    std::sort( rules.begin(), rules.end(),
               []( const PreflightRulePtr &a, const PreflightRulePtr &b ) {
                   return a->id() < b->id();
               } );
    return rules;
}

} // namespace sicnu::preflight
