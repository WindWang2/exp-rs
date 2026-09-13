// src/agent/harness/tool_shortlist.cpp
#include "tool_shortlist.h"

#include "capability_catalog.h"
#include "capability_knowledge.h"
#include "../spatial_tools/spatial_tool.h"

#include <json/writer.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

std::string rendered( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

struct ScoredItem
{
    std::string id;
    std::string surface;
    std::string summary;
    int score = 0;
    Json::Value why{Json::arrayValue};
};

void addWhy( ScoredItem &item, const char *reason, const std::string &detail )
{
  // Bounded provenance: an adversarial filter list must not grow one item's
  // why-array without end (review B-10).
  if ( item.why.size() >= 4 )
    return;
  Json::Value entry( Json::objectValue );
  entry["reason"] = reason;
  entry["detail"] = detail;
  item.why.append( entry );
}

/// Filter arrays are bounded too: at most 16 entries, each truncated.
Json::Value boundedFilter( const Json::Value &filters )
{
  Json::Value out( Json::arrayValue );
  if ( !filters.isArray() )
    return out;
  for ( const Json::Value &entry : filters )
  {
    if ( out.size() >= 16 )
      break;
    out.append( entry.isString() ? entry.asString().substr( 0, 64 ) : std::string() );
  }
  return out;
}

/// Closed scoring rules. Every inclusion MUST land at least one why entry —
/// an item with score 0 is excluded (no unexplained padding).
void scoreCandidate( ScoredItem &item, const std::string &intent, const Json::Value &families,
                     const Json::Value &tags )
{
  if ( !intent.empty() )
  {
    const std::string needle = lowered( intent );
    if ( lowered( item.id ).find( needle ) != std::string::npos )
    {
      item.score += 2;
      addWhy( item, "id_evidence", "id mentions '" + intent + "'" );
    }
    if ( lowered( item.summary ).find( needle ) != std::string::npos )
    {
      item.score += 1;
      addWhy( item, "description_evidence", "description mentions '" + intent + "'" );
    }
  }
  for ( const Json::Value &family : families )
  {
    if ( !family.isString() )
      continue;
    if ( lowered( item.summary ).find( lowered( family.asString() ) ) != std::string::npos ||
         lowered( item.id ).find( lowered( family.asString() ) ) != std::string::npos )
    {
      item.score += 2;
      addWhy( item, "family_match", "matches family '" + family.asString() + "'" );
    }
  }
  for ( const Json::Value &tag : tags )
  {
    if ( !tag.isString() )
      continue;
    if ( lowered( item.summary ).find( lowered( tag.asString() ) ) != std::string::npos )
    {
      item.score += 1;
      addWhy( item, "tag_match", "matches tag '" + tag.asString() + "'" );
    }
  }
}

/// Trims the rendered document into the byte budget: drop lowest-score items
/// first (never silently — truncated:true and total_unfiltered record it).
Json::Value renderBounded( std::vector<ScoredItem> scored, const std::string &intent,
                           const Json::Value &families, const Json::Value &tags, int limit )
{
  std::sort( scored.begin(), scored.end(),
             []( const ScoredItem &a, const ScoredItem &b )
             {
               if ( a.score != b.score )
                 return a.score > b.score;
               return a.id < b.id;
             } );
  const int total = static_cast<int>( scored.size() );
  const int capped = std::min( limit, kShortlistMaxLimit );
  if ( static_cast<int>( scored.size() ) > capped )
    scored.resize( capped );

  auto build = [ & ]( int keep ) {
    Json::Value doc( Json::objectValue );
    if ( !intent.empty() )
      doc["intent"] = intent;
    if ( families.isArray() && !families.empty() )
      doc["families"] = families;
    if ( tags.isArray() && !tags.empty() )
      doc["tags"] = tags;
    doc["budget_bytes"] = static_cast<Json::Int64>( kShortlistBudgetBytes );
    doc["total_unfiltered"] = total;
    doc["truncated"] = keep < total;
    Json::Value items( Json::arrayValue );
    for ( int i = 0; i < keep && i < static_cast<int>( scored.size() ); ++i )
    {
      Json::Value entry( Json::objectValue );
      entry["id"] = scored[ i ].id;
      entry["surface"] = scored[ i ].surface;
      entry["summary"] = scored[ i ].summary;
      entry["score"] = scored[ i ].score;
      entry["why_included"] = scored[ i ].why;
      items.append( entry );
    }
    doc["items"] = items;
    // Measure with the field already present, then assign the measured value
    // — the final document is at most a few digits larger than recorded
    // (review B-4); the budget loop re-renders the final form.
    doc["rendered_bytes"] = static_cast<Json::Int64>( 0 );
    const long long measured = static_cast<long long>( rendered( doc ).size() );
    doc["rendered_bytes"] = static_cast<Json::Int64>( measured );
    return doc;
  };

  Json::Value doc = build( static_cast<int>( scored.size() ) );
  auto overBudget = [ & ]()
  { return rendered( doc ).size() > kShortlistBudgetBytes; };
  int keep = static_cast<int>( scored.size() );
  while ( overBudget() && keep > 1 )
  {
    --keep;
    doc = build( keep );
  }
  // Hard-budget fallback: even a single oversized item fits — trim summaries
  // until the page closes under the cap (or is one bare id row).
  while ( overBudget() )
  {
    bool trimmed = false;
    for ( int i = 0; i < keep && i < static_cast<int>( scored.size() ); ++i )
    {
      std::string &summary = scored[ i ].summary;
      if ( summary.size() > 16 )
      {
        summary.resize( summary.size() / 2 );
        trimmed = true;
      }
    }
    doc = build( keep );
    if ( !trimmed )
      break;
  }
  return doc;
}

} // namespace

Json::Value toolShortlist( const std::string &intent, const Json::Value &families,
                           const Json::Value &tags, int limit )
{
  if ( intent.empty() && ( !families.isArray() || families.empty() ) &&
       ( !tags.isArray() || tags.empty() ) )
  {
    Json::Value empty( Json::objectValue );
    empty["items"] = Json::Value( Json::arrayValue );
    empty["truncated"] = false;
    empty["total_unfiltered"] = 0;
    return empty;
  }
  if ( limit <= 0 )
    limit = 8;
  const Json::Value boundedFamilies = boundedFilter( families );
  const Json::Value boundedTags = boundedFilter( tags );
  const std::string boundedIntent = intent.substr( 0, 64 );

  std::vector<ScoredItem> scored;

  // Surface 1: registered spatial tools (fast, read-only, agent-executable).
  for ( const SpatialToolPtr &tool : SpatialToolRegistry::instance().tools() )
  {
    ScoredItem item;
    item.id = tool->name();
    item.surface = "spatial_tool";
    item.summary = tool->description().substr( 0, 200 );
    for ( const std::string &tag : tool->tags() )
    {
      for ( const Json::Value &wanted : tags )
      {
        if ( wanted.isString() && lowered( tag ) == lowered( wanted.asString() ) )
        {
          item.score += 2;
          addWhy( item, "tag_match", "tool tag '" + tag + "'" );
        }
      }
    }
    scoreCandidate( item, boundedIntent, boundedFamilies, boundedTags );
    if ( item.score > 0 )
      scored.push_back( std::move( item ) );
  }

  // Surface 2: capability catalog operators (the 111-op knowledge).
  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  for ( const std::string &operatorId : catalog.entryIds() )
  {
    const Json::Value capability = catalog.capability( operatorId );
    ScoredItem item;
    item.id = operatorId;
    item.surface = "operator";
    item.summary = capability.get( "summary", "" ).asString().substr( 0, 200 );
    if ( item.summary.empty() && capability.isMember( "family" ) )
      item.summary = "family: " + capability["family"].asString();
    if ( !intent.empty() )
    {
      const Json::Value knowledge =
        CapabilityKnowledge::instance().entryForOperator( operatorId, Json::Value() );
      if ( knowledge.isMember( "intents" ) && knowledge["intents"].isArray() )
      {
        for ( const Json::Value &served : knowledge["intents"] )
        {
          if ( served.asString() == intent )
          {
            item.score += 3;
            addWhy( item, "serves_intent", "capability declares intent '" + intent + "'" );
          }
        }
      }
    }
    for ( const Json::Value &family : families )
    {
      if ( family.isString() && capability.get( "family", "" ).asString() == family.asString() )
      {
        item.score += 2;
        addWhy( item, "family_match", "capability family '" + family.asString() + "'" );
      }
    }
    scoreCandidate( item, boundedIntent, Json::Value(), boundedTags );
    if ( item.score > 0 )
      scored.push_back( std::move( item ) );
  }

  return renderBounded( std::move( scored ), boundedIntent, boundedFamilies, boundedTags, limit );
}

Json::Value knowledgeBudgetReport()
{
  Json::Value doc( Json::objectValue );
  Json::Value surfaces( Json::arrayValue );
  auto add = [ &surfaces ]( const char *surface, long long budget, long long measured )
  {
    Json::Value entry( Json::objectValue );
    entry["surface"] = surface;
    entry["budget_bytes"] = static_cast<Json::Int64>( budget );
    entry["measured_bytes"] = static_cast<Json::Int64>( measured );
    entry["within_budget"] = measured <= budget;
    surfaces.append( entry );
  };

  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  const Json::Value manifest = catalog.manifestPage( "", 1, 64 );
  add( "capability_manifest_page", static_cast<long long>( CapabilityCatalog::kManifestBudgetBytes ),
       static_cast<long long>( rendered( manifest ).size() ) );
  const Json::Value errorCatalog = catalog.errorCatalog();
  add( "error_catalog",
       static_cast<long long>( CapabilityCatalog::kErrorCatalogBudgetBytes ),
       static_cast<long long>( rendered( errorCatalog ).size() ) );

  const Json::Value shortlist = toolShortlist( "ndvi", Json::Value(), Json::Value(), 8 );
  add( "tool_shortlist_sample", static_cast<long long>( kShortlistBudgetBytes ),
       static_cast<long long>( shortlist["rendered_bytes"].asInt64() ) );

  doc["surfaces"] = surfaces;
  bool all = true;
  for ( const Json::Value &surface : surfaces )
    all = all && surface.get( "within_budget", false ).asBool();
  doc["all_within_budget"] = all;
  return doc;
}

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

class ToolShortlistTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:tool_shortlist"; }
    std::string displayName() const override { return "Tool Shortlist"; }
    std::string description() const override
    {
      return "Deterministic, budget-bounded shortlist of tools and operators "
             "for one intent — every inclusion carries its reason "
             "(why_included), so prompts never need all 111 operators. "
             "Read-only.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "shortlist", "budget", "discovery" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value intent( Json::objectValue );
      intent["type"] = "string";
      intent["description"] = "Scientific intent (closed vocabulary, e.g. ndvi, change, sar).";
      props["intent"] = intent;
      Json::Value families( Json::objectValue );
      families["type"] = "array";
      families["items"] = Json::Value( Json::stringValue );
      families["description"] = "Capability family filter (e.g. spectral_index, sar).";
      props["families"] = families;
      Json::Value tags( Json::objectValue );
      tags["type"] = "array";
      tags["items"] = Json::Value( Json::stringValue );
      props["tags"] = tags;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "Max items (default 8, hard cap 24).";
      props["limit"] = limit;
      return objectSchema( std::move( props ), Json::Value() );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["items"] = Json::Value( Json::arrayValue );
      props["budget_bytes"] = Json::Value( Json::intValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string intent = input.get( "intent", "" ).asString();
      const Json::Value families = input.get( "families", Json::Value() );
      const Json::Value tags = input.get( "tags", Json::Value() );
      const int limit = input.get( "limit", 8 ).asInt();
      if ( intent.empty() && ( !families.isArray() || families.empty() ) &&
           ( !tags.isArray() || tags.empty() ) )
        return SpatialToolResult::failure(
          "provide 'intent', 'families', or 'tags' to scope the shortlist",
          error_codes::kInvalidParameter, "validation" );
      return SpatialToolResult::ok( toolShortlist( intent, families, tags, limit ) );
    }
};

class KnowledgeBudgetTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:knowledge_budget"; }
    std::string displayName() const override { return "Knowledge Budget Report"; }
    std::string description() const override
    {
      return "Reports every bounded knowledge surface (capability manifest, "
             "error catalog, tool shortlist) with its hard byte budget and "
             "the current measured size. Read-only.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "budget", "knowledge", "diagnostics" };
    }

    Json::Value inputSchema() const override
    {
      return objectSchema( Json::Value( Json::objectValue ), Json::Value() );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["surfaces"] = Json::Value( Json::arrayValue );
      props["all_within_budget"] = Json::Value( Json::booleanValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value & ) override
    {
      return SpatialToolResult::ok( knowledgeBudgetReport() );
    }
};

} // namespace

void registerToolShortlistTools()
{
  SpatialToolRegistry::instance().registerTool( std::make_shared<ToolShortlistTool>() );
  SpatialToolRegistry::instance().registerTool( std::make_shared<KnowledgeBudgetTool>() );
}

} // namespace sicnu::agent::harness
