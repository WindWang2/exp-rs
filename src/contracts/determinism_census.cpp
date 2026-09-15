/***************************************************************************
 * determinism_census.cpp — Determinism & Contract Census (Platform 11.0)
 *
 * Implementation notes:
 *   - The scan is a bounded text scan over src/** (same discipline as the
 *     contract-9 scanners): files are pre-filtered by a cheap substring
 *     before any regex runs, and regexes are anchored to the exact
 *     registration shapes used by this tree.
 *   - Class bodies are located with a `class <Name>` prefix search and cut
 *     at the next line-start `};` (the tree's uniform class-closing style).
 *     Inheritance chains are resolved by name through the same scan, depth
 *     bounded, with every unresolved chain recorded as a note — never
 *     silently guessed.
 ***************************************************************************/
#include "determinism_census.h"
#include "scientific_contract.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sicnu::contracts {

namespace {

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( std::istreambuf_iterator<char>( in ),
                        std::istreambuf_iterator<char>() );
}

bool readJsonFile( const std::string &path, Json::Value &out )
{
    const std::string text = readFile( path );
    if ( text.empty() )
        return false;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is( text );
    return Json::parseFromStream( b, is, &out, &errs );
}

/// Relative-to-src path of every *.h/*.cpp under sourceRoot/src whose text
/// contains ANY of @p needles. Sorted for deterministic iteration.
std::vector<std::string> filesContainingAny(
    const std::string &sourceRoot, const std::vector<std::string> &needles )
{
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path root
      = std::filesystem::path( sourceRoot ) / "src";
    if ( !std::filesystem::exists( root, ec ) )
        return out;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec );
    std::filesystem::recursive_directory_iterator end;
    while ( !ec && it != end )
    {
        std::error_code fileEc;
        const std::filesystem::path &p = it->path();
        const std::string ext = p.extension().string();
        if ( it->is_regular_file( fileEc )
             && ( ext == ".h" || ext == ".hpp" || ext == ".cpp" ) )
        {
            const std::string text = readFile( p.string() );
            for ( const std::string &needle : needles )
                if ( text.find( needle ) != std::string::npos )
                {
                    out.push_back( p.string() );
                    break;
                }
        }
        it.increment( ec );
    }
    std::sort( out.begin(), out.end() );
    return out;
}

struct RegistrationSite
{
    std::string operatorId;
    std::string className;
    std::string file; // relative to src/
};

/// Registration sites across the two shapes this tree uses:
///   REGISTER_RS_OPERATOR( ClassName, "prefix:id" )
///   add( "prefix:id", [] { return std::make_unique<ClassName>(); } );
/// Registration sites across the two shapes this tree uses:
///   REGISTER_RS_OPERATOR( ClassName, "prefix:id" )
///   add( "prefix:id", [] { return std::make_unique<ClassName>(); } );
/// Only first-party family prefixes count — documentation comments quote the
/// macro with placeholder ids ("my:operator"), which must never enter the
/// census.
std::vector<RegistrationSite> scanRegistrationSites( const std::string &sourceRoot )
{
    std::vector<RegistrationSite> sites;
    static const std::regex macroRe(
        R"re(REGISTER_RS_OPERATOR\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*\))re" );
    static const std::regex addRe(
        R"re(add\(\s*"([^"]+)"\s*,\s*\[\s*\]\s*\{\s*return\s+std::make_unique<\s*(\w+)\s*>\s*\(\s*\))re" );
    static const std::set<std::string> kFirstPartyFamilies = { "rs", "gdal", "io",
                                                               "otb", "opencv",
                                                               "cartography" };
    const auto firstPartyId = [ & ]( const std::string &id ) {
        const auto colon = id.find( ':' );
        return colon != std::string::npos
               && kFirstPartyFamilies.count( id.substr( 0, colon ) ) > 0;
    };

    // The macro-use text ("REGISTER_RS_OPERATOR") appears only at use sites;
    // the lambda `add(` sites carry "registerOperator" inside the wrapper.
    const std::vector<std::string> needles = { "REGISTER_RS_OPERATOR", "registerOperator" };
    for ( const std::string &path : filesContainingAny( sourceRoot, needles ) )
    {
        const std::string text = readFile( path );
        if ( text.empty() )
            continue;
        std::smatch m;
        for ( auto it = std::sregex_iterator( text.begin(), text.end(), macroRe );
              it != std::sregex_iterator(); ++it )
        {
            if ( !firstPartyId( ( *it )[2].str() ) )
                continue;
            RegistrationSite s;
            s.className = ( *it )[1].str();
            s.operatorId = ( *it )[2].str();
            s.file = std::filesystem::proximate( path, std::filesystem::path( sourceRoot ) / "src" ).string();
            sites.push_back( std::move( s ) );
        }
        for ( auto it = std::sregex_iterator( text.begin(), text.end(), addRe );
              it != std::sregex_iterator(); ++it )
        {
            if ( !firstPartyId( ( *it )[1].str() ) )
                continue;
            RegistrationSite s;
            s.operatorId = ( *it )[1].str();
            s.className = ( *it )[2].str();
            s.file = std::filesystem::proximate( path, std::filesystem::path( sourceRoot ) / "src" ).string();
            sites.push_back( std::move( s ) );
        }
    }
    std::sort( sites.begin(), sites.end(),
               []( const RegistrationSite &a, const RegistrationSite &b ) {
                   if ( a.operatorId != b.operatorId )
                       return a.operatorId < b.operatorId;
                   return a.file < b.file;
               } );
    // De-duplicate exact (id, class) pairs from the macro+lambda double
    // registration convention (#707): the pair is one operator, not two.
    sites.erase( std::unique( sites.begin(), sites.end(),
                              []( const RegistrationSite &a, const RegistrationSite &b ) {
                                  return a.operatorId == b.operatorId
                                         && a.className == b.className;
                              } ),
                 sites.end() );
    return sites;
}

/// A class declaration slice: the text from `class <Name>` up to the next
/// line-start `};`, plus the declared base-class name list.
struct ClassSlice
{
    std::string file;
    std::string body;
    std::vector<std::string> bases;
};

/// Locate class declaration slices for @p classNames across src/**. Only
/// files whose text mentions at least one wanted name are read.
std::map<std::string, ClassSlice> scanClassSlices(
    const std::string &sourceRoot, const std::set<std::string> &classNames )
{
    std::map<std::string, ClassSlice> slices;
    if ( classNames.empty() )
        return slices;

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path( sourceRoot ) / "src";
    if ( !std::filesystem::exists( root, ec ) )
        return slices;

    // Match `class [Export] Name [final] [: bases] {` then cut the body at
    // the next line-start `};`. The tree styles include export macros and
    // trailing `final` (`class IoTranslateOperator final : public ...`), so
    // the NAME is taken from the trailing token group and `final` itself is
    // never accepted as a class name.
    static const std::regex declRe(
        R"re(\bclass\s+((?:\w+\s+)*)(\w+)\s*(?::\s*([^{]+))?\{)re" );

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec );
    std::filesystem::recursive_directory_iterator end;
    while ( !ec && it != end )
    {
        std::error_code fileEc;
        const std::filesystem::path &p = it->path();
        const std::string ext = p.extension().string();
        if ( !it->is_regular_file( fileEc )
             || ( ext != ".h" && ext != ".hpp" && ext != ".cpp" ) )
        {
            it.increment( ec );
            continue;
        }
        const std::string text = readFile( p.string() );
        bool mentions = false;
        for ( const std::string &name : classNames )
            if ( text.find( name ) != std::string::npos )
            {
                mentions = true;
                break;
            }
        if ( !mentions )
        {
            it.increment( ec );
            continue;
        }
        const std::string rel = std::filesystem::proximate(
            p, std::filesystem::path( sourceRoot ) / "src" ).string();
        for ( auto mit = std::sregex_iterator( text.begin(), text.end(), declRe );
              mit != std::sregex_iterator(); ++mit )
        {
            std::string name = ( *mit )[2].str();
            if ( name == "final" )
            {
                // Trailing `final`: the real name is the last prefix token
                // ("class Foo final : ...").
                const std::string prefix = ( *mit )[1].str();
                const auto last = prefix.rfind_last_of( " \t" );
                name = prefix.substr(
                    last == std::string::npos ? 0 : last + 1 );
                while ( !name.empty() && ( name.back() == ' ' || name.back() == '\t' ) )
                    name.pop_back();
            }
            if ( !classNames.count( name ) || slices.count( name ) )
                continue;
            ClassSlice slice;
            slice.file = rel;
            const size_t bodyStart = static_cast<size_t>( ( *mit ).position() )
                                     + static_cast<size_t>( ( *mit ).length() ) - 1;
            const size_t close = text.find( "\n};", bodyStart );
            if ( close == std::string::npos )
                slice.body = text.substr( bodyStart );
            else
                slice.body = text.substr( bodyStart, close - bodyStart );
            const std::string basesText = ( *mit )[3].str();
            // Each comma-separated base clause is a possibly-qualified name
            // ("public sicnu::operators::RSOperator") — the BASE is its last
            // identifier token; access specifiers are dropped.
            static const std::regex baseTokenRe( R"re(\b(\w+)\b)re" );
            std::stringstream basesStream( basesText );
            std::string clause;
            while ( std::getline( basesStream, clause, ',' ) )
            {
                std::string base;
                for ( auto bit = std::sregex_iterator( clause.begin(), clause.end(),
                                                       baseTokenRe );
                      bit != std::sregex_iterator(); ++bit )
                {
                    const std::string tok = ( *bit )[1].str();
                    if ( tok == "public" || tok == "private" || tok == "protected"
                         || tok == "virtual" )
                        continue;
                    base = tok; // keep the LAST identifier (namespace-qualified)
                }
                if ( !base.empty() )
                    slice.bases.push_back( std::move( base ) );
            }
            slices.emplace( name, std::move( slice ) );
        }
        it.increment( ec );
    }
    return slices;
}

struct OverrideFacts
{
    bool gradeOverride = false;
    std::string gradeLiteral;
    bool runtimeOverride = false;
    std::string runtimeLiteral;
    bool found = false;
};

OverrideFacts factsInBody( const std::string &body )
{
    OverrideFacts f;
    static const std::regex gradeRe(
        R"re(determinismGrade\(\)\s*const\s*override\s*\{[^}]*return\s+"([^"]+)")re" );
    static const std::regex runtimeRe(
        R"re(RSOperatorDeterminism\s+determinism\(\)\s*const\s*override\s*\{[^}]*RSOperatorDeterminism::(\w+))re" );
    std::smatch m;
    if ( std::regex_search( body, m, gradeRe ) )
    {
        f.gradeOverride = true;
        f.gradeLiteral = m[1].str();
    }
    if ( std::regex_search( body, m, runtimeRe ) )
    {
        f.runtimeOverride = true;
        f.runtimeLiteral = m[1].str();
    }
    f.found = f.gradeOverride || f.runtimeOverride;
    return f;
}

/// Resolve the effective override for @p className through its (bounded)
/// inheritance chain.
DeterminismOverrideInfo resolveOverrides( const std::string &className,
                                          const ClassSlice &slice,
                                          const std::map<std::string, ClassSlice> &all,
                                          std::string &note )
{
    DeterminismOverrideInfo info;
    info.operatorClass = className;
    info.file = slice.file;
    info.classFound = true;

    std::string current = className;
    for ( int depth = 0; depth <= 4; ++depth )
    {
        const auto it = all.find( current );
        if ( it == all.end() )
        {
            if ( depth > 0 )
                note += "ancestor '" + current + "' not in scan; ";
            break;
        }
        const OverrideFacts f = factsInBody( it->second.body );
        if ( f.gradeOverride && !info.gradeOverride )
        {
            info.gradeOverride = true;
            info.gradeLiteral = f.gradeLiteral;
            info.baseDepth = depth;
        }
        if ( f.runtimeOverride && !info.runtimeOverride )
        {
            info.runtimeOverride = true;
            info.runtimeLiteral = f.runtimeLiteral;
        }
        if ( info.gradeOverride && info.runtimeOverride )
            break;
        if ( it->second.bases.empty() )
            break;
        current = it->second.bases.front();
    }
    if ( !info.gradeOverride && !info.runtimeOverride )
        note += "no explicit override up the scanned chain (framework default applies)";
    return info;
}

std::string slugForSidecar( const std::string &operatorId )
{
    std::string slug = operatorId;
    std::replace( slug.begin(), slug.end(), ':', '-' );
    std::replace( slug.begin(), slug.end(), '_', '-' );
    return slug;
}

std::string prefixOf( const std::string &id )
{
    const auto colon = id.find( ':' );
    return colon == std::string::npos ? "" : id.substr( 0, colon + 1 );
}

} // namespace

std::string normalizeDeterminismGrade( std::string grade )
{
    std::replace( grade.begin(), grade.end(), '-', '_' );
    return grade;
}

std::map<std::string, DeterminismOverrideInfo> scanDeterminismOverrides(
    const std::string &sourceRoot )
{
    const std::vector<RegistrationSite> sites = scanRegistrationSites( sourceRoot );

    std::set<std::string> classNames;
    for ( const RegistrationSite &s : sites )
        classNames.insert( s.className );
    // The framework bases participate in every chain resolution.
    classNames.insert( "RSOperator" );
    const std::map<std::string, ClassSlice> slices = scanClassSlices( sourceRoot, classNames );

    std::map<std::string, DeterminismOverrideInfo> result;
    std::set<std::string> idsSeenMoreThanOnce;
    for ( const RegistrationSite &s : sites )
    {
        if ( result.count( s.operatorId ) )
        {
            idsSeenMoreThanOnce.insert( s.operatorId );
            continue; // first (sorted) registration wins; extra ones noted
        }
        DeterminismOverrideInfo info;
        info.operatorId = s.operatorId;
        info.file = s.file;
        const auto cls = slices.find( s.className );
        if ( cls == slices.end() )
        {
            info.operatorClass = s.className;
            info.note = "class declaration not located in src scan";
        }
        else
        {
            std::string note;
            info = resolveOverrides( s.className, cls->second, slices, note );
            info.operatorId = s.operatorId;
            if ( !note.empty() )
                info.note = note;
        }
        result.emplace( s.operatorId, std::move( info ) );
    }
    return result;
}

std::map<std::string, ContractExemption> loadContractExemptions(
    const std::string &sourceRoot, std::string &error )
{
    std::map<std::string, ContractExemption> out;
    const std::filesystem::path path = std::filesystem::path( sourceRoot )
                                       / "data" / "contracts" / "contract_exemptions.json";
    Json::Value doc;
    if ( !readJsonFile( path.string(), doc ) )
        return out; // absent = no exemptions; gates decide whether that is ok
    if ( doc["schema"].asString() != "exp.contract_exemptions.v1" )
    {
        error = "contract_exemptions.json: schema must be exp.contract_exemptions.v1";
        return out;
    }
    for ( const Json::Value &entry : doc["exemptions"] )
    {
        ContractExemption e;
        e.operatorId = entry["operatorId"].asString();
        e.reason = entry["reason"].asString();
        e.evidence = entry["evidence"].asString();
        if ( e.operatorId.empty() || e.reason.empty() || e.evidence.empty() )
        {
            error = "contract_exemptions.json: operatorId/reason/evidence are required";
            return out;
        }
        if ( !out.emplace( e.operatorId, e ).second )
        {
            error = "contract_exemptions.json: duplicate operatorId " + e.operatorId;
            return out;
        }
    }
    return out;
}

DeterminismCensus buildDeterminismCensus( const std::string &sourceRoot )
{
    DeterminismCensus census;
    const auto overrides = scanDeterminismOverrides( sourceRoot );
    std::string exemptionError;
    const auto exemptions = loadContractExemptions( sourceRoot, exemptionError );
    if ( !exemptionError.empty() )
        census.notes.push_back( exemptionError );

    for ( const auto &[id, info] : overrides )
    {
        DeterminismCensusEntry e;
        e.operatorId = id;
        e.prefix = prefixOf( id );
        e.source = info;
        e.schemaGrade = info.gradeOverride ? info.gradeLiteral : std::string();
        // Runtime grade: the scanned literal, or the framework default
        // (RSOperator::determinism() == BitExact, rs_operator.h L170).
        if ( info.runtimeOverride )
            e.runtimeGrade = info.runtimeLiteral == "Tolerance" ? "tolerance" : "bit_exact";
        else
            e.runtimeGrade = "bit_exact"; // framework default

        // Knowledge layer: capability sidecar claim (absent file = "").
        Json::Value sidecar;
        const std::string sidecarPath = ( std::filesystem::path( sourceRoot )
                                          / "data" / "processing" / "algorithm_meta"
                                          / "capability"
                                          / ( slugForSidecar( id ) + ".json" ) )
                                          .string();
        if ( readJsonFile( sidecarPath, sidecar ) )
        {
            const Json::Value &det = sidecar["capability"]["determinism"];
            if ( det.isMember( "grade" ) )
                e.sidecarGrade = det["grade"].asString();
            if ( det.isMember( "stochastic" ) )
                e.sidecarStochastic = det["stochastic"].asBool() ? "true" : "false";
        }

        // Contract membership or exemption.
        if ( const ScientificContract *c = findScientificContract( id ) )
        {
            e.hasScientificContract = true;
            e.seedPolicy = c->seedPolicy;
        }
        if ( const auto it = exemptions.find( id ); it != exemptions.end() )
        {
            e.exempted = true;
            e.exemptionReason = it->second.reason;
        }
        census.entries.push_back( std::move( e ) );
    }
    std::sort( census.entries.begin(), census.entries.end(),
               []( const DeterminismCensusEntry &a, const DeterminismCensusEntry &b ) {
                   return a.operatorId < b.operatorId;
               } );
    return census;
}

Json::Value determinismCensusToJson( const DeterminismCensus &census )
{
    Json::Value doc;
    doc["schema"] = "exp.determinism_census.v1";
    doc["entryCount"] = static_cast<Json::ArrayIndex>( census.entries.size() );
    for ( const std::string &note : census.notes )
        doc["notes"].append( note );
    for ( const DeterminismCensusEntry &e : census.entries )
    {
        Json::Value row;
        row["operatorId"] = e.operatorId;
        row["prefix"] = e.prefix;
        row["schemaGrade"] = e.schemaGrade;
        row["runtimeGrade"] = e.runtimeGrade;
        row["sidecarGrade"] = e.sidecarGrade;
        row["sidecarStochastic"] = e.sidecarStochastic;
        row["hasScientificContract"] = e.hasScientificContract;
        row["seedPolicy"] = e.seedPolicy;
        row["exempted"] = e.exempted;
        if ( e.exempted )
            row["exemptionReason"] = e.exemptionReason;
        row["source"]["class"] = e.source.operatorClass;
        row["source"]["file"] = e.source.file;
        row["source"]["classFound"] = e.source.classFound;
        row["source"]["gradeOverride"] = e.source.gradeOverride;
        if ( e.source.gradeOverride )
            row["source"]["gradeLiteral"] = e.source.gradeLiteral;
        row["source"]["runtimeOverride"] = e.source.runtimeOverride;
        if ( e.source.runtimeOverride )
            row["source"]["runtimeLiteral"] = e.source.runtimeLiteral;
        row["source"]["baseDepth"] = e.source.baseDepth;
        if ( !e.source.note.empty() )
            row["source"]["note"] = e.source.note;
        doc["entries"].append( std::move( row ) );
    }
    return doc;
}

} // namespace sicnu::contracts
