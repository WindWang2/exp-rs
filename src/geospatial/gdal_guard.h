/***************************************************************************
  geospatial/gdal_guard.h
  Geospatial I/O Foundation 4.0 — small RAII / hygiene helpers around the GDAL
  C API (process init, error silencing, dataset ownership).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_GDAL_GUARD_H
#define SICNU_GEOSPATIAL_GDAL_GUARD_H

#include <gdal.h>

#include <mutex>
#include <utility>

namespace sicnu::geo
{

/// One-time GDALAllRegister for the foundation layer. Other subsystems keep
/// their own singletons; this one is self-contained so the Qt-free core never
/// depends on src/processing or src/data.
inline void ensureGdalRegistered()
{
  static std::once_flag s_once;
  std::call_once( s_once, [] { GDALAllRegister(); } );
}

/// Silences CPL error propagation for inspection code paths that probe
/// datasets which may legitimately fail to open. Restores on scope exit.
class QuietCplErrors
{
  public:
    QuietCplErrors() { CPLPushErrorHandler( &QuietCplErrors::handler ); }
    ~QuietCplErrors() { CPLPopErrorHandler(); }
    QuietCplErrors( const QuietCplErrors & ) = delete;
    QuietCplErrors &operator=( const QuietCplErrors & ) = delete;

  private:
    static void CPL_STDCALL handler( CPLErr, CPLErrorNum, const char * ) {}
};

/// Owns a GDALDatasetH; closes (flushing) on scope exit.
class GdalDatasetGuard
{
  public:
    explicit GdalDatasetGuard( GDALDatasetH handle = nullptr ) : mHandle( handle ) {}
    ~GdalDatasetGuard() { reset(); }
    GdalDatasetGuard( const GdalDatasetGuard & ) = delete;
    GdalDatasetGuard &operator=( const GdalDatasetGuard & ) = delete;
    GdalDatasetGuard( GdalDatasetGuard &&other ) noexcept : mHandle( other.mHandle ) { other.mHandle = nullptr; }
    GdalDatasetGuard &operator=( GdalDatasetGuard &&other ) noexcept
    {
      if ( this != &other )
      {
        reset();
        mHandle = std::exchange( other.mHandle, nullptr );
      }
      return *this;
    }

    GDALDatasetH get() const { return mHandle; }
    explicit operator bool() const { return mHandle != nullptr; }
    GDALDatasetH release() { return std::exchange( mHandle, nullptr ); }
    void reset( GDALDatasetH handle = nullptr )
    {
      if ( mHandle )
        GDALClose( mHandle );
      mHandle = handle;
    }

  private:
    GDALDatasetH mHandle = nullptr;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_GDAL_GUARD_H
