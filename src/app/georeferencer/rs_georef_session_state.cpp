#include "rs_georef_session_state.h"

#include <QByteArray>
#include <QMainWindow>
#include <QSettings>
#include <QWidget>

namespace {
  constexpr auto kPrefix = "Georeferencer/";
}

void RsGeorefSessionState::setLastPointsPath( const QString &path )
{
  mLastPointsPath = path;
  QSettings().setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), path );
}

void RsGeorefSessionState::saveWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings s;
  s.setValue( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ), w->saveGeometry() );
  // QMainWindow::saveState only if cast succeeds — callers may pass QMainWindow*
  if ( auto *mw = qobject_cast<QMainWindow *>( w ) )
    s.setValue( QStringLiteral( "%1windowState" ).arg( QLatin1String( kPrefix ) ), mw->saveState() );
}

void RsGeorefSessionState::restoreWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings s;
  const QByteArray geo = s.value( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ) ).toByteArray();
  if ( !geo.isEmpty() )
    w->restoreGeometry( geo );
  if ( auto *mw = qobject_cast<QMainWindow *>( w ) )
  {
    const QByteArray st = s.value( QStringLiteral( "%1windowState" ).arg( QLatin1String( kPrefix ) ) ).toByteArray();
    if ( !st.isEmpty() )
      mw->restoreState( st );
  }
}

void RsGeorefSessionState::saveWorkflow( const WorkflowSnapshot &snap )
{
  QSettings s;
  s.setValue( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), snap.mode );
  s.setValue( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), snap.transformMethod );
  s.setValue( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), snap.resamplingMethod );
  s.setValue( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ), snap.lastSourcePath );
  s.setValue( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ), snap.lastRefPath );
  s.setValue( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ), snap.lastOutputPath );
  s.setValue( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ), snap.lastDemPath );
  s.setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), snap.lastPointsPath );
  s.setValue( QStringLiteral( "%1lastDestCrs" ).arg( QLatin1String( kPrefix ) ), snap.lastDestCrsAuthId );
  s.setValue( QStringLiteral( "%1demZOffset" ).arg( QLatin1String( kPrefix ) ), snap.demZOffset );
  s.setValue( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), snap.syncZoom );
  mLastPointsPath = snap.lastPointsPath;
}

RsGeorefSessionState::WorkflowSnapshot RsGeorefSessionState::restoreWorkflow()
{
  QSettings s;
  WorkflowSnapshot o;
  o.mode = s.value( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.transformMethod = s.value( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.resamplingMethod = s.value( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.lastSourcePath = s.value( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastRefPath = s.value( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastOutputPath = s.value( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastDemPath = s.value( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastPointsPath = s.value( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastDestCrsAuthId = s.value( QStringLiteral( "%1lastDestCrs" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.demZOffset = s.value( QStringLiteral( "%1demZOffset" ).arg( QLatin1String( kPrefix ) ), 0.0 ).toDouble();
  o.syncZoom = s.value( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), true ).toBool();
  // Keep member in sync so lastPointsPath() works after restart without an extra setter.
  mLastPointsPath = o.lastPointsPath;
  return o;
}
