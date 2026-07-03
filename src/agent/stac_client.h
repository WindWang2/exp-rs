#ifndef STAC_CLIENT_H
#define STAC_CLIENT_H

#include <QObject>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

class StacClient : public QObject
{
    Q_OBJECT
signals:
    void searchCompleted(const QVariantList &features, const QString &error = QString());

public:
    explicit StacClient(QObject *parent = nullptr) : QObject(parent) {}

    static QUrl buildSearchUrl(const QString &endpoint, const QString &collection,
                               const QString &datetime, const QStringList &bbox);

    void search(const QString &endpoint, const QString &collection,
                const QString &datetime, const QStringList &bbox);

private:
    QNetworkAccessManager mManager;
};

#endif // STAC_CLIENT_H