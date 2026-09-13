// src/operators/rs/rs_partial_output_guard.h — fail-closed partial-output cleanup
#pragma once

#include <QFile>

#include <QString>
#include <QStringList>

#include <string>

namespace sicnu::operators::rs {

/// Removes every registered path when still armed (operator threw or was
/// cancelled mid-write), so a half-written raster or sidecar artifact never
/// sits at a path a workflow will treat as a produced deliverable. Disarm
/// exactly once on the success path, after the outputs were finalized with
/// closeWithError().
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
        for ( const QString &path : m_paths )
            QFile::remove( path );
    }

    PartialOutputGuard( const PartialOutputGuard & ) = delete;
    PartialOutputGuard &operator=( const PartialOutputGuard & ) = delete;

    void disarm() { m_armed = false; }

  private:
    QStringList m_paths;
    bool m_armed = true;
};

} // namespace sicnu::operators::rs
