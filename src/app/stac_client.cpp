// stac_client.cpp — Qt adapter over the canonical geospatial STAC client.
//
// #1394 item 3: this TU used to carry a second STAC HTTP client
// (QNetworkAccessManager + private URL building). The transport now lives in
// the geospatial layer (geospatial/stac/stac_client.h, the same client
// fabric/catalog_service use) and this file keeps only:
//   * the app's SSRF policy helpers (validateUrlPolicy / validateAssetHref /
//     selectCogHref) and the test-pinned buildSearchUrl helper, and
//   * the worker-thread plumbing that preserves the historical
//     searchCompleted / searchDropped contract of stac_browser_dialog.
//
// Deliberate deltas from the old QNetworkAccessManager transport, all
// observable only in error paths or hardening:
//   * the geospatial fetch layer is bounded (8 MiB answer budget by default)
//     and synchronous, so searches run on a QThreadPool worker exactly like
//     the layer's threading contract demands — the signals, the generation
//     gate and the 10 s single-attempt timeout are preserved (parity oracle
//     AS-2 waits out the real timeout);
//   * the endpoint is SSRF-checked on the GUI thread before dispatch (same
//     messages); the old per-redirect re-validation (#392 belt-and-suspenders)
//     is not carried over — the CPL fetch layer owns redirects now;
//   * a non-numeric bbox is refused as a typed local error instead of being
//     forwarded verbatim to the origin.

#include "stac_client.h"
#include "agent/env_flag.h"
#include "geospatial/stac/stac_client.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QThreadPool>
#include <QUrlQuery>

#include <json/json.h>

#include <exception>

namespace {

bool isPrivateOrLocalHost(const QString &host)
{
    if (host.isEmpty())
        return true;

    const QString h = host.toLower();
    if (h == QLatin1String("localhost") || h.endsWith(QLatin1String(".localhost")))
        return true;
    if (h == QLatin1String("metadata.google.internal"))
        return true;

    QHostAddress addr(host);
    if (addr.isNull()) {
        // Not a literal IP — allow by name (DNS rebinding residual risk accepted for MVP).
        // Hostname "localhost" already handled above.
        return false;
    }

    if (addr.isLoopback())
        return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6: isLinkLocal covers fe80::/10 and 169.254.0.0/16
    if (addr.isLinkLocal())
        return true;
#endif

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = addr.toIPv4Address();
        // 10.0.0.0/8
        if ((ip & 0xFF000000u) == 0x0A000000u)
            return true;
        // 172.16.0.0/12
        if ((ip & 0xFFF00000u) == 0xAC100000u)
            return true;
        // 192.168.0.0/16
        if ((ip & 0xFFFF0000u) == 0xC0A80000u)
            return true;
        // 169.254.0.0/16 link-local
        if ((ip & 0xFFFF0000u) == 0xA9FE0000u)
            return true;
        // 127.0.0.0/8 (also covered by isLoopback, but be explicit)
        if ((ip & 0xFF000000u) == 0x7F000000u)
            return true;
        // 0.0.0.0/8
        if ((ip & 0x00000000u) == 0x00000000u)
            return true;
        // 100.64.0.0/10 CGNAT
        if ((ip & 0xFFC00000u) == 0x64400000u)
            return true;
    } else if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        // Unique local fc00::/7
        const Q_IPV6ADDR v6 = addr.toIPv6Address();
        if ((v6[0] & 0xFE) == 0xFC)
            return true;
    }

    return false;
}

/// One GeoJSON feature document from the origin's answer, verbatim: the
/// adapter must not re-map features through the strict StacItem parse, so the
/// dialog keeps seeing exactly the documents the old client forwarded.
QVariantMap featureToVariant(const Json::Value &feature)
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string text = Json::writeString(writer, feature);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(text), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    return doc.object().toVariantMap();
}

/// rel="next" continuation href of one answer document ("" when none).
QUrl nextPageUrl(const Json::Value &document)
{
    if (!document.isMember("links") || !document["links"].isArray())
        return {};

    for (const Json::Value &link : document["links"])
    {
        if (!link.isObject())
            continue;
        const Json::Value &rel = link["rel"];
        const Json::Value &href = link["href"];
        if (rel.isString() && rel.asString() == "next" && href.isString())
            return QUrl(QString::fromStdString(href.asString()));
    }
    return {};
}

QVariantList featuresToVariantList(const Json::Value &document)
{
    QVariantList features;
    if (document.isMember("features") && document["features"].isArray())
    {
        for (const Json::Value &feature : document["features"])
            features.append(featureToVariant(feature));
    }
    return features;
}

} // namespace

/// Worker-thread body: bounded geospatial fetch + verbatim feature mapping.
/// Never touches Qt GUI state; the result is handed back through
/// QMetaObject::invokeMethod onto the client's own thread.
void StacClient::runQueryJob(const QPointer<StacClient> &guard, const QueryJob &job)
{
    QVariantList features;
    QString error;
    QUrl next;

    try
    {
        sicnu::geo::StacClientOptions options;
        options.timeoutSeconds = StacClient::kFetchTimeoutSeconds;
        options.connectTimeoutSeconds = StacClient::kConnectTimeoutSeconds;
        options.maxRetries = StacClient::kMaxRetries;

        Json::Value document;
        if (!job.continuationUrl.empty())
        {
            // Pagination: a full rel="next" href, fetched verbatim (the
            // historical client also issued the server's href as-is).
            const sicnu::geo::StacClient client(job.root, options);
            document = client.getDocument(job.continuationUrl);
        }
        else
        {
            const sicnu::geo::StacClient client(job.root, options);
            sicnu::geo::StacSearchQuery query;
            if (!job.collection.empty())
                query.collections.push_back(job.collection);
            query.datetime = job.datetime;
            query.bbox = job.bbox;
            if (job.limit > 0)
                query.limit = job.limit;
            document = client.searchDocument(query);
        }

        features = featuresToVariantList(document);
        next = nextPageUrl(document);
    }
    catch (const std::exception &e)
    {
        // Typed geospatial failures (transport, timeout, non-JSON answer,
        // byte-budget cut) surface as the completion-channel error string —
        // the same channel the old reply->errorString() fed.
        error = QString::fromStdString(e.what());
    }

    if (!guard)
        return; // host gone mid-flight; nothing left to trace to

    QMetaObject::invokeMethod(guard, [guard, job, features, error, next]() {
        if (!guard)
            return;
        guard->deliver(job.generation, features, error, next);
    }, Qt::QueuedConnection);
}

QString StacClient::validateUrlPolicy(const QUrl &url, bool requireHttpsPreferred)
{
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return QStringLiteral("Invalid URL");

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http"))
        return QStringLiteral("URL scheme must be http or https");

    // Prefer https: file://, ftp://, etc. already rejected. Plain http is allowed
    // for public hosts; private/lab HTTP is gated with the private-host flag below.
    Q_UNUSED(requireHttpsPreferred);

    if (!envFlagEnabled("SICNU_STAC_ALLOW_PRIVATE")) {
        if (isPrivateOrLocalHost(url.host()))
            return QStringLiteral(
                "Private / loopback / link-local STAC hosts are blocked "
                "(set SICNU_STAC_ALLOW_PRIVATE=1 to allow)");
    }

    return {};
}

QString StacClient::validateAssetHref(const QString &href)
{
    if (href.isEmpty())
        return QStringLiteral("Empty asset href");

    // Reject GDAL VSI paths that could already encode schemes
    if (href.startsWith(QLatin1Char('/')) && href.contains(QStringLiteral("/vsi"), Qt::CaseInsensitive))
        return QStringLiteral("Pre-formed VSI paths are not accepted as asset hrefs");

    const QUrl url(href);
    if (!url.isValid() || url.scheme().isEmpty())
        return QStringLiteral("Asset href must be an absolute http(s) URL");

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http"))
        return QStringLiteral("Asset href scheme must be http or https (got '%1')").arg(scheme);

    return validateUrlPolicy(url, /*requireHttpsPreferred=*/true);
}

QString StacClient::selectCogHref(const QJsonObject &stacItemFeature)
{
    const QJsonObject assets = stacItemFeature.value(QStringLiteral("assets")).toObject();

    QString cogHref;
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const QJsonObject asset = it.value().toObject();
        const QString href = asset.value(QStringLiteral("href")).toString();
        if (href.endsWith(QStringLiteral(".tif"), Qt::CaseInsensitive) ||
            asset.value(QStringLiteral("type")).toString().contains(QStringLiteral("image/tiff"))) {
            cogHref = href;
            break;
        }
    }

    if (cogHref.isEmpty())
        return {};

    // SSRF policy applies to asset hrefs too (private hosts, bad schemes,
    // pre-formed VSI paths are all rejected here).
    if (!validateAssetHref(cogHref).isEmpty())
        return {};

    return QStringLiteral( "/vsicurl/" ) + cogHref;
}

QUrl StacClient::buildSearchUrl(const QString &endpoint, const QString &collection,
                                const QString &datetime, const QStringList &bbox,
                                int limit)
{
    QUrl url(endpoint + QStringLiteral("/search"));
    QUrlQuery query;
    if (!collection.isEmpty())
        query.addQueryItem(QStringLiteral("collections"), collection);
    if (!datetime.isEmpty())
        query.addQueryItem(QStringLiteral("datetime"), datetime);
    if (!bbox.isEmpty())
        query.addQueryItem(QStringLiteral("bbox"), bbox.join(QStringLiteral(",")));
    if (limit > 0)
        query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);
    return url;
}

void StacClient::search(const QString &endpoint, const QString &collection,
                        const QString &datetime, const QStringList &bbox,
                        int limit)
{
    // F-02: every user-initiated search supersedes every older one (the
    // generation the finished handlers compare against). searchNext()
    // deliberately does NOT bump — pagination continues the same query.
    const quint64 generation = ++m_searchGeneration;
    m_continuationUrl.clear();
    m_root = endpoint.toStdString();

    QUrl endpointUrl(endpoint);
    // Allow endpoint without path scheme form "https://host/stac"
    if (!endpointUrl.scheme().isEmpty()) {
        const QString policyError = validateUrlPolicy(endpointUrl, /*requireHttpsPreferred=*/true);
        if (!policyError.isEmpty()) {
            emit searchCompleted(QVariantList(), policyError);
            return;
        }
    } else {
        emit searchCompleted(QVariantList(), QStringLiteral("STAC endpoint must be an absolute http(s) URL"));
        return;
    }

    QueryJob job;
    job.generation = generation;
    job.root = endpoint.toStdString();
    job.collection = collection.toStdString();
    job.datetime = datetime.toStdString();
    job.limit = limit;
    for (const QString &value : bbox)
    {
        bool ok = false;
        const double parsed = value.trimmed().toDouble(&ok);
        if (!ok)
        {
            // The old client forwarded the raw string and let the origin
            // reject it; the geospatial query contract refuses a non-numeric
            // bbox locally with a typed message on the same channel.
            emit searchCompleted(QVariantList(),
                                 QStringLiteral("Bounding box values must be numeric: '%1'").arg(value));
            return;
        }
        job.bbox.push_back(parsed);
    }

    dispatch(job);
}

void StacClient::searchNext()
{
    if (m_continuationUrl.empty())
        return;

    QueryJob job;
    // Pagination continues the current query's generation: a newer search()
    // still supersedes it, matching the historical reply-generation check.
    job.generation = m_searchGeneration.load();
    job.root = m_root;
    job.continuationUrl = m_continuationUrl;
    dispatch(job);
}

void StacClient::dispatch(const QueryJob &job)
{
    // The geospatial fetch layer is synchronous and bounded: keep it off the
    // GUI thread (its own threading contract). QThreadPool workers run the
    // fetch; the result comes back through deliver() on this object's thread.
    QThreadPool::globalInstance()->start([guard = QPointer<StacClient>(this), job]() {
        runQueryJob(guard, job);
    });
}

void StacClient::deliver(quint64 generation, const QVariantList &features, const QString &error,
                         const QUrl &nextPage)
{
    // F-02/F-03: the reply belongs to a superseded query (newer search
    // started, host closed, or the query timed out after its successor
    // succeeded). Late results — successes AND errors — must never
    // overwrite the newest state: drop with a trace.
    if (generation != m_searchGeneration.load())
    {
        emit searchDropped(QStringLiteral(
            "stale STAC search result dropped (superseded generation)"));
        return;
    }

    // Only the newest continuation is pageable; an errored reply clears it
    // (the More button hides, exactly as the invalid nextPage arg did).
    m_continuationUrl = nextPage.isValid() && !nextPage.isEmpty()
                            ? nextPage.toString().toStdString()
                            : std::string();

    emit searchCompleted(features, error, nextPage);
}
