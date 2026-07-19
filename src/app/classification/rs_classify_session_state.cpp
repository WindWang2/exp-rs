#include "rs_classify_session_state.h"

#include <QByteArray>
#include <QMainWindow>
#include <QSettings>
#include <QWidget>

namespace {
  constexpr auto kPrefix = "Classification/";
}

void RsClassifySessionState::setLastRoisPath( const QString &path )
{
  mLastRoisPath = path;
  QSettings().setValue( QStringLiteral( "%1lastRoisPath" ).arg( QLatin1String( kPrefix ) ), path );
}

void RsClassifySessionState::saveWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings s;
  s.setValue( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ), w->saveGeometry() );
  if ( auto *mw = qobject_cast<QMainWindow *>( w ) )
    s.setValue( QStringLiteral( "%1windowState" ).arg( QLatin1String( kPrefix ) ), mw->saveState() );
}

void RsClassifySessionState::restoreWindow( QWidget *w )
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

void RsClassifySessionState::saveWorkflow( const WorkflowSnapshot &snap )
{
  QSettings s;
  s.setValue( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ), snap.lastSourcePath );
  s.setValue( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ), snap.lastOutputPath );
  s.setValue( QStringLiteral( "%1lastRoisPath" ).arg( QLatin1String( kPrefix ) ), snap.lastRoisPath );
  s.setValue( QStringLiteral( "%1lastModelPath" ).arg( QLatin1String( kPrefix ) ), snap.lastModelPath );
  s.setValue( QStringLiteral( "%1classifierKind" ).arg( QLatin1String( kPrefix ) ), snap.classifierKind );
  s.setValue( QStringLiteral( "%1trainRatio" ).arg( QLatin1String( kPrefix ) ), snap.trainRatio );
  s.setValue( QStringLiteral( "%1wandTolerance" ).arg( QLatin1String( kPrefix ) ), snap.wandTolerance );
  mLastRoisPath = snap.lastRoisPath;
}

RsClassifySessionState::WorkflowSnapshot RsClassifySessionState::restoreWorkflow()
{
  QSettings s;
  WorkflowSnapshot o;
  o.lastSourcePath = s.value( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastOutputPath = s.value( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastRoisPath = s.value( QStringLiteral( "%1lastRoisPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastModelPath = s.value( QStringLiteral( "%1lastModelPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.classifierKind = s.value( QStringLiteral( "%1classifierKind" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.trainRatio = s.value( QStringLiteral( "%1trainRatio" ).arg( QLatin1String( kPrefix ) ), 0.7 ).toDouble();
  o.wandTolerance = s.value( QStringLiteral( "%1wandTolerance" ).arg( QLatin1String( kPrefix ) ), 20.0 ).toDouble();
  mLastRoisPath = o.lastRoisPath;
  return o;
}
