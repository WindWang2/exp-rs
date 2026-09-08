/***************************************************************************
  geospatial/raster/raster_writer.h
  Geospatial I/O Foundation 4.0 — atomic raster write contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract (temp → write → flush → validate → publish):
  * the writer creates a STAGED dataset next to the target and publishes only
    at finalize(): a crash or cancel never corrupts an existing output
  * metadata declared at create time (nodata/scale/offset/unit/CRS/geotransform)
    is written before pixels — fidelity is part of the transaction
  * finalize() flushes, fsyncs, re-opens the staged file for validation, then
    publishes (main file + sidecars as a group); cancel() removes staging
  * values written are STORED values; physical scaling is the caller's math
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RASTER_WRITER_H
#define SICNU_GEOSPATIAL_RASTER_WRITER_H

#include "geospatial/common.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h" // RasterWindow + validateWindow contract

#include <string>
#include <vector>

namespace sicnu::geo
{

struct RasterWriteOptions
{
    std::string driver = "GTiff";
    std::vector<std::string> creationOptions; ///< driver creation options (COG presets etc.)
    bool overwrite = false;                   ///< false: existing target is an error (checked at create)
};

/// One-band declaration for create(); absence flags are meaningful.
struct RasterBandSpec
{
    std::string dtype = "Float32"; ///< GDAL data type name (Byte..Float64)
    std::string description;
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool noDataIsNaN = false;
    bool hasScale = false;
    double scale = 1.0;
    bool hasOffset = false;
    double offset = 0.0;
    std::string unit;
    std::string role;               ///< canonical band role (SICNU_BAND_ROLE)
    bool hasWavelength = false;
    double wavelengthNm = 0.0;
    bool hasFwhm = false;
    double fwhmNm = 0.0;
    std::string colorInterpretation; ///< optional (e.g. "Red")
};

class RasterWriter
{
  public:
    /// Stages a new dataset next to targetPath. Throws GeoError(InvalidArgument)
    /// for bad geometry, GeoError(Unsupported) for unknown dtype/driver,
    /// GeoError(WriteFailed) when creation fails, GeoError(IoError) when the
    /// target exists and overwrite is false or cannot be staged.
    static RasterWriter create( const std::string &targetPath, int width, int height,
                                const std::vector<RasterBandSpec> &bands, const RasterWriteOptions &options = {} );

    RasterWriter() = default;
    ~RasterWriter();
    RasterWriter( const RasterWriter & ) = delete;
    RasterWriter &operator=( const RasterWriter & ) = delete;
    RasterWriter( RasterWriter &&other ) noexcept;
    RasterWriter &operator=( RasterWriter &&other ) noexcept;

    bool isOpen() const { return mHandle != nullptr; }
    const std::string &targetPath() const { return mTargetPath; }
    const std::string &stagedPath() const { return mStagedPath; }

    void setGeotransform( const std::array<double, 6> &geotransform );
    void setCrs( const Crs &crs );
    void setDatasetMetadataItem( const std::string &key, const std::string &value );

    /// Writes stored values for one band (1-based). Values double-buffered;
    /// integer dtypes must contain exact values (out-of-range → GeoError).
    void writeWindow( int band1Based, const RasterWindow &window, const double *values );

    /// Flush → fsync → validate (reopen) → publish main file + sidecars.
    /// The writer is closed afterwards. Throws GeoError(WriteFailed/IoError);
    /// the staged file is discarded on any failure.
    void finalize();

    /// Aborts the transaction: staged files are discarded, target untouched.
    void cancel();

  private:
    void *mHandle = nullptr;  // GDALDatasetH
    std::string mTargetPath;
    std::string mStagedPath;
    int mWidth = 0;
    int mHeight = 0;
    int mBandCount = 0;
    bool mFinalized = false;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RASTER_WRITER_H
