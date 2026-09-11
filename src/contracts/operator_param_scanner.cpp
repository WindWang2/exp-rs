/***************************************************************************
 * operator_param_scanner.cpp — implementation-vs-schema parameter extraction
 ***************************************************************************/
#include "operator_param_scanner.h"

#include "text_scan.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace sicnu::contracts {

namespace
{

const char *const kHelperNames =
    "requireString|getString|getInt|getDouble|getBool|hasNumber|getEnum|"
    "getStringArray";

bool isIdentChar( char c )
{
    return std::isalnum( static_cast<unsigned char>( c ) ) || c == '_';
}

std::string readFile( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return {};
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
}

/// Extracts the identifier passed as argument `index` (1-based) of the first
/// `callName(` occurrence inside `body`. Returns "" when not found.
std::string callArgIdentifier( std::string_view body, const char *callName,
                               int index )
{
    const size_t pos = body.find( callName );
    if ( pos == std::string_view::npos )
        return {};
    const size_t openParen = body.find( '(', pos );
    if ( openParen == std::string_view::npos )
        return {};
    int depth = 0;
    size_t close = openParen;
    for ( size_t i = openParen; i < body.size(); ++i )
    {
        if ( body[i] == '(' )
            ++depth;
        else if ( body[i] == ')' )
        {
            --depth;
            if ( depth == 0 )
            {
                close = i;
                break;
            }
        }
    }
    const Span argsSpan{ openParen + 1, close };
    const auto args = splitArgs( body, argsSpan );
    if ( static_cast<int>( args.size() ) < index )
        return {};
    return firstIdentifier(
        body.substr( args[index - 1].begin,
                     args[index - 1].end - args[index - 1].begin ) );
}

/// All `Json::Value [&] NAME` parameter identifiers in a signature (const
/// and mutable refs both consume/extend the container).
std::vector<std::string> jsonValueParams( std::string_view signature )
{
    static const std::regex re(
        R"((?:const\s+)?Json::Value\s*&\s*([A-Za-z_]\w*)\b)" );
    const std::string sig( signature );
    std::vector<std::string> names;
    auto it = std::sregex_iterator( sig.begin(), sig.end(), re );
    for ( ; it != std::sregex_iterator(); ++it )
        names.push_back( ( *it )[1].str() );
    return names;
}

struct ReadCollector
{
    std::set<std::string> keys;
    std::vector<std::string> unresolved;

    void collect( std::string_view body, const std::vector<std::string> &vars )
    {
        const Span whole{ 0, body.size() };
        const std::string text( body );
        std::smatch dynamicMatch;
        for ( const auto &var : vars )
        {
            // 1. literal indexing: var["key"] (read or write — both prove
            //    the key is consumed by this implementation)
            {
                const std::string reIndexed =
                    "\\b" + var + R"re(\s*\[\s*"([^"]+)"\s*\])re";
                for ( const auto &key : findMatches( body, whole, reIndexed ) )
                    keys.insert( key );
            }
            // 2. dynamic (non-literal) indexing on a tracked var
            {
                const std::regex reDyn( "\\b" + var + R"re(\s*\[\s*(?!"))re" );
                if ( std::regex_search( text, dynamicMatch, reDyn ) )
                    unresolved.push_back( "dynamic key access on '" + var +
                                          "'" );
            }
            // 3. isMember / get
            {
                const std::string reMember = "\\b" + var +
                    R"re(\s*\.\s*(?:isMember|get)\s*\(\s*"([^"]+)")re";
                for ( const auto &key : findMatches( body, whole, reMember ) )
                    keys.insert( key );
            }
            // 4. shared params helpers with var as first argument
            {
                const std::string reHelper = "\\b(?:" +
                    std::string( kHelperNames ) + R"re()\s*\(\s*)re" + var +
                    R"re(\s*,\s*"([^"]+)")re";
                for ( const auto &key : findMatches( body, whole, reHelper ) )
                    keys.insert( key );
            }
            // 5. parseBands(var) consumes the "bands" key
            {
                const std::regex reBands(
                    "\\bparseBands\\s*\\(\\s*" + var + "\\b" );
                if ( std::regex_search( text, dynamicMatch, reBands ) )
                    keys.insert( "bands" );
            }
        }
    }
};

ReadCollector collectReads( std::string_view body,
                            const std::vector<std::string> &rootVars )
{
    ReadCollector collector;
    std::vector<std::string> vars = rootVars;
    // Local aliases: `const Json::Value &alias = root;`
    static const std::regex reAlias(
        R"(\bconst\s+Json::Value\s*&\s*([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*;)" );
    const std::string text( body );
    for ( auto it = std::sregex_iterator( text.begin(), text.end(), reAlias );
          it != std::sregex_iterator(); ++it )
    {
        const std::string alias = ( *it )[1].str();
        const std::string target = ( *it )[2].str();
        if ( std::find( vars.begin(), vars.end(), target ) != vars.end() &&
             std::find( vars.begin(), vars.end(), alias ) == vars.end() )
            vars.push_back( alias );
    }
    collector.collect( body, vars );
    return collector;
}

/// Walks backwards from an identifier start over identifier and `::`
/// characters to capture the qualified name (e.g. "detail::fn").
std::string qualifiedNameAt( std::string_view src, size_t nameStart )
{
    static const auto isNameChar = []( char c ) {
        return std::isalnum( static_cast<unsigned char>( c ) ) || c == '_';
    };
    size_t end = nameStart;
    while ( end < src.size() && isNameChar( src[end] ) )
        ++end;
    size_t begin = nameStart;
    while ( begin > 0 )
    {
        if ( isNameChar( src[begin - 1] ) )
        {
            --begin;
            continue;
        }
        // "::" continuation
        if ( begin >= 2 && src[begin - 1] == ':' && src[begin - 2] == ':' )
        {
            begin -= 2;
            continue;
        }
        break;
    }
    std::string out( src.substr( begin, end - begin ) );
    while ( out.size() >= 2 &&
            out.compare( out.size() - 2, 2, "::" ) == 0 )
        out.erase( out.size() - 2 );
    return out;
}

/// Names called as `name(` at code positions, excluding method calls and a
/// fixed set of framework/builtin names. Namespace-qualified calls
/// (`detail::fn(...)`) are keyed by their qualified name.
std::set<std::string> calledFunctions( std::string_view body )
{
    static const std::string reCall = R"re(([A-Za-z_]\w*)\s*\()re";
    static const std::set<std::string> kIgnored = {
        "if",         "for",             "while",
        "switch",     "return",          "sizeof",
        "catch",      "defined",         "static_cast",
        "dynamic_cast", "const_cast",    "reinterpret_cast",
        "makeRootSchema", "makeRequired", "stampDeterminismGrade",
        "makeStringParam", "makeNumberParam", "makeIntegerParam",
        "makeBooleanParam", "makeEnumParam", "makeRasterParam",
        "makeVectorParam", "makeOutputParam", "setRange",
        "baseParam",  "requireString",   "getString",
        "getInt",     "getDouble",       "getBool",
        "hasNumber",  "getEnum",         "getStringArray",
        "parseBands", "guarded",         "Json",
        "std",        "tr",              "QString",
        "QStringLiteral", "arg",         "QStringList",
    };
    std::set<std::string> out;
    for ( const auto &m :
          findMatchesWithPos( body, { 0, body.size() }, reCall ) )
    {
        if ( m.pos > 0 )
        {
            const char prev = body[m.pos - 1];
            if ( prev == '.' || prev == '>' || prev == '-' )
                continue; // member call through object/pointer
            // ':' (namespace qualification) and everything else is kept.
        }
        if ( kIgnored.count( m.text ) )
            continue;
        out.insert( qualifiedNameAt( body, m.pos ) );
    }
    return out;
}

/// Index of same-file free-function definitions: qualified name → body
/// span. Namespace-qualified definitions (`ns::fn(...)`) are keyed by their
/// qualified name; class methods (`Class::method`) land under their
/// qualified name too and thus never collide with free functions.
std::map<std::string, Span>
indexFreeFunctions( std::string_view src )
{
    std::map<std::string, Span> out;
    static const std::string reCall = R"re(([A-Za-z_]\w*)\s*\()re";
    for ( const auto &m :
          findMatchesWithPos( src, { 0, src.size() }, reCall ) )
    {
        if ( m.pos > 0 && isIdentChar( src[m.pos - 1] ) )
            continue;
        // Skip calls through objects/pointers; qualified definitions are
        // kept (keyed by their qualified name).
        if ( m.pos > 0 )
        {
            const char prev = src[m.pos - 1];
            if ( prev == '.' || prev == '>' || prev == '-' )
                continue;
        }
        // Balance parens (literal-aware) to the closing ')'.
        size_t p = m.pos + m.whole.size();
        // m.whole includes the '('; walk with depth-aware scan.
        int depth = 1;
        while ( p < src.size() && depth > 0 )
        {
            const char c = src[p];
            if ( c == '(' )
                ++depth;
            else if ( c == ')' )
                --depth;
            ++p;
        }
        if ( depth != 0 )
            continue;
        // Optional const qualifier, then '{'.
        p = skipSpace( src, p );
        static const std::regex qualifier( R"(^(const|noexcept|override)\b)" );
        std::cmatch cm;
        while ( p < src.size() &&
                std::regex_search( src.data() + p,
                                   src.data() + std::min( src.size(),
                                                          p + 24 ),
                                   cm, qualifier ) )
            p = skipSpace( src, p + cm.length() );
        if ( p >= src.size() || src[p] != '{' )
            continue;
        const Span body = matchBrace( src, p );
        if ( !body.valid() )
            continue;
        const std::string key = qualifiedNameAt( src, m.pos );
        if ( out.count( key ) )
            continue;
        out[key] = body;
    }
    return out;
}

/// Per-file index used for cross-file body lookup.
struct FileUnit
{
    std::string path;
    std::string src;
    struct ClassBodies
    {
        Span schema;
        Span run;
    };
    std::map<std::string, ClassBodies> classes;
    std::map<std::string, Span> helpers;
    std::vector<std::pair<std::string, std::string>> registrations; // (cls,id)
};

/// A resolved helper definition (unit + body span).
struct HelperDef
{
    const FileUnit *unit;
    Span span;
};

/// Cross-file helper table: qualified name -> definitions.
using GlobalHelpers = std::map<std::string, std::vector<HelperDef>>;

/// Resolves a helper by qualified name: same-file definition first, then a
/// unique global definition. Ambiguous global names resolve to nothing
/// (conservative: missing reads surface as findings, never as wrong
/// attributions).
const HelperDef *resolveHelper( const GlobalHelpers &global,
                                const FileUnit &unit, const std::string &name,
                                HelperDef &storage )
{
    const auto same = unit.helpers.find( name );
    if ( same != unit.helpers.end() && same->second.valid() )
    {
        storage.unit = &unit;
        storage.span = same->second;
        return &storage;
    }
    const auto it = global.find( name );
    if ( it != global.end() && it->second.size() == 1 )
        return &it->second.front();
    return nullptr;
}

/// Declared-key extraction for one schema-building body: the container
/// passed as makeRootSchema's 3rd argument plus root["properties"]["k"]
/// additions. Returns false when no makeRootSchema container is found.
bool extractDeclaredFrom( std::string_view body,
                          std::set<std::string> &out )
{
    const std::string container =
        callArgIdentifier( body, "makeRootSchema", 3 );
    if ( container.empty() )
        return false;
    const std::string reDecl =
        "\\b" + container + R"re(\s*\[\s*"([^"]+)"\s*\]\s*=(?!=))re";
    for ( const auto &k : findMatches( body, { 0, body.size() }, reDecl ) )
        out.insert( k );
    const std::string reProps = "\\b" + container +
        R"re(\s*\[\s*"properties"\s*\]\s*\[\s*"([^"]+)"\s*\])re";
    for ( const auto &k : findMatches( body, { 0, body.size() }, reProps ) )
        out.insert( k );
    return true;
}

/// Declared-key collection for a schema-building body, following helpers
/// that RECEIVE the container as an argument (e.g. addCommonProps(props)).
/// Returns false when no makeRootSchema container is found in @p body.
bool collectDeclaredKeys( const FileUnit &unit, const GlobalHelpers &global,
                          const std::string_view &body,
                          std::set<std::string> &visited, int depth,
                          std::set<std::string> &out )
{
    const std::string container =
        callArgIdentifier( body, "makeRootSchema", 3 );
    if ( container.empty() )
        return false;
    const std::string reDecl =
        "\\b" + container + R"re(\s*\[\s*"([^"]+)"\s*\]\s*=(?!=))re";
    for ( const auto &k : findMatches( body, { 0, body.size() }, reDecl ) )
        out.insert( k );
    const std::string reProps = "\\b" + container +
        R"re(\s*\[\s*"properties"\s*\]\s*\[\s*"([^"]+)"\s*\])re";
    for ( const auto &k : findMatches( body, { 0, body.size() }, reProps ) )
        out.insert( k );

    // Helpers receiving the container (e.g. addCommonProps(props)): their
    // bodies declare parameters through the helper's own parameter name.
    if ( depth <= 0 )
        return true;
    const std::string reCall =
        "\\b([A-Za-z_]\\w*)\\s*\\(\\s*" + container + "\\s*[,)]";
    for ( const auto &m : findMatchesWithPos( body, { 0, body.size() }, reCall ) )
    {
        const std::string fn = m.text;
        if ( visited.count( fn ) )
            continue;
        visited.insert( fn );
        HelperDef storage;
        const HelperDef *def = resolveHelper( global, unit, fn, storage );
        if ( !def )
            continue;
        const std::string helperBody = def->unit->src.substr(
            def->span.begin, def->span.end - def->span.begin );
        const Span sigSpan =
            functionSignature( def->unit->src, def->span.begin - 1 );
        const auto vars = jsonValueParams( def->unit->src.substr(
            sigSpan.begin, sigSpan.end - sigSpan.begin ) );
        for ( const auto &local : vars )
        {
            const std::string reLocal =
                "\\b" + local + R"re(\s*\[\s*"([^"]+)"\s*\]\s*=(?!=))re";
            for ( const auto &k :
                  findMatches( helperBody, { 0, helperBody.size() }, reLocal ) )
                out.insert( k );
            // Nested adds (local["k"]["x-rs-…"]) still declare the outer key.
            const std::string reNested =
                "\\b" + local +
                R"re(\s*\[\s*"([^"]+)"\s*\]\s*\[)re";
            for ( const auto &k :
                  findMatches( helperBody, { 0, helperBody.size() }, reNested ) )
                out.insert( k );
        }
    }
    return true;
}

/// Collects parameter reads for one params-consuming body plus the helpers
/// it calls (same file or uniquely global, depth-bounded).
/// Helpers without a Json::Value parameter (e.g. guarded(...)) are skipped
/// silently — they consume no parameters; only the operator's own run body
/// reports an unresolved signature.
void collectReadsInto( const FileUnit &unit, const GlobalHelpers &global,
                       const Span &bodySpan, OperatorScanResult &r,
                       std::set<std::string> &visited, int maxDepth,
                       bool isRoot = false )
{
    const std::string_view src = unit.src;
    const Span sigSpan = functionSignature( src, bodySpan.begin - 1 );
    auto vars = jsonValueParams(
        src.substr( sigSpan.begin, sigSpan.end - sigSpan.begin ) );
    if ( vars.empty() )
    {
        if ( isRoot )
            r.unresolved.push_back(
                "run(): no const Json::Value& parameter identified" );
        return;
    }
    const std::string text(
        src.substr( bodySpan.begin, bodySpan.end - bodySpan.begin ) );
    ReadCollector reads = collectReads( text, vars );
    r.readParams.insert( reads.keys.begin(), reads.keys.end() );
    r.unresolved.insert( r.unresolved.end(), reads.unresolved.begin(),
                         reads.unresolved.end() );
    if ( maxDepth <= 0 )
        return;
    for ( const auto &fn : calledFunctions( text ) )
    {
        if ( visited.count( fn ) )
            continue;
        visited.insert( fn );
        HelperDef storage;
        const HelperDef *def = resolveHelper( global, unit, fn, storage );
        if ( !def )
            continue;
        collectReadsInto( *def->unit, global, def->span, r, visited,
                          maxDepth - 1, false );
    }
}

/// Extracts declared/read sets for one operator from its implementation
/// unit. Appends to `r.unresolved` when bodies or containers are missing.
void extractOperator( const FileUnit &unit,
                      const FileUnit::ClassBodies &bodies,
                      const GlobalHelpers &global, OperatorScanResult &r )
{
    const std::string_view src = unit.src;
    std::set<std::string> visited;

    // == schema() =========================================================
    if ( bodies.schema.valid() )
    {
        r.schemaFound = true;
        const std::string_view body =
            src.substr( bodies.schema.begin,
                        bodies.schema.end - bodies.schema.begin );
        if ( !collectDeclaredKeys( unit, global, body, visited, 1,
                                   r.declaredParams ) )
        {
            // Delegated schema: follow same-file/global helpers (depth <= 2)
            // until one body builds the makeRootSchema container.
            bool found = false;
            std::vector<std::string> frontier;
            for ( const auto &fn : calledFunctions( body ) )
                frontier.push_back( fn );
            int depth = 0;
            size_t budget = 32;
            while ( !frontier.empty() && depth < 2 && budget > 0 && !found )
            {
                std::vector<std::string> next;
                for ( const auto &fn : frontier )
                {
                    if ( visited.count( fn ) || budget == 0 )
                        continue;
                    visited.insert( fn );
                    --budget;
                    HelperDef storage;
                    const HelperDef *def =
                        resolveHelper( global, unit, fn, storage );
                    if ( !def )
                        continue;
                    // NB: std::string::substr returns an owning temporary —
                    // binding it to a string_view would dangle immediately.
                    const std::string helperBody =
                        def->unit->src.substr( def->span.begin,
                                               def->span.end -
                                                   def->span.begin );
                    if ( extractDeclaredFrom( helperBody,
                                              r.declaredParams ) )
                    {
                        found = true;
                        break;
                    }
                    for ( const auto &n : calledFunctions( helperBody ) )
                        next.push_back( n );
                }
                frontier = std::move( next );
                ++depth;
            }
            if ( !found )
                r.unresolved.push_back(
                    "schema(): makeRootSchema container not identified" );
        }
    }
    else
    {
        r.unresolved.push_back( "schema() body not found for class " +
                                r.className );
    }

    // == run() + reachable helpers (same file and global) =================
    if ( bodies.run.valid() )
    {
        r.runFound = true;
        collectReadsInto( unit, global, bodies.run, r, visited, 2, true );
    }
    else
    {
        r.unresolved.push_back( "run() body not found for class " +
                                r.className );
    }
}

/// Builds the per-file index over one source buffer.
FileUnit indexUnit( std::string_view src, const std::string &path )
{
    FileUnit unit;
    unit.path = path;
    unit.src = std::string( src );

    // Registrations.
    static const std::regex re(
        R"re(REGISTER_RS_OPERATOR\s*\(\s*([A-Za-z_]\w*)\s*,\s*"([^"]+)")re" );
    for ( auto it = std::sregex_iterator( unit.src.begin(), unit.src.end(),
                                          re );
          it != std::sregex_iterator(); ++it )
        unit.registrations.push_back(
            { ( *it )[1].str(), ( *it )[2].str() } );

    // Classes defining schema()/run() bodies in this file.
    static const std::string reMethod =
        R"re(\b([A-Za-z_]\w*)::(schema|run)\s*\()re";
    std::set<std::string> classes;
    for ( const auto &m :
          findMatchesWithPos( src, { 0, src.size() }, reMethod ) )
        classes.insert( m.text );
    for ( const auto &cls : classes )
    {
        FileUnit::ClassBodies bodies;
        bodies.schema = findFunctionBody( src, cls + "::schema" );
        bodies.run = findFunctionBody( src, cls + "::run" );
        if ( bodies.schema.valid() || bodies.run.valid() )
            unit.classes[cls] = bodies;
    }

    unit.helpers = indexFreeFunctions( src );
    return unit;
}

} // namespace

std::set<std::string> OperatorScanResult::undeclaredReads() const
{
    std::set<std::string> out;
    for ( const auto &k : readParams )
        if ( !declaredParams.count( k ) )
            out.insert( k );
    return out;
}

std::set<std::string> OperatorScanResult::deadSchemaParams() const
{
    std::set<std::string> out;
    for ( const auto &k : declaredParams )
        if ( !readParams.count( k ) )
            out.insert( k );
    return out;
}

OperatorParamScanner::OperatorParamScanner( std::string sourceRoot,
                                            size_t maxFileBytes )
    : m_sourceRoot( std::move( sourceRoot ) ), m_maxFileBytes( maxFileBytes )
{
}

std::vector<OperatorScanResult>
OperatorParamScanner::scanSource( std::string_view src,
                                  const std::string &fileName ) const
{
    FileUnit unit = indexUnit( src, fileName );
    GlobalHelpers global;
    for ( const auto &[name, span] : unit.helpers )
        global[name].push_back( { &unit, span } );
    std::vector<OperatorScanResult> results;
    for ( const auto &[cls, id] : unit.registrations )
    {
        OperatorScanResult r;
        r.operatorId = id;
        r.className = cls;
        r.file = fileName;
        const auto it = unit.classes.find( cls );
        if ( it != unit.classes.end() )
            extractOperator( unit, it->second, global, r );
        else
            r.unresolved.push_back( "no schema()/run() body found for class " +
                                    cls );
        results.push_back( std::move( r ) );
    }
    return results;
}

std::vector<std::string> collectCppFiles( const std::string &dir )
{
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path root( dir );
    if ( !std::filesystem::exists( root, ec ) )
        return out;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied,
        ec );
    std::filesystem::recursive_directory_iterator end;
    while ( !ec && it != end )
    {
        std::error_code fileEc;
        if ( it->is_regular_file( fileEc ) && it->path().extension() == ".cpp" )
            out.push_back( it->path().string() );
        it.increment( ec );
    }
    std::sort( out.begin(), out.end() );
    return out;
}

std::vector<OperatorScanResult> OperatorParamScanner::scanAll() const
{
    std::vector<OperatorScanResult> out;
    std::vector<FileUnit> units;
    const std::string opsDir =
        ( std::filesystem::path( m_sourceRoot ) / "src" / "operators" )
            .string();
    for ( const auto &file : collectCppFiles( opsDir ) )
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size( file, ec );
        if ( ec )
            continue;
        if ( size > m_maxFileBytes )
        {
            OperatorScanResult r;
            r.file = file;
            r.operatorId = "<oversized>";
            r.unresolved.push_back( "file exceeds scan cap (" +
                                    std::to_string( size ) + " bytes)" );
            out.push_back( std::move( r ) );
            continue;
        }
        units.push_back( indexUnit( readFile( file ), file ) );
    }

    // Cross-file helper table: uniquely-named helpers resolve globally
    // (e.g. spectral_index_detail::runSpectralIndexCore); ambiguous names
    // resolve to nothing.
    GlobalHelpers global;
    for ( const auto &unit : units )
        for ( const auto &[name, span] : unit.helpers )
            global[name].push_back( { &unit, span } );

    for ( const auto &unit : units )
    {
        for ( const auto &[cls, id] : unit.registrations )
        {
            OperatorScanResult r;
            r.operatorId = id;
            r.className = cls;
            r.file = unit.path;
            // Locate the implementation unit for this class: the unit that
            // indexes bodies for it (often the same unit that registers it).
            const FileUnit *implUnit = nullptr;
            const FileUnit::ClassBodies *bodies = nullptr;
            for ( const auto &candidate : units )
            {
                const auto it = candidate.classes.find( cls );
                if ( it != candidate.classes.end() )
                {
                    implUnit = &candidate;
                    bodies = &it->second;
                    break;
                }
            }
            if ( implUnit && bodies )
                extractOperator( *implUnit, *bodies, global, r );
            else
                r.unresolved.push_back(
                    "no schema()/run() body found for class " + cls );
            out.push_back( std::move( r ) );
        }
    }
    return out;
}

} // namespace sicnu::contracts
