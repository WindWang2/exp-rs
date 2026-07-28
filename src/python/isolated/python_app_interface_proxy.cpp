#include "python_app_interface_proxy.h"
#include "active_view_host.h"
#include "processing/framework/python_algorithm_adapter.h"
#include "processing/framework/algorithm_engine.h"

#include <qgsmaplayer.h>

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::python::isolated
{

PythonAppInterfaceProxy::PythonAppInterfaceProxy( PythonIpcServer *ipcServer, QMenu *parentMenu, ActiveViewHost *activeViewHost, QObject *parent )
  : QObject( parent )
  , m_ipcServer( ipcServer )
  , m_parentMenu( parentMenu )
  , m_activeViewHost( activeViewHost )
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
    if ( m_activeViewHost )
    {
      QgsRectangle extent = m_activeViewHost->mapCanvasExtent();
      QJsonArray extentArr;
      extentArr.append( extent.xMinimum() );
      extentArr.append( extent.yMinimum() );
      extentArr.append( extent.xMaximum() );
      extentArr.append( extent.yMaximum() );

      res[QStringLiteral( "extent" )] = extentArr;
      res[QStringLiteral( "scale" )] = m_activeViewHost->mapCanvasScale();
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
    if ( m_activeViewHost )
    {
      m_activeViewHost->pushMessageBarAlert( title, text, Qgis::MessageLevel::Info );
    }
    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = QStringLiteral( "pushed" );
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "processing.register_algorithm" ) )
  {
    QString algoId = params[QStringLiteral( "id" )].toString();
    QString name = params[QStringLiteral( "name" )].toString();
    QString group = params[QStringLiteral( "group" )].toString();
    QString desc = params[QStringLiteral( "description" )].toString();

    sicnu::AlgorithmDescriptor algoDesc;
    algoDesc.id = algoId;
    algoDesc.name = name.isEmpty() ? algoId : name;
    algoDesc.group = group.isEmpty() ? QStringLiteral( "Python Plugins" ) : group;
    algoDesc.description = desc;

    auto adapter = std::make_shared<sicnu::PythonAlgorithmAdapter>(
      algoDesc,
      [this, algoId]( const QVariantMap &execParams, std::function<void(double)> progress, QString &err ) -> bool {
        if ( !m_ipcServer )
        {
          err = QStringLiteral( "IPC Server not available" );
          return false;
        }
        QJsonObject req;
        req[QStringLiteral( "id" )] = algoId;
        m_ipcServer->sendRequest( QStringLiteral( "processing.execute_algorithm" ), req );
        if ( progress ) progress( 1.0 );
        return true;
      }
    );

    sicnu::AlgorithmEngine::instance().registerAlgorithm( adapter );

    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = QStringLiteral( "registered" );
      res[QStringLiteral( "id" )] = algoId;
      m_ipcServer->sendResponse( msgId, res );
    }
  }
}

} // namespace sicnu::python::isolated
