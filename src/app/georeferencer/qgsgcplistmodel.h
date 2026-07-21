/***************************************************************************
    qgsgcplistmodel.h - SICNU port of QGIS GCP list table model
     --------------------------------------
    Date                 : 2026-06-02 (SICNU port)
    Originally           : 27-Feb-2009, (c) 2009 Manuel Massing
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

#include <QAbstractTableModel>

class QgsGcpPoint;
class QgsGeorefTransform;
class QgsGCPList;

class QgsGCPListModel : public QAbstractTableModel
{
    Q_OBJECT

  public:
    enum class Column : int
    {
      Enabled,
      ID,
      SourceX,
      SourceY,
      DestinationX,
      DestinationY,
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
     * Column header units:
     * \a sourceIsMap — source picks are layer/map coords (georeferenced source)
     * \a residualIsMap — residuals reported in map units (else pixels)
     * \a destCrsAuth — e.g. "EPSG:32650" for dest column tooltip/header
     */
    void setCoordinateDisplayMode( bool sourceIsMap, bool residualIsMap,
                                   const QString &destCrsAuth = QString() );

    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    bool setData( const QModelIndex &index, const QVariant &value, int role = Qt::EditRole ) override;
    Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;

    void updateResiduals();

    /// Refresh all cells without full model reset (after residual recompute).
    void refreshAll();

    static QString formatNumber( double number );

    bool sourceIsMapCoords() const { return mSourceIsMap; }
    bool residualIsMapUnits() const { return mResidualIsMap; }

  signals:
    void pointEnabled( QgsGcpPoint *pnt, int i );

  private:
    Qgis::RenderUnit residualUnit() const;

    QgsCoordinateReferenceSystem mTargetCrs;
    QgsCoordinateTransformContext mTransformContext;

    QgsGCPList *mGCPList = nullptr;
    QgsGeorefTransform *mGeorefTransform = nullptr;

    bool mSourceIsMap = false;
    bool mResidualIsMap = false;
    QString mDestCrsAuth;
};

#endif
