/***************************************************************************
     mapcoordsdialog.cpp
     --------------------------------------
    Date                 : 2005
    Copyright            : (C) 2005 by Lars Luthman
    Email                : larsl at users dot sourceforge dot net
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgsmapcoordsdialog.h"

#include <memory>

#include "qgis.h"
#include "qgsapplication.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgeorefvalidators.h"
#include "qgsgui.h"
#include "qgsmapcanvas.h"
#include "qgsmapmouseevent.h"
#include "qgsprojectionselectionwidget.h"
#include "qgssettings.h"
#include "qgssnappingutils.h"

#include <QPushButton>
#include <QString>
#include <QValidator>

#include "dialogs/dialog_help_catalog.h"

using namespace Qt::StringLiterals;

QgsMapCoordsDialog::QgsMapCoordsDialog( QgsMapCanvas *qgisCanvas, QgsGeorefDataPoint *georefDataPoint, QgsCoordinateReferenceSystem &rasterCrs, QWidget *parent )
  : QDialog( parent, Qt::Dialog )
  , mQgisCanvas( qgisCanvas )
  , mRasterCrs( rasterCrs )
  , mNewlyAddedPoint( georefDataPoint )
{
  setupUi( this );
  QgsGui::enableAutoGeometryRestore( this );

  connect( buttonBox, &QDialogButtonBox::accepted, this, &QgsMapCoordsDialog::buttonBox_accepted );

  setAttribute( Qt::WA_DeleteOnClose );

  mPointFromCanvasPushButton = new QPushButton( QgsApplication::getThemeIcon( "georeferencer/mPushButtonPencil.svg" ), tr( "从地图取点" ) );
  mPointFromCanvasPushButton->setToolTip( tr( "点击后在主地图上点选一点，自动填入坐标。" ) );
  mPointFromCanvasPushButton->setStatusTip( tr( "从地图画布取点" ) );
  mPointFromCanvasPushButton->setCheckable( true );
  buttonBox->addButton( mPointFromCanvasPushButton, QDialogButtonBox::ActionRole );
  mPointFromCanvasPushButton->setFocus();

  auto *helpBtn = buttonBox->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  SicnuDialogHelp::tip( helpBtn, tr( "打开 GCP 目标坐标输入帮助说明。" ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "map_coords" ), windowTitle() );
  } );

  // User can input either DD or DMS coords (from QGIS mapcanvas we take DD coords)
  QgsDMSAndDDValidator *validator = new QgsDMSAndDDValidator( this );
  leXCoord->setValidator( validator );
  leYCoord->setValidator( validator );
  SicnuDialogHelp::tip( leXCoord, tr( "目标 X 坐标（经度或投影东坐标，支持十进制度或度分秒 DMS）" ) );
  SicnuDialogHelp::tip( leYCoord, tr( "目标 Y 坐标（纬度或投影北坐标，支持十进制度或度分秒 DMS）" ) );

  mToolEmitPoint = new QgsGeorefMapToolEmitPoint( qgisCanvas );
  mToolEmitPoint->setButton( mPointFromCanvasPushButton );

  const QgsSettings settings;
  mMinimizeWindowCheckBox->setChecked( settings.value( u"/Plugin-GeoReferencer/Config/Minimize"_s, u"1"_s ).toBool() );
  SicnuDialogHelp::tip( mMinimizeWindowCheckBox, tr( "点击「从地图取点」时自动最小化配准窗口，便于在主画布上选点" ) );

  connect( mPointFromCanvasPushButton, &QAbstractButton::clicked, this, &QgsMapCoordsDialog::setToolEmitPoint );

  connect( mToolEmitPoint, &QgsGeorefMapToolEmitPoint::canvasClicked, this, &QgsMapCoordsDialog::maybeSetXY );
  connect( mToolEmitPoint, &QgsGeorefMapToolEmitPoint::mouseReleased, this, &QgsMapCoordsDialog::setPrevTool );

  connect( leXCoord, &QLineEdit::textChanged, this, &QgsMapCoordsDialog::updateOK );
  connect( leYCoord, &QLineEdit::textChanged, this, &QgsMapCoordsDialog::updateOK );

  mProjectionSelector->setCrs( mRasterCrs );
  SicnuDialogHelp::tip( mProjectionSelector, tr( "指定 GCP 点的目标坐标参考系 (CRS)" ) );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "map_coords" ) );

  updateOK();
}

QgsMapCoordsDialog::~QgsMapCoordsDialog()
{
  delete mToolEmitPoint;

  QgsSettings settings;
  settings.setValue( u"/Plugin-GeoReferencer/Config/Minimize"_s, mMinimizeWindowCheckBox->isChecked() );
}

void QgsMapCoordsDialog::updateOK()
{
  const bool enable = ( leXCoord->text().size() != 0 && leYCoord->text().size() != 0 );
  QPushButton *okPushButton = buttonBox->button( QDialogButtonBox::Ok );
  okPushButton->setEnabled( enable );
}

void QgsMapCoordsDialog::setPrevTool()
{
  mQgisCanvas->setMapTool( mPrevMapTool );
}

void QgsMapCoordsDialog::buttonBox_accepted()
{
  bool ok;
  double x = leXCoord->text().toDouble( &ok );
  if ( !ok )
    x = dmsToDD( leXCoord->text() );

  double y = leYCoord->text().toDouble( &ok );
  if ( !ok )
    y = dmsToDD( leYCoord->text() );

  emit pointAdded( mNewlyAddedPoint->sourcePoint(), QgsPointXY( x, y ), mProjectionSelector->crs().isValid() ? mProjectionSelector->crs() : mRasterCrs );
  close();
}

void QgsMapCoordsDialog::maybeSetXY( const QgsPointXY &xy, Qt::MouseButton button )
{
  // Only LeftButton should set point
  if ( Qt::LeftButton == button )
  {
    const QgsPointXY mapCoordPoint = xy;

    leXCoord->clear();
    leYCoord->clear();
    leXCoord->setText( qgsDoubleToString( mapCoordPoint.x() ) );
    leYCoord->setText( qgsDoubleToString( mapCoordPoint.y() ) );

    mNewlyAddedPoint->setDestinationPoint( mapCoordPoint );
  }

  // only restore window if it was minimized
  if ( parentWidget()->windowState().testFlag( Qt::WindowMinimized ) )
    parentWidget()->showNormal();
  parentWidget()->activateWindow();
  parentWidget()->raise();

  // set CRS to match canvas' point coordinates
  mProjectionSelector->setCrs( mQgisCanvas->mapSettings().destinationCrs() );

  mPointFromCanvasPushButton->setChecked( false );
  buttonBox->button( QDialogButtonBox::Ok )->setFocus();
  activateWindow();
  raise();
}

void QgsMapCoordsDialog::setToolEmitPoint( bool isEnable )
{
  if ( isEnable )
  {
    if ( mMinimizeWindowCheckBox->isChecked() && parentWidget() )
    {
      parentWidget()->showMinimized();
    }

    // Raise the main application window that owns the map canvas.
    if ( mQgisCanvas )
    {
      if ( QWidget *w = mQgisCanvas->window() )
      {
        w->showNormal();
        w->activateWindow();
        w->raise();
      }
    }

    mPrevMapTool = mQgisCanvas ? mQgisCanvas->mapTool() : nullptr;
    if ( mQgisCanvas )
      mQgisCanvas->setMapTool( mToolEmitPoint );
  }
  else
  {
    if ( mQgisCanvas && mPrevMapTool )
      mQgisCanvas->setMapTool( mPrevMapTool );
  }
}

double QgsMapCoordsDialog::dmsToDD( const QString &dms )
{
  const QStringList list = dms.split( ' ' );
  QString tmpStr = list.at( 0 );
  double res = std::fabs( tmpStr.toDouble() );

  tmpStr = list.value( 1 );
  if ( !tmpStr.isEmpty() )
    res += tmpStr.toDouble() / 60;

  tmpStr = list.value( 2 );
  if ( !tmpStr.isEmpty() )
    res += tmpStr.toDouble() / 3600;

  if ( dms.startsWith( '-' ) )
    return -res;
  else
    return res;
}

void QgsMapCoordsDialog::updateSourceCoordinates( const QgsPointXY &sourceCoordinates )
{
  mNewlyAddedPoint->setSourcePoint( sourceCoordinates );
}


QgsGeorefMapToolEmitPoint::QgsGeorefMapToolEmitPoint( QgsMapCanvas *canvas )
  : QgsMapTool( canvas )
{
  mSnapIndicator = std::make_unique<QgsSnapIndicator>( canvas );
}

void QgsGeorefMapToolEmitPoint::canvasMoveEvent( QgsMapMouseEvent *e )
{
  mSnapIndicator->setMatch( mapPointMatch( e ) );
}

void QgsGeorefMapToolEmitPoint::canvasPressEvent( QgsMapMouseEvent *e )
{
  const QgsPointLocator::Match m = mapPointMatch( e );
  emit canvasClicked( m.isValid() ? m.point() : toMapCoordinates( e->pos() ), e->button() );
}

void QgsGeorefMapToolEmitPoint::canvasReleaseEvent( QgsMapMouseEvent *e )
{
  QgsMapTool::canvasReleaseEvent( e );
  emit mouseReleased();
}

void QgsGeorefMapToolEmitPoint::deactivate()
{
  mSnapIndicator->setMatch( QgsPointLocator::Match() );

  QgsMapTool::deactivate();
}

QgsPointLocator::Match QgsGeorefMapToolEmitPoint::mapPointMatch( QMouseEvent *e )
{
  const QgsPointXY pnt = toMapCoordinates( e->pos() );
  return canvas()->snappingUtils()->snapToMap( pnt );
}
