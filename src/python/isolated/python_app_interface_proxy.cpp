#include "python_app_interface_proxy.h"
#include "active_view_host.h"
#include "data/asset_types.h"
#include "data/data_manager.h"
#include "processing/framework/python_algorithm_adapter.h"
#include "processing/framework/python_processing_provider_adapter.h"
#include "processing/framework/algorithm_engine.h"

#include <qgsmaplayer.h>

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::python::isolated
{

PythonAppInterfaceProxy::PythonAppInterfaceProxy( PythonIpcServer *ipcServer, sicnu::data::DataManager *dataManager, QMenu *parentMenu, ActiveViewHost *activeViewHost, QObject *parent )
  : QObject( parent )
  , m_ipcServer( ipcServer )
  , m_parentMenu( parentMenu )
  , m_bridge( dataManager, activeViewHost, this )
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

    if ( !m_parentMenu )
    {
      // Headless mode: no menu host — report ui_unavailable instead of
      // registering a dead QAction.
      if ( m_ipcServer && msgId > 0 )
      {
        QJsonObject res;
        res[QStringLiteral( "status" )] = QStringLiteral( "ui_unavailable" );
        res[QStringLiteral( "callback_id" )] = callbackId;
        m_ipcServer->sendResponse( msgId, res );
      }
      return;
    }

    auto *action = new QAction( actionTitle, this );
    m_registeredActions[callbackId] = action;
    m_parentMenu->addAction( action );

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
  QJsonObject response;
  if ( m_bridge.dispatchIpcMessage( message, response ) )
  {
    if ( m_ipcServer && msgId > 0 )
    {
      m_ipcServer->sendResponse( msgId, response );
    }
    return;
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
    algoDesc.resourceProfile = sicnu::ProviderResourceProfile::PythonWorkerProcess;

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
        req[QStringLiteral( "params" )] = QJsonObject::fromVariantMap( execParams );

        QJsonObject execResult;
        bool execIsError = false;
        const AwaitStatus awaitStatus = m_ipcServer->sendRequestAndAwait(
          QStringLiteral( "processing.execute_algorithm" ), req, execResult, execIsError, 300000 );
        switch ( awaitStatus )
        {
          case AwaitStatus::NoClient:
            err = QStringLiteral( "IPC client not connected" );
            return false;
          case AwaitStatus::Disconnected:
            err = QStringLiteral( "Python worker disconnected during algorithm execution" );
            return false;
          case AwaitStatus::Timeout:
            err = QStringLiteral( "Python algorithm execution timed out" );
            return false;
          case AwaitStatus::Ok:
            break;
        }
        if ( execIsError )
        {
          err = execResult[QStringLiteral( "message" )].toString( QStringLiteral( "Python algorithm execution failed" ) );
          return false;
        }
        if ( execResult[QStringLiteral( "status" )].toString() != QStringLiteral( "ok" ) )
        {
          err = QStringLiteral( "Python algorithm execution failed" );
          return false;
        }
        if ( progress ) progress( 1.0 );
        return true;
      }
    );

    auto providers = sicnu::AlgorithmEngine::instance().registeredProviders();
    std::shared_ptr<sicnu::PythonProcessingProviderAdapter> pythonProvider;
    for ( const auto &provider : providers )
    {
      if ( provider && provider->providerId() == QStringLiteral( "python_plugins" ) )
      {
        pythonProvider = std::dynamic_pointer_cast<sicnu::PythonProcessingProviderAdapter>( provider );
        break;
      }
    }

    if ( pythonProvider )
    {
      pythonProvider->addAlgorithm( adapter );
    }
    else
    {
      sicnu::AlgorithmEngine::instance().registerAlgorithm( adapter );
    }

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
