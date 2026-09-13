/***************************************************************************
 * va_source.h — Workbench 10.0 VA async data source
 *
 * Runs a bounded compute job on the sanctioned UI scan pool (RsScanPool —
 * two workers, generation tokens) and marshals the finished payload onto
 * the GUI thread (marshal_ui.h). Newer requests supersede older ones;
 * stale completions and destroyed owners drop silently; cancellation is
 * cooperative (the job polls the stale flag between chunks).
 *
 * The source NEVER touches raster I/O itself — the injected ComputeFn owns
 * the (bounded) read strategy through the geospatial contract seams.
 ***************************************************************************/
#pragma once

#include "va_data.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

namespace sicnu::app::va
{

class VaDataSource : public QObject
{
    Q_OBJECT
  public:
    /// Computes one payload. `stale` returns true once the request was
    /// superseded or canceled — poll between chunks and bail early (the
    /// dropped result is never delivered anyway, but polling keeps the
    /// worker free). Throwing inside computes a `failed` delivery with the
    /// exception text.
    using ComputeFn = std::function<VaData( const std::function<bool()> &stale )>;

    explicit VaDataSource( QObject *parent = nullptr );
    ~VaDataSource() override;

    /// Supersedes any in-flight request and starts @p fn on the pool.
    void request( ComputeFn fn );
    /// Cancels the current generation (idle-safe).
    void cancel();
    /// True while a request is in flight (loading state machine input).
    bool isBusy() const { return m_busy.load( std::memory_order_acquire ); }

  signals:
    void loading();
    void ready( const sicnu::app::va::VaData &data );
    void failed( const QString &message );

  private:
    std::atomic<quint64> m_generation { 0 };
    std::atomic<bool> m_busy { false };
};

} // namespace sicnu::app::va

Q_DECLARE_METATYPE( sicnu::app::va::VaData )
