/***************************************************************************
  geospatial/io/param_guard.cpp
  Geospatial I/O, COG & Interchange 11.0 — operator-boundary path guards.
 ***************************************************************************/

#include "geospatial/io/param_guard.h"

#include <filesystem>
#include <system_error>
#include "platform/portable.h"

namespace sicnu::geo::io
{
namespace
{

CheckedPath buildChecked( const ResourceUri &uri )
{
  CheckedPath checked;
  checked.raw = uri.raw;
  checked.canonical = uri.canonical();
  checked.display = uri.display();
  checked.isLocalPayload = uri.isLocalPayload();
  checked.isRemote = uri.isRemote();
  checked.longPathSpelling = ResourceUri::toWindowsLongPath( uri.raw );
  return checked;
}

} // namespace

CheckedPath checkSourcePath( const std::string &raw )
{
  if ( raw.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "source path parameter is empty" );
  const ResourceUri uri = ResourceUri::parse( raw );
  if ( uri.kind == ResourceKind::Invalid )
  {
    Json::Value details;
    details["reason"] = uri.parseReason;
    details["display"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "source path parameter cannot be classified", details );
  }
  return buildChecked( uri );
}

CheckedPath checkTargetPath( const std::string &raw )
{
  if ( raw.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "target path parameter is empty" );
  ResourceUri uri = ResourceUri::parse( raw );
  if ( uri.kind == ResourceKind::Invalid )
  {
    // The parser's existence requirement is an INPUT semantic: an output
    // target legitimately does not exist yet. A local path that is absent
    // classifies by shape here; anything else stays a refusal.
    if ( uri.parseReason != "local path does not exist" )
    {
      Json::Value details;
      details["reason"] = uri.parseReason;
      details["display"] = uri.display();
      throw GeoError( ErrorCode::InvalidArgument, "target path parameter cannot be classified", details );
    }
    uri.kind = ResourceKind::LocalFile;
  }
  if ( uri.kind == ResourceKind::LocalFile || uri.kind == ResourceKind::LocalDirectory )
  {
    // The staged pipeline stages NEXT TO the target: its directory must
    // already exist (we never create directories implicitly). /vsimem has no
    // directory semantics.
    const std::string &path = uri.raw;
    if ( path.rfind( "/vsimem/", 0 ) != 0 )
    {
      const std::size_t lastSlash = path.find_last_of( "/\\" );
      const std::string parent = lastSlash == std::string::npos
                                   ? std::string( "." )
                                   : path.substr( 0, lastSlash == 0 ? 1 : lastSlash );
      std::error_code ec;
      if ( !std::filesystem::is_directory( sicnu::portable::pathFromUtf8( parent ), ec ) || ec )
      {
        Json::Value details;
        details["reason"] = "target_directory_missing";
        details["parent_display"] = ResourceUri::parse( parent ).display();
        throw GeoError( ErrorCode::InvalidArgument,
                        "target directory does not exist; io: never creates directories implicitly", details );
      }
    }
  }
  if ( uri.isRemote() )
  {
    // Writes go through the staged local-atomic pipeline; pushing bytes to a
    // network store needs the fabric mirror/object-store seam, which declares
    // credentials explicitly. An io:* operator must never leak an implicit
    // upload, so this is a typed refusal, not a best-effort local write.
    Json::Value details;
    details["reason"] = "remote_write_offline_policy";
    details["display"] = uri.display();
    throw GeoError( ErrorCode::Unsupported, "target path is a network resource; "
                                            "remote writes are refused at the io: boundary", details );
  }
  if ( uri.kind == ResourceKind::VirtualDataset || uri.kind == ResourceKind::StacAsset )
  {
    Json::Value details;
    details["reason"] = "read_only_projection_kind";
    details["kind"] = resourceKindName( uri.kind );
    details["display"] = uri.display();
    throw GeoError( ErrorCode::Unsupported, "target path names a read-only projection, not a storage target", details );
  }
  return buildChecked( uri );
}

} // namespace sicnu::geo::io
