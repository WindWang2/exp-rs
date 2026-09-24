// src/verify_adapters/provenance_sidecar_view.cpp
#include "verify_adapters/provenance_sidecar_view.h"

#include "bounded_io.h"

#include <filesystem>

namespace sicnu::verify_adapters
{

std::string provenanceSidecarPathFor( const std::string &productPath )
{
    return productPath + ".prov.json";
}

namespace
{

ProvenanceReadResult classifyEnvelope( const std::string &path, const char *expectedSchema,
                                       const char *expectedKind, const char *expectedVersion )
{
    ProvenanceReadResult result;
    const IoResult read = readFileBounded( path, kMaxAdapterDocumentBytes );
    if ( read.failure != IoFailure::None )
    {
        // Distinguish "nothing there" (the crash window the writer contract
        // can leave behind) from "there but unreadable".
        if ( read.failure == IoFailure::Missing )
        {
            result.status = ProvenanceReadStatus::Missing;
            result.detail = "record '" + path + "' does not exist";
        }
        else
        {
            result.status = ProvenanceReadStatus::Unreadable;
            result.detail = "record '" + path + "' cannot be read: " + read.detail;
        }
        return result;
    }
    std::string error;
    const std::optional<Json::Value> parsed = parseJsonBounded( read.text, error );
    if ( !parsed )
    {
        result.status = ProvenanceReadStatus::Unreadable;
        result.detail = "record '" + path + "' is not parseable JSON: " + error;
        return result;
    }
    if ( expectedKind )
    {
        const Json::Value &kind = ( *parsed )["kind"];
        const Json::Value &version = ( *parsed )["version"];
        if ( !kind.isString() || kind.asString() != expectedKind || !version.isString() ||
             version.asString() != expectedVersion )
        {
            result.status = ProvenanceReadStatus::ForeignEnvelope;
            result.detail = "record '" + path + "' carries kind/version '" +
                            ( kind.isString() ? kind.asString() : "" ) + "'/'" +
                            ( version.isString() ? version.asString() : "" ) + "'";
            return result;
        }
    }
    else
    {
        const Json::Value &schema = ( *parsed )["schema"];
        if ( !schema.isString() || schema.asString() != expectedSchema )
        {
            result.status = ProvenanceReadStatus::ForeignEnvelope;
            result.detail = "record '" + path + "' carries schema '" +
                            ( schema.isString() ? schema.asString() : "" ) + "'";
            return result;
        }
    }
    result.status = ProvenanceReadStatus::Ok;
    result.document = std::move( *parsed );
    return result;
}

} // namespace

ProvenanceReadResult readProvenanceSidecar( const std::string &productPath )
{
    return classifyEnvelope( provenanceSidecarPathFor( productPath ), kProvSidecarSchema,
                             nullptr, nullptr );
}

ProvenanceReadResult readRunProvenance( const std::string &runId,
                                        const std::vector<std::string> &runDirectories )
{
    ProvenanceReadResult result; // Missing when no candidate file exists anywhere
    result.detail = "provenance_<runId>.json not found in any run directory";
    for ( const std::string &dir : runDirectories )
    {
        // workflow_provenance writes provenance_<runId>.json beside the run.
        if ( dir.empty() || !pathExists( dir ) )
            continue;
        const std::string path =
            ( pathFromUtf8( dir ) / ( "provenance_" + runId + ".json" ) ).generic_string();
        ProvenanceReadResult candidate =
            classifyEnvelope( path, nullptr, kRunProvenanceKind, kRunProvenanceVersion );
        if ( candidate.status != ProvenanceReadStatus::Missing )
            return candidate;
    }
    return result;
}

SidecarProvenanceView::SidecarProvenanceView( std::vector<std::string> runDirectories )
    : mRunDirectories( std::move( runDirectories ) )
{
    if ( mRunDirectories.empty() )
        mRunDirectories.push_back( "." );
}

std::optional<Json::Value> SidecarProvenanceView::provenanceForPath( const std::string &path )
{
    const ProvenanceReadResult read = readProvenanceSidecar( path );
    if ( read.status != ProvenanceReadStatus::Ok )
        return std::nullopt;
    return read.document;
}

std::optional<Json::Value> SidecarProvenanceView::provenanceForRun( const std::string &runId )
{
    if ( runId.empty() )
        return std::nullopt;
    const ProvenanceReadResult read = readRunProvenance( runId, mRunDirectories );
    if ( read.status != ProvenanceReadStatus::Ok )
        return std::nullopt;
    return read.document;
}

} // namespace sicnu::verify_adapters
