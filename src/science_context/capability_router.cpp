// src/science_context/capability_router.cpp
#include "science_context/capability_router.h"

#include <algorithm>
#include <cctype>
#include <map>
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
};

const std::vector<IntentSpec> &specs()
{
    static const std::vector<IntentSpec> kSpecs = {
        { "ndvi",
          { "red", "nir" },
          { "optical", "hyperspectral" },
          { "surface_reflectance", "toa_reflectance" },
          { "radiance" },
          "rs:spectral_index/ndvi" },
        { "evi",
          { "red", "nir", "blue" },
          { "optical", "hyperspectral" },
          { "surface_reflectance", "toa_reflectance" },
          { "radiance" },
          "rs:spectral_index/evi" },
        { "change",
          { "red", "nir" },
          { "optical", "hyperspectral", "sar" },
          {},
          {},
          "rs:change_detect" },
        { "sar_change",
          {},
          { "sar" },
          { "sigma0", "gamma0", "beta0" },
          {},
          "rs:sar_change" },
        { "classify",
          {},
          { "optical", "hyperspectral", "sar" },
          {},
          {},
          "rs:classify" },
    };
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

CapabilityEntry evaluate( const IntentSpec &spec, const Json::Value &observed )
{
    CapabilityEntry e;
    e.capabilityId = spec.capabilityId;
    e.intent = spec.intent;
    e.structuralOnly = true;
    e.score = 1.0;

    const auto roles = bandRoleSet( observed );
    const std::string radio = radiometryOf( observed );
    const std::string modality = modalityOf( observed );

    // Modality gate
    if ( !spec.opticalOnly.empty() && !modality.empty() && modality != "unknown" )
    {
        bool okMod = false;
        for ( const auto &m : spec.opticalOnly )
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

} // namespace

bool isKnownBrokerIntent( const std::string &intent )
{
    return findSpec( intent ) != nullptr;
}

std::string resolveIntentFromGoal( const std::string &goalText, std::string *status )
{
    const std::string g = lower( goalText );
    struct Hit
    {
        std::string intent;
        int score = 0;
    };
    std::vector<Hit> hits;
    const std::map<std::string, std::vector<std::string>> kWords = {
        { "ndvi", { "ndvi", "vegetation index", "归一化植被" } },
        { "evi", { "evi", "enhanced vegetation" } },
        { "change", { "change detect", "change detection", "变化检测", "optical change" } },
        { "sar_change", { "sar change", "sar 变化", "backscatter change" } },
        { "classify", { "classify", "classification", "分类", "land cover" } },
    };
    for ( const auto &kv : kWords )
    {
        int score = 0;
        for ( const auto &w : kv.second )
        {
            if ( g.find( lower( w ) ) != std::string::npos )
                score += 1;
        }
        if ( score > 0 )
            hits.push_back( { kv.first, score } );
    }
    std::sort( hits.begin(), hits.end(), []( const Hit &a, const Hit &b ) {
        if ( a.score != b.score )
            return a.score > b.score;
        return a.intent < b.intent;
    } );
    if ( hits.empty() )
    {
        if ( status )
            *status = "unresolved";
        return {};
    }
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

CapabilityRouterResult routeCapabilities( const CapabilityQuery &query )
{
    CapabilityRouterResult result;
    std::string intent = query.intent;
    std::string intentStatus = "resolved";
    if ( intent.empty() )
        intent = resolveIntentFromGoal( query.goal, &intentStatus );
    else if ( !isKnownBrokerIntent( intent ) )
    {
        intentStatus = "unresolved";
        result.openQuestions.push_back( "unknown_operator_or_intent:" + intent );
        result.intent = intent;
        result.intentStatus = intentStatus;
        CapabilityEntry unk;
        unk.capabilityId = "unknown";
        unk.intent = intent;
        unk.status = "impossible";
        unk.reasons.push_back( "UNKNOWN_INTENT:" + intent );
        unk.score = 0.0;
        result.entries.push_back( unk );
        return result;
    }
    result.intent = intent;
    result.intentStatus = intentStatus;

    if ( intentStatus != "resolved" || intent.empty() )
    {
        result.openQuestions.push_back( "intent_" + intentStatus );
        return result;
    }

    const IntentSpec *spec = findSpec( intent );
    if ( !spec )
    {
        result.openQuestions.push_back( "unknown_operator_or_intent:" + intent );
        return result;
    }

    CapabilityEntry entry = evaluate( *spec, query.observedState );
    result.entries.push_back( entry );
    if ( entry.status != "direct" )
    {
        for ( const auto &r : entry.reasons )
            result.openQuestions.push_back( r );
    }

    // Deterministic order if we later add more candidates.
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

    if ( query.limit > 0 && static_cast<int>( result.entries.size() ) > query.limit )
        result.entries.resize( static_cast<std::size_t>( query.limit ) );

    return result;
}

} // namespace sicnu::science_context
