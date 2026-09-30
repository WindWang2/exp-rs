#ifndef STAC_CLIENT_H
#define STAC_CLIENT_H

#include <QObject>
#include <QVariantList>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <memory>

namespace sicnu::geo
{
class StacClient;
struct StacPage;
struct StacRequest;
}

/// Thin Qt/UI adapter over the domain STAC transport (R6 convergence,
/// #1394 item 3). ALL transport and data logic — request building, egress
/// policy, pagination continuations, timeouts, retries, JSON document
/// fetching — lives in sicnu::geo::StacClient (src/geospatial/stac/); this
/// object only marshals between that bounded synchronous/detached domain
/// world and Qt signals on the object's thread, and keeps the late-arrival
/// landing policy (generation stamp + searchDropped trace) the UI relies on.
///
/// The public Qt-facing API is unchanged from the pre-convergence client so
/// stac_browser_dialog and its tests churn zero.
///
/// Threading: the adapter must live on the main (GUI) thread — it is created
/// by the dialog there, and results are marshalled through the application
/// object's event loop. The domain fetch itself runs on a detached domain
/// worker (sicnu::geo::StacClient::requestPageDetached); a finished result
/// whose generation was superseded is dropped with a searchDropped trace,
/// never delivered (F-02/F-03 parity contract, unchanged).
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
    explicit StacClient(QObject *parent = nullptr);
    ~StacClient() override;

    StacClient(const StacClient &) = delete;
    StacClient &operator=(const StacClient &) = delete;

    static QUrl buildSearchUrl(const QString &endpoint, const QString &collection,
                               const QString &datetime, const QStringList &bbox,
                               int limit = 50);

    /**
     * \brief Validate a STAC endpoint or asset URL for SSRF safety.
     *
     * Prefers https. Blocks private / loopback / link-local hosts unless
     * SICNU_STAC_ALLOW_PRIVATE=1. Returns empty string when OK, else an error.
     * Delegates to sicnu::geo::StacClient::egressPolicyError — the single
     * home of the policy.
     */
    static QString validateUrlPolicy(const QUrl &url, bool requireHttpsPreferred = true);

    /**
     * \brief Validate an asset href before prefixing /vsicurl/.
     * Allows only http/https schemes (https preferred). Delegates to
     * sicnu::geo::StacClient::validateAssetHref.
     */
    static QString validateAssetHref(const QString &href);

    /**
     * \brief Return the /vsicurl/ URL of the item's COG asset, or empty.
     *
     * Delegates to sicnu::geo::StacClient::selectCogVsicurlHref (first asset
     * in asset-key order whose href ends with ".tif" or whose type is
     * image/tiff, validated, prefixed /vsicurl/).
     */
    static QString selectCogHref(const QJsonObject &stacItemFeature);

    /// Follows the `next` link of the previous search (pagination, #634).
    /// No-op when no next page exists.
    void searchNext();

    /// F-04: invalidate every in-flight search (host dialog closed). Late
    /// replies are dropped with a searchDropped trace instead of mutating
    /// hidden state.
    void cancelInFlight();

    void search(const QString &endpoint, const QString &collection,
                const QString &datetime, const QStringList &bbox,
                int limit = 50);

private:
    /// One delivered page fetch (shared with the domain worker).
    struct PageDelivery;

    /// Domain transport for @p endpoint, creating (and caching) it on first
    /// use. Throws sicnu::geo::GeoError for an unusable endpoint.
    sicnu::geo::StacClient &domainClientFor(const QString &endpoint);

    /// Dispatches one bounded page request on the domain's detached worker;
    /// the completion is marshalled back onto this object's thread.
    void launchPageFetch(const sicnu::geo::StacRequest &request);

    /// GUI-thread landing: generation check, drop trace, signal emission.
    void deliverPage(const std::shared_ptr<PageDelivery> &delivery);

    std::unique_ptr<class StacClientPrivate> d;
};

#endif // STAC_CLIENT_H
