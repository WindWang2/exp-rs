/***************************************************************************
 * asset_preview_service.h — Professional Workbench 8.0 (package E)
 *
 * Single owner of catalog-scale lazy previews: bounded asynchronous raster
 * thumbnails and vector previews for panels (data manager detail pane today,
 * further consumers later).
 *
 * Contracts (see ARCHITECTURE.md D1):
 *  - The service runs jobs on the bounded RsScanPool (#797 discipline —
 *    never QThreadPool::globalInstance(), never the GUI thread).
 *  - Raster pixels flow through sicnu::geo::RasterReader only, with
 *    OverviewPolicy::Nearest — the seam the geospatial contract explicitly
 *    documents for preview surfaces. No second GDAL I/O path.
 *  - Vector previews render through QGIS primitives
 *    (QgsMapRendererCustomPainterJob) — QGIS stays the only render engine.
 *    Pathological layers (> kMaxVectorFeatures) get a typed refusal, never
 *    an unbounded render.
 *
 *    Off-thread render safety argument (audited, DECIDED-KEEP as
 *    documented-acceptable): (a) the QgsVectorLayer is standalone — never
 *    registered in QgsProject, and created, rendered and destroyed on one
 *    pool thread with no cross-thread handoff; (b) the render itself runs on
 *    QgsMapRendererCustomPainterJob, documented by QGIS as a background-
 *    thread renderer (vendored
 *    src/core/maprenderer/qgsmaprendererjob.h:289-291); (c) labeling is
 *    deliberately disabled (DrawLabeling off — font/labeling caches are the
 *    main off-thread hazard); (d) work is bounded before any pixel work
 *    (≤ kMaxVectorFeatures = 200000 features, output edges clamped to
 *    16..kMaxEdgePixels = 1024 px, finite non-empty extent required) and the
 *    finished image is marshaled back to the GUI thread via a queued
 *    QMetaObject::invokeMethod in dispatch(). Residual risk: upstream QGIS
 *    does not formally guarantee off-main-thread layer CONSTRUCTION; if that
 *    ever bites, the fallback is a dedicated single preview render thread.
 *  - Generation + receiver semantics: a newer request for the same receiver
 *    supersedes older ones; results are delivered on the service's thread
 *    (the GUI thread in production) and dropped silently when the receiver
 *    died (QPointer), the request was canceled, or a newer request for the
 *    same receiver exists. No UAF after teardown. Superseding and canceling
 *    abort the queued work too (a shared abort flag the worker checks before
 *    it touches pixels), so a burst of selections cannot leave a thousand
 *    renders queued behind the one the user is waiting for.
 *  - Results are cached (bounded by entries and bytes, LRU) keyed by
 *    path + file size + mtime + target size, so project switches and
 *    re-selection of unchanged assets do not re-read pixels. The identity
 *    (hence the cache key) is captured ONCE, at request time, and a render
 *    is only admitted to the cache when that identity still describes the
 *    file at completion — pixels read from generation A are therefore never
 *    cached, and never answered FROM THE CACHE, under generation B's
 *    identity. The identity is size+mtime (no content hash: hashing a
 *    multi-GB raster per thumbnail is exactly what this service refuses to
 *    do), so a rewrite that preserves BOTH size and mtime is indistinguishable
 *    here; Result::identityChanged is how a consumer learns that the file
 *    moved at all.
 *  - Failures are typed (Failed / Unsupported), never fabricated images.
 *
 * The service is GUI-free at the core: renderRasterPreview/renderVectorPreview
 * are pure functions (blocking; tests call them directly on one thread);
 * the QObject wrapper only adds pool scheduling, cancellation and marshaling.
 ***************************************************************************/
#pragma once

#include <QAtomicInt>
#include <QAtomicInteger>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QSize>
#include <QString>

#include <functional>

class QThreadPool;

namespace sicnu::app
{

/// Hard bounds (all honored before any pixel work).
struct PreviewLimits
{
    /// Vector layers above this feature count refuse with `Unsupported`
    /// (feature rendering cost is extent-driven; refusing is honest and
    /// keeps the bounded pool free).
    static constexpr long long kMaxVectorFeatures = 200000;
    /// Rasters WITHOUT overview levels above this pixel count refuse with
    /// `Unsupported` — a native full-resolution read would occupy a shared
    /// scan worker for seconds/minutes (bounded-pool discipline).
    static constexpr long long kMaxNativePreviewPixels = 40000000LL;
    /// Maximum thumbnail edge the renderers accept.
    static constexpr int kMaxEdgePixels = 1024;
    /// Cache defaults.
    static constexpr int kDefaultCacheEntries = 64;
    static constexpr qint64 kDefaultCacheBytes = 32LL * 1024 * 1024;
};

/// Outcome of one pure render call. `image` is null unless Ready.
struct PreviewRender
{
    enum class Status
    {
        Ready,
        Failed,     ///< open/decode failure
        Unsupported ///< declared refusal (e.g. vector feature cap)
    };
    Status status = Status::Ready;
    QImage image;
    QString error; ///< human-readable reason for Failed/Unsupported
};

/// Pure raster preview (blocking, thread-agnostic — callers or tests own the
/// thread). Stretch: per-band min/max over FINITE samples excluding
/// NoData/NaN, NoData → black, flat data → neutral gray. Non-finite samples
/// (±Inf, NaN) are a display miss, exactly like NoData: they neither widen
/// the stretch range nor reach the byte conversion. RGB from bands 1-3 when
/// the raster has ≥3 bands, grayscale otherwise. Never upsamples.
/// Overview-less rasters above @p maxNativePixels refuse with Unsupported
/// (bounded-pool discipline).
PreviewRender renderRasterPreview( const QString &path, const QSize &targetSize,
                                   long long maxNativePixels = PreviewLimits::kMaxNativePreviewPixels );

/// Pure vector preview via QGIS rendering primitives. The layer is created,
/// rendered and destroyed on the calling thread (never the GUI's project).
/// The full off-thread render safety argument — standalone layer,
/// background-thread job, labeling off, bounded work, queued marshaling back
/// to the GUI thread, and the residual layer-construction risk — lives in
/// the header contract block above.
/// @p maxFeatures bounds the work: larger layers refuse with Unsupported
/// (default: PreviewLimits::kMaxVectorFeatures).
PreviewRender renderVectorPreview( const QString &path, const QSize &targetSize,
                                   long long maxFeatures = PreviewLimits::kMaxVectorFeatures );

class AssetPreviewService : public QObject
{
    Q_OBJECT
  public:
    enum class Kind
    {
      Raster,
      Vector,
    };

    struct Result
    {
      PreviewRender::Status status = PreviewRender::Status::Ready;
      QImage image;
      QString error;
      QString path;
      Kind kind = Kind::Raster;
      QSize requestedSize;
      bool fromCache = false;
      /// True when the file's on-disk identity (size/mtime) moved between the
      /// request and the delivery. The image is then a render of a generation
      /// OTHER than the one the cache key describes — not necessarily the one
      /// that was current when the request was made: a rename/replace landing
      /// just before the worker opens the file yields the later generation.
      /// Either way it was NOT cached, so the next request re-reads the file.
      bool identityChanged = false;
    };

    struct Request
    {
      QString path;
      Kind kind = Kind::Raster;
      QSize size{ 256, 256 };
    };

    explicit AssetPreviewService( QObject *parent = nullptr );
    ~AssetPreviewService() override;

    /// Override the worker pool (tests). Default: the bounded RsScanPool.
    /// Not owned; nullptr restores the default.
    void setPool( QThreadPool *pool );

    /**
     * Requests an asynchronous preview. @p callback fires at most once on
     * this service's thread — or never, when the receiver dies, the request
     * is canceled, or a newer request for the same receiver supersedes it.
     * A dead receiver stops the QUEUED work as well, not just the delivery:
     * the same abort flag that cancel()/supersede use is set when the
     * receiver's destroyed() fires, so a worker that has not entered the
     * renderer yet returns without reading the file (R35-T06 WP02). A render
     * already inside GDAL cannot be interrupted — that is bounded by the
     * pool's concurrency, not by this service.
     * Returns the request token.
     */
    quint64 requestPreview( const Request &request, QObject *receiver,
                            std::function<void( const Result & )> callback );

    /// Drops @p token if it is still in flight (its result never delivers).
    void cancel( quint64 token );

    /// Requests still registered for delivery. Bounded by the number of live
    /// receivers (superseded/canceled requests leave the registry
    /// immediately) — NOT a count of running renders: a worker that is
    /// already inside GDAL keeps its pool thread until the read returns, and
    /// receiver-less requests (`receiver == nullptr`) are never superseded, so
    /// callers that pass nullptr must bound their own submissions.
    /// Service-thread only, like every other member.
    int pendingRequests() const;

    /// Renders this service has STARTED (the worker entered the pure render
    /// call). This is the real-work counter that `pendingRequests()` is not:
    /// it never decreases, it counts a render whose result is later dropped,
    /// and it is readable from any thread. Diagnostic only — nothing in the
    /// service schedules on it.
    qint64 rendersStarted() const;

    /// Cache configuration (entries + total image bytes, LRU eviction).
    void setCacheLimits( int maxEntries, qint64 maxBytes );
    int cacheEntries() const;
    qint64 cacheBytes() const;
    void clearCache();

  private:
    struct InFlight
    {
      QPointer<QObject> receiver;
      bool hadReceiver = false; ///< a dead receiver's request must drop, never deliver
      std::function<void( const Result & )> callback;
      Request request;      ///< normalized path: cache key and render I/O
      QString originalPath; ///< caller's path; Result.path must not use request.path
      /// Identity captured once, at request time: the cache slot this render
      /// belongs to no matter what the file does while the worker runs.
      QString cacheKey;
      /// Per-request abort flag, created for THIS token and shared with its
      /// worker; set by cancel()/supersede so a worker that has not started
      /// yet skips the render entirely. Never migrated between requests.
      QSharedPointer<QAtomicInt> abort;
    };

    void dispatch( quint64 token );
    void onComputed( quint64 token, const PreviewRender &render );
    /// Marks @p token aborted and removes it from the pending registry; its
    /// queued/worker-side lambda then returns without rendering or delivering.
    void abortRequest( quint64 token );
    static QString cacheKey( const Request &request );
    void evictIfNeeded();

    QThreadPool *m_pool = nullptr; // null = RsScanPool default
    quint64 m_nextToken = 1;
    /// Real-work counter shared with the workers (they cannot touch `this`).
    QSharedPointer<QAtomicInteger<qint64>> m_renderCounter;
    QHash<quint64, InFlight> m_inFlight;      // token → request state
    /// Supersede map keyed by RAW pointer identity only (never dereferenced —
    /// liveness is tracked separately through the receiver QPointer); Qt6
    /// provides no qHash for QPointer keys.
    QHash<QObject *, quint64> m_latestByReceiver;
    struct CacheEntry
    {
      QImage image;
      qint64 bytes = 0;
    };
    QHash<QString, CacheEntry> m_cache;
    QList<QString> m_lru; // least-recently-used first
    int m_maxEntries = PreviewLimits::kDefaultCacheEntries;
    qint64 m_maxBytes = PreviewLimits::kDefaultCacheBytes;
    qint64 m_cacheTotalBytes = 0;
};

} // namespace sicnu::app
