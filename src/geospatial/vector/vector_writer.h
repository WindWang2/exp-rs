/***************************************************************************
  geospatial/vector/vector_writer.h
  Geospatial I/O Foundation 4.0 — atomic vector write contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * the writer stages a dataset group next to the target; finalize() publishes
    sidecars first and the main file last (dataset-group atomicity), cancel()
    removes the staging set
  * features stream in; memory stays bounded by the caller's write loop
  * drivers are addressed by short name (GPKG / GeoJSON / "ESRI Shapefile" /
    FlatGeobuf when available); unsupported-for-write drivers fail closed
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_VECTOR_WRITER_H
#define SICNU_GEOSPATIAL_VECTOR_WRITER_H

#include "geospatial/common.h"
#include "geospatial/crs/crs_policy.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

struct VectorFieldSpec
{
    std::string name;
    std::string typeName = "String";  ///< "Integer" | "Integer64" | "Real" | "String" | "Date"
    int width = 0;
    int precision = 0;
};

struct VectorWriteOptions
{
    std::string driver = "GPKG";
    std::vector<std::string> creationOptions;
    bool overwrite = false;
    std::string layerCreationOptions; ///< single LCO string (e.g. "GEOMETRY=POINT") or empty
};

class VectorWriter
{
  public:
    /// Stages a new single-layer dataset next to targetPath. crs may be an
    /// invalid Crs (geometryless/unknown CRS layers) — never a guess.
    static VectorWriter create( const std::string &targetPath, const std::string &layerName,
                                const std::string &geometryTypeName, const std::vector<VectorFieldSpec> &fields,
                                const Crs &crs, const VectorWriteOptions &options = {} );

    VectorWriter() = default;
    ~VectorWriter();
    VectorWriter( const VectorWriter & ) = delete;
    VectorWriter &operator=( const VectorWriter & ) = delete;
    VectorWriter( VectorWriter &&other ) noexcept;
    VectorWriter &operator=( VectorWriter &&other ) noexcept;

    bool isOpen() const { return mHandle != nullptr; }
    const std::string &targetPath() const { return mTargetPath; }
    const std::string &stagedPath() const { return mStagedPath; }

    /// Appends one feature. attributes: {field: value} (missing fields left
    /// null; unknown fields → GeoError). geometryWkt: "" for geometryless.
    void writeFeature( const Json::Value &attributes, const std::string &geometryWkt = std::string() );

    /// Flush → fsync → validate → dataset-group publish. Closed afterwards.
    void finalize();

    /// Aborts: staging discarded, target untouched.
    void cancel();

  private:
    void *mHandle = nullptr;     // GDALDatasetH
    void *mLayer = nullptr;      // OGRLayerH
    std::string mTargetPath;
    std::string mStagedPath;
    bool mTransactionActive = false;
    bool mFinalized = false;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_VECTOR_WRITER_H
