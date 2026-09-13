// src/processing/algorithms/spectral_wavelength.cpp — wavelength grid helpers
#include "spectral_wavelength.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace SpectralWavelength
{
namespace
{

std::string lowerAscii( std::string s )
{
    for ( char &c : s )
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    return s;
}

/// True for the micro-meter unit spellings we accept (micro sign U+00B5 in
/// UTF-8 is 0xC2 0xB5; Greek mu U+03BC is 0xCE 0xBC).
bool isMicrometerUnits( const std::string &units )
{
    const std::string u = lowerAscii( units );
    if ( u == "um" || u == "micrometer" || u == "micrometers" || u == "micron"
         || u == "microns" )
        return true;
    const std::string microUtf8 = "\xc2\xb5";
    const std::string muUtf8 = "\xce\xbc";
    if ( u.size() == microUtf8.size() + 1u && u.compare( 0, microUtf8.size(), microUtf8 ) == 0
         && u.back() == 'm' )
        return true;
    if ( u.size() == muUtf8.size() + 1u && u.compare( 0, muUtf8.size(), muUtf8 ) == 0
         && u.back() == 'm' )
        return true;
    return false;
}

Status normalizePair( const std::pair<double, std::string> &pair, float *outNm )
{
    if ( !std::isfinite( pair.first ) )
        return Status::NonFinite;
    const std::string units = pair.second;
    if ( !units.empty() && lowerAscii( units ) != "nm"
         && lowerAscii( units ) != "nanometer" && lowerAscii( units ) != "nanometers"
         && !isMicrometerUnits( units ) )
        return Status::UnknownUnits;
    if ( isMicrometerUnits( units ) )
        *outNm = static_cast<float>( pair.first * 1000.0 );
    else
        *outNm = static_cast<float>( pair.first );
    if ( !std::isfinite( *outNm ) )
        return Status::NonFinite;
    return Status::Ok;
}

} // namespace

const char *statusText( Status status )
{
    switch ( status )
    {
        case Status::Ok:
            return "ok";
        case Status::Empty:
            return "no band carries WAVELENGTH metadata";
        case Status::Partial:
            return "only some bands carry WAVELENGTH metadata";
        case Status::NonMonotonic:
            return "band centers are not strictly increasing";
        case Status::UnknownUnits:
            return "unsupported WAVELENGTH_UNITS (expected nm or um)";
        case Status::NonFinite:
            return "unparsable or non-finite wavelength value";
        case Status::SizeMismatch:
            return "FWHM count differs from center count";
    }
    return "unknown status";
}

bool normalizeToNm( double value, const std::string &units, float *outNm )
{
    if ( !outNm )
        return false;
    return normalizePair( { value, units }, outNm ) == Status::Ok;
}

Status gridFromBandValues( const std::vector<std::pair<double, std::string>> &wavelengths,
                           const std::vector<std::pair<double, std::string>> &fwhm,
                           Grid *out )
{
    if ( !out || wavelengths.empty() )
        return Status::Empty;

    Grid grid;
    grid.centersNm.resize( wavelengths.size() );
    for ( size_t i = 0; i < wavelengths.size(); ++i )
    {
        const Status s = normalizePair( wavelengths[i], &grid.centersNm[i] );
        if ( s != Status::Ok )
            return s;
    }
    if ( grid.centersNm.front() <= 0.0f )
        return Status::NonFinite; // centers must be positive (documented)
    for ( size_t i = 1; i < grid.centersNm.size(); ++i )
        if ( !( grid.centersNm[i] > grid.centersNm[i - 1] ) )
            return Status::NonMonotonic;

    if ( !fwhm.empty() )
    {
        if ( fwhm.size() != wavelengths.size() )
            return Status::SizeMismatch;
        grid.fwhmNm.resize( fwhm.size() );
        for ( size_t i = 0; i < fwhm.size(); ++i )
        {
            const Status s = normalizePair( fwhm[i], &grid.fwhmNm[i] );
            if ( s != Status::Ok )
                return s;
            if ( grid.fwhmNm[i] <= 0.0f )
                return Status::NonFinite;
        }
    }

    *out = std::move( grid );
    return Status::Ok;
}

bool rangesOverlap( const Grid &a, const Grid &b, std::string *reason )
{
    const float lo = std::max( a.minNm(), b.minNm() );
    const float hi = std::min( a.maxNm(), b.maxNm() );
    if ( !( hi > lo ) )
    {
        if ( reason )
            *reason = "wavelength ranges are disjoint: [" + std::to_string( a.minNm() ) + ", "
                      + std::to_string( a.maxNm() ) + "] nm vs [" + std::to_string( b.minNm() )
                      + ", " + std::to_string( b.maxNm() ) + "] nm";
        return false;
    }
    return true;
}

} // namespace SpectralWavelength
