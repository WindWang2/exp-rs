/***************************************************************************
 * snapshot_diff.cpp — implementation of the readable drift report.
 * See snapshot_diff.h for the rationale and the identity rules.
 ***************************************************************************/
#include "snapshot_diff.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace sicnu::contracts {
namespace {

constexpr const char *kGraphSchema = "exp.contract.graph.v1";
constexpr const char *kCensusSchema = "exp.determinism_census.v1";

std::string stringOr( const Json::Value &v, const char *key )
{
    if ( !v.isObject() || !v.isMember( key ) )
        return std::string();
    const Json::Value &m = v[key];
    if ( m.isString() )
        return m.asString();
    // Non-string payloads (e.g. a numeric attribute) are rendered through the
    // canonical writer so the report stays comparable across types.
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString( b, m );
}

/// Identity of a graph node: (kind, id). Kept as a single joined key so the
/// element set is a plain std::set and iteration is sorted → deterministic.
std::string nodeKey( const Json::Value &n )
{
    return stringOr( n, "kind" ) + "\x1f" + stringOr( n, "id" );
}

/// Identity of a graph edge: (kind, from, to). `origin` is payload.
std::string edgeKey( const Json::Value &e )
{
    return stringOr( e, "kind" ) + "\x1f" + stringOr( e, "from" ) + "\x1f"
           + stringOr( e, "to" );
}

/// Identity of a census entry: operatorId.
std::string entryKey( const Json::Value &e )
{
    return stringOr( e, "operatorId" );
}

struct Element
{
    std::string kind;  // node kind / "edge" / "entry"
    std::string id;    // human-facing identity (readable, not the \x1f key)
    std::string origin; // payload we track for "changed"
};

/// Index a list under an identity function. First occurrence wins, so a
/// duplicate id cannot silently overwrite a real element (duplicate nodes are
/// already a graph finding, reported elsewhere).
template <typename KeyFn, typename KindFn, typename IdFn>
std::map<std::string, Element> indexElements( const Json::Value &list, KeyFn keyFn,
                                              KindFn kindFn, IdFn idFn )
{
    std::map<std::string, Element> out;
    for ( const auto &v : list )
    {
        Element el;
        el.kind = kindFn( v );
        el.id = idFn( v );
        el.origin = stringOr( v, "origin" );
        out.emplace( keyFn( v ), el );
    }
    return out;
}

/// The human-facing identity of a graph node is just its id; the kind is
/// already carried in the `kind` column of the report line. Prefixing the id
/// with the kind would print `capability_entry capability_entry/…`.
std::string nodeIdColumn( const Json::Value &n )
{
    return stringOr( n, "id" );
}

void diffElementSets( const std::map<std::string, Element> &recorded,
                      const std::map<std::string, Element> &live,
                      SnapshotDiffReport &out )
{
    for ( const auto &kv : recorded )
    {
        auto it = live.find( kv.first );
        if ( it == live.end() )
        {
            SnapshotDiffLine line; line.change = "removed";
            line.kind = kv.second.kind; line.id = kv.second.id;
            out.lines.push_back( line );
            continue;
        }
        if ( kv.second.origin != it->second.origin )
        {
            SnapshotDiffLine line; line.change = "changed";
            line.kind = kv.second.kind; line.id = kv.second.id;
            line.field = "origin";
            line.before = kv.second.origin; line.after = it->second.origin;
            out.lines.push_back( line );
        }
    }
    for ( const auto &kv : live )
    {
        if ( recorded.find( kv.first ) == recorded.end() )
        {
            SnapshotDiffLine line; line.change = "added";
            line.kind = kv.second.kind; line.id = kv.second.id;
            out.lines.push_back( line );
        }
    }
}

bool diffGraph( const Json::Value &recorded, const Json::Value &live,
                SnapshotDiffReport &out )
{
    if ( !recorded.isMember( "nodes" ) || !recorded.isMember( "edges" )
         || !live.isMember( "nodes" ) || !live.isMember( "edges" ) )
        return false;

    auto recNodes = indexElements(
        recorded["nodes"], nodeKey,
        []( const Json::Value &n ) { return stringOr( n, "kind" ); },
        nodeIdColumn );
    auto liveNodes = indexElements(
        live["nodes"], nodeKey,
        []( const Json::Value &n ) { return stringOr( n, "kind" ); },
        nodeIdColumn );
    diffElementSets( recNodes, liveNodes, out );

    auto recEdges = indexElements(
        recorded["edges"], edgeKey,
        []( const Json::Value & ) { return std::string( "edge" ); },
        []( const Json::Value &e ) {
            return stringOr( e, "kind" ) + " " + stringOr( e, "from" ) + " -> "
                   + stringOr( e, "to" );
        } );
    auto liveEdges = indexElements(
        live["edges"], edgeKey,
        []( const Json::Value & ) { return std::string( "edge" ); },
        []( const Json::Value &e ) {
            return stringOr( e, "kind" ) + " " + stringOr( e, "from" ) + " -> "
                   + stringOr( e, "to" );
        } );
    diffElementSets( recEdges, liveEdges, out );

    return true;
}

bool diffCensus( const Json::Value &recorded, const Json::Value &live,
                 SnapshotDiffReport &out )
{
    if ( !recorded.isMember( "entries" ) || !live.isMember( "entries" ) )
        return false;

    auto mk = []( const Json::Value &e ) { return entryKey( e ); };
    auto kj = []( const Json::Value & ) { return std::string( "entry" ); };
    auto ij = []( const Json::Value &e ) { return stringOr( e, "operatorId" ); };

    auto rec = indexElements( recorded["entries"], mk, kj, ij );
    auto liv = indexElements( live["entries"], mk, kj, ij );
    diffElementSets( rec, liv, out );
    return true;
}

/// Stable ordering: by change class, then kind, then id, so the report does
/// not depend on the traversal order of the maps above.
void sortLines( SnapshotDiffReport &out )
{
    static const std::map<std::string, int> rank = {
        { "added", 0 }, { "removed", 1 }, { "changed", 2 } };
    std::stable_sort( out.lines.begin(), out.lines.end(),
                      []( const SnapshotDiffLine &a, const SnapshotDiffLine &b ) {
                          int ra = rank.count( a.change ) ? rank.at( a.change ) : 9;
                          int rb = rank.count( b.change ) ? rank.at( b.change ) : 9;
                          if ( ra != rb )
                              return ra < rb;
                          if ( a.kind != b.kind )
                              return a.kind < b.kind;
                          return a.id < b.id;
                      } );
}

} // namespace

bool diffContractSnapshot( const Json::Value &recorded,
                           const Json::Value &live,
                           SnapshotDiffReport &out,
                           std::string &error )
{
    out = SnapshotDiffReport();
    error.clear();

    const std::string recSchema = stringOr( recorded, "schema" );
    const std::string liveSchema = stringOr( live, "schema" );
    if ( recSchema.empty() )
    {
        error = "recorded document carries no `schema` member";
        return false;
    }
    if ( liveSchema.empty() )
    {
        error = "live document carries no `schema` member";
        return false;
    }
    if ( recSchema != liveSchema )
    {
        out.schema = recSchema;
        out.schemaMismatch = true;
        return true;
    }

    out.schema = recSchema;
    bool ok = false;
    if ( recSchema == kGraphSchema )
        ok = diffGraph( recorded, live, out );
    else if ( recSchema == kCensusSchema )
        ok = diffCensus( recorded, live, out );
    else
    {
        error = "unrecognized snapshot schema: " + recSchema;
        return false;
    }

    if ( !ok )
    {
        error = "document does not match schema " + recSchema
                + " (expected `nodes`/`edges` or `entries`)";
        out = SnapshotDiffReport();
        return false;
    }

    sortLines( out );
    return true;
}

std::string formatSnapshotDiff( const SnapshotDiffReport &report )
{
    std::ostringstream os;
    os << "snapshot schema: " << report.schema << "\n";
    if ( report.schemaMismatch )
    {
        os << "!! SCHEMA MISMATCH — the recorded and live documents do not\n"
              "   describe the same contract surface. This is not a small\n"
              "   drift; regenerate and review before proceeding.\n";
        return os.str();
    }
    if ( report.empty() )
    {
        os << "no element differences\n";
        return os.str();
    }

    std::size_t added = 0, removed = 0, changed = 0;
    for ( const auto &l : report.lines )
    {
        if ( l.change == "added" ) ++added;
        else if ( l.change == "removed" ) ++removed;
        else ++changed;
    }
    os << "differences: " << report.lines.size() << " (+" << added << " -"
       << removed << " ~" << changed << ")\n";
    for ( const auto &l : report.lines )
    {
        if ( l.change == "added" )
            os << "  + " << l.kind << " " << l.id << "\n";
        else if ( l.change == "removed" )
            os << "  - " << l.kind << " " << l.id << "\n";
        else
            os << "  ~ " << l.kind << " " << l.id << " " << l.field << ": `"
               << l.before << "` -> `" << l.after << "`\n";
    }
    return os.str();
}

} // namespace sicnu::contracts
