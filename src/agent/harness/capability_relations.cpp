// src/agent/harness/capability_relations.cpp
#include "capability_relations.h"

#include "capability_catalog.h"
#include "spatial_tools/spatial_tool.h"

#include <QDir>
#include <QFile>
#include <QIODevice>

#include <algorithm>
#include <functional>
#include <memory>
#include <map>

namespace sicnu::agent::harness {

namespace {

using Json::Value;

bool factsMatch( const Value &when, const Value &facts, std::string *missingKey )
{
  if ( !when.isObject() )
    return true;
  for ( const std::string &key : when.getMemberNames() )
  {
    if ( !facts.isObject() || !facts.isMember( key ) )
    {
      if ( missingKey )
        *missingKey = key;
      return false;
    }
    const Value &wanted = when[ key ];
    const Value &actual = facts[ key ];
    if ( wanted.isArray() )
    {
      bool matched = false;
      for ( const Value &candidate : wanted )
      {
        if ( candidate.isString() && actual.isString() &&
             candidate.asString() == actual.asString() )
        {
          matched = true;
          break;
        }
      }
      if ( !matched )
      {
        if ( missingKey )
          *missingKey = key;
        return false;
      }
    }
    else if ( wanted.isString() )
    {
      // Bool facts match their wire words so {aligned: true} satisfies the
      // "true" gate without a string-typed facts document.
      const bool boolMatch = actual.isBool() &&
        ( ( actual.asBool() && wanted.asString() == "true" ) ||
          ( !actual.asBool() && wanted.asString() == "false" ) );
      const bool stringMatch =
        actual.isString() && actual.asString() == wanted.asString();
      if ( !boolMatch && !stringMatch )
      {
        if ( missingKey )
          *missingKey = key;
        return false;
      }
    }
  }
  return true;
}

} // namespace

CapabilityRelations &CapabilityRelations::instance()
{
  static CapabilityRelations relations;
  return relations;
}

void CapabilityRelations::setFilePath( const std::string &path )
{
  mFilePath = path;
  mLoaded = false;
}

std::string CapabilityRelations::defaultFilePath() const
{
  return CapabilityCatalog::instance().directory() + "/capability_relations.json";
}

std::vector<std::string> validateRelations( const Value &doc,
                                            const std::set<std::string> &knownOperatorIds )
{
  std::vector<std::string> problems;
  if ( !doc.isObject() )
  {
    problems.push_back( "relations document must be an object" );
    return problems;
  }
  if ( doc[ "schema_version" ].asInt() != 1 )
    problems.push_back( "schema_version must be 1" );

  const auto checkOperator = [ & ]( const Value &id, const std::string &where ) {
    if ( !id.isString() || id.asString().empty() )
    {
      problems.push_back( where + ": operator id must be a non-empty string" );
      return false;
    }
    if ( !knownOperatorIds.count( id.asString() ) )
    {
      problems.push_back( where + ": dangling operator reference '" + id.asString() + "'" );
      return false;
    }
    return true;
  };

  std::vector<std::pair<std::string, std::string>> edges;
  if ( !doc[ "chains" ].isArray() )
    problems.push_back( "chains must be an array" );
  else
  {
    int index = 0;
    for ( const Value &edge : doc[ "chains" ] )
    {
      ++index;
      const std::string where = "chains[" + std::to_string( index ) + "]";
      if ( !edge.isObject() )
      {
        problems.push_back( where + ": edge must be an object" );
        continue;
      }
      for ( const std::string &key : edge.getMemberNames() )
      {
        if ( key != "from" && key != "to" && key != "when" && key != "why" )
          problems.push_back( where + ": unknown key '" + key + "'" );
      }
      const bool okFrom = checkOperator( edge[ "from" ], where );
      const bool okTo = checkOperator( edge[ "to" ], where );
      if ( okFrom && okTo && edge[ "from" ].asString() == edge[ "to" ].asString() )
        problems.push_back( where + ": self edge" );
      if ( okFrom && okTo )
        edges.emplace_back( edge[ "from" ].asString(), edge[ "to" ].asString() );
      if ( edge[ "why" ] && !edge[ "why" ].isString() )
        problems.push_back( where + ": why must be a string" );
      const Value &when = edge[ "when" ];
      if ( !when.isNull() )
      {
        if ( !when.isObject() )
          problems.push_back( where + ": when must be an object" );
        else
        {
          for ( const std::string &key : when.getMemberNames() )
          {
            const Value &values = when[ key ];
            const bool okValues =
              ( values.isString() ) ||
              ( values.isArray() && std::all_of( values.begin(), values.end(),
                                                 []( const Value &v ) { return v.isString(); } ) );
            if ( !okValues )
              problems.push_back( where + ": when." + key + " must be a string or string array" );
          }
        }
      }
    }
  }

  if ( !doc[ "exclusive" ].isArray() )
    problems.push_back( "exclusive must be an array" );
  else
  {
    int index = 0;
    for ( const Value &pair : doc[ "exclusive" ] )
    {
      ++index;
      const std::string where = "exclusive[" + std::to_string( index ) + "]";
      if ( !pair.isArray() || pair.size() != 2 )
      {
        problems.push_back( where + ": must be a [a, b] pair" );
        continue;
      }
      checkOperator( pair[ 0 ], where );
      checkOperator( pair[ 1 ], where );
      if ( pair[ 0 ].isString() && pair[ 1 ].isString() )
      {
        const std::string &a = pair[ 0 ].asString();
        const std::string &b = pair[ 1 ].asString();
        if ( a == b )
          problems.push_back( where + ": pair must be two distinct operators" );
        // Exclusive pairs may not double as chain edges — the graph would
        // then both recommend and forbid the same step.
        for ( const auto &[ from, to ] : edges )
        {
          if ( ( from == a && to == b ) || ( from == b && to == a ) )
          {
            problems.push_back( where + ": exclusive pair also appears as a chain edge (" +
                                from + " -> " + to + ")" );
            break;
          }
        }
      }
    }
  }

  if ( !doc[ "requires_shared_grid" ].isArray() )
    problems.push_back( "requires_shared_grid must be an array" );
  else
    for ( const Value &id : doc[ "requires_shared_grid" ] )
      checkOperator( id, "requires_shared_grid" );

  if ( doc[ "grid_fixer" ] && !doc[ "grid_fixer" ].isNull() )
    checkOperator( doc[ "grid_fixer" ], "grid_fixer" );

  // DAG check on the chain edges (globally — a cycle anywhere is an authoring
  // error even if no composition path currently traverses it).
  {
    std::map<std::string, int> state; // 0 = unvisited, 1 = in stack, 2 = done
    std::map<std::string, std::vector<std::string>> adjacency;
    for ( const auto &[ from, to ] : edges )
      adjacency[ from ].push_back( to );
    std::function<bool( const std::string & )> visit = [ & ]( const std::string &node ) -> bool {
      state[ node ] = 1;
      for ( const std::string &next : adjacency[ node ] )
      {
        if ( state[ next ] == 1 )
          return false;
        if ( state[ next ] == 0 && !visit( next ) )
          return false;
      }
      state[ node ] = 2;
      return true;
    };
    for ( const auto &[ node, _ ] : adjacency )
    {
      if ( state[ node ] == 0 && !visit( node ) )
      {
        problems.push_back( "chains: cycle detected through '" + node + "'" );
        break;
      }
    }
  }
  return problems;
}

int CapabilityRelations::reload()
{
  mChains.clear();
  mExclusive.clear();
  mSharedGrid.clear();
  mGridFixer = "rs:align";
  mLoadProblems.clear();
  mLoaded = true;

  const std::string path = mFilePath.empty() ? defaultFilePath() : mFilePath;
  QFile file( QString::fromStdString( path ) );
  if ( !file.exists() )
  {
    mLoadProblems.push_back( "relations file not found: " + path );
    return 0;
  }
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    mLoadProblems.push_back( "relations file unreadable: " + path );
    return 0;
  }
  const QByteArray raw = file.readAll();
  Value parsed;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errors;
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &errors ) )
  {
    mLoadProblems.push_back( "relations parse error: " + errors );
    return 0;
  }

  const std::vector<std::string> knownIds = CapabilityCatalog::instance().entryIds();
  const std::set<std::string> known( knownIds.begin(), knownIds.end() );
  for ( const std::string &problem : validateRelations( parsed, known ) )
    mLoadProblems.push_back( problem );
  if ( !mLoadProblems.empty() )
    return 0;

  for ( const Value &edge : parsed[ "chains" ] )
    mChains.push_back( edge );
  for ( const Value &pair : parsed[ "exclusive" ] )
    mExclusive.emplace_back( pair[ 0 ].asString(), pair[ 1 ].asString() );
  for ( const Value &id : parsed[ "requires_shared_grid" ] )
    mSharedGrid.insert( id.asString() );
  if ( parsed[ "grid_fixer" ].isString() )
    mGridFixer = parsed[ "grid_fixer" ].asString();
  return static_cast<int>( mChains.size() );
}

bool CapabilityRelations::loaded() const
{
  return mLoaded;
}

std::vector<std::string> CapabilityRelations::loadProblems() const
{
  return mLoadProblems;
}

bool CapabilityRelations::hasEdge( const std::string &from, const std::string &to ) const
{
  for ( const Value &edge : mChains )
  {
    if ( edge[ "from" ].asString() == from && edge[ "to" ].asString() == to )
      return true;
  }
  return false;
}

bool CapabilityRelations::exclusiveWith( const std::string &a, const std::string &b ) const
{
  for ( const auto &[ first, second ] : mExclusive )
  {
    if ( ( first == a && second == b ) || ( first == b && second == a ) )
      return true;
  }
  return false;
}

bool CapabilityRelations::requiresGrid( const std::string &operatorId ) const
{
  return mSharedGrid.count( operatorId ) > 0;
}

std::string CapabilityRelations::gridFixer() const
{
  return mGridFixer;
}

Json::Value chainFrom( const std::string &operatorId )
{
  auto &relations = CapabilityRelations::instance();
  auto &catalog = CapabilityCatalog::instance();
  if ( !relations.loaded() )
    relations.reload();
  if ( !catalog.loaded() )
    catalog.reload();

  Value document( Json::objectValue );
  document[ "id" ] = operatorId;
  if ( !catalog.hasEntry( operatorId ) )
  {
    document[ "error" ] = "UNKNOWN_OPERATOR";
    document[ "message" ] = "目标算子不在能力目录中: " + operatorId;
    return document;
  }
  Value upstream( Json::arrayValue );
  Value downstream( Json::arrayValue );
  for ( const Value &edge : relations.chains() )
  {
    if ( edge[ "to" ].asString() == operatorId )
    {
      Value row( Json::objectValue );
      row[ "from" ] = edge[ "from" ];
      row[ "why" ] = edge[ "why" ];
      row[ "when" ] = edge[ "when" ];
      upstream.append( row );
    }
    if ( edge[ "from" ].asString() == operatorId )
    {
      Value row( Json::objectValue );
      row[ "to" ] = edge[ "to" ];
      row[ "why" ] = edge[ "why" ];
      row[ "when" ] = edge[ "when" ];
      downstream.append( row );
    }
  }
  document[ "upstream" ] = upstream;
  document[ "downstream" ] = downstream;
  document[ "requires_shared_grid" ] = relations.requiresGrid( operatorId );
  return document;
}

Json::Value composeChain( const std::string &targetId, const Value &facts )
{
  auto &relations = CapabilityRelations::instance();
  auto &catalog = CapabilityCatalog::instance();
  if ( !relations.loaded() )
    relations.reload();
  if ( !catalog.loaded() )
    catalog.reload();

  Value document( Json::objectValue );
  document[ "target" ] = targetId;
  // Fail-closed: a broken graph must surface as a typed error, never as a
  // silently-empty "successful" plan.
  if ( !relations.loadProblems().empty() )
  {
    document[ "error" ] = "RELATIONS_LOAD_FAILED";
    document[ "message" ] = relations.loadProblems().front();
    return document;
  }
  if ( !catalog.hasEntry( targetId ) )
  {
    document[ "error" ] = "UNKNOWN_OPERATOR";
    document[ "message" ] = "目标算子不在能力目录中: " + targetId;
    return document;
  }

  // Closed set of composed operators + per-step reasons.
  std::map<std::string, std::string> steps;
  steps[ targetId ] = "目标算子";
  Value skipped( Json::arrayValue );
  // The fixpoint loop rescans all edges every pass; record each edge decision
  // exactly once (review P0: duplicates on the flagship NDVI path).
  std::set<std::pair<std::string, std::string>> recordedSkips;
  auto recordSkip = [ & ]( const std::string &from, const std::string &to,
                           const std::string &reason ) {
    if ( recordedSkips.insert( { from, to } ).second )
    {
      Value row( Json::objectValue );
      row[ "from" ] = from;
      row[ "to" ] = to;
      row[ "reason" ] = reason;
      skipped.append( row );
    }
  };

  // Fact-gated upstream closure: an edge participates when its `when` gate
  // is satisfied by the facts. An empty gate means "hard prerequisite —
  // always fires"; a gate key missing from the facts never matches
  // (deterministic, conservative).
  bool changed = true;
  while ( changed )
  {
    changed = false;
    for ( const Value &edge : relations.chains() )
    {
      const std::string from = edge[ "from" ].asString();
      const std::string to = edge[ "to" ].asString();
      if ( !steps.count( to ) || steps.count( from ) )
        continue;
      std::string missingKey;
      if ( !factsMatch( edge[ "when" ], facts, &missingKey ) )
      {
        recordSkip( from, to, "条件未满足: " + missingKey );
        continue;
      }
      bool conflict = false;
      for ( const auto &[ id, reason ] : steps )
      {
        if ( relations.exclusiveWith( from, id ) )
        {
          recordSkip( from, to, "与已选算子互斥: " + id );
          conflict = true;
          break;
        }
      }
      if ( conflict )
        continue;
      steps[ from ] = edge[ "why" ].isString() ? edge[ "why" ].asString()
                                               : ( "链式前置: " + from + " -> " + to );
      changed = true;
    }
  }

  // Shared-grid rule (ADR 0098): the grid fixer runs ahead of the chain
  // unless the facts declare the inputs already aligned.
  const std::string fixer = relations.gridFixer();
  const bool aligned = facts.isObject() && facts.isMember( "aligned" ) &&
                       facts[ "aligned" ].isBool() && facts[ "aligned" ].asBool();
  if ( relations.requiresGrid( targetId ) && !aligned && !steps.count( fixer ) )
  {
    steps[ fixer ] = "requires_shared_grid 契约: 先对齐到统一网格 (ADR 0098)";
  }

  // Deterministic topological order: Kahn's algorithm over the included
  // chain edges, lexicographic tie-break; the grid fixer precedes everything.
  std::map<std::string, int> indegree;
  std::map<std::string, std::vector<std::string>> adjacency;
  for ( const auto &[ id, reason ] : steps )
    indegree[ id ] = 0;
  for ( const Value &edge : relations.chains() )
  {
    const std::string from = edge[ "from" ].asString();
    const std::string to = edge[ "to" ].asString();
    if ( steps.count( from ) && steps.count( to ) )
    {
      adjacency[ from ].push_back( to );
      ++indegree[ to ];
    }
  }
  std::set<std::string> ready;
  for ( const auto &[ id, degree ] : indegree )
  {
    if ( degree == 0 && id != fixer )
      ready.insert( id );
  }
  Value ordered( Json::arrayValue );
  std::set<std::string> placed;
  auto place = [ & ]( const std::string &id, const char *reason ) {
    Value row( Json::objectValue );
    row[ "id" ] = id;
    row[ "reason" ] = steps[ id ];
    if ( reason )
      row[ "reason" ] = std::string( reason );
    ordered.append( row );
    placed.insert( id );
  };
  // The fixer always runs first when present.
  if ( steps.count( fixer ) )
    place( fixer, nullptr );
  while ( !ready.empty() )
  {
    const std::string id = *ready.begin();
    ready.erase( ready.begin() );
    place( id, nullptr );
    for ( const std::string &next : adjacency[ id ] )
    {
      if ( placed.count( next ) )
        continue;
      if ( --indegree[ next ] == 0 && next != fixer )
        ready.insert( next );
    }
  }
  document[ "steps" ] = ordered;
  document[ "skipped" ] = skipped;

  // Advisory notes: prerequisites, reproducibility (ADR 0124), stochasticity.
  Value notes( Json::arrayValue );
  const Json::Value block = catalog.capability( targetId );
  for ( const Value &prereq : block[ "prerequisites" ] )
  {
    if ( prereq.isString() )
      notes.append( "前置条件: " + prereq.asString() );
  }
  const std::string grade = catalog.determinismOf( targetId );
  if ( grade == "tolerance" )
    notes.append( Value( "确定性等级 tolerance: 并行/分块执行下连续统计量与串行基线可能在 "
                         "1e-6 相对容差内差异，两次运行不保证逐位一致。" ) );
  if ( catalog.isStochastic( targetId ) )
    notes.append( Value( "随机性算子: 未固定随机种子时，相同输入在不同运行可能得到不同结果，"
                         "请注意结果可能不可复现。" ) );
  document[ "notes" ] = notes;
  return document;
}

// --- harness:compose_chain tool ---------------------------------------------

namespace {

using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;

class ComposeChainTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:compose_chain"; }
    std::string displayName() const override { return "Compose capability chain"; }
    std::string description() const override
    {
      return "Deterministic chain composition over the capability relation graph "
             "(no model call): target operator + dataset facts -> ordered steps "
             "(grid alignment first, fact-gated prerequisites next, target last), "
             "skipped edges with reasons, and advisory notes (prerequisites, ADR "
             "0124 reproducibility warnings). Input: {target, facts?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "capability", "graph", "composition", "planning" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema[ "type" ] = "object";
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      schema[ "required" ] = required;
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target[ "type" ] = "string";
      target[ "description" ] = "Target operator id, e.g. rs:spectral_index";
      props[ "target" ] = target;
      Json::Value facts( Json::objectValue );
      facts[ "type" ] = "object";
      facts[ "description" ] = "Dataset facts gating chain edges, e.g. "
                               "{radiometric_state: surface_reflectance, aligned: true}";
      props[ "facts" ] = facts;
      schema[ "properties" ] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema[ "type" ] = "object";
      schema[ "properties" ][ "target" ] = Json::Value( Json::objectValue );
      schema[ "properties" ][ "steps" ] = Json::Value( Json::objectValue );
      schema[ "properties" ][ "skipped" ] = Json::Value( Json::objectValue );
      schema[ "properties" ][ "notes" ] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string target = input.isMember( "target" ) && input[ "target" ].isString()
        ? input[ "target" ].asString()
        : std::string();
      if ( target.empty() )
        return SpatialToolResult::failure( "Provide the target operator id", "INVALID_PARAMETER",
                                           "validation" );
      const Json::Value document =
        composeChain( target, input.isMember( "facts" ) ? input[ "facts" ] : Json::Value() );
      if ( document.isMember( "error" ) )
        return SpatialToolResult::failure( document[ "message" ].asString(), "NOT_SUPPORTED",
                                           "validation" );
      return SpatialToolResult::ok( document );
    }
};

} // namespace

void registerCapabilityCompositionTools()
{
  static std::shared_ptr<SpatialTool> tool = std::make_shared<ComposeChainTool>();
  SpatialToolRegistry::instance().registerTool( tool );
}

} // namespace sicnu::agent::harness
