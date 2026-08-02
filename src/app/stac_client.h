#ifndef STAC_CLIENT_H
#define STAC_CLIENT_H

#include <QObject>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QString>
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

    /**
     * \brief Validate a STAC endpoint or asset URL for SSRF safety.
     *
     * Prefers https. Blocks private / loopback / link-local hosts unless
     * SICNU_STAC_ALLOW_PRIVATE=1. Returns empty string when OK, else an error.
     */
    static QString validateUrlPolicy(const QUrl &url, bool requireHttpsPreferred = true);

    /**
     * \brief Validate an asset href before prefixing /vsicurl/.
     * Allows only http/https schemes (https preferred).
     */
    static QString validateAssetHref(const QString &href);

    /**
     * \brief Return the /vsicurl/ URL of the item's COG asset, or empty.
     *
     * Selects the first asset (in asset-key order) whose href ends with
     * ".tif" or whose type is image/tiff, validates the href with
     * validateAssetHref, and prefixes /vsicurl/. Returns empty when the
     * item has no usable COG asset.
     */
    static QString selectCogHref(const QJsonObject &stacItemFeature);

    void search(const QString &endpoint, const QString &collection,
                const QString &datetime, const QStringList &bbox);

private:
    QNetworkAccessManager mManager;
};

#endif // STAC_CLIENT_H
