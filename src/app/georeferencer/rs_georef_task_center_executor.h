// rs_georef_task_center_executor.h — Production warp executor over Task Center
// (ADR 0020 decision 3). Delegates RsGeorefWarpExecutor calls to
// sicnu::TaskCenter::instance() and forwards its taskUpdated signal.
#pragma once

#include "rs_georeferencing_session.h"

/**
 * Production RsGeorefWarpExecutor: thin adapter over the TaskCenter singleton.
 * Created by RsGeoreferencingSession when no executor is injected, so existing
 * consumers (shell window) behave exactly as before.
 */
class RsGeorefTaskCenterExecutor : public RsGeorefWarpExecutor
{
  Q_OBJECT
  public:
    explicit RsGeorefTaskCenterExecutor( QObject *parent = nullptr );

    long submitWarp( const sicnu::jobs::JobRequest &request,
                     const sicnu::TaskCenter::JobExecutor &executor,
                     const sicnu::TaskCenter::CancelHook &onCancel ) override;
    bool cancelWarp( long taskId ) override;
};
