// rs_pixel_ignore_options.h — Shared NoData / ignore-value rules for classify.
#pragma once

#include <QString>
#include <QVector>

#include <cmath>
#include <vector>

/**
 * Rules for treating source pixels as "edge / background / invalid"
 * so they are not classified (and preferably not used as training samples).
 */
struct RsPixelIgnoreOptions
{
    /// Honor GDAL GetNoDataValue() on each selected source band.
    bool useSourceNodata = true;
    /// Treat IEEE NaN as ignore.
    bool ignoreNaN = true;
    /// Extra values to ignore (any band match, subject to \a mode).
    QVector<double> ignoreValues;
    /// AnyBand: if any feature is ignore → pixel ignored.
    /// AllBands: only if every feature is ignore → pixel ignored.
    enum class Mode
    {
      AnyBand = 0,
      AllBands = 1,
    };
    Mode mode = Mode::AnyBand;
    /// Class id written for ignored pixels (and typically GDAL NoData).
    int unclassifiedValue = 0;
    /// Set GDAL NoData on the output band to unclassifiedValue.
    bool writeOutputNodata = true;
    /// Absolute tolerance when comparing to ignore/NoData (float).
    double valueTolerance = 1e-5;

    /// Parse "0,-9999,255" style list into ignoreValues (replaces previous list).
    void setIgnoreValuesFromText( const QString &text );
    QString ignoreValuesText() const;

    static bool nearlyEqual( double a, double b, double tol )
    {
      return std::abs( a - b ) <= std::max( tol, std::abs( b ) * tol );
    }

    bool isIgnoreScalar( double v, bool hasBandNodata, double bandNodata ) const
    {
      if ( ignoreNaN && std::isnan( v ) )
        return true;
      if ( useSourceNodata && hasBandNodata && nearlyEqual( v, bandNodata, valueTolerance ) )
        return true;
      for ( double ig : ignoreValues )
      {
        if ( nearlyEqual( v, ig, valueTolerance ) )
          return true;
      }
      return false;
    }

    /**
     * Decide if a multi-band pixel (length B) is ignored.
     * \a bandHasNodata / \a bandNodata size B.
     * \a features row-major or pointer to B floats.
     */
    bool isIgnorePixel( const float *features, int B,
                        const std::vector<bool> &bandHasNodata,
                        const std::vector<float> &bandNodata ) const
    {
      if ( !features || B <= 0 )
        return true;
      int ignoreCount = 0;
      for ( int bi = 0; bi < B; ++bi )
      {
        const bool hasNd = bi < static_cast<int>( bandHasNodata.size() ) && bandHasNodata[static_cast<size_t>( bi )];
        const float nd = bi < static_cast<int>( bandNodata.size() ) ? bandNodata[static_cast<size_t>( bi )] : 0.f;
        if ( isIgnoreScalar( features[bi], hasNd, nd ) )
        {
          if ( mode == Mode::AnyBand )
            return true;
          ++ignoreCount;
        }
      }
      if ( mode == Mode::AllBands )
        return ignoreCount == B;
      return false;
    }
};
