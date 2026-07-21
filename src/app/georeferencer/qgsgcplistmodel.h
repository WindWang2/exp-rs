/***************************************************************************
    qgsgcplistmodel.h - SICNU port of QGIS GCP list table model
     --------------------------------------
    Date                 : 2026-06-02 (SICNU port)
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGS_GCP_LIST_MODEL_H
#define QGS_GCP_LIST_MODEL_H

#include "qgis.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsrasterchangecoords.h"

#include <QAbstractTableModel>
#include <QString>

class QgsGcpPoint;
class QgsGeorefTransform;
class QgsGCPList;

class QgsGCPListModel : public QAbstractTableModel
{
    Q_OBJECT

  public:
    enum class Column : int
    {
      Enabled = 0,
      ID,
      SourceMapX,
      SourceMapY,
      SourceCol,   ///< source image column (pixel)
      SourceRow,   ///< source image row (pixel)
      DestMapX,
      DestMapY,
      DestCol,     ///< reference/map image column
      DestRow,     ///< reference/map image row
      ResidualDx,
      ResidualDy,
      TotalResidual,
      PointType,
      LastColumn
    };

    enum class Role : int
    {
      SourcePointRole = Qt::UserRole + 1,
    };

    explicit QgsGCPListModel( QObject *parent = nullptr );

    void setGCPList( QgsGCPList *theGCPList );
    void setGeorefTransform( QgsGeorefTransform *georefTransform );

    void setTargetCrs( const QgsCoordinateReferenceSystem &targetCrs, const QgsCoordinateTransformContext &context );

    /**
     * \a sourceIsMap — source picks are layer/map coords
     * \a residualIsMap — residuals in map units (else source pixels)
     */
    void setCoordinateDisplayMode( bool sourceIsMap, bool residualIsMap,
                                   const QString &destCrsAuth = QString() );

    /// Enable col/row display via GDAL geotransform of source and dest rasters.
    void setRasterPaths( const QString &sourcePath, const QString &destPath );

    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    bool setData( const QModelIndex &index, const QVariant &value, int role = Qt::EditRole ) override;
    Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;

    void updateResiduals();
    void refreshAll();

    static QString formatNumber( double number );

    bool sourceIsMapCoords() const { return mSourceIsMap; }
    bool residualIsMapUnits() const { return mResidualIsMap; }

  signals:
    void pointEnabled( QgsGcpPoint *pnt, int i );

  private:
    Qgis::RenderUnit residualUnit() const;
    QgsPointXY toSourcePixel( const QgsPointXY &mapOrPixel ) const;
    QgsPointXY toDestPixel( const QgsPointXY &mapOrPixel ) const;

    QgsCoordinateReferenceSystem mTargetCrs;
    QgsCoordinateTransformContext mTransformContext;

    QgsGCPList *mGCPList = nullptr;
    QgsGeorefTransform *mGeorefTransform = nullptr;

    bool mSourceIsMap = false;
    bool mResidualIsMap = false;
    QString mDestCrsAuth;

    QgsRasterChangeCoords mSrcCoords;
    QgsRasterChangeCoords mDstCoords;
    bool mHasSrcRaster = false;
    bool mHasDstRaster = false;
};

#endif
