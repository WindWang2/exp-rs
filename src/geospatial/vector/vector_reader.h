/***************************************************************************
  geospatial/vector/vector_reader.h
  Geospatial I/O Foundation 4.0 — streaming vector read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * features stream in bounded batches — no whole-table materialization path
    exists in this API at all (100k+ features stay bounded by batch size)
  * attribute projection, attribute filter (OGR SQL WHERE, driver-evaluated)
    and a bbox spatial filter (layer CRS) are declared before iteration
  * an optional declared CRS transform converts geometry WKT output to a
    target CRS with the foundation axis-order policy
  * exact feature counts are an explicit, scan-triggering request
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_VECTOR_READER_H
#define SICNU_GEOSPATIAL_VECTOR_READER_H

#include "geospatial/common.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct VectorFeature
{
    std::int64_t fid = -1;
    Json::Value attributes;   ///< object of {field: value} (projected)
    std::string geometryWkt;  ///< WKT in (possibly transformed) layer CRS; empty when geometryless
};

/// 9.0 M6 — declared layer envelope. `exact` reports whether the driver
/// COMPUTED the extent from geometry (or a declared envelope exists);
/// metadata-carried envelopes are exact-by-declaration, fast-path failures
/// are honestly invalid.
struct VectorExtent
{
    bool valid = false;
    bool exact = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;

    Json::Value toJson() const;
};

/// 9.0 M6 — driver-evaluated aggregate statistics for one numeric field.
/// Nulls never count as zeros: each aggregate carries its own presence flag.
struct VectorFieldStatistics
{
    std::string field;
    std::int64_t nonNullCount = 0;
    bool hasMin = false;
    double minValue = 0.0;
    bool hasMax = false;
    double maxValue = 0.0;
    bool hasSum = false;
    double sum = 0.0;
    bool hasMean = false;
    double mean = 0.0;

    Json::Value toJson() const;
};

class VectorReader
{
  public:
    /// Opens read-only. layerSelector: 0-based index or layer name; default
    /// first layer. Throws GeoError(OpenFailed/InvalidArgument/Unsupported).
    static VectorReader open( const std::string &path, const std::string &layerSelector = std::string() );

    VectorReader() = default;
    ~VectorReader();
    VectorReader( const VectorReader & ) = delete;
    VectorReader &operator=( const VectorReader & ) = delete;
    VectorReader( VectorReader &&other ) noexcept;
    VectorReader &operator=( VectorReader &&other ) noexcept;

    bool isOpen() const { return mHandle != nullptr; }
    void close();
    const VectorLayerInfo &layerInfo() const { return mLayerInfo; }
    const VectorMetadata &datasetInfo() const { return mDatasetInfo; }

    /// Attribute projection: only these fields appear in feature attributes.
    /// Unknown names throw GeoError(InvalidArgument). Empty = all fields.
    void setAttributeProjection( const std::vector<std::string> &fieldNames );

    /// Driver-evaluated OGR SQL WHERE clause ("" clears). Failing clauses
    /// surface driver errors as GeoError(InvalidArgument).
    void setAttributeFilter( const std::string &whereClause );

    /// Bbox spatial filter in LAYER coordinates (traditional GIS order).
    void setSpatialFilter( const CrsBoundingBox &box );

    /// Declares that subsequent streamed geometry WKT should be transformed
    /// into `target`. Uses the foundation axis-order policy. Clears via a
    /// null Crs. Throws GeoError(TransformFailed) when no path exists.
    void setTargetCrs( const Crs &target );

    /// Streams at most maxFeatures further features into `out`. Returns false
    /// when the iteration is exhausted (out may still receive features).
    bool nextBatch( std::vector<VectorFeature> &out, std::size_t maxFeatures = 1024 );

    /// Restarts the stream (respecting filters).
    void resetStream();

    /// Explicit exact count — for drivers without cheap counts this scans the
    /// layer (documented, opt-in; never called by inspection paths).
    std::int64_t exactFeatureCount();

    // --- 9.0 M6: extent & aggregate statistics ----------------------------

    /// Layer envelope without a full-table surprise: the driver's cheap
    /// extent is used when available; `allowScan` additionally permits a
    /// geometry scan. Without the flag, an unavailable extent reports
    /// valid=false (never a silent full-table load).
    VectorExtent extent( bool allowScan = false ) const;

    /// Driver-evaluated MIN/MAX/SUM/AVG/COUNT over one NUMERIC field
    /// (optionally WHERE-filtered — driver-evaluated OGR SQL). Explicit
    /// opt-in: aggregates inherently scan the features in the driver, so
    /// this is a caller decision, never an inspection side effect. String
    /// fields are a typed error. The current stream position is untouched.
    VectorFieldStatistics fieldStatistics( const std::string &fieldName,
                                           const std::string &whereClause = std::string() ) const;

  private:
    void *mHandle = nullptr;   // GDALDatasetH
    void *mLayer = nullptr;    // OGRLayerH (owned by dataset)
    CrsTransform mTransform;   // declared target-CRS transform (empty when none)
    VectorMetadata mDatasetInfo;
    VectorLayerInfo mLayerInfo;
    std::vector<std::string> mProjection;
    bool mStreamExhausted = false;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_VECTOR_READER_H
