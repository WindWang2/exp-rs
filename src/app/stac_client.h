#ifndef STAC_CLIENT_H
#define STAC_CLIENT_H

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <string>
#include <vector>

/// \brief Qt signals/slots facade over the canonical geospatial STAC client
/// (sicnu::geo::StacClient, geospatial/stac/stac_client.h — the one client
/// fabric/catalog_service already use).
///
/// #1394 item 3: the app used to carry its OWN STAC HTTP client
/// (QNetworkAccessManager plus private URL building); that duplicate is gone.
/// Every network call now rides the geospatial client on a worker thread
/// (the geospatial fetch layer is synchronous and bounded; its header
/// requires UI callers to stay off the GUI thread) and the results are
/// delivered through the historical signals below.
///
/// Preserved behavior of stac_browser_dialog:
///  * searchCompleted(features, error, nextPage) — the origin's VERBATIM
///    feature documents (the adapter never re-maps them through the strict
///    StacItem parse), plus the rel="next" continuation URL;
///  * searchDropped(reason) — a finished reply whose query generation was
///    superseded (a newer search started, the host dialog closed) never
///    reaches the completion channel, and the drop stays observable;
///  * search() validates the endpoint against the SSRF policy synchronously
///    (same messages as before; SICNU_STAC_ALLOW_PRIVATE still gates private
///    hosts), and pagination (searchNext) continues the same generation.
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
    /// One dispatched search/pagination job. Everything the worker thread
    /// needs; the geospatial client itself is constructed on the worker (the
    /// layer is synchronous and must not run on the GUI thread).
    struct QueryJob
    {
        quint64 generation = 0;
        std::string root;             ///< STAC API root (search requests)
        std::string collection;       ///< "" = no collections filter
        std::string datetime;         ///< "" = no temporal filter
        std::vector<double> bbox;     ///< 4 or 6 values; empty = unfiltered
        int limit = 0;
        std::string continuationUrl;  ///< non-empty = plain GET of this URL
    };

    /// Runs @p job on a QThreadPool worker and delivers the result through
    /// the generation-checked completion channel.
    void dispatch(const QueryJob &job);
    /// Worker-thread body (bounded geospatial fetch + verbatim feature
    /// mapping); hands the outcome back onto the client's own thread.
    static void runQueryJob(const QPointer<StacClient> &guard, const QueryJob &job);
    /// GUI-thread side: generation gate + signal emission.
    void deliver(quint64 generation, const QVariantList &features, const QString &error,
                 const QUrl &nextPage);

    /// Monotonic query generation (same shape as RsScanPool's generation
    /// token, DECISIONS D-1): every user-initiated search supersedes every
    /// older one; superseded replies are dropped with a trace.
    /// Unsigned: this counter lives for the process lifetime. Atomic: written
    /// by the GUI thread, read by the finishing workers.
    std::atomic<quint64> m_searchGeneration{0};

    /// rel="next" href of the last delivered page ("" when none). Only the
    /// GUI thread touches it; workers read their captured job instead.
    std::string m_continuationUrl;

    /// Bounded fetch budget matched to the historical client: the whole
    /// request (not just the connection) dies at 10 s, single attempt — the
    /// parity oracle AS-2 waits out that real timeout.
    static constexpr int kFetchTimeoutSeconds = 10;
    static constexpr int kConnectTimeoutSeconds = 5;
    static constexpr int kMaxRetries = 0;
};

#endif // STAC_CLIENT_H
