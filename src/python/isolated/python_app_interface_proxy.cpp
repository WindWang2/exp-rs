// src/python/isolated/python_app_interface_proxy.cpp
#include "python_app_interface_proxy.h"

#include <QDebug>
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

int PythonAppInterfaceProxy::registeredActionCount() const
{
  return m_registeredActions.size();
}

void PythonAppInterfaceProxy::handleIpcMessage( const QJsonObject &message )
{
  if ( !message.contains( QStringLiteral( "method" ) ) )
    return;

  QString method = message[QStringLiteral( "method" )].toString();
  if ( method == QStringLiteral( "ui.add_plugin_menu" ) )
  {
    QJsonObject params = message[QStringLiteral( "params" )].toObject();
    QString menuTitle = params[QStringLiteral( "menu_title" )].toString();
    QString actionTitle = params[QStringLiteral( "action_title" )].toString();
    QString callbackId = params[QStringLiteral( "callback_id" )].toString();
    int msgId = message.value( QStringLiteral( "id" ) ).toInt( 0 );

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
}

} // namespace sicnu::python::isolated
