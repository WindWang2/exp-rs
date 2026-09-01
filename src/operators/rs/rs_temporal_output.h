// src/operators/rs/rs_temporal_output.h
// Shared output helpers for temporal operators: RAII partial-output cleanup
// (goal §49 — a cancelled/failed run never leaves a seemingly-valid file) and
// temporal provenance metadata on outputs (goal §29), reusing the existing
// SICNU_* dataset-metadata convention (see SICNU_CHANGE_* in the change
// detection operators).
#pragma once

#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QString>

#include <gdal.h>

namespace sicnu::operators::rs::temporal_output
{

/// RAII: closes the managed output dataset and deletes its file unless
/// commit() ran — a cancelled or failed run never leaves a half-written,
/// success-looking output (goal §49).
class TemporalOutputGuard
{
public:
  TemporalOutputGuard() = default;
  ~TemporalOutputGuard()
  {
    if ( !m_committed && !m_path.isEmpty() )
    {
      if ( m_dsPtr )
        m_dsPtr->close();
      QFile::remove( m_path );
    }
  }
  void manage( GdalDatasetWrapper *ds, const QString &path )
  {
    m_dsPtr = ds;
    m_path = path;
  }
  void commit() { m_committed = true; }

private:
  GdalDatasetWrapper *m_dsPtr = nullptr;
  QString m_path;
  bool m_committed = false;
};

/// Writes SICNU_TEMPORAL_* provenance metadata onto an open output dataset:
/// scene count, time range, algorithm id, parameter snapshot (short), scene
/// list, radiometric state passthrough. Mirrors SICNU_CHANGE_* usage.
void writeTemporalDatasetMetadata( const GdalDatasetWrapper &out,
                                   const sicnu::temporal::TemporalCollection &collection,
                                   const QString &algorithmId,
                                   const QString &parameterSummary = {} );

/// Per-band acquisition metadata for stacked time-series outputs (index
/// series): SICNU_ACQUISITION_DATE + SICNU_TEMPORAL_SCENE path + description.
void writeBandAcquisitionMetadata( const GdalDatasetWrapper &out, int band,
                                   const sicnu::temporal::TemporalSceneRef &scene,
                                   const QString &bandLabel );

} // namespace sicnu::operators::rs::temporal_output
