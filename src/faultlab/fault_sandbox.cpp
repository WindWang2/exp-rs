// fault_sandbox.cpp — the sandbox contract (see header).
#include "fault_sandbox.h"

#include "util/canonical_json.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace sicnu::faultlab
{

namespace
{

std::atomic<unsigned> gSandboxCounter{ 0 };

std::string uniqueSandboxName()
{
    std::random_device device;
    const auto counter = gSandboxCounter.fetch_add( 1 );
    char buffer[96];
    std::snprintf( buffer, sizeof( buffer ), "sicnu-faultlab-%llu-%u-%08x",
                   static_cast<unsigned long long>( device() ), counter,
                   static_cast<unsigned>( device() & 0xFFFFFFFFu ) );
    return buffer;
}

} // namespace

FaultResult<FaultSandbox> FaultSandbox::create( const std::string &root )
{
    std::error_code ec;
    fs::path base = root.empty() ? fs::temp_directory_path( ec ) : fs::path( root );
    if ( ec )
    {
        return makeError<FaultSandbox>( "faultlab.sandbox_failed",
                                        "cannot resolve the sandbox root directory" );
    }

    for ( int attempt = 0; attempt < 8; ++attempt )
    {
        const fs::path candidate = base / uniqueSandboxName();
        if ( fs::create_directory( candidate, ec ) )
        {
            FaultSandbox sandbox;
            sandbox.mPath = candidate.string();
            sandbox.mActive = true;
            return makeOk( std::move( sandbox ) );
        }
        if ( ec && !fs::exists( candidate ) )
        {
            return makeError<FaultSandbox>( "faultlab.sandbox_failed",
                                            "cannot create the sandbox directory: " + ec.message() );
        }
    }
    return makeError<FaultSandbox>( "faultlab.sandbox_failed",
                                    "cannot allocate a unique sandbox directory" );
}

FaultSandbox::~FaultSandbox()
{
    if ( mActive )
    {
        cleanup();
    }
}

FaultSandbox::FaultSandbox( FaultSandbox &&other ) noexcept
  : mPath( std::move( other.mPath ) ),
    mActive( other.mActive )
{
    other.mActive = false;
}

FaultSandbox &FaultSandbox::operator=( FaultSandbox &&other ) noexcept
{
    if ( this != &other )
    {
        if ( mActive )
        {
            cleanup();
        }
        mPath = std::move( other.mPath );
        mActive = other.mActive;
        other.mActive = false;
    }
    return *this;
}

void FaultSandbox::cleanup()
{
    if ( !mActive )
    {
        return;
    }
    std::error_code ec;
    fs::remove_all( mPath, ec );
    mActive = false;
}

bool FaultSandbox::verifyNoResidue() const
{
    std::error_code ec;
    return !fs::exists( mPath, ec ) && !ec;
}

FaultGrid FaultSandbox::copyOf( const FaultGrid &source )
{
    FaultGrid copy;
    copy.width = source.width;
    copy.height = source.height;
    copy.crsId = source.crsId;
    for ( int i = 0; i < 6; ++i )
    {
        copy.geoTransform[i] = source.geoTransform[i];
    }
    copy.hasNoData = source.hasNoData;
    copy.noDataValue = source.noDataValue;
    copy.bands = source.bands; // vector<BandSpec> deep-copies samples
    copy.extras = source.extras;
    return copy;
}

FaultResult<FaultGrid> FaultSandbox::copyWithinBudget( const FaultGrid &source,
                                                       std::uint64_t maxBytes )
{
    if ( source.sampleBytes() > maxBytes || source.sampleBytes() > kFaultLabMaxBytes )
    {
        return makeError<FaultGrid>( "faultlab.budget_exceeded",
                                     "fixture exceeds the sandbox byte budget" );
    }
    return makeOk( copyOf( source ) );
}

} // namespace sicnu::faultlab
