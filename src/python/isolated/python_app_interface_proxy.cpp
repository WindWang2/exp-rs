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
  , m_bridge( activeViewHost, this )
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
  m_bridge.setActiveViewHost( host );
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
    QJsonObject res = m_bridge.getActiveLayerSummary().toJsonObject();
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "data.add_layer" ) )
  {
    QString path = params[QStringLiteral( "path" )].toString();
    bool ok = m_bridge.openPath( path );
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
    QJsonObject res = m_bridge.getCanvasViewportSummary().toJsonObject();
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, res );
    }
  }
  else if ( method == QStringLiteral( "ui.push_message_bar" ) )
  {
    QString title = params[QStringLiteral( "title" )].toString();
    QString text = params[QStringLiteral( "text" )].toString();
    // Worker may send level as string ("info"/"warning"/"critical"/"success") or int (Qgis::MessageLevel).
    int level = 0;
    const QJsonValue levelVal = params.value( QStringLiteral( "level" ) );
    if ( levelVal.isString() )
    {
      const QString levelStr = levelVal.toString().toLower();
      if ( levelStr == QStringLiteral( "warning" ) || levelStr == QStringLiteral( "warn" ) )
        level = static_cast<int>( Qgis::MessageLevel::Warning );
      else if ( levelStr == QStringLiteral( "critical" ) || levelStr == QStringLiteral( "error" ) )
        level = static_cast<int>( Qgis::MessageLevel::Critical );
      else if ( levelStr == QStringLiteral( "success" ) )
        level = static_cast<int>( Qgis::MessageLevel::Success );
      else
        level = static_cast<int>( Qgis::MessageLevel::Info );
    }
    else if ( levelVal.isDouble() )
    {
      level = levelVal.toInt();
    }
    bool ok = m_bridge.pushMessageBarAlert( title, text, level );
    if ( m_ipcServer && msgId > 0 )
    {
      QJsonObject res;
      res[QStringLiteral( "status" )] = ok ? QStringLiteral( "pushed" ) : QStringLiteral( "failed" );
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
