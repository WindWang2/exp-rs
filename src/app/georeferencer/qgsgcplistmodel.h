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

#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgspointxy.h"

#include <QAbstractTableModel>
#include <QString>

#include <functional>

class RsGeoreferencingSession;

/**
 * GCP table model (ADR 0020 S3): session-backed ONLY.
 *
 * Rows, enabled flags, point types and residuals are pulled from the
 * Georeferencing Session (the sole owner of GCP/fit state); edits are
 * forwarded to the session's granular mutation methods. The model is pure
 * presentation — it holds no QgsGCPList, no QgsGeorefTransform and no GDAL
 * geotransform readers. Source/destination pixel conversion for the col/row
 * columns is injected by the shell as plain converter callables
 * (setPixelConverters).
 */
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

    /**
     * Attach the Georeferencing Session: rows, enabled flags, point types and
     * residuals are pulled from it (the sole owner of GCP/fit state); edits
     * are forwarded to the session's granular mutation methods.
     */
    void setGcpsSource( RsGeoreferencingSession *session );

    void setTargetCrs( const QgsCoordinateReferenceSystem &targetCrs, const QgsCoordinateTransformContext &context );

    /**
     * \a sourceIsMap — source picks are layer/map coords
     * \a residualIsMap — residuals in map units (else source pixels)
     */
    void setCoordinateDisplayMode( bool sourceIsMap, bool residualIsMap,
                                   const QString &destCrsAuth = QString() );

    /**
     * Enable col/row display via injected pixel converters (ADR 0020 S3).
     * The shell computes GDAL geotransforms and injects them here as plain
     * callables so the model never touches GDAL itself. An empty callable
     * disables the corresponding col/row columns (rendered as a dash).
     */
    void setPixelConverters( std::function<QgsPointXY( const QgsPointXY & )> sourceToPixel,
                             std::function<QgsPointXY( const QgsPointXY & )> destToPixel );

    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    bool setData( const QModelIndex &index, const QVariant &value, int role = Qt::EditRole ) override;
    Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;

    void refreshAll();

    static QString formatNumber( double number );

    bool sourceIsMapCoords() const { return mSourceIsMap; }
    bool residualIsMapUnits() const { return mResidualIsMap; }

    /// True when the source raster has a georeference (converter injected),
    /// i.e. source picks are layer/map coordinates rather than raw pixels.
    bool sourceHasExistingGeoreference() const;

  private:
    QgsPointXY toSourcePixel( const QgsPointXY &mapOrPixel ) const;
    QgsPointXY toDestPixel( const QgsPointXY &mapOrPixel ) const;

    /// Session row accessors.
    int gcpRowCount() const;
    bool rowEnabled( int row ) const;
    QgsPointXY rowSourcePoint( int row ) const;
    QgsPointXY rowDestinationPoint( int row ) const;
    QString rowPointType( int row ) const;
    QPointF rowResidual( int row ) const;

    QgsCoordinateReferenceSystem mTargetCrs;
    QgsCoordinateTransformContext mTransformContext;

    RsGeoreferencingSession *mSession = nullptr;

    bool mSourceIsMap = false;
    bool mResidualIsMap = false;
    QString mDestCrsAuth;

    std::function<QgsPointXY( const QgsPointXY & )> mSourceToPixel;
    std::function<QgsPointXY( const QgsPointXY & )> mDestToPixel;
};

#endif
