// src/operators/rs/rs_partial_output_guard.h — fail-closed partial-output cleanup
#pragma once

#include <QFile>

#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

namespace sicnu::operators::rs {

/// Removes every registered path when still armed (operator threw or was
/// cancelled mid-write), so a half-written raster or sidecar artifact never
/// sits at a path a workflow will treat as a produced deliverable. Disarm
/// exactly once on the success path, after the outputs were finalized with
/// closeWithError().
///
/// Windows constraint: a GDAL-opened file cannot be removed (sharing
/// violation). The guard's destructor runs BEFORE later-declared dataset
/// wrappers unwind, so a removal here would silently fail on Windows unless
/// the owning dataset handles were closed first. Register that closure with
/// setCloseFirst() — invoked before the first removal — instead of relying on
/// member declaration order.
///
/// Companion to the sibling convention (`GdalMultibandBlockStream::abandon`,
/// rs_qa_mask "failures/cancel abandon() the partial file") for operators
/// that drive GdalDatasetWrapper directly.
class PartialOutputGuard
{
  public:
    explicit PartialOutputGuard( const QStringList &paths )
        : m_paths( paths ) {}

    explicit PartialOutputGuard( const QString &path )
        : m_paths( QStringList{ path } ) {}

    ~PartialOutputGuard()
    {
        if ( !m_armed )
            return;
        if ( m_closeFirst )
            m_closeFirst();
        for ( const QString &path : m_paths )
            QFile::remove( path );
    }

    PartialOutputGuard( const PartialOutputGuard & ) = delete;
    PartialOutputGuard &operator=( const PartialOutputGuard & ) = delete;

    void disarm() { m_armed = false; }

    /// Invoked once, immediately before the first removal, when the guard
    /// fires. Use it to close the GDAL dataset handles owning the guarded
    /// paths; closing an already-closed wrapper is a no-op, so a defensive
    /// registration is always safe.
    void setCloseFirst( std::function<void()> closeFirst )
    {
        m_closeFirst = std::move( closeFirst );
    }

  private:
    QStringList m_paths;
    std::function<void()> m_closeFirst;
    bool m_armed = true;
};

} // namespace sicnu::operators::rs
