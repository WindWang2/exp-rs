/***************************************************************************
     qgsgeorefdelegates.h - SICNU port of QGIS georef delegates
     --------------------------------------
    Date                 : 14-Feb-2010 (upstream) / 2026-06-02 (SICNU port)
    Copyright            : (C) 2010 by Jack R, Maxim Dubinin (GIS-Lab)
    Email                : sim@gis-lab.info
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSDELEGATES_H
#define QGSDELEGATES_H

#include <QStringList>
#include <QStyledItemDelegate>

class QgsDmsAndDdDelegate : public QStyledItemDelegate
{
    Q_OBJECT

  public:
    explicit QgsDmsAndDdDelegate( QWidget *parent = nullptr );

    QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;

    void setEditorData( QWidget *editor, const QModelIndex &index ) const override;
    void setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const override;

    void updateEditorGeometry( QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;

  private:
    double dmsToDD( const QString &dms ) const;
};

class QgsCoordDelegate : public QStyledItemDelegate
{
    Q_OBJECT

  public:
    explicit QgsCoordDelegate( QWidget *parent = nullptr );

    QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;

    void setEditorData( QWidget *editor, const QModelIndex &index ) const override;
    void setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const override;

    void updateEditorGeometry( QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;
};

/**
 * Editable combobox delegate for the "type" column of the GCP table.
 *
 * Offers the SICNU semantic categories (road / river / bridge / crossroad /
 * building / other) while still allowing the user to type a freeform label.
 */
class RsGcpTypeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

  public:
    explicit RsGcpTypeDelegate( QObject *parent = nullptr );

    QWidget *createEditor( QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override;
    void setEditorData( QWidget *editor, const QModelIndex &index ) const override;
    void setModelData( QWidget *editor, QAbstractItemModel *model, const QModelIndex &index ) const override;

  private:
    QStringList mOptions = { QStringLiteral( "road" ),
                             QStringLiteral( "river" ),
                             QStringLiteral( "bridge" ),
                             QStringLiteral( "crossroad" ),
                             QStringLiteral( "building" ),
                             QStringLiteral( "other" ) };
};

#endif // QGSDELEGATES_H
