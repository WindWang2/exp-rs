// execution_resource_bridge.cpp — see execution_resource_bridge.h.
#include "execution_resource_bridge.h"

#include "framework/task_center.h"
#include "operators/runtime/nvml_inventory.h"

#include <algorithm>

namespace sicnu {
namespace processing {

VramWireResult wireVramBudgetFromDeviceTruth( TaskCenter &center, unsigned int reservePercent )
{
    VramWireResult result;
    const operators::runtime::NvidiaInventory inventory = operators::runtime::NvidiaInventory::probe();
    if ( !inventory.available )
    {
        result.unavailableReason = inventory.unavailableReason.empty()
                                       ? "no NVIDIA device truth"
                                       : inventory.unavailableReason.c_str();
        return result;
    }
    // The honest per-device free VRAM snapshot rides along for telemetry;
    // the BUDGET is set from total VRAM (free VRAM fluctuates per acquire —
    // the Model Runtime resolves admission free-VRAM at session acquire).
    result.deviceCount = static_cast<int>( inventory.devices.size() );
    int maxTotalMb = 0;
    int maxFreeMb = -1;
    for ( const auto &device : inventory.devices )
    {
        maxTotalMb = std::max( maxTotalMb, device.totalVramMb );
        maxFreeMb = std::max( maxFreeMb, device.freeVramMb );
    }
    if ( maxTotalMb <= 0 )
    {
        result.unavailableReason = "devices report unknown total VRAM";
        return result;
    }
    const unsigned long long reserve =
        static_cast<unsigned long long>( maxTotalMb ) * reservePercent / 100u;
    const unsigned int budget =
        static_cast<unsigned int>( static_cast<unsigned long long>( maxTotalMb )
                                   - std::min( reserve, static_cast<unsigned long long>( maxTotalMb ) ) );
    result.freeVramMb = maxFreeMb;
    center.setVramBudgetMb( budget );
    result.wired = true;
    result.budgetMb = budget;
    return result;
}

} // namespace processing
} // namespace sicnu
