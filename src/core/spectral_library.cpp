// src/core/spectral_library.cpp — D13 spectral library retriever
#include "spectral_library.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace exp_spectral
{
  namespace
  {
    constexpr double kPi = 3.14159265358979323846;
    /// FWHM → Gaussian sigma: sigma = FWHM / (2·sqrt(2 ln 2)).
    constexpr double kFwhmToSigma = 2.3548200450309493;
    constexpr int kJsonFormatVersion = 1;

    bool isConsistentEntry( const SpectralLibraryEntry &e )
    {
      if ( e.name.isEmpty() || e.spectrum.empty() || e.spectrum.size() != e.wavelengthsNm.size()
           || e.wavelengthsNm.size() != e.fwhmNm.size() )
        return false;
      // The SRF resampler bisects into the wavelength grid: it must be
      // strictly increasing or the windows are silently wrong.
      for ( size_t i = 1; i < e.wavelengthsNm.size(); ++i )
      {
        if ( !( e.wavelengthsNm[i] > e.wavelengthsNm[i - 1] ) )
          return false;
      }
      return true;
    }

    /// In-memory admission rule. File-loaded entries always carry a full grid
    /// (file schema v2), but SAM matching needs only the spectrum vector, so
    /// grid-free entries (both grids absent) are valid for in-memory
    /// libraries — including zero-norm spectra, which score at the maximum
    /// angle. A present grid must still be shape-correct and strictly
    /// increasing so resampling over it stays sound.
    bool isAdmissibleEntry( const SpectralLibraryEntry &e )
    {
      if ( e.name.isEmpty() || e.spectrum.empty() )
        return false;
      const bool gridless = e.wavelengthsNm.empty() && e.fwhmNm.empty();
      if ( gridless )
        return true;
      if ( e.spectrum.size() != e.wavelengthsNm.size()
           || e.wavelengthsNm.size() != e.fwhmNm.size() )
        return false;
      for ( size_t i = 1; i < e.wavelengthsNm.size(); ++i )
      {
        if ( !( e.wavelengthsNm[i] > e.wavelengthsNm[i - 1] ) )
          return false;
      }
      return true;
    }

    double dotProduct( const float *a, const float *b, size_t n )
    {
      double acc = 0.0;
      for ( size_t i = 0; i < n; ++i )
        acc += static_cast<double>( a[i] ) * static_cast<double>( b[i] );
      return acc;
    }

    double normOf( const float *a, size_t n )
    {
      return std::sqrt( std::max( 0.0, dotProduct( a, a, n ) ) );
    }

    double floatNorm( const std::vector<float> &v )
    {
      return v.empty() ? 0.0 : normOf( v.data(), v.size() );
    }

    QJsonArray floatsToJson( const std::vector<float> &values )
    {
      QJsonArray array;
      for ( float v : values )
        array.append( static_cast<double>( v ) );
      return array;
    }

    std::vector<float> jsonToFloats( const QJsonValue &value, bool *ok )
    {
      std::vector<float> out;
      if ( !value.isArray() )
      {
        *ok = false;
        return out;
      }
      const QJsonArray array = value.toArray();
      out.reserve( array.size() );
      for ( const QJsonValue &v : array )
      {
        if ( !v.isDouble() )
        {
          *ok = false;
          return {};
        }
        out.push_back( static_cast<float>( v.toDouble() ) );
      }
      *ok = true;
      return out;
    }
  } // namespace

  SpectralLibrary SpectralLibrary::fromJson( const QJsonObject &root, QString *errorMessage )
  {
    SpectralLibrary library;
    const QJsonValue entriesValue = root.value( QStringLiteral( "entries" ) );
    if ( !entriesValue.isArray() )
    {
      if ( errorMessage )
        *errorMessage = QStringLiteral( "spectral library JSON lacks an \"entries\" array" );
      return library;
    }

    const QJsonArray entries = entriesValue.toArray();
    for ( const QJsonValue &v : entries )
    {
      if ( !v.isObject() )
      {
        if ( errorMessage )
          *errorMessage = QStringLiteral( "spectral library entry is not an object" );
        library.m_entries.clear();
        return library;
      }
      const QJsonObject obj = v.toObject();

      SpectralLibraryEntry entry;
      entry.id = obj.value( QStringLiteral( "id" ) ).toString();
      entry.name = obj.value( QStringLiteral( "name" ) ).toString();
      entry.materialClass = obj.value( QStringLiteral( "materialClass" ) ).toString();
      entry.source = obj.value( QStringLiteral( "source" ) ).toString();

      bool ok = false;
      entry.spectrum = jsonToFloats( obj.value( QStringLiteral( "spectrum" ) ), &ok );
      if ( !ok )
      {
        if ( errorMessage )
          *errorMessage = QStringLiteral( "entry \"%1\" has a malformed spectrum" ).arg( entry.name );
        library.m_entries.clear();
        return library;
      }
      // Wavelength/FWHM grids are optional; when present they must match.
      const QJsonValue wlValue = obj.value( QStringLiteral( "wavelengthsNm" ) );
      const QJsonValue fwhmValue = obj.value( QStringLiteral( "fwhmNm" ) );
      if ( wlValue.isArray() )
      {
        entry.wavelengthsNm = jsonToFloats( wlValue, &ok );
        if ( !ok || entry.wavelengthsNm.size() != entry.spectrum.size() )
        {
          if ( errorMessage )
            *errorMessage = QStringLiteral( "entry \"%1\" wavelength grid size mismatch" ).arg( entry.name );
          library.m_entries.clear();
          return library;
        }
      }
      if ( fwhmValue.isArray() )
      {
        entry.fwhmNm = jsonToFloats( fwhmValue, &ok );
        if ( !ok || entry.fwhmNm.size() != entry.spectrum.size() )
        {
          if ( errorMessage )
            *errorMessage = QStringLiteral( "entry \"%1\" FWHM grid size mismatch" ).arg( entry.name );
          library.m_entries.clear();
          return library;
        }
      }

      if ( !isConsistentEntry( entry ) )
      {
        if ( errorMessage )
          *errorMessage = QStringLiteral( "entry \"%1\" is empty or inconsistent" ).arg( entry.name );
        library.m_entries.clear();
        return library;
      }
      library.m_entries.push_back( std::move( entry ) );
    }
    return library;
  }

  QJsonObject SpectralLibrary::toJson() const
  {
    QJsonObject root;
    root.insert( QStringLiteral( "format" ), kJsonFormatVersion );
    QJsonArray entries;
    for ( const SpectralLibraryEntry &e : m_entries )
    {
      QJsonObject obj;
      obj.insert( QStringLiteral( "id" ), e.id );
      obj.insert( QStringLiteral( "name" ), e.name );
      obj.insert( QStringLiteral( "materialClass" ), e.materialClass );
      obj.insert( QStringLiteral( "source" ), e.source );
      obj.insert( QStringLiteral( "spectrum" ), floatsToJson( e.spectrum ) );
      if ( !e.wavelengthsNm.empty() )
        obj.insert( QStringLiteral( "wavelengthsNm" ), floatsToJson( e.wavelengthsNm ) );
      if ( !e.fwhmNm.empty() )
        obj.insert( QStringLiteral( "fwhmNm" ), floatsToJson( e.fwhmNm ) );
      entries.append( obj );
    }
    root.insert( QStringLiteral( "entries" ), entries );
    return root;
  }

  bool SpectralLibrary::loadFromFile( const QString &filePath, QString *errorMsg )
  {
    QFile file( filePath );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
      if ( errorMsg )
        *errorMsg = QStringLiteral( "cannot open spectral library: %1" ).arg( filePath );
      return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
    {
      if ( errorMsg )
        *errorMsg = QStringLiteral( "spectral library JSON parse failure: %1" ).arg( parseError.errorString() );
      return false;
    }
    QString parseMessage;
    SpectralLibrary parsed = fromJson( doc.object(), &parseMessage );
    if ( !parseMessage.isEmpty() )
    {
      if ( errorMsg )
        *errorMsg = parseMessage;
      return false;
    }
    m_entries = std::move( parsed.m_entries );
    return true;
  }

  bool SpectralLibrary::saveToFile( const QString &filePath, QString *errorMsg ) const
  {
    QFile file( filePath );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
      if ( errorMsg )
        *errorMsg = QStringLiteral( "cannot write spectral library: %1" ).arg( filePath );
      return false;
    }
    const QJsonDocument doc( toJson() );
    if ( file.write( doc.toJson( QJsonDocument::Indented ) ) < 0 )
    {
      if ( errorMsg )
        *errorMsg = QStringLiteral( "spectral library write failure: %1" ).arg( filePath );
      return false;
    }
    return true;
  }

  bool SpectralLibrary::addEntry( const SpectralLibraryEntry &entry, QString *errorMessage )
  {
    if ( !isAdmissibleEntry( entry ) )
    {
      if ( errorMessage )
        *errorMessage = QStringLiteral( "refusing inconsistent entry \"%1\"" ).arg( entry.name );
      return false;
    }
    m_entries.push_back( entry );
    return true;
  }

  std::vector<SpectralMatchCandidate> SpectralLibrary::matchSpectrum( const float *querySpectrum,
                                                                      size_t bandCount, size_t topK,
                                                                      double maxAngleRad ) const
  {
    std::vector<SpectralMatchCandidate> matches;
    if ( querySpectrum == nullptr || bandCount == 0 )
      return matches;

    const double queryNorm = normOf( querySpectrum, bandCount );

    for ( const SpectralLibraryEntry &entry : m_entries )
    {
      if ( entry.spectrum.size() != bandCount )
        continue; // incomparable band geometry: skipped, never guessed

      SpectralMatchCandidate candidate;
      candidate.id = entry.id;
      candidate.name = entry.name;
      candidate.materialClass = entry.materialClass;

      const double entryNorm = floatNorm( entry.spectrum );
      if ( queryNorm <= 0.0 || entryNorm <= 0.0 )
      {
        candidate.spectralAngleRad = kPi / 2.0; // deterministic worst match
        candidate.correlation = 0.0;
        candidate.euclideanDistance = std::numeric_limits<double>::infinity();
      }
      else
      {
        const double cosTheta = std::clamp( dotProduct( querySpectrum, entry.spectrum.data(), bandCount )
                                              / ( queryNorm * entryNorm ),
                                            -1.0, 1.0 );
        candidate.spectralAngleRad = std::clamp( std::acos( cosTheta ), 0.0, kPi / 2.0 );

        // Pearson correlation of the paired band values.
        const double n = static_cast<double>( bandCount );
        double meanQ = 0.0, meanR = 0.0;
        for ( size_t i = 0; i < bandCount; ++i )
        {
          meanQ += querySpectrum[i];
          meanR += entry.spectrum[i];
        }
        meanQ /= n;
        meanR /= n;
        double cov = 0.0, varQ = 0.0, varR = 0.0;
        for ( size_t i = 0; i < bandCount; ++i )
        {
          const double dq = querySpectrum[i] - meanQ;
          const double dr = entry.spectrum[i] - meanR;
          cov += dq * dr;
          varQ += dq * dq;
          varR += dr * dr;
        }
        candidate.correlation = varQ > 0.0 && varR > 0.0
                                  ? std::clamp( cov / std::sqrt( varQ * varR ), -1.0, 1.0 )
                                  : 0.0;

        double distance = 0.0;
        for ( size_t i = 0; i < bandCount; ++i )
        {
          const double d = querySpectrum[i] - entry.spectrum[i];
          distance += d * d;
        }
        candidate.euclideanDistance = std::sqrt( distance );
      }

      if ( candidate.spectralAngleRad <= maxAngleRad )
        matches.push_back( candidate );
    }

    std::sort( matches.begin(), matches.end(), []( const SpectralMatchCandidate &a,
                                                   const SpectralMatchCandidate &b ) {
      if ( a.spectralAngleRad != b.spectralAngleRad )
        return a.spectralAngleRad < b.spectralAngleRad;
      if ( a.correlation != b.correlation )
        return a.correlation > b.correlation;
      return a.id < b.id;
    } );
    if ( matches.size() > topK )
      matches.resize( topK );
    return matches;
  }

  bool SpectralLibrary::resampleToSensor( const std::vector<float> &targetWavelengths,
                                          const std::vector<float> &targetFwhm,
                                          SpectralLibrary *outResampled ) const
  {
    if ( outResampled == nullptr || targetWavelengths.empty()
         || targetWavelengths.size() != targetFwhm.size() )
      return false;
    for ( size_t b = 0; b < targetWavelengths.size(); ++b )
    {
      if ( !( targetWavelengths[b] > 0.0f ) || !( targetFwhm[b] > 0.0f ) )
        return false;
    }

    // Build into a local library and swap only on full success, so a
    // mid-loop refusal never leaves partial state in the out parameter.
    SpectralLibrary staged;
    staged.m_entries.reserve( m_entries.size() );

    for ( const SpectralLibraryEntry &entry : m_entries )
    {
      if ( entry.wavelengthsNm.size() != entry.spectrum.size() || entry.spectrum.empty() )
        return false; // cannot resample without a wavelength grid — fail named at the seam

      SpectralLibraryEntry resampled = entry;
      resampled.spectrum.assign( targetWavelengths.size(), 0.0f );
      resampled.wavelengthsNm = targetWavelengths;
      resampled.fwhmNm = targetFwhm;

      for ( size_t b = 0; b < targetWavelengths.size(); ++b )
      {
        const double center = targetWavelengths[b];
        const double sigma = targetFwhm[b] / kFwhmToSigma;
        const double lowLambda = center - 3.0 * sigma;
        const double highLambda = center + 3.0 * sigma;

        // Bisect the integration window into the wavelength grid.
        auto first = std::lower_bound( entry.wavelengthsNm.begin(), entry.wavelengthsNm.end(),
                                       static_cast<float>( lowLambda ) );
        auto last = std::upper_bound( entry.wavelengthsNm.begin(), entry.wavelengthsNm.end(),
                                      static_cast<float>( highLambda ) );
        if ( first == last )
          return false; // target band outside the entry's source coverage

        double weighted = 0.0;
        double weightSum = 0.0;
        for ( auto it = first; it != last; ++it )
        {
          const size_t idx = static_cast<size_t>( std::distance( entry.wavelengthsNm.begin(), it ) );
          const double d = entry.wavelengthsNm[idx] - center;
          const double w = std::exp( -( d * d ) / ( 2.0 * sigma * sigma ) );
          weighted += w * entry.spectrum[idx];
          weightSum += w;
        }
        if ( !( weightSum > 0.0 ) )
          return false;
        resampled.spectrum[b] = static_cast<float>( weighted / weightSum );
      }
      staged.m_entries.push_back( std::move( resampled ) );
    }
    outResampled->m_entries = std::move( staged.m_entries );
    return true;
  }
} // namespace exp_spectral
