// rs_roi.h — Phase 10A: Region-of-Interest (geometry + class id + pixel indices)
#pragma once
#include "qgis_analysis_export.h"
#include "qgsgeometry.h"
#include <QVector>
#include <cstdint>

class QGIS_ANALYSIS_EXPORT RsRoi
{
  public:
    RsRoi() = default;
    RsRoi( int classId, const QgsGeometry &geom, const QVector<quint64> &pixelIndices )
      : mClassId( classId ), mGeometry( geom ), mPixelIndices( pixelIndices ) {}

    int classId() const { return mClassId; }
    QgsGeometry geometry() const { return mGeometry; }
    const QVector<quint64> &pixelIndices() const { return mPixelIndices; }

    void setClassId( int id ) { mClassId = id; }
    void setGeometry( const QgsGeometry &g ) { mGeometry = g; }
    void setPixelIndices( const QVector<quint64> &p ) { mPixelIndices = p; }

  private:
    int mClassId = 0;
    QgsGeometry mGeometry;
    QVector<quint64> mPixelIndices; // row * width + col
};
