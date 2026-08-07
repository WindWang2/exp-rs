// src/processing/algorithms/spectral_library.cpp — spectral library domain
#include "spectral_library.h"

#include "spectral_classification.h"
#include "spectral_resampling.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace SpectralLibrary
{

namespace
{

/// Reads a JSON array of numbers into a float vector; false on non-array or
/// non-numeric content.
bool readFloatArray( const QJsonValue &value, std::vector<float> *out )
{
    if ( !value.isArray() )
        return false;
    const QJsonArray array = value.toArray();
    out->resize( static_cast<size_t>( array.size() ) );
    for ( int i = 0; i < array.size(); ++i )
    {
        if ( !array[i].isDouble() )
            return false;
        ( *out )[static_cast<size_t>( i )] = static_cast<float>( array[i].toDouble() );
    }
    return true;
}

QJsonArray toJsonArray( const std::vector<float> &values )
{
    QJsonArray array;
    for ( float v : values )
        array.append( v );
    return array;
}

} // namespace

QJsonObject Library::toJson() const
{
    QJsonObject root;
    root.insert( QStringLiteral( "format" ), QStringLiteral( "sicnu-spectral-library" ) );
    root.insert( QStringLiteral( "version" ), 1 );

    const int bands = bandCount();
    if ( bands > 0 )
        root.insert( QStringLiteral( "bandCount" ), bands );

    const std::vector<float> wl = wavelengths();
    if ( !wl.empty() )
        root.insert( QStringLiteral( "wavelengths" ), toJsonArray( wl ) );

    QJsonArray entryArray;
    for ( const Entry &entry : entries )
    {
        QJsonObject e;
        e.insert( QStringLiteral( "name" ), entry.name );
        if ( !entry.material.isEmpty() )
            e.insert( QStringLiteral( "material" ), entry.material );
        if ( !entry.source.isEmpty() )
            e.insert( QStringLiteral( "source" ), entry.source );
        e.insert( QStringLiteral( "spectrum" ), toJsonArray( entry.spectrum ) );
        entryArray.append( e );
    }
    root.insert( QStringLiteral( "entries" ), entryArray );
    return root;
}

bool Library::fromJson( const QJsonObject &json, Library *out, QString *errorMessage )
{
    if ( !out )
        return false;

    Library library;
    const QJsonArray entryArray = json.value( QStringLiteral( "entries" ) ).toArray();
    int bandCount = -1;
    for ( const QJsonValue &value : entryArray )
    {
        const QJsonObject e = value.toObject();
        Entry entry;
        entry.name = e.value( QStringLiteral( "name" ) ).toString();
        if ( entry.name.isEmpty() )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entry is missing a name" );
            return false;
        }
        entry.material = e.value( QStringLiteral( "material" ) ).toString();
        entry.source = e.value( QStringLiteral( "source" ) ).toString();
        if ( !readFloatArray( e.value( QStringLiteral( "spectrum" ) ), &entry.spectrum )
             || entry.spectrum.empty() )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entry '%1' has an invalid spectrum" )
                                    .arg( entry.name );
            return false;
        }
        if ( bandCount < 0 )
            bandCount = static_cast<int>( entry.spectrum.size() );
        else if ( static_cast<int>( entry.spectrum.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library entries have inconsistent band counts" );
            return false;
        }
        library.entries.append( std::move( entry ) );
    }

    // Optional shared wavelength grid.
    std::vector<float> wl;
    if ( json.contains( QStringLiteral( "wavelengths" ) )
         && readFloatArray( json.value( QStringLiteral( "wavelengths" ) ), &wl ) )
    {
        if ( bandCount >= 0 && static_cast<int>( wl.size() ) != bandCount )
        {
            if ( errorMessage )
                *errorMessage = QStringLiteral( "Spectral library wavelength grid size "
                                                "does not match the band count" );
            return false;
        }
        for ( Entry &entry : library.entries )
            entry.wavelengths = wl;
    }

    *out = std::move( library );
    return true;
}

bool Library::save( const QString &path, QString *errorMessage ) const
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot write spectral library: %1" ).arg( path );
        return false;
    }
    file.write( QJsonDocument( toJson() ).toJson( QJsonDocument::Compact ) );
    return true;
}

bool Library::load( const QString &path, Library *out, QString *errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open spectral library: %1" ).arg( path );
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
    if ( !doc.isObject() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Spectral library file is not a JSON object: %1" ).arg( path );
        return false;
    }
    return fromJson( doc.object(), out, errorMessage );
}

int Library::bandCount() const
{
    if ( entries.isEmpty() )
        return 0;
    const int bands = static_cast<int>( entries.first().spectrum.size() );
    for ( const Entry &entry : entries )
    {
        if ( static_cast<int>( entry.spectrum.size() ) != bands )
            return 0;
    }
    return bands;
}

std::vector<float> Library::wavelengths() const
{
    std::vector<float> result;
    if ( entries.isEmpty() )
        return result;
    result = entries.first().wavelengths;
    for ( const Entry &entry : entries )
    {
        if ( entry.wavelengths != result )
            return {};
    }
    return result;
}

} // namespace SpectralLibrary

namespace SpectralLibrary
{

std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                       const Library &library,
                                       float nodata )
{
    return matchSpectrum( spectrum, {}, library, nodata );
}

std::vector<MatchScore> matchSpectrum( const std::vector<float> &spectrum,
                                       const std::vector<float> &spectrumWavelengths,
                                       const Library &library,
                                       float nodata )
{
    std::vector<MatchScore> scores;
    if ( spectrum.empty() )
        return scores;

    const int bands = static_cast<int>( spectrum.size() );
    for ( int i = 0; i < library.entries.size(); ++i )
    {
        const Entry &entry = library.entries.at( i );
        const std::vector<float> &entryRef = entry.spectrum;
        // Test spectrum used for scoring: the original, or a wavelength-
        // resampled copy when band counts differ and both sides carry grids.
        const float *testPtr = spectrum.data();
        int testBands = bands;
        std::vector<float> resampledSpectrum;
        bool resampled = false;
        if ( static_cast<int>( entryRef.size() ) != bands )
        {
            if ( spectrumWavelengths.size() == spectrum.size()
                 && entry.wavelengths.size() == entryRef.size() )
            {
                resampledSpectrum.resize( entryRef.size() );
                if ( SpectralResampling::resampleSpectrum(
                       spectrum.data(), spectrumWavelengths.data(), bands,
                       entry.wavelengths.data(), static_cast<int>( entryRef.size() ),
                       resampledSpectrum.data() ) )
                {
                    bool hasOutOfRange = false;
                    for ( float v : resampledSpectrum )
                    {
                        if ( std::isnan( v ) )
                        {
                            hasOutOfRange = true;
                            break;
                        }
                    }
                    if ( !hasOutOfRange )
                    {
                        testPtr = resampledSpectrum.data();
                        testBands = static_cast<int>( entryRef.size() );
                        resampled = true;
                    }
                }
            }
            if ( !resampled )
                continue; // cannot compare (band mismatch, no wavelengths)
        }

        MatchScore score;
        score.entryIndex = i;
        score.name = entry.name;
        score.material = entry.material;
        score.resampled = resampled;
        score.angleDegrees =
            SpectralClassification::spectralAngle( testPtr, entryRef.data(),
                                                   testBands, nodata )
            * ( 180.0 / std::acos( -1.0 ) );
        score.divergence =
            SpectralClassification::spectralDivergence( testPtr, entryRef.data(),
                                                        testBands, nodata );
        scores.push_back( std::move( score ) );
    }

    // Ascending SAM angle; undefined (NaN) angles sort last.
    std::stable_sort( scores.begin(), scores.end(),
                      []( const MatchScore &a, const MatchScore &b )
    {
        const auto key = []( double v )
        {
            return std::isnan( v ) ? std::numeric_limits<double>::infinity() : v;
        };
        return key( a.angleDegrees ) < key( b.angleDegrees );
    } );
    return scores;
}

} // namespace SpectralLibrary
