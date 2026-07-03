#include "stac_client.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrlQuery>

QUrl StacClient::buildSearchUrl(const QString &endpoint, const QString &collection,
                                const QString &datetime, const QStringList &bbox)
{
    QUrl url(endpoint + QStringLiteral("/search"));
    QUrlQuery query;
    if (!collection.isEmpty())
        query.addQueryItem(QStringLiteral("collections"), collection);
    if (!datetime.isEmpty())
        query.addQueryItem(QStringLiteral("datetime"), datetime);
    if (!bbox.isEmpty())
        query.addQueryItem(QStringLiteral("bbox"), bbox.join(QStringLiteral(",")));
    url.setQuery(query);
    return url;
}

void StacClient::search(const QString &endpoint, const QString &collection,
                        const QString &datetime, const QStringList &bbox)
{
    const QUrl url = buildSearchUrl(endpoint, collection, datetime, bbox);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit searchCompleted(QVariantList(), reply->errorString());
            return;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError)
        {
            emit searchCompleted(QVariantList(),
                                 QStringLiteral("JSON Parse Error: ") + error.errorString());
            return;
        }

        const QVariantMap root = doc.toVariant().toMap();
        const QVariantList features = root.value(QStringLiteral("features")).toList();
        emit searchCompleted(features);
    });
}