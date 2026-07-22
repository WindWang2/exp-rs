// rs_class_raster.h — K1 writeback: paint segment→class ids onto a class raster.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

#include <QColor>
#include <QHash>
#include <QMap>
#include <QString>

struct QGIS_ANALYSIS_EXPORT RsClassRasterResult
{
    bool ok = false;
    QString errorMessage;
    int totalPixels = 0;
    QString outputPath;
};

class QGIS_ANALYSIS_EXPORT RsClassRaster
{
  public:
    /// Paint predicted class ids onto the segment map geometry.
    /// referenceRasterPath supplies geotransform/projection; output is Byte or UInt16.
    /// Class ids must be ≥ 1; 0 is written as GDAL NoData (unclassified / background).
    /// Reference grid size must match the segment map when the reference opens.
    static RsClassRasterResult paint(
        const RsSegmentMap &segMap,
        const QMap<quint32, int> &segmentClasses,
        const QString &referenceRasterPath,
        const QString &outputPath,
        const QHash<int, QColor> &classColors = {} );

    /// Optional polygonize hook (GDAL Polygonize of class raster).
    /// Masks out value 0 (NoData/background) so unclassified regions are not vectorized.
    /// Returns ok=false with message when GDAL polygonize fails.
    static RsClassRasterResult polygonize(
        const QString &classRasterPath,
        const QString &outputVectorPath,
        const QString &fieldName = QStringLiteral( "class_id" ) );
};
