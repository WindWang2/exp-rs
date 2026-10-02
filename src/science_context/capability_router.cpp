// src/science_context/capability_router.cpp
#include "science_context/capability_router.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace sicnu::science_context {

namespace {

std::string lower( std::string s )
{
    for ( char &c : s )
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    return s;
}

std::set<std::string> bandRoleSet( const Json::Value &observed )
{
    std::set<std::string> roles;
    const Json::Value &arr = observed["band_roles"];
    if ( !arr.isArray() )
        return roles;
    for ( const auto &r : arr )
    {
        if ( r.isString() )
            roles.insert( lower( r.asString() ) );
    }
    return roles;
}

std::string radiometryOf( const Json::Value &observed )
{
    if ( observed.get( "radiometric_conflicted", false ).asBool() )
        return "conflicted";
    if ( !observed.isMember( "radiometric_state" ) )
        return "";
    return lower( observed["radiometric_state"].asString() );
}

std::string modalityOf( const Json::Value &observed )
{
    return lower( observed.get( "modality", "unknown" ).asString() );
}

bool hasRoles( const std::set<std::string> &have, const std::vector<std::string> &need )
{
    for ( const auto &n : need )
    {
        if ( !have.count( lower( n ) ) )
            return false;
    }
    return true;
}

struct IntentSpec
{
    std::string intent;
    std::vector<std::string> requiredRoles;
    std::vector<std::string> opticalOnly; ///< modalities accepted
    std::vector<std::string> preferredRadio;
    std::vector<std::string> warnRadio;
    std::string capabilityId;
    Json::Value options{Json::objectValue};
};

const std::vector<IntentSpec> &specs()
{
    static const std::vector<IntentSpec> kSpecs = []() {
        Json::Value ndviOpts( Json::objectValue );
        ndviOpts["index"] = "ndvi";
        Json::Value eviOpts( Json::objectValue );
        eviOpts["index"] = "evi";
        return std::vector<IntentSpec>{
            { "ndvi",
              { "red", "nir" },
              { "optical", "hyperspectral" },
              { "surface_reflectance", "toa_reflectance" },
              { "radiance" },
              "rs:spectral_index",
              ndviOpts },
            { "evi",
              { "red", "nir", "blue" },
              { "optical", "hyperspectral" },
              { "surface_reflectance", "toa_reflectance" },
              { "radiance" },
              "rs:spectral_index",
              eviOpts },
            { "change",
              { "red", "nir" },
              { "optical", "hyperspectral", "sar" },
              {},
              {},
              "rs:change_detection",
              Json::Value( Json::objectValue ) },
            { "sar_change",
              {},
              { "sar" },
              { "sigma0", "gamma0", "beta0" },
              {},
              "rs:sar_change",
              Json::Value( Json::objectValue ) },
            { "classify",
              {},
              { "optical", "hyperspectral", "sar" },
              {},
              {},
              "rs:classify",
              Json::Value( Json::objectValue ) },
        };
    }();
    return kSpecs;
}

const IntentSpec *findSpec( const std::string &intent )
{
    for ( const auto &s : specs() )
    {
        if ( s.intent == intent )
            return &s;
    }
    return nullptr;
}

/// Modality-agnostic spec shape. Built from the builtin table (no authority
/// wired) or from a live CapabilityKnowledge entry (merged, variant-resolved).
struct ResolvedSpec
{
    std::string capabilityId;
    std::string surface = "operator";
    std::vector<std::string> requiredRoles;
    std::vector<int> roleMinima; ///< parallel to requiredRoles (authority minima)
    std::vector<std::string> modalities; ///< empty = any
    std::vector<std::string> preferredRadio;
    std::vector<std::string> warnRadio;
    bool fromAuthority = false;
    Json::Value options{Json::objectValue};
};

ResolvedSpec builtinSpec( const IntentSpec &spec )
{
    ResolvedSpec s;
    if ( spec.intent == "ndvi" )
    {
        s.capabilityId = "rs:spectral_index";
        s.options = Json::Value( Json::objectValue );
        s.options["index"] = "ndvi";
    }
    else if ( spec.intent == "evi" )
    {
        s.capabilityId = "rs:spectral_index";
        s.options = Json::Value( Json::objectValue );
        s.options["index"] = "evi";
    }
    else
    {
        s.capabilityId = spec.capabilityId;
        s.options = spec.options;
    }
    s.requiredRoles = spec.requiredRoles;
    s.roleMinima.assign( spec.requiredRoles.size(), 1 );
    s.modalities = spec.opticalOnly;
    s.preferredRadio = spec.preferredRadio;
    s.warnRadio = spec.warnRadio;
    return s;
}

/// Knowledge shorthand tokens ("dn", "toa", "sr", "bt") mapped onto the
/// passport radiometric vocabulary; unknown tokens compare verbatim.
std::string canonicalRadioToken( const std::string &token )
{
    static const std::map<std::string, std::string> kAliases = {
        { "dn", "digital_number" },
        { "digital_number", "digital_number" },
        { "toa", "toa_reflectance" },
        { "toa_reflectance", "toa_reflectance" },
        { "sr", "surface_reflectance" },
        { "surface_reflectance", "surface_reflectance" },
        { "radiance", "radiance" },
        { "bt", "brightness_temperature" },
        { "brightness_temperature", "brightness_temperature" },
        { "sigma0", "sigma0" },
        { "gamma0", "gamma0" },
        { "beta0", "beta0" },
    };
    const auto it = kAliases.find( lower( token ) );
    return it == kAliases.end() ? lower( token ) : it->second;
}

std::vector<std::string> canonicalRadioList( const Json::Value &array )
{
    std::vector<std::string> out;
    for ( const auto &t : array )
    {
        if ( t.isString() )
            out.push_back( canonicalRadioToken( t.asString() ) );
    }
    return out;
}

CapabilityEntry evaluate( const ResolvedSpec &spec, const Json::Value &observed )
{
    CapabilityEntry e;
    e.capabilityId = spec.capabilityId;
    e.structuralOnly = true;
    e.score = 1.0;

    const auto roles = bandRoleSet( observed );
    const std::string radio = radiometryOf( observed );
    const std::string modality = modalityOf( observed );

    // Modality gate
    if ( !spec.modalities.empty() && !modality.empty() && modality != "unknown" )
    {
        bool okMod = false;
        for ( const auto &m : spec.modalities )
        {
            if ( m == modality )
            {
                okMod = true;
                break;
            }
        }
        if ( !okMod )
        {
            e.status = "impossible";
            e.reasons.push_back( "MODALITY_MISMATCH:" + modality );
            e.score = 0.0;
            return e;
        }
    }

    // Band roles
    if ( !spec.requiredRoles.empty() )
    {
        if ( !hasRoles( roles, spec.requiredRoles ) )
        {
            e.status = "unavailable";
            std::string missing;
            for ( const auto &need : spec.requiredRoles )
            {
                if ( !roles.count( lower( need ) ) )
                {
                    if ( !missing.empty() )
                        missing += ",";
                    missing += need;
                }
            }
            e.reasons.push_back( "MISSING_BAND_ROLE:" + missing );
            e.prepActions.push_back( "assign_band_roles:" + missing );
            e.score = 0.2;
            return e;
        }
        // Authority minima above 1 cannot be verified against the deduped
        // observed role set — fail closed to prep instead of a fake direct.
        if ( spec.fromAuthority )
        {
            for ( std::size_t i = 0; i < spec.requiredRoles.size(); ++i )
            {
                const int minimum = i < spec.roleMinima.size() ? spec.roleMinima[i] : 1;
                if ( minimum > 1 )
                {
                    e.status = "prep";
                    e.reasons.push_back( "BAND_ROLE_COUNT_UNVERIFIED:" +
                                         spec.requiredRoles[i] + ":" +
                                         std::to_string( minimum ) );
                    e.prepActions.push_back( "verify_band_role_counts:" +
                                             spec.requiredRoles[i] );
                    e.score = 0.35;
                    return e;
                }
            }
        }
    }

    // CRS / grid conflict flag from observed
    if ( observed.get( "crs_conflicted", false ).asBool() ||
         observed.get( "grid_conflicted", false ).asBool() )
    {
        e.status = "unavailable";
        e.reasons.push_back( "CRS_GRID_CONFLICT" );
        e.prepActions.push_back( "reconcile_crs_or_grid" );
        e.score = 0.15;
        return e;
    }

    // Radiometry
    if ( radio == "conflicted" )
    {
        e.status = "unavailable";
        e.reasons.push_back( "RADIOMETRIC_CONFLICTED" );
        e.prepActions.push_back( "resolve_radiometric_claim" );
        e.score = 0.1;
        return e;
    }

    if ( !spec.preferredRadio.empty() )
    {
        if ( radio.empty() )
        {
            e.status = "prep";
            e.reasons.push_back( "RADIOMETRIC_UNKNOWN" );
            e.prepActions.push_back( "declare_or_calibrate_radiometry" );
            e.score = 0.4;
            return e;
        }
        bool preferred = false;
        for ( const auto &p : spec.preferredRadio )
        {
            if ( p == radio )
            {
                preferred = true;
                break;
            }
        }
        if ( !preferred )
        {
            // DN → needs calibration before NDVI
            if ( radio == "digital_number" )
            {
                e.status = "prep";
                e.reasons.push_back( "RADIOMETRIC_DN_NEEDS_CALIBRATION" );
                e.prepActions.push_back( "calibrate_to_reflectance" );
                e.score = 0.45;
                return e;
            }
            bool warn = false;
            for ( const auto &w : spec.warnRadio )
            {
                if ( w == radio )
                {
                    warn = true;
                    break;
                }
            }
            if ( warn )
            {
                e.status = "prep";
                e.reasons.push_back( "RADIOMETRIC_SUBOPTIMAL:" + radio );
                e.prepActions.push_back( "prefer_surface_reflectance" );
                e.score = 0.55;
                return e;
            }
            e.status = "unavailable";
            e.reasons.push_back( "RADIOMETRIC_UNSUITABLE:" + radio );
            e.score = 0.2;
            return e;
        }
    }

    e.status = "direct";
    e.reasons.push_back( "FEASIBLE" );
    e.score = 1.0;
    return e;
}

bool matchesTerm( const std::string &text, const std::string &term )
{
    if ( term.empty() )
        return false;
    std::size_t pos = 0;
    while ( ( pos = text.find( term, pos ) ) != std::string::npos )
    {
        bool prefixOk = true;
        bool suffixOk = true;
        bool isAlpha = true;
        for ( char c : term )
        {
            if ( static_cast<unsigned char>( c ) >= 128 ||
                 ( !std::isalnum( static_cast<unsigned char>( c ) ) && c != '_' && c != ' ' ) )
            {
                isAlpha = false;
                break;
            }
        }
        if ( isAlpha )
        {
            if ( pos > 0 )
            {
                unsigned char prev = static_cast<unsigned char>( text[pos - 1] );
                if ( std::isalnum( prev ) || prev == '_' )
                    prefixOk = false;
            }
            std::size_t endPos = pos + term.size();
            if ( endPos < text.size() )
            {
                unsigned char next = static_cast<unsigned char>( text[endPos] );
                if ( std::isalnum( next ) || next == '_' )
                    suffixOk = false;
            }
        }
        if ( prefixOk && suffixOk )
            return true;
        pos += 1;
    }
    return false;
}

} // namespace

bool isKnownBrokerIntent( const std::string &intent )
{
    if ( findSpec( intent ) != nullptr )
        return true;
    static const std::set<std::string> kAll = {
        "ndvi", "change", "sar_change", "classify", "phenology",
        "evi", "savi", "ndre", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi",
        "water", "flood", "sar_water", "sar_flood", "sar", "ship",
        "temporal", "terrain", "accuracy", "qa", "preprocess", "inference", "zonal"
    };
    return kAll.count( intent ) > 0;
}

std::string resolveIntentFromGoal( const std::string &goalText, std::string *status )
{
    const std::string text = lower( goalText );
    if ( text.empty() )
    {
        if ( status )
            *status = "unresolved";
        return {};
    }

    struct TriggerEntry
    {
        std::string intent;
        std::vector<std::string> terms;
    };

    static const std::vector<TriggerEntry> kTriggers = {
        { "sar_flood", { "sar flood", "sar 洪水", "雷达洪水", "sar inundation" } },
        { "sar_change", { "sar change", "sar 变化", "雷达变化", "backscatter change" } },
        { "sar_water", { "sar water", "sar 水体", "雷达水体" } },
        { "ndvi", { "ndvi", "vegetation index", "归一化植被", "植被指数" } },
        { "evi", { "evi", "enhanced vegetation", "增强植被" } },
        { "savi", { "savi", "soil adjusted vegetation", "土壤调节植被" } },
        { "ndre", { "ndre", "red edge", "红边" } },
        { "ndwi", { "ndwi", "normalized difference water", "归一化水体" } },
        { "mndwi", { "mndwi", "modified normalized difference water", "改进水体" } },
        { "ndsi", { "ndsi", "snow index", "雪指数" } },
        { "nbr", { "nbr", "normalized burn", "燃烧指数" } },
        { "dnbr", { "dnbr", "delta nbr", "差分燃烧" } },
        { "ndbi", { "ndbi", "built-up index", "建筑指数" } },
        { "bsi", { "bsi", "bare soil", "裸土指数" } },
        { "water", { "water body", "water extraction", "水体提取", "水体", "水面", "water" } },
        { "flood", { "flood", "洪水", "淹没", "内涝", "inundation" } },
        { "sar", { "radar", "雷达", "合成孔径", "sar" } },
        { "ship", { "ship", "vessel", "船只", "船舶" } },
        { "change", { "change detect", "change detection", "变化检测", "bitemporal", "bi-temporal", "双时相", "变化", "optical change" } },
        { "classify", { "classification", "classify", "分类", "land cover", "land use", "土地覆盖", "土地利用", "kmeans", "监督分类", "supervised", "segmentation", "分割", "obia" } },
        { "phenology", { "phenology", "物候", "growing season", "生长季" } },
        { "temporal", { "time series", "temporal", "时间序列", "时序", "trend", "趋势" } },
        { "terrain", { "slope", "aspect", "hillshade", "dem", "terrain", "地形", "坡度", "坡向", "山体阴影", "高程" } },
        { "accuracy", { "confusion matrix", "accuracy", "精度", "混淆矩阵", "验证样本" } },
        { "qa", { "cloud mask", "qa", "云掩膜", "云 mask", "质量波段" } },
        { "preprocess", { "preprocessing", "preprocess", "mosaic", "reproject", "clip", "orthorectify", "atmospheric correction", "预处理", "镶嵌", "重投影", "裁剪", "正射", "大气校正" } },
        { "inference", { "deep learning", "inference", "unet", "segformer", "推理", "深度学习", "模型推理" } },
        { "zonal", { "zonal statistics", "zonal stats", "zonal", "per-zone", "区域统计", "分区统计" } },
    };

    struct CompoundRule
    {
        std::string gate;
        std::string baseIntent;
        std::string compoundIntent;
    };

    static const std::vector<CompoundRule> kCompoundRules = {
        { "sar", "flood", "sar_flood" },
        { "sar", "change", "sar_change" },
        { "sar", "water", "sar_water" },
        { "雷达", "flood", "sar_flood" },
        { "雷达", "change", "sar_change" },
        { "雷达", "water", "sar_water" },
    };

    std::map<std::string, int> scores;
    for ( const auto &entry : kTriggers )
    {
        for ( const auto &term : entry.terms )
        {
            if ( matchesTerm( text, lower( term ) ) )
            {
                scores[entry.intent] += 1;
            }
        }
    }

    for ( const auto &rule : kCompoundRules )
    {
        if ( matchesTerm( text, lower( rule.gate ) ) && scores.count( rule.baseIntent ) )
        {
            scores[rule.compoundIntent] += 2;
        }
    }

    // Specific spectral index acronyms explicitly named in the goal text
    // take precedence over generic domain words (e.g. "ndwi" over "water").
    static const std::set<std::string> kSpecificIndices = {
        "ndvi", "evi", "savi", "ndre", "ndwi", "mndwi", "ndsi", "nbr", "dnbr", "ndbi", "bsi"
    };
    for ( const auto &idx : kSpecificIndices )
    {
        if ( matchesTerm( text, idx ) )
        {
            scores[idx] += 2;
        }
    }

    if ( scores.empty() )
    {
        if ( status )
            *status = "unresolved";
        return {};
    }

    struct Hit
    {
        std::string intent;
        int score = 0;
    };
    std::vector<Hit> hits;
    hits.reserve( scores.size() );
    for ( const auto &kv : scores )
    {
        hits.push_back( { kv.first, kv.second } );
    }

    std::sort( hits.begin(), hits.end(), []( const Hit &a, const Hit &b ) {
        if ( a.score != b.score )
            return a.score > b.score;
        return a.intent < b.intent;
    } );

    if ( hits.size() >= 2 && hits[0].score == hits[1].score )
    {
        if ( status )
            *status = "ambiguous";
        return {};
    }

    if ( status )
        *status = "resolved";
    return hits[0].intent;
}

namespace {

CapabilityEntry unknownIntentEntry( const std::string &intent )
{
    CapabilityEntry unk;
    unk.capabilityId = "unknown";
    unk.intent = intent;
    unk.status = "impossible";
    unk.reasons.push_back( "UNKNOWN_INTENT:" + intent );
    unk.score = 0.0;
    return unk;
}

void finalizeEntries( CapabilityRouterResult &result, int limit )
{
    std::sort( result.entries.begin(), result.entries.end(),
               []( const CapabilityEntry &a, const CapabilityEntry &b ) {
                   const bool af = a.status == "direct";
                   const bool bf = b.status == "direct";
                   if ( af != bf )
                       return af > bf;
                   if ( a.score != b.score )
                       return a.score > b.score;
                   return a.capabilityId < b.capabilityId;
               } );
    if ( limit > 0 && static_cast<int>( result.entries.size() ) > limit )
        result.entries.resize( static_cast<std::size_t>( limit ) );
}

void recordUnknownIntent( CapabilityRouterResult &result, const std::string &intent )
{
    result.intentStatus = "unresolved";
    result.openQuestions.push_back( "unknown_operator_or_intent:" + intent );
    result.entries.push_back( unknownIntentEntry( intent ) );
}

/// Strict candidate shaping: a present-but-malformed constraint is treated as
/// malformed and the candidate is skipped (fail-closed), never as "no
/// requirement". Returns false when the candidate must be skipped.
bool authoritySpecFromCandidate( const Json::Value &candidate, ResolvedSpec &spec )
{
    if ( !candidate.isObject() )
        return false;
    const Json::Value &id = candidate["id"];
    if ( !id.isString() || id.asString().empty() )
        return false;
    spec = ResolvedSpec{};
    spec.fromAuthority = true;
    spec.capabilityId = id.asString();
    if ( candidate["surface"].isString() )
        spec.surface = candidate["surface"].asString();

    const Json::Value &roles = candidate["band_roles"];
    if ( !roles.isNull() )
    {
        if ( !roles.isObject() )
            return false;
        for ( const auto &name : roles.getMemberNames() )
        {
            const Json::Value &minimum = roles[name];
            if ( !minimum.isInt() && !minimum.isUInt() )
                return false;
            spec.requiredRoles.push_back( name );
            spec.roleMinima.push_back( minimum.asInt() );
        }
    }

    const Json::Value &modality = candidate["modality"];
    if ( !modality.isNull() )
    {
        if ( !modality.isArray() )
            return false;
        for ( const auto &m : modality )
        {
            if ( !m.isString() )
                return false;
            spec.modalities.push_back( lower( m.asString() ) );
        }
    }

    const Json::Value &radio = candidate["radiometric"];
    if ( !radio.isNull() )
    {
        if ( !radio.isObject() )
            return false;
        const Json::Value &acceptable = radio["acceptable"];
        if ( !acceptable.isNull() )
        {
            if ( !acceptable.isArray() )
                return false;
            spec.preferredRadio = canonicalRadioList( acceptable );
        }
        const Json::Value &warn = radio["warn"];
        if ( !warn.isNull() )
        {
            if ( !warn.isArray() )
                return false;
            spec.warnRadio = canonicalRadioList( warn );
        }
    }
    const Json::Value &options = candidate["options"];
    if ( options.isObject() )
        spec.options = options;
    return true;
}

/// Evaluates one resolved candidate and appends its entry; returns false when
/// the candidate's operator is absent from the registry (an impossible entry
/// is appended instead).
bool evaluateCandidate( const ResolvedSpec &spec, const CapabilityQuery &query,
                        const CapabilityFactsLookup &facts, CapabilityRouterResult &result )
{
    bool presenceUnknownHere = false;
    if ( facts.presence && !spec.capabilityId.empty() )
    {
        const OperatorPresence presence =
            facts.presence( spec.capabilityId, spec.surface );
        if ( presence == OperatorPresence::Absent )
        {
            CapabilityEntry e;
            e.capabilityId = spec.capabilityId;
            e.intent = query.intent;
            e.structuralOnly = true;
            e.status = "impossible";
            e.reasons.push_back( "OPERATOR_NOT_REGISTERED:" + spec.capabilityId );
            e.score = 0.0;
            result.entries.push_back( e );
            result.openQuestions.push_back( "operator_not_registered:" +
                                            spec.capabilityId );
            return false;
        }
        presenceUnknownHere = ( presence == OperatorPresence::Unknown );
        result.presenceUnknown = result.presenceUnknown || presenceUnknownHere;
    }

    CapabilityEntry entry = evaluate( spec, query.observedState );
    entry.intent = query.intent;
    if ( presenceUnknownHere )
    {
        entry.reasons.push_back( "OPERATOR_PRESENCE_UNKNOWN:" + spec.capabilityId );
        result.openQuestions.push_back( "operator_presence_unknown:" +
                                        spec.capabilityId );
        if ( entry.status == "direct" )
            entry.score = std::min( entry.score, 0.9 ); // unknown ≠ clean success
    }
    result.entries.push_back( entry );
    if ( entry.status != "direct" )
    {
        for ( const auto &r : entry.reasons )
            result.openQuestions.push_back( r );
    }
    return true;
}

} // namespace

CapabilityRouterResult routeCapabilities( const CapabilityQuery &query )
{
    CapabilityRouterResult result;
    std::string intent = query.intent;
    std::string intentStatus = "resolved";
    if ( intent.empty() )
        intent = resolveIntentFromGoal( query.goal, &intentStatus );
    result.intent = intent;
    result.intentStatus = intentStatus;

    if ( intentStatus != "resolved" || intent.empty() )
    {
        result.openQuestions.push_back( "intent_" + intentStatus );
        return result;
    }

    const CapabilityFactsLookup *facts = query.facts;
    const bool haveProvider = facts && static_cast<bool>( facts->entriesForIntent );

    if ( !haveProvider )
    {
        // No authority wired: the builtin table runs, and the bundle
        // provenance reports builtin_fallback/degraded.
        const IntentSpec *builtin = findSpec( intent );
        if ( !builtin )
        {
            recordUnknownIntent( result, intent );
            finalizeEntries( result, query.limit );
            return result;
        }
        evaluateCandidate( builtinSpec( *builtin ), query, CapabilityFactsLookup{},
                           result );
        finalizeEntries( result, query.limit );
        return result;
    }

    // Live authority: evaluate EVERY candidate fact-set it serves for the
    // intent. First-match selection would silently drop variant-scoped
    // requirements and fabricate feasibility; empty ⇒ intent not served and
    // must not fall back to the builtin table.
    const std::vector<Json::Value> candidates = facts->entriesForIntent( intent );
    result.factsFromAuthority = true;
    result.factsAuthority = facts->authority;
    result.factsRevision = facts->revision ? facts->revision() : 0;
    if ( candidates.empty() )
    {
        recordUnknownIntent( result, intent );
        finalizeEntries( result, query.limit );
        return result;
    }

    for ( const auto &candidate : candidates )
    {
        ResolvedSpec spec;
        if ( !authoritySpecFromCandidate( candidate, spec ) )
            continue; // malformed candidate — fail closed, never "no constraint"
        evaluateCandidate( spec, query, *facts, result );
    }
    if ( result.entries.empty() )
    {
        // Every candidate was malformed: the authority answered, but nothing
        // usable — report unknown rather than a fabricated capability.
        recordUnknownIntent( result, intent );
    }
    finalizeEntries( result, query.limit );
    return result;
}

} // namespace sicnu::science_context
