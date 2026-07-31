// rs_georef_task_center_executor.cpp — Production warp executor over Task Center
#include "rs_georef_task_center_executor.h"

RsGeorefTaskCenterExecutor::RsGeorefTaskCenterExecutor( QObject *parent )
  : RsGeorefWarpExecutor( parent )
{
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &RsGeorefWarpExecutor::taskUpdated );
}

long RsGeorefTaskCenterExecutor::submitWarp(
  const sicnu::jobs::JobRequest &request,
  const sicnu::TaskCenter::JobExecutor &executor,
  const sicnu::TaskCenter::CancelHook &onCancel )
{
  return sicnu::TaskCenter::instance().submitJob( request, executor, onCancel,
                                                  /*autoLoad=*/false );
}

bool RsGeorefTaskCenterExecutor::cancelWarp( long taskId )
{
  return sicnu::TaskCenter::instance().cancelTask( taskId );
}
