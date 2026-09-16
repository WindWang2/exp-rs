// rs_feature_schema.h — Classification & Object Intelligence 11.0 (F12).
//
// Typed, named feature-schema authority for training matrices.
//
// Problem it closes (audit gap #9): the supervised pipeline persisted only
// 1-based band numbers, so a reloaded model could not verify that the
// feature stack it is fed matches the one it was trained on. RsFeatureSchema
// is the single authority for feature identity:
//
//   * ordered (name, kind, source) descriptors — column order IS the schema
//     order, and the order is part of the fingerprint;
//   * fingerprint(): stable FNV-1a 64-bit over "schema v1" + descriptors,
//     hex-encoded; identical stacks on any platform produce identical
//     fingerprints (no locale, no pointer, no hash-seed dependence);
//   * JSON round-trip {version, features:[{name,kind,source}]};
//   * missing/NoData policy: RsFeatureAssembler marks NoData cells as NaN
//     in the assembled matrix and reports per-column valid counts — NaN is
//     the single "missing" sentinel crossing every seam (callers decide
//     drop/impute/refuse, and RsFeatureScaler::fit refuses non-finite
//     training input — Phase 5 hardening).
//
// Feature VALUES are produced by the existing domain kernels (spectral
// indices, glcm_texture, terrain, temporal summaries) or plain band
// columns; this module deliberately does not re-implement any of them.
#pragma once

#include "qgis_analysis_export.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <limits>
#include <vector>

class QGIS_ANALYSIS_EXPORT RsFeatureSchema
{
  public:
    enum class Kind
    {
      Band = 0,    ///< raw raster band column
      Index,       ///< spectral index (NDVI, NDWI, ...)
      Texture,     ///< GLCM/Haralick family
      Terrain,     ///< slope/aspect/roughness family
      Temporal,    ///< temporal summary statistic
      Other,
    };

    struct Descriptor
    {
      QString name;    ///< unique, non-empty (column identity)
      Kind kind = Kind::Band;
      QString source;  ///< provenance: band number, formula tag, kernel params
    };

    /// FNV-1a 64 over the full schema identity (version + all descriptors).
    static QString fingerprintFor( const QVector<Descriptor> &descriptors );

    static QString kindToString( Kind kind );
    static bool kindFromString( const QString &text, Kind &outKind );

    void clear() { mDescriptors.clear(); }
    bool isEmpty() const { return mDescriptors.isEmpty(); }
    int count() const { return mDescriptors.size(); }

    /// Appends a descriptor; fails (returns false, no change) on duplicate
    /// names or empty names.
    bool append( const Descriptor &descriptor );
    bool append( const QString &name, Kind kind, const QString &source );

    const Descriptor &at( int column ) const { return mDescriptors.at( column ); }
    const QVector<Descriptor> &descriptors() const { return mDescriptors; }

    /// Names in column order (convenience for consumers/persistence).
    QVector<QString> names() const;

    /// Hex fingerprint (16 lowercase hex chars). Empty schema → empty string.
    QString fingerprint() const { return fingerprintFor( mDescriptors ); }

    QJsonObject toJson() const;
    /// Returns false (and clears state) on structural violations
    /// (wrong version, duplicate names, unknown kind).
    bool fromJson( const QJsonObject &obj );

  private:
    QVector<Descriptor> mDescriptors;
};

/// Assembles named per-sample feature columns into a row-major matrix and
/// its derived schema. All columns must have equal length.
class QGIS_ANALYSIS_EXPORT RsFeatureAssembler
{
  public:
    struct Column
    {
      QString name;
      RsFeatureSchema::Kind kind = RsFeatureSchema::Kind::Band;
      QString source;
      std::vector<float> values;
    };

    /// Registers a column; fails on duplicate names, empty names, or length
    /// mismatch with previously added columns. Takes a copy of the values.
    bool addColumn( const Column &column );

    int rowCount() const { return mRowCount; }
    int columnCount() const { return static_cast<int>( mColumns.size() ); }

    /// NoData value marking (per-assembly, compared with exact float
    /// equality). Default NaN = "no NoData marker configured" (no cell is
    /// replaced). Cells equal to the marker become NaN in the output.
    void setNoDataValue( float nodata ) { mNoData = nodata; }

    /// Builds the schema (column order = assembly order).
    RsFeatureSchema schema() const;

    /// Emits a row-major rowCount × columnCount matrix. Returns false when
    /// no columns exist. NoData-marked cells become NaN; per-column valid
    /// (non-NoData, finite) counts are reported in \a outValidCounts.
    bool assemble( std::vector<float> &outRowMajor,
                   std::vector<int> &outValidCounts ) const;

  private:
    QVector<Column> mColumns;
    int mRowCount = 0;
    float mNoData = std::numeric_limits<float>::quiet_NaN();
};
