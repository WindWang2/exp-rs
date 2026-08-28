#include "stac_client.h"
#include "agent/env_flag.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrlQuery>

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
        if ((ip & 0xFF000000u) == 0x00000000u)
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

} // namespace

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

    const QUrl url = buildSearchUrl(endpoint, collection, datetime, bbox, limit);
    runSearch(url);
}

void StacClient::runSearch(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = mManager.get(request);
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl &url){
        // 392: re-validate every redirect target against the same SSRF policy
        const QString err = StacClient::validateUrlPolicy(url, true);
        if (!err.isEmpty()) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        // 392 belt-and-suspenders: final URL after redirects must still pass policy
        if (!validateUrlPolicy(reply->url(), true).isEmpty()) {
            emit searchCompleted(QVariantList(), validateUrlPolicy(reply->url(), true));
            return;
        }
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
        // STAC paging (#634): follow the `next` link when the server provides
        // one - a fixed limit silently hid everything past page one.
        QUrl next;
        const QVariantList links = root.value(QStringLiteral("links")).toList();
        for ( const QVariant &linkVar : links )
        {
            const QVariantMap link = linkVar.toMap();
            if ( link.value( QStringLiteral( "rel" ) ).toString() == QStringLiteral( "next" ) )
            {
                next = QUrl( link.value( QStringLiteral( "href" ) ).toString() );
                break;
            }
        }
        m_nextPage = next;
        emit searchCompleted(features, QString(), next);
    });
}

void StacClient::searchNext()
{
    if ( m_nextPage.isEmpty() || !m_nextPage.isValid() )
        return;
    runSearch( m_nextPage );
}
