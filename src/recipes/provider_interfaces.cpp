// src/recipes/provider_interfaces.cpp
#include "recipes/provider_interfaces.h"

#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>

namespace sicnu::recipes {

namespace fs = std::filesystem;

namespace {

/// Bounded read (R14-E audit F5): the sidecar directories are a scan surface;
/// an oversize file is skipped like an unreadable one, never slurped whole
/// into memory.
constexpr std::uintmax_t kMaxSidecarBytes = 16ull * 1024ull * 1024ull;

enum class CappedRead { Ok, Missing, Oversize };

// #1648: refuse on the bytes actually read, not only the stat. A failed
// stat or a file that grows after it used to fall through to rdbuf().
CappedRead readCapped( const fs::path &path, std::uintmax_t cap, std::string &body )
{
  body.clear();
  std::error_code ec;
  const std::uintmax_t size = fs::file_size( path, ec );
  if ( !ec && size > cap )
    return CappedRead::Oversize;
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return CappedRead::Missing;
  if ( !ec )
    body.reserve( static_cast<std::size_t>( size ) );
  char chunk[8192];
  while ( body.size() <= cap )
  {
    in.read( chunk, static_cast<std::streamsize>( sizeof chunk ) );
    const auto n = in.gcount();
    if ( n > 0 )
    {
      const auto add = static_cast<std::size_t>( n );
      if ( body.size() > cap || add > cap - body.size() )
      {
        body.clear();
        return CappedRead::Oversize;
      }
      body.append( chunk, add );
    }
    if ( !in )
      break;
  }
  if ( in.bad() )
  {
    body.clear();
    return CappedRead::Missing;
  }
  return CappedRead::Ok;
}

/// Collect operator ids from one JSON document: object-with-"id" or
/// array-of-objects-with-"id". `family:*` ids are family defaults, not
/// operators — skipped.
void collectDocIds( const Json::Value &doc, std::vector<std::string> &ids )
{
  auto take = [&ids]( const Json::Value &v )
  {
    // isString guard: asString() on a non-string id raises Json::LogicError,
    // and sidecar directories are a scan surface like the recipe registry.
    if ( !v[ "id" ].isString() )
      return;
    const std::string id = v[ "id" ].asString();
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
    std::string text;
    if ( readCapped( entry.path(), kMaxSidecarBytes, text ) != CappedRead::Ok )
      continue;

    Json::Value doc;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    // Pinned + caught (scientific_recipe.cpp discipline): jsoncpp THROWS on a
    // stackLimit violation, and sidecar directories are untrusted scan input
    // — an over-deep document is skipped like an unparseable one (#1607).
    builder["stackLimit"] = 256;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    try
    {
      if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &errors ) )
        continue;
    }
    catch ( const std::exception & )
    {
      continue;
    }
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
