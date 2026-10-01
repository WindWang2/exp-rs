// stac_client.cpp — thin Qt adapter over the domain STAC transport
// (R6 convergence, #1394 item 3: src/geospatial/stac/stac_client.* is the
// single data/transport authority; this file only marshals Qt signals).
#include "stac_client.h"

#include "geospatial/stac/stac_client.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>

#include <sstream>
#include <string>
#include <utility>

using sicnu::geo::StacClientOptions;
using sicnu::geo::StacPage;
using sicnu::geo::StacRequest;
using sicnu::geo::StacSearchQuery;

namespace {

/// jsoncpp -> QVariant bridge for one STAC feature document. The UI reads
/// the raw Item document (id/properties/assets/links) exactly as the
/// pre-convergence QNAM path delivered it — the raw document is the truth,
/// not the projected StacItem.
QVariant stacJsonToVariant(const Json::Value &value)
{
    if (!value.isObject() && !value.isArray())
        return {};
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    const std::string text = Json::writeString(writerBuilder, value);
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(text.data(), static_cast<int>(text.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {};
    return doc.toVariant();
}

} // namespace

// ---------------------------------------------------------------------------
// Delivery state shared between the adapter and the detached domain worker.
// The worker owns a shared_ptr copy, so a finished fetch never dereferences
// a dead adapter; landing happens only through the QPointer-guarded hop.
// ---------------------------------------------------------------------------
struct StacClient::PageDelivery
{
    quint64 generation = 0;
    bool ok = false;
    QString errorText;
    std::unique_ptr<StacPage> page;
};

class StacClientPrivate
{
public:
    /// Late-arrival landing policy: every user-initiated search supersedes
    /// every older one; superseded deliveries are dropped with a trace.
    /// Unsigned: this counter lives for the process lifetime (F-02, same
    /// shape as RsScanPool's generation token, DECISIONS D-1).
    quint64 searchGeneration = 0;

    /// Cached domain transport (rebuilt when the endpoint changes). Shared
    /// ownership: an in-flight detached fetch keeps it alive even if the
    /// adapter dies first.
    std::shared_ptr<sicnu::geo::StacClient> domain;
    std::string domainRoot;

    /// The page that produced the current results — its continuation
    /// descriptor feeds searchNext() (#634).
    std::unique_ptr<StacPage> lastPage;

    /// Transport budget: the pre-convergence client used a 10 s transfer
    /// timeout, a 5 s connect budget and exactly one attempt (no retries);
    /// those semantics are carried over verbatim onto the domain options.
    /// The SSRF egress policy is enforced HERE (it used to live in this
    /// client); SICNU_STAC_ALLOW_PRIVATE=1 stays the operator opt-out.
    static StacClientOptions transportOptions()
    {
        StacClientOptions options;
        options.timeoutSeconds = 10;
        options.connectTimeoutSeconds = 5;
        options.maxRetries = 0;
        options.blockPrivateNetworks = true;
        return options;
    }
};

StacClient::StacClient(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<StacClientPrivate>())
{
}

StacClient::~StacClient() = default;

QUrl StacClient::buildSearchUrl(const QString &endpoint, const QString &collection,
                                const QString &datetime, const QStringList &bbox,
                                int limit)
{
    StacSearchQuery query;
    if (!collection.isEmpty())
        query.collections.push_back(collection.toStdString());
    query.datetime = datetime.toStdString();
    for (const QString &value : bbox)
    {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (ok)
            query.bbox.push_back(number);
    }
    query.limit = limit;
    return QUrl(QString::fromStdString(
        sicnu::geo::StacClient::buildSearchUrl(endpoint.toStdString(), query)));
}

QString StacClient::validateUrlPolicy(const QUrl &url, bool requireHttpsPreferred)
{
    Q_UNUSED(requireHttpsPreferred);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return QStringLiteral("Invalid URL");
    return QString::fromStdString(
        sicnu::geo::StacClient::egressPolicyError(
            url.toString(QUrl::FullyEncoded).toStdString()));
}

QString StacClient::validateAssetHref(const QString &href)
{
    return QString::fromStdString(
        sicnu::geo::StacClient::validateAssetHref(href.toStdString()));
}

QString StacClient::selectCogHref(const QJsonObject &stacItemFeature)
{
    const QJsonDocument doc(stacItemFeature);
    const QByteArray text = doc.toJson(QJsonDocument::Compact);
    const std::string parseText(text.constData(), static_cast<std::size_t>(text.size()));
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    std::string parseErrors;
    std::istringstream stream(parseText);
    if (!Json::parseFromStream(builder, stream, &parsed, &parseErrors))
        return {};
    return QString::fromStdString(sicnu::geo::StacClient::selectCogVsicurlHref(parsed));
}

sicnu::geo::StacClient &StacClient::domainClientFor(const QString &endpoint)
{
    const std::string root = endpoint.toStdString();
    if (!d->domain || d->domainRoot != root)
    {
        d->domain = std::make_shared<sicnu::geo::StacClient>(
            root, StacClientPrivate::transportOptions());
        d->domainRoot = root;
    }
    return *d->domain;
}

void StacClient::search(const QString &endpoint, const QString &collection,
                        const QString &datetime, const QStringList &bbox,
                        int limit)
{
    // F-02: every user-initiated search supersedes every older one (the
    // generation the finished handlers compare against). searchNext()
    // deliberately does NOT bump — pagination continues the same query.
    ++d->searchGeneration;

    // Allow endpoint without path scheme form "https://host/stac"
    const QUrl endpointUrl(endpoint);
    if (endpointUrl.scheme().isEmpty())
    {
        emit searchCompleted(QVariantList(),
                             QStringLiteral("STAC endpoint must be an absolute http(s) URL"));
        return;
    }
    const QString policyError = validateUrlPolicy(endpointUrl, /*requireHttpsPreferred=*/true);
    if (!policyError.isEmpty())
    {
        emit searchCompleted(QVariantList(), policyError);
        return;
    }

    StacSearchQuery query;
    if (!collection.isEmpty())
        query.collections.push_back(collection.toStdString());
    query.datetime = datetime.toStdString();
    for (const QString &value : bbox)
    {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok)
        {
            emit searchCompleted(QVariantList(),
                                 QStringLiteral("Bounding box values must be numbers "
                                                "(min_lon,min_lat,max_lon,max_lat)"));
            return;
        }
        query.bbox.push_back(number);
    }
    query.limit = limit; // 0 = origin default (no limit param), as before

    try
    {
        const StacRequest request = domainClientFor(endpoint).buildSearchRequest(query);
        launchPageFetch(request);
    }
    catch (const sicnu::geo::GeoError &error)
    {
        emit searchCompleted(QVariantList(), QString::fromUtf8(error.what()));
    }
}

void StacClient::searchNext()
{
    if (!d->lastPage || !d->lastPage->hasMore())
        return;
    try
    {
        const StacRequest request = domainClientFor(
            QString::fromStdString(d->domainRoot)).buildNextRequest(*d->lastPage);
        launchPageFetch(request);
    }
    catch (const sicnu::geo::GeoError &error)
    {
        emit searchCompleted(QVariantList(), QString::fromUtf8(error.what()));
    }
}

void StacClient::cancelInFlight()
{
    ++d->searchGeneration;
}

void StacClient::launchPageFetch(const StacRequest &request)
{
    auto delivery = std::make_shared<PageDelivery>();
    delivery->generation = d->searchGeneration;
    auto domain = d->domain; // shared: the fetch outlives the adapter safely
    QPointer<StacClient> guard(this);

    domain->requestPageDetached(
        request,
        [delivery, domain, guard](StacPage page, const std::string &errorText) {
            delivery->ok = errorText.empty();
            delivery->errorText = QString::fromUtf8(errorText.c_str());
            if (delivery->ok)
                delivery->page = std::make_unique<StacPage>(std::move(page));
            // One marshalled hop onto the application thread; dropped
            // silently when the adapter (and its UI) are already gone.
            if (QCoreApplication *app = QCoreApplication::instance())
            {
                QMetaObject::invokeMethod(app, [guard, delivery]() {
                    if (guard)
                        guard->deliverPage(delivery);
                });
            }
        });
}

void StacClient::deliverPage(const std::shared_ptr<PageDelivery> &delivery)
{
    // F-02/F-03: the delivery belongs to a superseded query (newer search
    // started, host closed, or the query timed out after its successor
    // succeeded). Late results — successes AND errors — must never
    // overwrite the newest state: drop with a trace.
    if (delivery->generation != d->searchGeneration)
    {
        emit searchDropped(QStringLiteral(
            "stale STAC search result dropped (superseded generation)"));
        return;
    }
    if (!delivery->ok)
    {
        emit searchCompleted(QVariantList(), delivery->errorText);
        return;
    }

    d->lastPage = std::move(delivery->page);

    QVariantList features;
    features.reserve(static_cast<int>(d->lastPage->items.size()));
    for (const sicnu::geo::StacItem &item : d->lastPage->items)
        features.append(stacJsonToVariant(item.raw));

    // STAC paging (#634): offer the continuation when the server provides a
    // `next` link (resolved absolute by the domain transport).
    const QUrl nextPage = d->lastPage->hasMore()
                              ? QUrl(QString::fromStdString(d->lastPage->nextHref))
                              : QUrl();
    emit searchCompleted(features, QString(), nextPage);
}
