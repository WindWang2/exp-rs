// src/recipes/provider_interfaces.cpp
#include "recipes/provider_interfaces.h"

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace sicnu::recipes {

namespace fs = std::filesystem;

namespace {

/// Collect operator ids from one JSON document: object-with-"id" or
/// array-of-objects-with-"id". `family:*` ids are family defaults, not
/// operators — skipped.
void collectDocIds( const Json::Value &doc, std::vector<std::string> &ids )
{
  auto take = [&ids]( const Json::Value &v )
  {
    const std::string id = v.get( "id", "" ).asString();
    if ( !id.empty() && id.rfind( "family:", 0 ) != 0 )
      ids.push_back( id );
  };
  if ( doc.isObject() )
    take( doc );
  else if ( doc.isArray() )
    for ( const auto &item : doc )
      if ( item.isObject() )
        take( item );
}

/// Collect operator ids from a directory of sidecar JSONs. Non-recursive;
/// missing dir → empty. Deterministic order.
void collectIds( const fs::path &dir, std::vector<std::string> &ids )
{
  std::error_code ec;
  if ( dir.empty() || !fs::is_directory( dir, ec ) )
    return;
  for ( const auto &entry : fs::directory_iterator( dir, ec ) )
  {
    if ( ec )
      return;
    if ( !entry.is_regular_file() || entry.path().extension() != ".json" )
      continue;
    std::ifstream in( entry.path() );
    if ( !in )
      continue;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    Json::Value doc;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) )
      continue;
    collectDocIds( doc, ids );
  }
}

} // namespace

SidecarOperatorCatalog::SidecarOperatorCatalog( const std::string &directory )
  : SidecarOperatorCatalog( std::vector<std::string>{ directory } )
{
}

SidecarOperatorCatalog::SidecarOperatorCatalog( std::vector<std::string> directories )
{
  for ( const auto &directory : directories )
  {
    collectIds( fs::path( directory ), mIds );
    // The D8 capability sidecars live in a nested dir — fold them in so the
    // catalog covers the full operator set, not just the flat files.
    collectIds( fs::path( directory ) / "capability", mIds );
  }
  std::sort( mIds.begin(), mIds.end() );
  mIds.erase( std::unique( mIds.begin(), mIds.end() ), mIds.end() );
}

bool SidecarOperatorCatalog::hasOperator( const std::string &operatorId ) const
{
  return std::binary_search( mIds.begin(), mIds.end(), operatorId );
}

std::vector<std::string> SidecarOperatorCatalog::paramNames( const std::string & ) const
{
  // Sidecars don't carry an authoritative param-name list; the live registry
  // adapter is the future provider of that. Empty = "unknown", never "none".
  return {};
}

bool FakeOperatorCatalog::hasOperator( const std::string &operatorId ) const
{
  return std::find( mIds.begin(), mIds.end(), operatorId ) != mIds.end();
}

std::vector<std::string> FakeOperatorCatalog::paramNames( const std::string &operatorId ) const
{
  const auto it = mParams.find( operatorId );
  return it == mParams.end() ? std::vector<std::string>{} : it->second;
}

} // namespace sicnu::recipes
