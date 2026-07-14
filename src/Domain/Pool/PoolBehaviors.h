#pragma once

#include "Domain/Pool/PoolDomain.h"

namespace PoolBehaviors {

inline const PoolRoleSpec* domainSlotById(DomainSlotId id)
{
    return domainRoleById(PoolDomain::kPoolDomain, id);
}

inline IoSlotId ioSlotForDomainSlot(DomainSlotId id)
{
    return domainIoSlotForRole(PoolDomain::kPoolDomain, id);
}

inline const PoolDevicePreset* poolDeviceById(PoolDeviceId id)
{
    for (uint8_t i = 0; i < PoolDomain::kPoolDomain.poolDeviceCount; ++i) {
        const PoolDevicePreset& preset = PoolDomain::kPoolDomain.poolDevices[i];
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

inline IoId ioIdForDomainSlot(DomainSlotId id)
{
    return ioIdFromSlot(ioSlotForDomainSlot(id));
}

}  // namespace PoolBehaviors
