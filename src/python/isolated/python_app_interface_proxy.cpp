#include "python_app_interface_proxy.h"
#include "active_view_host.h"

#include <qgsmapcanvas.h>
#include <qgsmessagebar.h>
#include <qgsmaplayer.h>

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::python::isolated
{

PythonAppInterfaceProxy::PythonAppInterfaceProxy( PythonIpcServer *ipcServer, QMenu *parentMenu, QObject *parent )
  : QObject( parent )
  , m_ipcServer( ipcServer )
  , m_parentMenu( parentMenu )
{
  if ( m_ipcServer )
  {
    connect( m_ipcServer, &PythonIpcServer::messageReceived, this, &PythonAppInterfaceProxy::handleIpcMessage );
  }
}

void PythonAppInterfaceProxy::setParentMenu( QMenu *parentMenu )
{
  m_parentMenu = parentMenu;
}

void PythonAppInterfaceProxy::setActiveViewHost( ActiveViewHost *host )
{
  m_activeViewHost = host;
}

void PythonAppInterfaceProxy::setMapCanvas( QgsMapCanvas *canvas )
{
  m_mapCanvas = canvas;
}

void PythonAppInterfaceProxy::setMessageBar( QgsMessageBar *bar )
{
  m_messageBar = bar;
}

int PythonAppInterfaceProxy::registeredActionCount() const
{
  return m_registeredActions.size();
}

void PythonAppInterfaceProxy::handleIpcMessage( const QJsonObject &message )
{
  if ( !message.contains( QStringLiteral( "method" ) ) )
    return;

  QString method = message[QStringLiteral( "method" )].toString();
  int msgId = message.value( QStringLiteral( "id" ) ).toInt( 0 );
  QJsonObject params = message[QStringLiteral( "params" )].toObject();

  if ( method == QStringLiteral( "ui.add_plugin_menu" ) )
  {
    QString menuTitle = params[QStringLiteral( "menu_title" )].toString();
    QString actionTitle = params[QStringLiteral( "action_title" )].toString();
    QString callbackId = params[QStringLiteral( "callback_id" )].toString();

    auto *action = new QAction( actionTitle, this );
    m_registeredActions[callbackId] = action;

    if ( m_parentMenu )
    {
      m_parentMenu->addAction( action );
    }

    connect( action, &QAction::triggered, this, [this, callbackId]() {
      emit actionTriggered( callbackId );
      if ( m_ipcServer )
      {
        QJsonObject triggerParams;
        triggerParams[QStringLiteral( "callback_id" )] = callbackId;
        m_ipcServer->sendRequest( QStringLiteral( "ui.on_action_triggered" ), triggerParams );
      }
    } );

    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = QStringLiteral( "registered" );
      res[QStringLiteral( "callback_id" )] = callbackId;
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "catalog.get_active_layer" ) )
  {
    QJsonObject res;
    QgsMapLayer *activeLyr = m_activeViewHost ? m_activeViewHost->activeLayer() : nullptr;
    if ( activeLyr )
    {
      res[QStringLiteral( "name" )] = activeLyr->name();
      res[QStringLiteral( "source" )] = activeLyr->source();
      res[QStringLiteral( "type" )] = ( activeLyr->type() == Qgis::LayerType::Raster ) ? QStringLiteral( "raster" ) : QStringLiteral( "vector" );
      res[QStringLiteral( "crs" )] = activeLyr->crs().authid();
      res[QStringLiteral( "status" )] = QStringLiteral( "ok" );
    }
    else
    {
      res[QStringLiteral( "status" )] = QStringLiteral( "no_active_layer" );
    }
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "data.add_layer" ) )
  {
    QString path = params[QStringLiteral( "path" )].toString();
    QString name = params[QStringLiteral( "name" )].toString();
    bool ok = false;
    if ( m_activeViewHost && !path.isEmpty() )
    {
      auto openRes = m_activeViewHost->openPath( path );
      ok = static_cast<bool>( openRes );
    }
    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = ok ? QStringLiteral( "added" ) : QStringLiteral( "failed" );
      res[QStringLiteral( "path" )] = path;
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "canvas.get_state" ) )
  {
    QJsonObject res;
    if ( m_mapCanvas )
    {
      QgsRectangle extent = m_mapCanvas->extent();
      QJsonArray extentArr;
      extentArr.append( extent.xMinimum() );
      extentArr.append( extent.yMinimum() );
      extentArr.append( extent.xMaximum() );
      extentArr.append( extent.yMaximum() );

      res[QStringLiteral( "extent" )] = extentArr;
      res[QStringLiteral( "scale" )] = m_mapCanvas->scale();
      res[QStringLiteral( "crs" )] = m_mapCanvas->mapSettings().destinationCrs().authid();
      res[QStringLiteral( "status" )] = QStringLiteral( "ok" );
    }
    else
    {
      res[QStringLiteral( "status" )] = QStringLiteral( "no_canvas" );
    }
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "ui.push_message_bar" ) )
  {
    QString title = params[QStringLiteral( "title" )].toString();
    QString text = params[QStringLiteral( "text" )].toString();
    if ( m_messageBar )
    {
      m_messageBar->pushMessage( title, text, Qgis::MessageLevel::Info );
    }
    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = QStringLiteral( "pushed" );
      m_ipcServer->sendResponse( msgId, res );
    }
  }
}

} // namespace sicnu::python::isolated
