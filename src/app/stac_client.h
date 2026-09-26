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
    void searchCompleted(const QVariantList &features, const QString &error = QString(),
                         const QUrl &nextPage = QUrl());
    /// F-02 (ui-backend-state-parity-r4): a finished reply whose query
    /// generation was superseded (a newer search started, the host dialog
    /// closed, or the query timed out after its successor succeeded). The
    /// payload is the drop reason — the observable trace for late-arrival
    /// drops, so a silent swallow is impossible (parity landing policy).
    void searchDropped(const QString &reason);

public:
    explicit StacClient(QObject *parent = nullptr) : QObject(parent) {}

    static QUrl buildSearchUrl(const QString &endpoint, const QString &collection,
                               const QString &datetime, const QStringList &bbox,
                               int limit = 50);

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

    /// Follows the `next` link of the previous search (pagination, #634).
    /// No-op when no next page exists.
    void searchNext();

    /// F-04: invalidate every in-flight search (host dialog closed). Late
    /// replies are dropped with a searchDropped trace instead of mutating
    /// hidden state.
    void cancelInFlight() { ++m_searchGeneration; }

    void search(const QString &endpoint, const QString &collection,
                const QString &datetime, const QStringList &bbox,
                int limit = 50);

private:
    QNetworkAccessManager mManager;

private:
    QUrl m_nextPage;
    /// Monotonic query generation (same shape as RsScanPool's generation
    /// token, DECISIONS D-1): every user-initiated search supersedes every
    /// older one; superseded replies are dropped with a trace.
    int m_searchGeneration = 0;

    void runSearch(const QUrl &url);
};

#endif // STAC_CLIENT_H
