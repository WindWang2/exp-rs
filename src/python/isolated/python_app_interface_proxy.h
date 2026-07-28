// src/python/isolated/python_app_interface_proxy.h
#pragma once

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QString>

#include "python_ipc_server.h"

class ActiveViewHost;

namespace sicnu::python::isolated
{

class PythonAppInterfaceProxy : public QObject
{
  Q_OBJECT

  public:
    explicit PythonAppInterfaceProxy( PythonIpcServer *ipcServer, QMenu *parentMenu = nullptr, ActiveViewHost *activeViewHost = nullptr, QObject *parent = nullptr );
    ~PythonAppInterfaceProxy() override = default;

    void setParentMenu( QMenu *parentMenu );
    void setActiveViewHost( ActiveViewHost *host );

    int registeredActionCount() const;

  public slots:
    void handleIpcMessage( const QJsonObject &message );

  signals:
    void actionTriggered( const QString &callbackId );

  private:
    PythonIpcServer *m_ipcServer = nullptr;
    QMenu *m_parentMenu = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    QMap<QString, QAction *> m_registeredActions;
};

} // namespace sicnu::python::isolated
